/* adb_client.c — USB Host ADB 客户端 ([硬件相关·需真机验证])
 *
 * 手机 (device) ←OTG→ P4 (host): 枚举 ADB 接口 (0x42/0x01/0x01) →
 * CNXN → AUTH(RSAPUBLICKEY) → 手机弹授权 → AUTH(SIGNATURE) → CNXN →
 * OPEN "shell:" 常驻流 → WRTE 命令/接收输出.
 *
 * USB 层按 IDF v5.5 usb_host API (usb/usb_host.h):
 *   usb_host_client_register(&cfg, &handle) / usb_host_client_handle_events
 *   usb_host_transfer_alloc(size, flags, &transfer) → submit (回调式)
 * RSA2048 密钥首次生成存 NVS (红线②: 纯软件, 不碰 eFuse).
 */
#include "adb_client.h"
#include "adb_proto.h"
#include "adb_rsa.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "mbedtls/md.h"

#include "usb/usb_host.h"

static const char *TAG = "adbtool";

/* ---------------- 状态 ---------------- */
static usb_host_client_handle_t s_client;
static usb_device_handle_t s_dev;
static uint8_t s_dev_addr;            /* usb_host 层地址 */
static uint8_t s_itf_num;
static uint8_t s_ep_out_addr;         /* bulk OUT */
static usb_transfer_t *s_xfer_in;     /* bulk IN  (常驻挂起收数据) */
static usb_transfer_t *s_xfer_out;    /* bulk OUT (发送) */
static volatile bool s_running;
static volatile bool s_ready;         /* 授权完成 */
static volatile bool s_dev_open;      /* 设备已打开且接口已 claim */
static adb_line_cb_t s_line_cb;
static void *s_line_user;

/* 当前激活 HID 消费者的报告描述符与 OUT 收包入口
 * (FIDO: hal_usb_ctap.c attach 时设置; BadUSB: hal_usb_hid.c init 时设置) */
uint8_t const *__hid_report_desc;
void (*__hid_rx_fn)(const uint8_t *buf, size_t len);

static volatile uint32_t s_local_id = 1;
static volatile uint32_t s_remote_id;
static char s_line_buf[ADB_MAX_PAYLOAD];
static size_t s_line_len;

/* 发送/接收的完成信号 (阻塞包装用) */
static SemaphoreHandle_t s_out_done;
static volatile usb_transfer_status_t s_out_status;

/* ---------------- 前置 ---------------- */
static void adb_handshake(void);
static void in_xfer_done_cb(usb_transfer_t *t);

/* ---------------- USB transfer 完成回调 ---------------- */
static void out_done_cb(usb_transfer_t *t)
{
    s_out_status = t->status;
    xSemaphoreGive(s_out_done);
}

static int adb_send_v2(uint32_t cmd, uint32_t a0, uint32_t a1,
                       const uint8_t *data, size_t len)
{
    if (!s_dev_open) return -1;
    if (len > ADB_MAX_PAYLOAD) return -1;
    xSemaphoreTake(s_out_done, 0);

    /* 1) 头 */
    adb_msg_hdr_t hdr;
    adb_pack(&hdr, cmd, a0, a1, data, len);
    s_xfer_out->device_handle = s_dev;
    s_xfer_out->bEndpointAddress = s_ep_out_addr;
    memcpy((void *)s_xfer_out->data_buffer, &hdr, ADB_HDR_SZ);
    s_xfer_out->num_bytes = ADB_HDR_SZ;
    s_out_status = (usb_transfer_status_t)-1;
    if (usb_host_transfer_submit(s_xfer_out) != ESP_OK) return -1;
    if (xSemaphoreTake(s_out_done, pdMS_TO_TICKS(3000)) != pdTRUE ||
        s_out_status != USB_TRANSFER_STATUS_COMPLETED) {
        return -1;
    }

    /* 2) 数据 */
    if (len > 0) {
        s_xfer_out->device_handle = s_dev;
        s_xfer_out->bEndpointAddress = s_ep_out_addr;
        memcpy((void *)s_xfer_out->data_buffer, data, len);
        s_xfer_out->num_bytes = len;
        s_out_status = (usb_transfer_status_t)-1;
        if (usb_host_transfer_submit(s_xfer_out) != ESP_OK) return -1;
        if (xSemaphoreTake(s_out_done, pdMS_TO_TICKS(3000)) != pdTRUE ||
            s_out_status != USB_TRANSFER_STATUS_COMPLETED) {
            return -1;
        }
    }
    return 0;
}

/* ---------------- 收包处理 ---------------- */
static void in_xfer_done_cb(usb_transfer_t *t)
{
    if (!s_running) return;
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        const uint8_t *p = t->data_buffer;
        size_t remain = t->actual_num_bytes;
        /* 可能一包里含 24B 头 + data (adb 常这样发), 按 ADB 消息流解析 */
        while (remain >= ADB_HDR_SZ) {
            adb_msg_hdr_t hdr;
            memcpy(&hdr, p, ADB_HDR_SZ);
            if (adb_hdr_check(&hdr) != 0 || hdr.data_len > remain - ADB_HDR_SZ) {
                break;
            }
            const uint8_t *payload = p + ADB_HDR_SZ;
            switch (hdr.cmd) {
            case ADB_CMD_CNXN:
                if (!s_ready && s_line_cb) {
                    s_line_cb("[adb] 已授权, 连接就绪", s_line_user);
                }
                s_ready = true;
                break;
            case ADB_CMD_AUTH:
                if (hdr.arg0 == 1) {
                    /* AUTH RSAPUBLICKEY: 回 "host:"+b64(SPKI) 让手机弹授权 */
                    uint8_t pubkey[ADB_MAX_PAYLOAD];
                    int pklen = adb_rsa_pubkey_adb(pubkey, sizeof(pubkey));
                    if (pklen > 0) {
                        adb_send_v2(ADB_CMD_AUTH, 1, 0, pubkey, pklen);
                    }
                } else if (hdr.arg0 == 2) {
                    /* AUTH SIGNATURE: 手机送 token, 签 SHA1(token) 回传 */
                    uint8_t digest[20];
                    uint8_t sig[256];
                    size_t sig_len = sizeof(sig);
                    const mbedtls_md_info_t *md =
                        mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
                    mbedtls_md(md, payload, hdr.data_len, digest);
                    if (adb_rsa_sign(digest, sizeof(digest), sig, &sig_len) == 0) {
                        adb_send_v2(ADB_CMD_AUTH, 2, 0, sig, sig_len);
                    }
                }
                break;
            case ADB_CMD_OKAY:
                s_remote_id = hdr.arg1 ? hdr.arg1 : hdr.arg0;
                break;
            case ADB_CMD_WRTE:
                /* 按行送 UI */
                {
                    for (size_t i = 0; i < hdr.data_len; i++) {
                        if (payload[i] == '\n') {
                            s_line_buf[s_line_len] = '\0';
                            if (s_line_cb && s_line_len) {
                                s_line_cb(s_line_buf, s_line_user);
                            }
                            s_line_len = 0;
                        } else if (payload[i] != '\r' && s_line_len < sizeof(s_line_buf) - 1) {
                            s_line_buf[s_line_len++] = (char)payload[i];
                        }
                    }
                }
                adb_send_v2(ADB_CMD_OKAY, s_remote_id, s_local_id, NULL, 0);
                break;
            case ADB_CMD_CLSE:
                s_remote_id = 0;
                break;
            default:
                break;
            }
            p += ADB_HDR_SZ + hdr.data_len;
            remain -= ADB_HDR_SZ + hdr.data_len;
        }
    }
    /* 重新挂起收包 */
    if (s_dev_open && s_running) {
        usb_host_transfer_submit(t);
    }
}

/* ---------------- 握手 ---------------- */
static void adb_handshake(void)
{
    ESP_LOGI(TAG, "handshake: CNXN ->");
    if (adb_send_v2(ADB_CMD_CNXN, ADB_VERSION, ADB_MAX_PAYLOAD, NULL, 0) != 0) return;
    if (s_ready) return; /* 已授权过 */

    /* 手机回 AUTH(1 RSAPUBLICKEY) 或直接 CNXN */
    /* 收包由常驻 in transfer 完成 (in_xfer_done_cb -> 处理), 此处等待 ready */
}

/* ---------------- USB host 事件与任务 ---------------- */
static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        /* v5.5: 事件只给 new_dev.address, 用它打开设备 */
        ESP_LOGI(TAG, "new device addr=%u", msg->new_dev.address);
        if (usb_host_device_open(s_client, msg->new_dev.address, &s_dev) != ESP_OK) {
            s_dev = NULL;
            return;
        }
        s_dev_open = true;
        /* 建收发 transfer */
        usb_host_transfer_alloc(ADB_HDR_SZ + ADB_MAX_PAYLOAD, 0, &s_xfer_in);
        usb_host_transfer_alloc(ADB_HDR_SZ + ADB_MAX_PAYLOAD, 0, &s_xfer_out);
        s_xfer_in->device_handle = s_dev;
        s_xfer_in->callback = in_xfer_done_cb;
        s_xfer_out->device_handle = s_dev;
        s_xfer_out->callback = out_done_cb;

        /* 描述符 (v5.5: (dev_hdl, &desc) 两参) */
        const usb_config_desc_t *cfg_desc = NULL;
        usb_host_get_active_config_descriptor(s_dev, &cfg_desc);
        if (!cfg_desc) {
            ESP_LOGW(TAG, "no config descriptor");
            s_dev_open = false;
            usb_host_device_close(s_client, s_dev);
            s_dev = NULL;
            return;
        }
        /* 手动遍历接口描述符, 找 ADB (0x42/0x01/0x01) */
        const usb_intf_desc_t *itf = NULL;
        int offset = cfg_desc->bLength;
        while (offset < cfg_desc->wTotalLength) {
            const usb_intf_desc_t *d =
                (const usb_intf_desc_t *)((const uint8_t *)cfg_desc + offset);
            if (d->bLength == 0) {
                break;
            }
            if (d->bDescriptorType == 0x04 && /* INTERFACE */
                d->bInterfaceClass == 0x42 && d->bInterfaceSubClass == 0x01 &&
                d->bInterfaceProtocol == 0x01) {
                itf = d;
                break;
            }
            offset += d->bLength;
        }
        if (!itf) {
            ESP_LOGW(TAG, "not an ADB device");
            s_dev_open = false;
            usb_host_device_close(s_client, s_dev);
            s_dev = NULL;
            return;
        }
        s_itf_num = itf->bInterfaceNumber;
        usb_host_interface_claim(s_client, s_dev, s_itf_num, 0);
        /* 找 bulk EP (usb_parse_endpoint_descriptor_by_index(itf, idx, wTotalLength, &off)) */
        int ep_off = 0;
        for (int p = 0; p < itf->bNumEndpoints; p++) {
            const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(
                itf, p, cfg_desc->wTotalLength, &ep_off);
            if (!ep) break;
            if ((ep->bmAttributes & 0x03) == 0x02) { /* BULK */
                if (ep->bEndpointAddress & 0x80) {
                    s_xfer_in->bEndpointAddress = ep->bEndpointAddress;
                } else {
                    s_ep_out_addr = ep->bEndpointAddress;
                }
            }
        }
        /* 挂起常驻 IN */
        s_xfer_in->num_bytes = ADB_HDR_SZ + ADB_MAX_PAYLOAD;
        usb_host_transfer_submit(s_xfer_in);
        /* 握手 */
        adb_handshake();
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGW(TAG, "device gone");
        s_dev_open = false;
        s_ready = false;
        if (s_xfer_in) usb_host_transfer_free(s_xfer_in);
        if (s_xfer_out) usb_host_transfer_free(s_xfer_out);
        s_xfer_in = s_xfer_out = NULL;
        if (s_dev) {
            usb_host_device_close(s_client, s_dev);
            s_dev = NULL;
        }
    }
}

static void adb_host_task(void *arg)
{
    while (s_running) {
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

/* ---------------- 对外 ---------------- */
int adb_client_init(void)
{
    if (s_running) return 0;
    if (adb_rsa_init() != 0) return -1;
    usb_host_config_t cfg = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    if (usb_host_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed");
        return -1;
    }
    usb_host_client_config_t ccfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = { .client_event_callback = client_event_cb, },
    };
    if (usb_host_client_register(&ccfg, &s_client) != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_client_register failed");
        return -1;
    }
    s_out_done = xSemaphoreCreateBinary();
    s_running = true;
    if (xTaskCreate(adb_host_task, "adbhost", 6144, NULL, 5, NULL) != pdPASS) return -1;
    ESP_LOGI(TAG, "USB host ADB client ready");
    return 0;
}

int adb_client_start(void) { return 0; }
void adb_client_stop(void) { s_running = false; }
bool adb_client_ready(void) { return s_ready; }

int adb_client_shell(const char *cmd)
{
    if (!s_ready) return -1;
    char service[ADB_MAX_PAYLOAD];
    snprintf(service, sizeof(service), "shell:%s", cmd);
    uint32_t local = ++s_local_id;
    return adb_send_v2(ADB_CMD_OPEN, local, 0, (const uint8_t *)service, strlen(service));
}

int adb_client_set_line_cb(adb_line_cb_t cb, void *user)
{
    s_line_cb = cb;
    s_line_user = user;
    return 0;
}

/* ---------------- TinyUSB HID class 回调 ([硬件相关·需真机验证]) ----------------
 * 全固件唯一定义点: FIDO (app_security_key) 与 BadUSB 键盘 (app_hackerkit)
 * 互斥使用 HID, 由 usb_arbiter 切换时更新 __hid_report_desc 指向各自的报告描述符. */

uint8_t const *__hid_report_desc;

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return __hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    memset(buffer, 0, reqlen);
    return (uint16_t)reqlen;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type;
    if (bufsize == 0) return;
    /* 路由到当前激活的 HID 消费者 (FIDO CTAP demux 等) */
    if (__hid_rx_fn) {
        __hid_rx_fn(buffer, bufsize);
    }
}

/* adb_client.c — USB Host ADB 客户端 ([硬件相关·需真机验证])
 *
 * 手机 (device) ←OTG→ P4 (host): 枚举 ADB 接口 (0x42/0x01/0x01) →
 * CNXN → AUTH(RSAPUBLICKEY) → 手机弹授权 → AUTH(SIGNATURE) → shell 通道.
 *
 * 状态机 (单任务 adb_host_task 驱动):
 *   IDLE ──(枚举到 ADB 接口)──> HANDSHAKE
 *   HANDSHAKE: 发 CNXN →
 *       收 AUTH arg0=1 (RSAPUBLICKEY) → 回公钥 ("host:" + b64(SPKI))
 *       收 AUTH arg0=2 (SIGNATURE, 20B token) → SHA1 + RSASSA-PKCS1-v1_5 签名回传
 *       收 CNXN → s_ready=true → OPEN "shell:" 常驻流 → (OKAY 记下 remote_id)
 *   READY: WRTE 到 remote_id 发命令; 收 WRTE 按行送 s_line_cb 并回 OKAY.
 *   DEV_GONE → 回 IDLE 重新握手.
 */
#include "adb_client.h"
#include "adb_proto.h"
#include "adb_rsa.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "usb/usb_host.h"
#include "usb/usb_helpers.h"

static const char *TAG = "adbtool";

/* ---------------- USB Host ---------------- */
static usb_host_client_handle_t s_client;
static usb_device_handle_t s_dev;
static uint8_t s_itf_num, s_ep_in_addr, s_ep_out_addr;
static volatile bool s_running;
static volatile bool s_ready;      /* 授权完成 (CNXN 已收到) */
static adb_line_cb_t s_line_cb;
static void *s_line_user;
static SemaphoreHandle_t s_lock;   /* 保护 adb_send / shell 与 rx 线程互斥 */

static volatile uint32_t s_local_id = 1;   /* 本端 stream id (shell: 流) */
static volatile uint32_t s_remote_id;      /* OKAY 时回填的手机端 id */
static volatile bool s_stream_open;        /* "shell:" 流已 OPEN */

static uint8_t s_rx_data[ADB_MAX_PAYLOAD + 1]; /* +1 便于按 C 字符串处理 */
static adb_msg_hdr_t s_rx_hdr;

/* ---------------- 同步 bulk 传输 ---------------- */
typedef struct {
    SemaphoreHandle_t done;
    bool timed_out;
} xfer_sync_t;

static void xfer_done_cb(usb_transfer_t *t)
{
    xfer_sync_t *sync = (xfer_sync_t *)t->context;
    xSemaphoreGive(sync->done);
}

/* 简化阻塞 bulk 传输: >0 = 实际字节数, <0 = 失败/超时 */
static int usb_dev_bulk_transfer(usb_device_handle_t dev, uint8_t ep_addr,
                                 uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (!dev || len == 0 || len > 65536) return -1;
    usb_transfer_t *t = usb_host_transfer_alloc(len, 0);
    if (!t) return -1;

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done) { usb_host_transfer_free(t); return -1; }
    static xfer_sync_t sync; /* 单任务串行使用 */
    sync.done = done;
    sync.timed_out = false;

    t->device_handle = dev;
    t->bEndpointAddress = ep_addr;
    t->num_bytes = len;
    if (ep_addr & 0x80) {
        /* IN: 数据收进 t->data, 外层再拷走 */
    } else {
        memcpy(t->data, data, len);
    }
    t->context = &sync;
    t->callback = xfer_done_cb;

    if (usb_host_transfer_submit(t) != ESP_OK) {
        usb_host_transfer_free(t);
        vSemaphoreDelete(done);
        return -1;
    }
    int ret = -1;
    if (xSemaphoreTake(done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        ret = (t->status == USB_TRANSFER_STATUS_COMPLETED) ? (int)t->actual_num_bytes : -1;
        if (ep_addr & 0x80 && ret > 0) memcpy(data, t->data, ret);
    } else {
        usb_host_transfer_abort(t);
    }
    usb_host_transfer_free(t);
    vSemaphoreDelete(done);
    return ret;
}

/* 收一条完整 ADB 报文 (头 + payload); 0 = 成功 */
static int adb_recv_msg(adb_msg_hdr_t *hdr, uint8_t *data, uint32_t timeout_ms)
{
    if (usb_dev_bulk_transfer(s_dev, s_ep_in_addr, (uint8_t *)hdr, ADB_HDR_SZ,
                              timeout_ms) != ADB_HDR_SZ) {
        return -1;
    }
    if (adb_hdr_check(hdr) != 0) return -1;
    if (hdr->data_len > 0) {
        if (hdr->data_len > ADB_MAX_PAYLOAD) return -1;
        if (usb_dev_bulk_transfer(s_dev, s_ep_in_addr, data, hdr->data_len,
                                  timeout_ms) != (int)hdr->data_len) {
            return -1;
        }
        data[hdr->data_len] = '\0';
    }
    return 0;
}

/* 发送报文: 头与 payload 分两次 bulk 传输 (ADB 标准做法) */
static int adb_send(uint32_t cmd, uint32_t a0, uint32_t a1,
                    const uint8_t *data, size_t len)
{
    adb_msg_hdr_t hdr;
    adb_pack(&hdr, cmd, a0, a1, data, len);
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return -1;
    int r = usb_dev_bulk_transfer(s_dev, s_ep_out_addr, (uint8_t *)&hdr,
                                  ADB_HDR_SZ, 3000);
    if (r == ADB_HDR_SZ && len > 0) {
        r = usb_dev_bulk_transfer(s_dev, s_ep_out_addr, (uint8_t *)data, len, 3000);
        r = (r == (int)len) ? (int)len : -1;
    }
    xSemaphoreGive(s_lock);
    return (r < 0) ? -1 : 0;
}

/* ---------------- 枚举: 找 ADB 接口 (0x42/0x01/0x01) ---------------- */
static void adb_usb_event(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (msg->type == USB_HOST_EVENT_NEW) {
        usb_device_handle_t dev;
        if (usb_host_device_open(s_client, msg->device.address) != ESP_OK) return;
        const usb_device_desc_t *desc;
        usb_host_get_device_descriptor(s_client, msg->device.address, &desc);
        (void)desc;
        const usb_config_desc_t *cfg;
        usb_host_get_active_config_descriptor(s_client, msg->device.address, &cfg);
        const usb_intf_desc_t *adb_itf = NULL;
        int itf_idx = -1;
        for (int i = 0; i < cfg->bNumInterfaces && !adb_itf; i++) {
            const usb_intf_desc_t *itf = usb_parse_interface_descriptor(cfg, i, 0, &itf_idx);
            if (itf->bInterfaceClass == 0x42 && itf->bInterfaceSubClass == 0x01 &&
                itf->bInterfaceProtocol == 0x01) {
                adb_itf = itf;
            }
        }
        if (!adb_itf) {
            usb_host_device_close(s_client, msg->device.address);
            return;
        }
        s_dev = dev;
        s_itf_num = adb_itf->bInterfaceNumber;
        s_ep_in_addr = 0;
        s_ep_out_addr = 0;
        /* 收集该接口的 bulk EP */
        for (int p = 0; p < adb_itf->bNumEndpoints; p++) {
            const usb_ep_desc_t *ep =
                usb_parse_endpoint_descriptor_by_index(adb_itf, p, cfg);
            if ((ep->bmAttributes & 0x03) == 2) { /* bulk */
                if (ep->bEndpointAddress & 0x80) s_ep_in_addr = ep->bEndpointAddress;
                else s_ep_out_addr = ep->bEndpointAddress;
            }
        }
        usb_host_interface_open(s_client, dev, s_itf_num, 0);
        ESP_LOGI(TAG, "ADB interface found (itf %d, IN 0x%02x, OUT 0x%02x)",
                 s_itf_num, s_ep_in_addr, s_ep_out_addr);
        s_ready = false;
        s_stream_open = false;
        s_remote_id = 0;
        xSemaphoreTake(s_lock, 0); /* 清零计数, 握手在 task 中触发 */
    } else if (msg->type == USB_HOST_EVENT_DEV_GONE && s_dev) {
        ESP_LOGW(TAG, "phone disconnected");
        s_ready = false;
        s_stream_open = false;
        s_remote_id = 0;
        s_dev = NULL;
    }
}

/* ---------------- 握手状态机 ---------------- */
static int auth_reply_signature(const uint8_t *token, size_t token_len)
{
    /* 对 token 做 SHA1 → RSASSA-PKCS1-v1_5 签名 → AUTH(arg0=2) 回传 */
    uint8_t hash[20];
    if (mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1),
                   token, token_len, hash) != 0) {
        return -1;
    }
    uint8_t sig[256];
    size_t sig_len = sizeof(sig);
    if (adb_rsa_sign(hash, sizeof(hash), sig, &sig_len) != 0) return -1;
    return adb_send(ADB_CMD_AUTH, 2, 0, sig, sig_len);
}

static void adb_handshake(void)
{
    ESP_LOGI(TAG, "handshake: sending CNXN");
    if (adb_send(ADB_CMD_CNXN, ADB_VERSION, ADB_MAX_PAYLOAD, NULL, 0) != 0) return;

    /* 循环处理 AUTH, 直到收到 CNXN (已授权设备直接回 CNXN) */
    for (int rounds = 0; rounds < 8; rounds++) {
        if (adb_recv_msg(&s_rx_hdr, s_rx_data, 5000) != 0) {
            ESP_LOGW(TAG, "handshake rx timeout");
            return;
        }
        if (s_rx_hdr.cmd == ADB_CMD_CNXN) {
            s_ready = true;
            ESP_LOGI(TAG, "ADB authorized & connected");
            if (s_line_cb) s_line_cb("[adb] 手机已授权, 连接就绪", s_line_user);
            return;
        }
        if (s_rx_hdr.cmd == ADB_CMD_AUTH && s_rx_hdr.arg0 == 1) {
            /* RSAPUBLICKEY: 回公钥, 手机弹授权框 */
            uint8_t pubkey[ADB_MAX_PAYLOAD];
            int pklen = adb_rsa_pubkey_adb(pubkey, sizeof(pubkey));
            if (pklen < 0 || adb_send(ADB_CMD_AUTH, 1, 0, pubkey, pklen) != 0) return;
            ESP_LOGI(TAG, "pubkey sent, waiting for user confirmation on phone...");
            continue;
        }
        if (s_rx_hdr.cmd == ADB_CMD_AUTH && s_rx_hdr.arg0 == 2) {
            /* SIGNATURE: 20B token, 签名回传 */
            if (auth_reply_signature(s_rx_data, s_rx_hdr.data_len) != 0) {
                ESP_LOGE(TAG, "token sign failed");
                return;
            }
            continue;
        }
        ESP_LOGW(TAG, "unexpected msg 0x%08x during handshake", s_rx_hdr.cmd);
    }
    ESP_LOGW(TAG, "handshake: too many auth rounds");
}

/* ---------------- shell 流数据接收 (按行回调) ---------------- */
static char s_line_buf[ADB_MAX_PAYLOAD];
static size_t s_line_len;

static void rx_stream_data(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            s_line_buf[s_line_len] = '\0';
            if (s_line_cb) s_line_cb(s_line_buf, s_line_user);
            s_line_len = 0;
        } else if (c != '\r' && s_line_len < sizeof(s_line_buf) - 1) {
            s_line_buf[s_line_len++] = c;
        }
    }
}

/* ---------------- 主任务 ---------------- */
static void adb_host_task(void *arg)
{
    (void)arg;
    usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = { .client_event_callback = adb_usb_event, },
    };
    /* 只注册一次 (usb_host_install 已在 adb_client_init 完成) */
    if (usb_host_client_register(s_client, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_client_register failed");
        vTaskDelete(NULL);
        return;
    }

    while (s_running) {
        uint32_t flags;
        usb_host_client_handle_events(s_client, 0);

        if (!s_dev || !s_ep_out_addr) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!s_ready) {
            adb_handshake();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        /* READY: 首次打开常驻 "shell:" 流 */
        if (!s_stream_open) {
            const char *service = "shell:";
            if (adb_send(ADB_CMD_OPEN, s_local_id, 0,
                         (const uint8_t *)service, strlen(service)) == 0) {
                ESP_LOGI(TAG, "OPEN 'shell:' stream (local id %u)", (unsigned)s_local_id);
                s_stream_open = true; /* remote_id 待 OKAY 回填 */
            } else {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        /* 收发循环: 处理 OKAY/WRTE/CLSE */
        if (adb_recv_msg(&s_rx_hdr, s_rx_data, 200) == 0) {
            switch (s_rx_hdr.cmd) {
            case ADB_CMD_OKAY:
                /* OKAY(arg0=remote_id, arg1=local_id) */
                s_remote_id = s_rx_hdr.arg0;
                break;
            case ADB_CMD_WRTE:
                if (s_line_cb) rx_stream_data(s_rx_data, s_rx_hdr.data_len);
                /* 回 OKAY: arg0=本端 id, arg1=对端 id */
                adb_send(ADB_CMD_OKAY, s_local_id, s_rx_hdr.arg0, NULL, 0);
                break;
            case ADB_CMD_CLSE:
                ESP_LOGW(TAG, "shell stream closed by peer, reopening");
                s_stream_open = false;
                s_remote_id = 0;
                break;
            default:
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

/* ---------------- 对外 ---------------- */
int adb_client_init(void)
{
    if (s_lock) return 0;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return -1;
    if (adb_rsa_init() != 0) return -1;
    usb_host_config_t cfg = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    if (usb_host_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed");
        return -1;
    }
    return 0;
}

int adb_client_start(void)
{
    if (!s_lock) return -1;
    s_running = true;
    if (xTaskCreate(adb_host_task, "adbhost", 8192, NULL, 5, NULL) != pdPASS) return -1;
    return 0;
}

void adb_client_stop(void)
{
    s_running = false;
}

bool adb_client_ready(void)
{
    return s_ready;
}

int adb_client_shell(const char *cmd)
{
    if (!s_ready || !s_stream_open || s_remote_id == 0) {
        ESP_LOGW(TAG, "shell request before stream ready");
        return -1;
    }
    /* 常驻 "shell:" 流: 直接把命令文本 WRTE 过去 (相当于在交互 shell 里敲一行) */
    char buf[ADB_MAX_PAYLOAD];
    int n = snprintf(buf, sizeof(buf), "%s\n", cmd);
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    return adb_send(ADB_CMD_WRTE, s_local_id, s_remote_id, (const uint8_t *)buf, n);
}

int adb_client_set_line_cb(adb_line_cb_t cb, void *user)
{
    s_line_cb = cb;
    s_line_user = user;
    return 0;
}

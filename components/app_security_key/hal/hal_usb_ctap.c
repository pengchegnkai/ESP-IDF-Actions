/* hal_usb_ctap.c — TinyUSB CTAP over HID 传输 ([硬件相关·需真机验证])
 *
 * CTAP-HID (FIDO HID spec, bcdHID 0x0120) 设备描述符 + CTAP2 字节流 ↔
 * core/ctap2 的胶水. 借鉴 pico-fido 的 tinyusb fido HID 传输层.
 *
 * 实装说明 (lead 集成, esp_tinyusb 2.4.0):
 *   - attach  = tinyusb_driver_install (TINYUSB_DEFAULT_CONFIG + descriptor 覆写)
 *   - detach  = tinyusb_driver_uninstall (让位 BadUSB 的 hal_usb_hid_init)
 *   - 收      = tud_hid_set_report_cb (64B OUT report) → ctap2_demux_push
 *   - 发      = ctap2_frame_msg 切帧 → tud_hid_report 逐 64B report 发送
 *   - 签名    = 软件占位 (TODO(lead#3/4): C6 通道待 esp_hosted 2.x 升级)
 */
#include "hal_sk.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "ctap2.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"
#include "usb_arbiter.h"

static const char *TAG = "sk_usb";

/* ---------------- CTAP-HID 描述符 (bcd 0x0120, report 64B) ----------------
 * Usage Page = FIDO Alliance (0xFF1C), Usage = 0x01; Input/Output report
 * 均 65B (report id 1 + 64B data). */
static const uint8_t s_fido_report_desc[] = {
    0x06, 0xD0, 0xF1, /* Usage Page (FIDO Alliance) */
    0x09, 0x01,       /* Usage (FIDO CTAP HID) */
    0xA1, 0x01,       /* Collection (Application) */
    0x09, 0x20,       /*   Usage (Input Report Data) */
    0x15, 0x00,       /*   Logical Min 0 */
    0x26, 0xFF, 0x00, /*   Logical Max 255 */
    0x75, 0x08,       /*   Report Size 8 */
    0x95, 0x40,       /*   Report Count 64 */
    0x81, 0x02,       /*   Input (Data,Var,Abs) */
    0x09, 0x21,       /*   Usage (Output Report Data) */
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40,
    0x91, 0x02,       /*   Output (Data,Var,Abs) */
    0xC0,             /* End Collection */
};

/* USB 设备描述符 (FIDO2 HID) */
static const tusb_desc_device_t s_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0120,           /* CTAP-HID 要求 bcd 1.20 */
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x20A0,           /* fido 键盘常用 VID (Nitrokey 系) */
    .idProduct          = 0x4287,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 1,
};

static const char *s_string_desc[] = {
    "SecurityKey",        /* 1: Manufacturer */
    "SecurityKey FIDO2",  /* 2: Product */
    "SK000001",           /* 3: Serial */
};

#define HK_HID_EPIN_ADDR    0x81
#define HK_HID_EPIN_SIZE    0x40 /* 64B: CTAP-HID 规范要求 */
#define HK_HID_INTERVAL     1    /* ms */

#define SK_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t s_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, SK_CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(0, 0, false, sizeof(s_fido_report_desc),
                       HK_HID_EPIN_ADDR, HK_HID_EPIN_SIZE, HK_HID_INTERVAL),
};

/* ---------------- 状态与胶水 ---------------- */
typedef struct {
    bool attached;
    hal_usb_ctap_msg_cb_t cb;
    void *user;
    ctap2_demux_t demux;   /* CTAP-HID 分包器 ([纯逻辑] core 复用) */
    uint8_t last_cmd;      /* 最近收到的 CTAP2 命令 (应答帧同命令号) */
    uint8_t out[CTAPHID_REPORT_SZ];
} sk_usb_t;

static sk_usb_t s_usb;

/* TinyUSB 收到 64B report 的回调: feed 进 demux → 完整 CTAP2 消息 → 通知 app 层. */
static void usb_ctap_report_in(const uint8_t report[CTAPHID_REPORT_SZ])
{
    ctap2_msg_t msg;
    int rc = ctap2_demux_push(&s_usb.demux, report, CTAPHID_REPORT_SZ, &msg);
    if (rc == 1) {
        s_usb.last_cmd = msg.cmd;
        if (s_usb.cb) {
            s_usb.cb(msg.cid, msg.payload, msg.len, s_usb.user);
        }
    } else if (rc < 0) {
        ESP_LOGW(TAG, "CTAP-HID frame error, resynced");
        ctap2_demux_init(&s_usb.demux);
    }
}

int hal_usb_ctap_attach(void)
{
    /* 红线③: 只有 arbiter 判定 FIDO2 持有 USB 时才允许挂载 */
    if (usb_arb_owner_get() != USB_ARB_OWNER_FIDO2) {
        ESP_LOGE(TAG, "attach rejected: arbiter not FIDO2");
        return -1;
    }
    if (s_usb.attached) return 0;
    ctap2_demux_init(&s_usb.demux);
    s_usb.last_cmd = 0;

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &s_desc_device;
    tusb_cfg.descriptor.string = s_string_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]);
    tusb_cfg.descriptor.full_speed_config = s_desc_configuration;
    tusb_cfg.descriptor.high_speed_config = s_desc_configuration;

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return -1;
    }
    s_usb.attached = true;
    ESP_LOGI(TAG, "FIDO2 CTAP-HID attached");
    return 0;
}

void hal_usb_ctap_detach(void)
{
    if (!s_usb.attached) return;
    esp_err_t err = tinyusb_driver_uninstall();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tinyusb_driver_uninstall: %s", esp_err_to_name(err));
    }
    s_usb.attached = false;
    ESP_LOGI(TAG, "FIDO2 CTAP-HID detached");
}

int hal_usb_ctap_set_msg_cb(hal_usb_ctap_msg_cb_t cb, void *user)
{
    s_usb.cb = cb;
    s_usb.user = user;
    return 0;
}

int hal_usb_ctap_send(uint32_t cid, const uint8_t *msg, size_t len)
{
    if (!s_usb.attached) return -1;
    if (len == 0 || msg == NULL) return -1;

    uint8_t framed[CTAPHID_MSG_MAX + CTAPHID_REPORT_SZ];
    /* 应答帧命令号 = 最近收到的请求命令 (CTAP-HID 规范) */
    int n = ctap2_frame_msg(cid, s_usb.last_cmd, msg, len, framed, sizeof(framed));
    if (n <= 0) return -1;

    /* 按 64B report 逐帧发送 ( tud_hid_report: 不带 report id, CTAP 描述符未启用 ID) */
    for (int off = 0; off < n; off += CTAPHID_REPORT_SZ) {
        if (!tud_hid_report(0, framed + off, CTAPHID_REPORT_SZ)) {
            ESP_LOGW(TAG, "tud_hid_report failed at %d/%d", off, n);
            return -1;
        }
    }
    return 0;
}

/* ---------------- TinyUSB HID class 回调 (weak 覆盖, 全固件唯一定义) ---------------- */

/* GET_REPORT: CTAP-HID 规范要求返回当前待发送数据; 简化为空帧. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    memset(buffer, 0, reqlen);
    return (uint16_t)reqlen;
}

/* SET_REPORT (control endpoint) 与 interrupt OUT 共用同一解包路径 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type;
    if (bufsize == 0) return;
    usb_ctap_report_in(buffer);
}

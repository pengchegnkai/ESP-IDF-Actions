/* hal_usb_hid.c — [硬件相关·需真机验证] TinyUSB HID 键盘实现
 *
 * 基于 espressif/esp_tinyusb v2 (idf_component.yml 声明 ^2.0.0),
 * 实现 hk_usb_ops_t::send_keyboard_report (6 键 rollover keyboard).
 *
 * 指令来源: /sdcard/payloads/*.txt (借鉴 EvilKey-firmware, USB Rubber Ducky 思路).
 *
 * 集成时需要的 Kconfig 项 (由 lead 统一加, 本文件仅列出):
 *   CONFIG_TINYUSB_ENABLED=y
 *   CONFIG_TINYUSB_HID_COUNT=1          (HID 接口数量, 若使用内置 HID 类)
 *   CONFIG_TINYUSB_CDC_COUNT=0          (不需要 CDC)
 *   CONFIG_TINYUSB_MSC_COUNT=0          (不需要 MSC)
 *
 * 需 lead 集成时核对的 API (已核对 v5.5 文档 + esp_tinyusb ^2.0.0):
 *   1. tinyusb_config_t: P4 高速设备使用 .fs_configuration_descriptor /
 *      .hs_configuration_descriptor / .qualifier_descriptor (无 *_size 字段).
 *   2. tinyusb_driver_install() / tinyusb_driver_uninstall() 返回 esp_err_t
 *      (CI 编译最终验证).
 *   3. tud_hid_keyboard_report(instance, modifier, keycode[6]) 来自 TinyUSB
 *      class/hid/hid_device.h; 接口实例号 HK_HID_ITF_KEYBOARD=0
 *      (CONFIG_TINYUSB_HID_COUNT=1).
 */
#include "hal_usb_hid.h"

#include <string.h>

#include "tinyusb.h"
#include "tusb.h"

#include "esp_log.h"

static const char *TAG = "hk_usb_hid";

#define HK_HID_ITF_KEYBOARD 0

/* ─── 描述符 (USB 设备描述符 + HID 报告描述符) ─────────────────────────── */

#define HK_BCD_USB       0x0200
#define HK_VID           0x2E8A /* Raspberry Pi (Rubber Ducky 风格伪装) */
#define HK_PID           0x0003
#define HK_BCD_DEVICE    0x0100

/* HID 报告描述符: 6 键 rollover 键盘 (modifier byte + reserved + keycode[6]) */
static const uint8_t hk_hid_report_desc[] = {
    0x05, 0x01,  /* Usage Page (Generic Desktop)      */
    0x09, 0x06,  /* Usage (Keyboard)                  */
    0xA1, 0x01,  /* Collection (Application)          */
    0x05, 0x07,  /*   Usage Page (Key Codes)          */
    0x19, 0xE0,  /*   Usage Minimum (224) — modifiers */
    0x29, 0xE7,  /*   Usage Maximum (231)             */
    0x15, 0x00,  /*   Logical Minimum (0)             */
    0x25, 0x01,  /*   Logical Maximum (1)             */
    0x75, 0x01,  /*   Report Size (1)                 */
    0x95, 0x08,  /*   Report Count (8)                */
    0x81, 0x02,  /*   Input (Data, Var, Abs)          */
    0x95, 0x01,  /*   Report Count (1)                */
    0x75, 0x08,  /*   Report Size (8)                 */
    0x81, 0x01,  /*   Input (Const) — reserved        */
    0x95, 0x06,  /*   Report Count (6)                */
    0x75, 0x08,  /*   Report Size (8)                 */
    0x15, 0x00,  /*   Logical Minimum (0)             */
    0x25, 0x65,  /*   Logical Maximum (101)           */
    0x05, 0x07,  /*   Usage Page (Key Codes)          */
    0x19, 0x00,  /*   Usage Minimum (0)               */
    0x29, 0x65,  /*   Usage Maximum (101)             */
    0x81, 0x00,  /*   Input (Data, Array)             */
    0xC0         /* End Collection                    */
};

#define HK_HID_REPORT_DESC_LEN (sizeof(hk_hid_report_desc))

/* USB 设备描述符 (键盘伪装) */
static const tusb_desc_device_t hk_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = USB_DESC_TYPE_DEVICE,
    .bcdUSB             = HK_BCD_USB,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = HK_VID,
    .idProduct          = HK_PID,
    .bcdDevice          = HK_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 1
};

/* 字符串描述符 */
static const char *hk_string_desc[] = {
    "HackerKit",        /* 1: Manufacturer */
    "HackerKit KBD",    /* 2: Product */
    "HK000001",         /* 3: Serial */
};

/* 配置描述符: 1 个 HID 键盘接口 (TUD_HID_DESCRIPTOR 内含报告描述符引用) */
#define HK_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define HK_HID_EPIN_ADDR    0x81
#define HK_HID_EPIN_SIZE    0x08
#define HK_HID_INTERVAL     1 /* ms, full speed */

static const uint8_t hk_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, HK_CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, HK_HID_REPORT_DESC_LEN,
                       HK_HID_EPIN_ADDR, HK_HID_EPIN_SIZE, HK_HID_INTERVAL),
};

/* TinyUSB 轮询/回调由 esp_tinyusb 内部任务处理 (v2 synchronous=false) */

bool hk_tud_send_report(const uint8_t report[8])
{
    if (!tud_hid_ready()) {
        return false; /* USB 未就绪 (未接入/被仲裁卸载) */
    }
    uint8_t keycode[6] = {0};
    memcpy(keycode, &report[2], 6);
    /* 修饰键 + keycode[6] 一次发送 */
    return tud_hid_keyboard_report(HK_HID_ITF_KEYBOARD, report[0], keycode);
}

/* hk_usb_ops_t::send_keyboard_report — [硬件相关·需真机验证] */
static void hal_send_keyboard_report(const uint8_t report[HK_REPORT_LEN])
{
    if (!hk_tud_send_report(report)) {
        ESP_LOGW(TAG, "HID report dropped (not ready)");
    }
}

/* [硬件相关·需真机验证] TinyUSB 驱动安装 (HID 键盘) */
esp_err_t hal_usb_hid_init(void)
{
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &hk_desc_device,
        .string_descriptor = hk_string_desc,
        .string_descriptor_count = sizeof(hk_string_desc) / sizeof(hk_string_desc[0]),
        .external_phy = false,
        /* P4 高速设备: full-speed / high-speed 配置描述符均指向同一数组,
         * qualifier 不使用 (无独立 HS 描述符需求). */
        .fs_configuration_descriptor = hk_desc_configuration,
        .hs_configuration_descriptor = hk_desc_configuration,
        .qualifier_descriptor = NULL,
    };
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "TinyUSB HID keyboard ready (VID=0x%04X PID=0x%04X)", HK_VID, HK_PID);
    return ESP_OK;
}

/* [硬件相关·需真机验证] 卸载 (USB 仲裁切换给 FIDO2 前调用) */
esp_err_t hal_usb_hid_deinit(void)
{
    return tinyusb_driver_uninstall();
}

/* 填充 hk_usb_ops_t (app_hackerkit.cpp 在 arb_request 成功后使用) */
void hal_usb_hid_get_ops(hk_usb_ops_t *ops)
{
    if (ops) {
        ops->send_keyboard_report = hal_send_keyboard_report;
    }
}

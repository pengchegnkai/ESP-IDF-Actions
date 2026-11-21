/**
 * @file sys_hal_usb.c
 * @brief USB / FIDO HID 层 —— CTAP2 over HID (TinyUSB)
 *
 * P4 目标 (微雪 ESP32-P4-WIFI6-Touch-LCD-4.3):
 *   本板 Type-C "USB OTG" 口接 ESP32-P4 专用 HS OTG 引脚 USBD_P/USBD_N
 *   (芯片引脚 50/49, 不经 GPIO 矩阵, 无 Kconfig 可配); esp_tinyusb 在 P4 的
 *   默认端口即 TINYUSB_PORT_HIGH_SPEED_0, 故直接用 TINYUSB_DEFAULT_CONFIG()。
 *   FIDO HID: usage page 0xF1D0 / usage 0x03 (CTAPHID), 64B IN/OUT 报告,
 *   报告 ID 0 (CTAP 规范允许 0)。
 *   attach/detach → sys_event_report_usb(true/false) (SYS_EVENT_USB_IN/OUT)。
 *   手动激活路由: set_active(false) 时丢弃主机 CTAP 数据, 不应答。
 * 其他芯片目标: 全部 stub 返回 ESP_ERR_NOT_SUPPORTED (house pattern)。
 */
#include <string.h>
#include "sys_hal/sys_hal.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tinyusb.h"               /* esp_tinyusb: install/config/event */
#include "tinyusb_default_config.h" /* TINYUSB_DEFAULT_CONFIG (P4 → HS 端口) */
#include "tusb.h"                  /* TinyUSB 核心 + HID 类 (CFG_TUD_HID=1) */
#include "sys_core/sys_event_bus.h" /* sys_event_report_usb */

static const char *TAG = "sys_hal_usb";

#define FIDO_REPORT_LEN 64  /* CTAPHID 报告固定 64 字节 */

static bool            s_inited  = false;
static volatile bool   s_active  = false;  /* 手动激活路由开关 (默认 OFF) */
static void (*s_rx_cb)(const uint8_t *data, int len, void *ctx) = NULL;
static void            *s_rx_ctx = NULL;
static SemaphoreHandle_t s_send_mux;  /* 串行化 IN 报告发送 (LVGL/USB 两任务可用) */

/* ---------- USB 描述符 (FIDO CTAP2 HID) ---------- */

/* HID 报告描述符: usage page 0xF1D0 (FIDO), usage 0x03 (CTAPHID),
 * Data In/Out 各 64x8bit, 无报告 ID (CTAP 用 report ID 0) */
static const uint8_t s_fido_hid_desc[] = {
    HID_USAGE_PAGE_N(HID_USAGE_PAGE_FIDO, 2),  /* 0x06 0xD0 0xF1 */
    HID_USAGE(0x03),                           /* CTAPHID */
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_USAGE(HID_USAGE_FIDO_DATA_IN),         /* 0x20 */
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX_N(0xFF, 2),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(FIDO_REPORT_LEN),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_USAGE(HID_USAGE_FIDO_DATA_OUT),        /* 0x21 */
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX_N(0xFF, 2),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(FIDO_REPORT_LEN),
    HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

/* 配置描述符: 1 配置 / 1 HID 接口 / EP01 OUT + EP81 IN (中断, 64B, 1ms)。
 * 用 TinyUSB 内置 TUD_HID_INOUT_DESCRIPTOR (接口+HID 类描述符+2端点一体),
 * 避免手拼字节出错; wMaxPacketSize=64 在 FS/HS 均合法 → FS/HS 两份配置同字节。 */
#define FIDO_CFG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t s_fido_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, FIDO_CFG_TOTAL_LEN, 0, 100),
    /* itf=0, 无字符串, boot 协议 none, 报告描述符, EP01 OUT + EP81 IN, 64B, 1ms */
    TUD_HID_INOUT_DESCRIPTOR(0, 0, 0, sizeof(s_fido_hid_desc),
                             0x01, 0x81, FIDO_REPORT_LEN, 1),
};

/* ---------- TinyUSB 回调 ---------- */

/* 设备事件 (esp_tinyusb 任务上下文): 插拔 → 系统事件总线 */
static void usb_evt_cb(tinyusb_event_t *evt, void *arg)
{
    (void)arg;
    switch (evt->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(TAG, "usb attached");
        sys_event_report_usb(true);
        break;
    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(TAG, "usb detached");
        sys_event_report_usb(false);
        break;
    default:
        break;
    }
}

/* GET HID REPORT DESCRIPTOR */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_fido_hid_desc;
}

/* GET_REPORT (控制传输): FIDO v1 不支持按需取报告 → 返回 0 (协议 STALL) */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

/* OUT 数据 / SET_REPORT (TinyUSB 任务上下文) —— CTAP2 命令入口 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;    /* 报告 ID 0 */
    (void)report_type;
    if (!s_inited || !buffer || bufsize <= 0) {
        return;
    }
    if (!s_active) {
        /* 未激活 (手动激活模式): 丢弃 CTAP 数据, 不应答 */
        return;
    }
    if (s_rx_cb) {
        /* 注意: buffer 仅回调期间有效, 接收方需自行拷贝 */
        s_rx_cb(buffer, (int)bufsize, s_rx_ctx);
    }
}

/* ---------- 公共 API ---------- */

esp_err_t sys_hal_usb_fido_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    if (!s_send_mux) {
        s_send_mux = xSemaphoreCreateMutex();
        if (!s_send_mux) {
            return ESP_ERR_NO_MEM;
        }
    }

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(usb_evt_cb);
    /* P4 默认端口 = TINYUSB_PORT_HIGH_SPEED_0 (本板 OTG 口即 HS 口);
     * HS PHY 引脚固定于芯片内部 (USBD_P/USBD_N), 无需引脚配置 */
    cfg.descriptor.device = NULL;          /* 默认设备描述符 (Kconfig VID/PID) */
    cfg.descriptor.string = NULL;          /* 默认字符串描述符 */
    cfg.descriptor.full_speed_config = s_fido_cfg_desc;
#if (TUD_OPT_HIGH_SPEED)
    cfg.descriptor.high_speed_config = s_fido_cfg_desc;
    cfg.descriptor.qualifier = NULL;       /* 默认 qualifier 描述符 */
#endif

    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    s_active = false;  /* 手动激活模式: 开机默认 OFF */
    ESP_LOGI(TAG, "fido hid ready (usage page 0xF1D0/0x03, 64B reports)");
    return ESP_OK;
}

void sys_hal_usb_fido_set_active(bool active)
{
    s_active = active;
    ESP_LOGI(TAG, "fido routing %s", active ? "ON" : "OFF");
}

void sys_hal_usb_fido_register_rx_cb(void (*cb)(const uint8_t *data, int len, void *ctx),
                                     void *ctx)
{
    s_rx_cb  = cb;
    s_rx_ctx = ctx;
}

bool sys_hal_usb_fido_connected(void)
{
    return s_inited && tud_mounted();
}

void sys_hal_usb_fido_send(const uint8_t *data, int len)
{
    if (!data || len <= 0 || len > FIDO_REPORT_LEN) {
        ESP_LOGW(TAG, "send len %d invalid (max %d), drop", len, FIDO_REPORT_LEN);
        return;
    }
    if (!s_inited) {
        ESP_LOGW(TAG, "send before init, drop");
        return;
    }
    /* 发送可来自 LVGL 任务 (弹窗确认) 与 TinyUSB 任务 (自动应答), 需串行化 */
    if (xSemaphoreTake(s_send_mux, portMAX_DELAY) != pdTRUE) {
        return;
    }
    bool ok = tud_hid_report(0, data, (uint16_t)len);
    xSemaphoreGive(s_send_mux);
    if (!ok) {
        ESP_LOGW(TAG, "hid IN report xfer failed (device not ready?)");
    }
}

#else /* !CONFIG_IDF_TARGET_ESP32P4 —— 其他芯片目标 stub */

esp_err_t sys_hal_usb_fido_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void sys_hal_usb_fido_set_active(bool active)
{
    (void)active;
}

void sys_hal_usb_fido_register_rx_cb(void (*cb)(const uint8_t *data, int len, void *ctx),
                                     void *ctx)
{
    (void)cb;
    (void)ctx;
}

bool sys_hal_usb_fido_connected(void)
{
    return false;
}

void sys_hal_usb_fido_send(const uint8_t *data, int len)
{
    (void)data;
    (void)len;
}

#endif /* CONFIG_IDF_TARGET_ESP32P4 */

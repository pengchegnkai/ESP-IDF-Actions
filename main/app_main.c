/**
 * @file app_main.c
 * @brief 系统启动流程 —— 硬件 -> 事件总线 -> UI -> 默认 APP
 *
 * 初始化顺序 (有依赖, 不可乱):
 *   1. NVS           非易失存储 (WiFi/校准数据等依赖)
 *   2. 事件总线      其他子系统都会发布/订阅
 *   3. 硬件抽象层    屏幕/触摸/音频/I2C
 *   4. UI 子系统     LVGL 渲染任务
 *   5. 注册所有 APP
 *   6. 启动默认 APP (Launcher)
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"

#include "sys_core/sys_event_bus.h"
#include "sys_core/sys_app_manager.h"
#include "sys_hal/sys_hal.h"
#include "sys_ui/sys_ui.h"
#include "sys_core/sys_time_service.h"
#include "app_launcher/launcher_app.h"
#include "app_settings/app_settings.h"

static const char *TAG = "main";

/* 目标板卡: 微雪 ESP32-P4-WIFI6-Touch-LCD-4.3
 * LCD: 4.3" IPS 480x800 (竖屏原生), MIPI-DSI 2-lane, 驱动 ST7701
 * 触摸: GT911 电容 5 点 (I2C), I2S 音频: ES8311 + ES7210
 * 换板时只改这里的配置值 */
static const sys_hal_display_cfg_t DISPLAY_CFG = {
    .h_res = 480, .v_res = 800, .refresh_hz = 60,
};

static const sys_hal_touch_cfg_t TOUCH_CFG = {
    .x_max = 480, .y_max = 800,
};

/* 音频: ES8311 扬声器 + ES7210 双麦, 官方示例默认 16kHz/2ch/16bit */
static const sys_hal_audio_cfg_t AUDIO_CFG = {
    .sample_rate = 16000, .channels = 2,
};

/* 板载 I2C 总线: GT911 / ES8311 / ES7210 共用 (SDA=GPIO7, SCL=GPIO8) */
static const sys_hal_i2c_cfg_t I2C_CFG = {
    .port = 0, .scl_hz = 400000, .sda_gpio = 7, .scl_gpio = 8,
};

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== System Base boot ===");

    /* 1. NVS */
    init_nvs();

    /* 2. 事件总线 (最先建立, 其余子系统依赖) */
    ESP_ERROR_CHECK(sys_event_bus_init());

    /* 3. 硬件抽象层 (stub, 待板卡驱动接入) */
    sys_hal_display_t *display = NULL;
    sys_hal_touch_t   *touch   = NULL;
    sys_hal_i2c_t     *i2c     = NULL;
    ESP_ERROR_CHECK(sys_hal_display_init(&DISPLAY_CFG, &display));
    ESP_ERROR_CHECK(sys_hal_touch_init(&TOUCH_CFG, &touch));
    if (sys_hal_i2c_init(&I2C_CFG, &i2c) != ESP_OK) {
        ESP_LOGW(TAG, "i2c init failed, peripherals unavailable");
    }

    /* 音频 (失败不阻塞启动: 无音频硬件时仍可跑 UI) */
    sys_hal_audio_t *audio = NULL;
    if (sys_hal_audio_init(&AUDIO_CFG, &audio) == ESP_OK) {
        sys_hal_audio_set_volume(audio, 60);
    } else {
        ESP_LOGW(TAG, "audio init failed, sound disabled");
    }

    /* 4. UI 子系统 */
    ESP_ERROR_CHECK(sys_ui_init());

    /* 5. 系统时间服务 (1Hz TIME_TICK) */
    ESP_ERROR_CHECK(sys_time_service_init());

    /* 6. 注册 APP (后续新 APP 在这里追加) */
    ESP_ERROR_CHECK(sys_app_register(launcher_app_get()));
    ESP_ERROR_CHECK(sys_app_register(settings_app_get()));

    /* 7. 启动默认桌面 */
    ESP_ERROR_CHECK(sys_app_launch(LAUNCHER_APP_ID));

    ESP_LOGI(TAG, "system ready, foreground app: %s",
             sys_app_current() ? sys_app_current()->id : "(none)");
}

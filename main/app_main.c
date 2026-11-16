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
#include "sys_core/sys_settings.h"
#include "app_splash/splash_app.h"
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

/* 音频初始化放后台任务: 某些板卡 I2S/codec 初始化可能阻塞, 不能挡住 UI 启动 */
static void audio_boot_task(void *arg)
{
    (void)arg;
    sys_hal_audio_t *audio = NULL;
    if (sys_hal_audio_init(&AUDIO_CFG, &audio) == ESP_OK) {
        int vol = 60;
        sys_settings_get_volume(&vol);
        sys_hal_audio_set_volume(audio, vol);
    } else {
        ESP_LOGW(TAG, "audio init failed, sound disabled");
    }
    vTaskDelete(NULL);
}

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

    /* 1. NVS + 用户设置 (亮度/音量/音效, 持久化) */
    init_nvs();
    ESP_ERROR_CHECK(sys_settings_init());

    /* 2. 事件总线 (最先建立, 其余子系统依赖) */
    ESP_ERROR_CHECK(sys_event_bus_init());
    /* 2.5 APP 调度器 (跨任务互斥) */
    ESP_ERROR_CHECK(sys_app_manager_init());

    /* 3. 硬件抽象层 (stub, 待板卡驱动接入) */
    sys_hal_display_t *display = NULL;
    sys_hal_touch_t   *touch   = NULL;
    sys_hal_i2c_t     *i2c     = NULL;
    ESP_ERROR_CHECK(sys_hal_display_init(&DISPLAY_CFG, &display));
    ESP_ERROR_CHECK(sys_hal_touch_init(&TOUCH_CFG, &touch));
    if (sys_hal_i2c_init(&I2C_CFG, &i2c) != ESP_OK) {
        ESP_LOGW(TAG, "i2c init failed, peripherals unavailable");
    }

    /* 恢复上次亮度 */
    int bright = 100;
    sys_settings_get_brightness(&bright);
    sys_hal_display_set_brightness(bright);

    /* 3.5 SD 卡 (中文字库/资源); 无卡不致命, 界面降级为英文 */
    if (sys_hal_sd_mount() != ESP_OK) {
        ESP_LOGW(TAG, "SD card unavailable -> English UI (CJK TTF goes to /sdcard/fonts/)");
    }

    /* 4. UI 子系统 */
    ESP_ERROR_CHECK(sys_ui_init());

    /* 4.5 字库: 从 SD 卡加载中文字体并确定界面语言 (必须在 APP 建 UI 前) */
    if (sys_ui_font_init() != ESP_OK) {
        ESP_LOGW(TAG, "no CJK font on SD card -> English UI");
    }
    ESP_LOGI(TAG, "UI language: %s", sys_ui_lang() == SYS_UI_LANG_ZH ? "zh" : "en");

    /* 5. 系统时间服务 (1Hz TIME_TICK) */
    ESP_ERROR_CHECK(sys_time_service_init());

    /* 6. 注册 APP (后续新 APP 在这里追加) */
    ESP_ERROR_CHECK(sys_app_register(splash_app_get()));
    ESP_ERROR_CHECK(sys_app_register(launcher_app_get()));
    ESP_ERROR_CHECK(sys_app_register(settings_app_get()));
    ESP_LOGI(TAG, "%u apps registered", (unsigned)sys_app_count());

    ESP_LOGI(TAG, "launching splash");
    /* 7. 启动画面 (数秒后自动切入 Launcher) */
    ESP_ERROR_CHECK(sys_app_launch(SPLASH_APP_ID));

    /* 音频在后台异步初始化, 不阻塞 UI */
    xTaskCreate(audio_boot_task, "audio_boot", 6144, NULL, 4, NULL);

    ESP_LOGI(TAG, "system ready, foreground app: %s",
             sys_app_current() ? sys_app_current()->id : "(none)");
}

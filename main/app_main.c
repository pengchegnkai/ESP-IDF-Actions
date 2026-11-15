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
#include "app_launcher/launcher_app.h"

static const char *TAG = "main";

/* 目标板卡显示参数 —— 换板时只改这里的配置值 */
static const sys_hal_display_cfg_t DISPLAY_CFG = {
    .h_res = 1024, .v_res = 600, .refresh_hz = 60,
};

static const sys_hal_touch_cfg_t TOUCH_CFG = {
    .x_max = 1024, .y_max = 600,
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
    ESP_ERROR_CHECK(sys_hal_display_init(&DISPLAY_CFG, &display));
    ESP_ERROR_CHECK(sys_hal_touch_init(&TOUCH_CFG, &touch));

    /* 4. UI 子系统 */
    ESP_ERROR_CHECK(sys_ui_init());

    /* 5. 注册 APP (后续新 APP 在这里追加) */
    ESP_ERROR_CHECK(sys_app_register(launcher_app_get()));

    /* 6. 启动默认桌面 */
    ESP_ERROR_CHECK(sys_app_launch(LAUNCHER_APP_ID));

    ESP_LOGI(TAG, "system ready, foreground app: %s",
             sys_app_current() ? sys_app_current()->id : "(none)");
}

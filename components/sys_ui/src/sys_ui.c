/**
 * @file sys_ui.c
 * @brief UI 框架实现 —— LVGL 心跳任务 / 屏幕切换 / lv_layer_top 顶层弹窗
 */
#include <string.h>
#include "sys_ui/sys_ui.h"
#include "sys_core/sys_app_manager.h"
#include "sys_hal/sys_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "sys_ui";

static SemaphoreHandle_t s_lock;
static lv_display_t     *s_display;
static lv_obj_t         *s_app_root;   /* 当前 APP 的根 screen */
static lv_obj_t         *s_overlay_bg; /* 顶层弹窗背景 (挂在 lv_layer_top) */

/* ---------- LVGL 渲染心跳任务 ---------- */
static void ui_tick_task(void *arg)
{
    for (;;) {
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            uint32_t next = lv_timer_handler(); /* 返回建议等待时间 ms */
            xSemaphoreGive(s_lock);
            vTaskDelay(pdMS_TO_TICKS(next > 0 && next < SYS_UI_TICK_MS * 5
                                          ? next : SYS_UI_TICK_MS));
        } else {
            vTaskDelay(1);
        }
    }
}

/* ---------- 屏幕切换: 转发到调度器 ---------- */
esp_err_t sys_ui_switch_to(const char *app_id)
{
    return sys_app_launch(app_id);
}

void sys_ui_set_root(lv_obj_t *screen)
{
    s_app_root = screen;
    if (s_display) {
        lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }
}

/* ---------- 顶层弹窗 ---------- */
esp_err_t sys_ui_overlay_show(lv_obj_t *content, lv_obj_t **out_bg)
{
    if (!content) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_overlay_bg) {
        return ESP_ERR_INVALID_STATE; /* 已有弹窗, 互斥 */
    }

    lv_obj_t *bg = lv_obj_class_create_obj(&lv_obj_class, lv_layer_top());
    if (!bg) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));     /* 全屏遮罩 */
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_70, 0);         /* 半透明压暗背景 */
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_parent(content, bg);                    /* 弹窗内容挂进遮罩 */
    lv_obj_center(content);

    s_overlay_bg = bg;
    if (out_bg) {
        *out_bg = bg;
    }
    ESP_LOGI(TAG, "overlay shown");
    return ESP_OK;
}

void sys_ui_overlay_dismiss(void)
{
    if (s_overlay_bg) {
        lv_obj_delete(s_overlay_bg); /* content 一并销毁 (是其子对象) */
        s_overlay_bg = NULL;
        ESP_LOGI(TAG, "overlay dismissed");
    }
}

bool sys_ui_overlay_active(void)
{
    return s_overlay_bg != NULL;
}

/* ---------- 初始化 ---------- */
void sys_ui_lock(void)   { xSemaphoreTakeRecursive((SemaphoreHandle_t)s_lock, portMAX_DELAY); }
void sys_ui_unlock(void) { xSemaphoreGiveRecursive((SemaphoreHandle_t)s_lock); }

esp_err_t sys_ui_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    sys_ui_lock();
    lv_init();
    /* TODO(board): s_display = lv_mipi_dsi_create_display / lv_st7789_create 等,
       挂接 sys_hal_display_t 的真实驱动后配置 flush_cb */
    sys_ui_unlock();

    if (xTaskCreate(ui_tick_task, "lvgl", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ui subsystem ready");
    return ESP_OK;
}

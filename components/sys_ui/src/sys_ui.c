/**
 * @file sys_ui.c
 * @brief UI 框架实现 —— 屏幕切换 / lv_layer_top 顶层弹窗
 *
 * 线程模型: P4 目标上 LVGL 由 BSP 的 esp_lv_adapter 任务驱动, 锁用 bsp_display_lock;
 * 其他目标由本文件自建 tick 任务驱动, 锁用 HAL 的递归互斥量。
 */
#include <string.h>
#include "sdkconfig.h"
#include "sys_ui/sys_ui.h"
#include "sys_core/sys_app_manager.h"
#include "sys_hal/sys_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sys_ui";

static bool      s_inited;
static lv_obj_t *s_app_root;   /* 当前 APP 的根 screen */
static lv_obj_t *s_overlay_bg; /* 顶层弹窗背景 (挂在 lv_layer_top) */

/* ---------- 锁: 统一转发到 HAL (P4 上是 BSP 锁) ----------
   返回 false = 没拿到锁, 调用方禁止执行任何 LVGL 操作 */
bool sys_ui_lock(void)   { return sys_hal_display_lock(); }
void sys_ui_unlock(void) { sys_hal_display_unlock(); }

#if !CONFIG_IDF_TARGET_ESP32P4
/* 非 P4 目标: 自建 LVGL 心跳任务 */
static void ui_tick_task(void *arg)
{
    (void)arg;
    for (;;) {
        sys_ui_lock();
        uint32_t next = lv_timer_handler();
        sys_ui_unlock();
        if (next == 0 || next > SYS_UI_TICK_MS * 5) {
            next = SYS_UI_TICK_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(next));
    }
}
#endif

/* ---------- 屏幕切换: 转发到调度器 ---------- */
esp_err_t sys_ui_switch_to(const char *app_id)
{
    return sys_app_launch(app_id);
}

void sys_ui_set_root(lv_obj_t *screen)
{
    s_app_root = screen;
    if (screen) {
        /* 中文模式: 屏幕根对象设中文字体, 其下所有控件继承
           (新屏幕不继承旧屏幕的样式, 所以每次 set_root 都要重设) */
        lv_font_t *f = sys_ui_cjk_font(SYS_UI_FONT_DEFAULT_PX);
        if (f) {
            lv_obj_set_style_text_font(screen, f, LV_STATE_DEFAULT);
        }
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

    lv_obj_t *bg = lv_obj_create(lv_layer_top()); /* 挂到顶层图层, 永远置顶 */
    if (!bg) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, 0);   /* 半透明压暗背景 */
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_parent(content, bg);               /* 弹窗内容挂进遮罩 */
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
        lv_obj_delete(s_overlay_bg); /* content 为其子对象, 一并销毁 */
        s_overlay_bg = NULL;
        ESP_LOGI(TAG, "overlay dismissed");
    }
}

bool sys_ui_overlay_active(void)
{
    return s_overlay_bg != NULL;
}

/* ---------- 初始化 ---------- */
esp_err_t sys_ui_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

#if CONFIG_IDF_TARGET_ESP32P4
    /* BSP 已完成 lv_init + 显示/触摸注册, 并自带 LVGL 任务 */
    ESP_LOGI(TAG, "ui ready (BSP lv adapter)");
    /* 自检: 官方模式 bsp_display_lock(-1) 应在适配任务释放后立即拿到 */
    if (sys_ui_lock()) {
        ESP_LOGI(TAG, "ui init: display lock self-test OK");
        sys_ui_unlock();
    } else {
        ESP_LOGE(TAG, "ui init: display lock self-test FAILED");
    }
#else
    sys_ui_lock();
    lv_init();
    sys_ui_unlock();
    if (xTaskCreate(ui_tick_task, "lvgl", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ui ready (self tick task)");
#endif

    s_inited = true;
    return ESP_OK;
}

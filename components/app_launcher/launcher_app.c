/**
 * @file launcher_app.c
 * @brief Launcher 实现 —— 演示标准生命周期 + 事件订阅 + UI 屏幕挂载
 */
#include <stdlib.h>
#include "app_launcher/launcher_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_ui/sys_ui.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "launcher";

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *clock_label;
    lv_obj_t *event_label;
} launcher_priv_t;

/* ---------- 事件总线回调 (运行在 sys_evt 任务上下文) ---------- */
static void on_time_tick(int32_t event_id, void *event_data, void *user_ctx)
{
    sys_app_t *self = user_ctx;
    launcher_priv_t *p = self->priv;
    if (p && p->clock_label && self->state == SYS_APP_STATE_ACTIVE) {
        /* 通过锁把更新转到 LVGL 上下文; 实际项目里可改用 lv_async_call */
        sys_ui_lock();
        lv_label_set_text_fmt(p->clock_label, "tick: %d", (int)lv_tick_get() / 1000);
        sys_ui_unlock();
    }
}

static void on_system_event(int32_t event_id, void *event_data, void *user_ctx)
{
    sys_app_t *self = user_ctx;
    launcher_priv_t *p = self->priv;
    if (!p || !p->event_label) {
        return;
    }
    sys_ui_lock();
    lv_label_set_text_fmt(p->event_label, "event: %ld", (long)event_id);
    sys_ui_unlock();
}

/* ---------- 生命周期 ---------- */
static esp_err_t on_init(sys_app_t *app)
{
    launcher_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;

    /* 根屏幕 + 简单桌面布局 */
    sys_ui_lock();
    p->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(p->screen, lv_color_hex(0x101418), 0);

    lv_obj_t *title = lv_label_create(p->screen);
    lv_label_set_text(title, "System Base | Launcher");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    p->clock_label = lv_label_create(p->screen);
    lv_label_set_text(p->clock_label, "tick: 0");
    lv_obj_align(p->clock_label, LV_ALIGN_CENTER, 0, -10);

    p->event_label = lv_label_create(p->screen);
    lv_label_set_text(p->event_label, "event: -");
    lv_obj_set_style_text_color(p->event_label, lv_color_hex(0x7EC8E3), 0);
    lv_obj_align(p->event_label, LV_ALIGN_CENTER, 0, 20);
    sys_ui_unlock();

    /* 订阅系统事件: handler 的 user_ctx 传回 APP 自己 */
    esp_event_handler_t h;
    sys_event_subscribe(SYS_EVENT_TIME_TICK, on_time_tick, app, &h);
    sys_event_subscribe(SYS_EVENT_APP_STATE, on_system_event, app, &h);

    ESP_LOGI(TAG, "launcher initialized");
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    launcher_priv_t *p = app->priv;
    sys_ui_set_root(p->screen); /* 前台: 挂载自己的屏幕 */
    return ESP_OK;
}

static void on_suspend(sys_app_t *app)
{
    ESP_LOGI(TAG, "launcher to background");
    /* 退后台: 停动画/传感器轮询等, 屏幕对象保留 */
}

static void on_stop(sys_app_t *app)
{
    launcher_priv_t *p = app->priv;
    sys_event_unsubscribe(SYS_EVENT_TIME_TICK, (esp_event_handler_t)on_time_tick);
    sys_event_unsubscribe(SYS_EVENT_APP_STATE, (esp_event_handler_t)on_system_event);
    free(p);
    app->priv = NULL;
    ESP_LOGI(TAG, "launcher stopped");
}

/* ---------- 对外: APP 对象定义 ---------- */
static sys_app_t s_launcher = {
    .id         = LAUNCHER_APP_ID,
    .name       = "Launcher",
    .icon       = 0,
    .on_init    = on_init,
    .on_start   = on_start,
    .on_suspend = on_suspend,
    .on_stop    = on_stop,
    .on_event   = NULL, /* 事件走独立 handler, 也可统一进这里 */
};

sys_app_t *launcher_app_get(void)
{
    return &s_launcher;
}

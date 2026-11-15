/**
 * @file launcher_app.c
 * @brief Launcher (桌面) —— APP 网格、状态栏、系统顶层弹窗演示
 */
#include <stdlib.h>
#include <string.h>
#include "app_launcher/launcher_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_core/sys_time_service.h"
#include "sys_ui/sys_ui.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "launcher";

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *clock_label;
    lv_obj_t *event_label;
    lv_obj_t *grid;
    sys_event_handle_t tick_sub;
    sys_event_handle_t state_sub;
} launcher_priv_t;

/* ---------- 弹窗演示: 点"知道了"关闭 ---------- */
static void overlay_ok_cb(lv_event_t *e)
{
    (void)e;
    sys_ui_overlay_dismiss();
}

static void show_demo_overlay(void)
{
    if (sys_ui_overlay_active()) {
        return;
    }
    lv_obj_t *content = lv_obj_create(lv_layer_top());
    lv_obj_set_size(content, 320, 180);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(content, 12, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x232A31), 0);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, LV_SYMBOL_WARNING "  安全警告");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF6B6B), 0);

    lv_obj_t *body = lv_label_create(content);
    lv_label_set_text(body, "这是一个系统级弹窗\n永远显示在所有 APP 之上");

    lv_obj_t *btn = lv_button_create(content);
    lv_obj_add_event_cb(btn, overlay_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "知道了");
    lv_obj_center(btn_label);

    esp_err_t err = sys_ui_overlay_show(content, NULL);
    if (err != ESP_OK) {
        lv_obj_delete(content); /* 已有弹窗时回收本次内容 */
    }
}

static void demo_btn_cb(lv_event_t *e)
{
    (void)e;
    show_demo_overlay();
}

/* ---------- APP 图标点击: 切换前台 ---------- */
static void app_btn_cb(lv_event_t *e)
{
    const char *app_id = lv_event_get_user_data(e);
    if (app_id) {
        sys_app_launch(app_id);
    }
}

/* ---------- 事件总线回调 (运行在 sys_evt 任务上下文) ---------- */
static void on_time_tick(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id; (void)event_data;
    sys_app_t *self = user_ctx;
    launcher_priv_t *p = self->priv;
    if (!p || !p->clock_label || self->state != SYS_APP_STATE_ACTIVE) {
        return;
    }
    uint32_t sec = sys_time_uptime_sec();
    sys_ui_lock();
    lv_label_set_text_fmt(p->clock_label, "%02u:%02u:%02u",
                          (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
    sys_ui_unlock();
}

static void on_system_event(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_data;
    sys_app_t *self = user_ctx;
    launcher_priv_t *p = self->priv;
    if (!p || !p->event_label) {
        return;
    }
    static const char *names[] = {
        "TIME_TICK", "USB_IN", "USB_OUT", "NET_UP", "NET_DOWN", "BATTERY_LOW", "APP_STATE",
    };
    const char *nm = (event_id >= 0 && event_id <= SYS_EVENT_APP_STATE) ? names[event_id] : "?";
    sys_ui_lock();
    lv_label_set_text_fmt(p->event_label, "last event: %s", nm);
    sys_ui_unlock();
}

/* ---------- 生命周期 ---------- */
static void build_grid(launcher_priv_t *p)
{
    for (size_t i = 0; i < sys_app_count(); i++) {
        sys_app_t *app = sys_app_get(i);
        if (!app || strcmp(app->id, LAUNCHER_APP_ID) == 0) {
            continue;
        }
        lv_obj_t *btn = lv_button_create(p->grid);
        lv_obj_set_size(btn, 130, 60);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C6E9B), 0);
        lv_obj_add_event_cb(btn, app_btn_cb, LV_EVENT_CLICKED, (void *)app->id);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, app->name ? app->name : app->id);
        lv_obj_center(label);
    }

    /* 系统弹窗演示入口 */
    lv_obj_t *demo = lv_button_create(p->grid);
    lv_obj_set_size(demo, 130, 60);
    lv_obj_set_style_bg_color(demo, lv_color_hex(0x8A5A00), 0);
    lv_obj_add_event_cb(demo, demo_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *demo_label = lv_label_create(demo);
    lv_label_set_text(demo_label, LV_SYMBOL_WARNING " 弹窗演示");
    lv_obj_center(demo_label);
}

static esp_err_t on_init(sys_app_t *app)
{
    launcher_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;

    sys_ui_lock();
    p->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(p->screen, lv_color_hex(0x101418), 0);
    lv_obj_set_flex_flow(p->screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p->screen, 12, 0);

    /* 状态栏 */
    lv_obj_t *bar = lv_obj_create(p->screen);
    lv_obj_set_size(bar, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1B2129), 0);
    lv_obj_set_style_radius(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "System Base");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    p->clock_label = lv_label_create(bar);
    lv_label_set_text(p->clock_label, "00:00:00");
    lv_obj_set_style_text_color(p->clock_label, lv_color_hex(0x7EC8E3), 0);
    lv_obj_align(p->clock_label, LV_ALIGN_RIGHT_MID, -12, 0);

    /* APP 网格 */
    p->grid = lv_obj_create(p->screen);
    lv_obj_set_width(p->grid, LV_PCT(100));
    lv_obj_set_flex_grow(p->grid, 1);
    lv_obj_set_flex_flow(p->grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(p->grid, 12, 0);
    lv_obj_set_style_pad_column(p->grid, 12, 0);
    lv_obj_set_style_bg_opa(p->grid, LV_OPA_0, 0);

    /* 事件提示 */
    p->event_label = lv_label_create(p->screen);
    lv_label_set_text(p->event_label, "last event: -");
    lv_obj_set_style_text_color(p->event_label, lv_color_hex(0x7EC8E3), 0);
    sys_ui_unlock();

    /* 订阅系统事件 */
    sys_event_subscribe(SYS_EVENT_TIME_TICK, on_time_tick, app, &p->tick_sub);
    sys_event_subscribe(SYS_EVENT_APP_STATE, on_system_event, app, &p->state_sub);

    ESP_LOGI(TAG, "launcher initialized");
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    launcher_priv_t *p = app->priv;
    sys_ui_lock();
    lv_obj_clean(p->grid);   /* 每次进前台刷新 APP 列表 (可能新增/注销) */
    build_grid(p);
    sys_ui_unlock();
    sys_ui_set_root(p->screen);
    return ESP_OK;
}

static void on_suspend(sys_app_t *app)
{
    (void)app;
    ESP_LOGI(TAG, "launcher to background");
}

static void on_stop(sys_app_t *app)
{
    launcher_priv_t *p = app->priv;
    if (!p) {
        return;
    }
    sys_event_unsubscribe(p->tick_sub);
    sys_event_unsubscribe(p->state_sub);
    sys_ui_lock();
    if (p->screen) {
        lv_obj_delete(p->screen);
    }
    sys_ui_unlock();
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
    .on_event   = NULL,
};

sys_app_t *launcher_app_get(void)
{
    return &s_launcher;
}

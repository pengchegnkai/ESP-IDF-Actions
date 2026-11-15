/**
 * @file app_settings.c
 * @brief 设置 APP 实现 —— 演示 LVGL 控件、返回栈、事件订阅
 */
#include <stdlib.h>
#include "app_settings/app_settings.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_ui/sys_ui.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "settings";

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *usb_label;
    sys_event_handle_t usb_in_sub;
    sys_event_handle_t usb_out_sub;
} settings_priv_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (sys_app_back() != ESP_OK) {
        sys_app_launch("launcher"); /* 栈底时回桌面 */
    }
}

static void on_usb_event(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_data;
    sys_app_t *self = user_ctx;
    settings_priv_t *p = self->priv;
    if (!p || !p->usb_label) {
        return;
    }
    sys_ui_lock();
    lv_label_set_text(p->usb_label,
                      event_id == SYS_EVENT_USB_IN ? "USB: 已插入" : "USB: 已拔出");
    sys_ui_unlock();
}

static esp_err_t on_init(sys_app_t *app)
{
    ESP_LOGI(TAG, "settings on_init: enter");
    settings_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;
    ESP_LOGI(TAG, "settings on_init: ui lock");
    sys_ui_lock();
    ESP_LOGI(TAG, "settings on_init: building ui");
    p->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(p->screen, lv_color_hex(0x14181C), 0);
    lv_obj_set_flex_flow(p->screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p->screen, 16, 0);
    lv_obj_set_style_pad_row(p->screen, 16, 0);

    lv_obj_t *title = lv_label_create(p->screen);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  设置");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);

    /* 一个开关控件, 演示 LVGL 控件使用 */
    lv_obj_t *sw = lv_switch_create(p->screen);
    lv_obj_set_width(sw, 70);
    lv_obj_t *sw_label = lv_label_create(p->screen);
    lv_label_set_text(sw_label, "示例开关 (Wi-Fi 开关占位)");
    lv_obj_set_style_text_color(sw_label, lv_color_hex(0x9AA5B1), 0);

    p->usb_label = lv_label_create(p->screen);
    lv_label_set_text(p->usb_label, "USB: -");
    lv_obj_set_style_text_color(p->usb_label, lv_color_hex(0x7EC8E3), 0);

    lv_obj_t *back = lv_button_create(p->screen);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT "  返回");
    lv_obj_center(back_label);
    sys_ui_unlock();
    ESP_LOGI(TAG, "settings on_init: ui built");

    sys_event_subscribe(SYS_EVENT_USB_IN, on_usb_event, app, &p->usb_in_sub);
    sys_event_subscribe(SYS_EVENT_USB_OUT, on_usb_event, app, &p->usb_out_sub);

    ESP_LOGI(TAG, "settings initialized");
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    settings_priv_t *p = app->priv;
    sys_ui_set_root(p->screen);
    return ESP_OK;
}

static void on_suspend(sys_app_t *app)
{
    (void)app;
    ESP_LOGI(TAG, "settings to background");
}

static void on_stop(sys_app_t *app)
{
    settings_priv_t *p = app->priv;
    if (!p) {
        return;
    }
    sys_event_unsubscribe(p->usb_in_sub);
    sys_event_unsubscribe(p->usb_out_sub);
    sys_ui_lock();
    if (p->screen) {
        lv_obj_delete(p->screen);
    }
    sys_ui_unlock();
    free(p);
    app->priv = NULL;
    ESP_LOGI(TAG, "settings stopped");
}

static sys_app_t s_settings = {
    .id         = SETTINGS_APP_ID,
    .name       = "Settings",
    .icon       = 1,
    .on_init    = on_init,
    .on_start   = on_start,
    .on_suspend = on_suspend,
    .on_stop    = on_stop,
    .on_event   = NULL,
};

sys_app_t *settings_app_get(void)
{
    return &s_settings;
}

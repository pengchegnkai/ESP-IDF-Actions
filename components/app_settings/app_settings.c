/**
 * @file app_settings.c
 * @brief 设置 APP —— 亮度/音量/音效 (NVS 持久化) + 系统信息
 */
#include <stdlib.h>
#include <string.h>
#include "app_settings/app_settings.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_core/sys_time_service.h"
#include "sys_core/sys_settings.h"
#include "sys_hal/sys_hal.h"
#include "sys_ui/sys_ui.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "settings";
/* 前向声明: APP 对象定义在文件尾, 生命周期函数在后 */
static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *bright_slider;
    lv_obj_t *bright_value;
    lv_obj_t *vol_slider;
    lv_obj_t *vol_value;
    lv_obj_t *sfx_switch;
    lv_obj_t *psram_label;
    lv_obj_t *uptime_label;
    bool ui_built;
    sys_event_handle_t tick_sub;
} settings_priv_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (sys_app_back() != ESP_OK) {
        sys_app_launch("launcher"); /* 栈底时回桌面 */
    }
}

/* 按键音效 (受设置开关控制) */
static void beep_if_sfx(void)
{
    bool on = false;
    if (sys_settings_get_sfx(&on) == ESP_OK && on) {
        sys_hal_audio_beep(880, 120);
    }
}

static void bright_cb(lv_event_t *e)
{
    settings_priv_t *p = lv_event_get_user_data(e);
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    sys_settings_set_brightness(v);
    sys_hal_display_set_brightness(v);
    beep_if_sfx();
    if (p && p->bright_value) {
        lv_label_set_text_fmt(p->bright_value, "%d%%", v);
    }
}

static void vol_cb(lv_event_t *e)
{
    settings_priv_t *p = lv_event_get_user_data(e);
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    sys_settings_set_volume(v);
    sys_hal_audio_set_volume(sys_hal_audio_get(), v);
    beep_if_sfx();
    if (p && p->vol_value) {
        lv_label_set_text_fmt(p->vol_value, "%d%%", v);
    }
}

static void sfx_cb(lv_event_t *e)
{
    (void)e;
    bool on = lv_switch_get_state(lv_event_get_target_obj(e));
    sys_settings_set_sfx(on);
    if (on) {
        sys_hal_audio_beep(880, 120); /* 打开时试听 */
    }
}

/* 每秒刷新系统信息 (运行时间/内存余量) */
static void on_time_tick(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id; (void)event_data;
    sys_app_t *self = user_ctx;
    settings_priv_t *p = self->priv;
    if (!p || !p->uptime_label || self->state != SYS_APP_STATE_ACTIVE) {
        return;
    }
    uint32_t sec = sys_time_uptime_sec();
    if (!sys_ui_lock()) {
        return;
    }
    lv_label_set_text_fmt(p->uptime_label, "%02u:%02u:%02u",
                          (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
    if (p->psram_label) {
        size_t free_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        size_t total_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
        lv_label_set_text_fmt(p->psram_label, "%u / %u KB",
                              (unsigned)free_kb, (unsigned)total_kb);
    }
    sys_ui_unlock();
}

/* 生成一行 "名称(左) + 数值(右)" 标签行, 返回行对象 */
static lv_obj_t *info_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 30);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    lv_obj_t *nm = lv_label_create(row);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_color(val, lv_color_hex(0x7EC8E3), 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
    *value_out = val;
    return row;
}

/* 生成一个滑杆 (0-100), 带数值标签 */
static lv_obj_t *add_slider(settings_priv_t *p, int value, lv_obj_t *value_label,
                            lv_event_cb_t cb)
{
    lv_obj_t *s = lv_slider_create(p->screen);
    lv_obj_set_width(s, LV_PCT(100));
    lv_slider_set_range(s, 0, 100);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, p);
    lv_label_set_text_fmt(value_label, "%d%%", value);
    return s;
}

static esp_err_t build_ui(sys_app_t *app)
{
    settings_priv_t *p = app->priv;
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }
    p->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(p->screen, lv_color_hex(0x14181C), 0);
    lv_obj_set_flex_flow(p->screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p->screen, 16, 0);
    lv_obj_set_style_pad_row(p->screen, 12, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(p->screen);
    lv_label_set_text(title, sys_ui_str(SYS_UI_STR_APP_SETTINGS));
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);

    /* 亮度 */
    info_row(p->screen, sys_ui_str(SYS_UI_STR_SET_BRIGHTNESS), &p->bright_value);
    int b = 100;
    sys_settings_get_brightness(&b);
    p->bright_slider = add_slider(p, b, p->bright_value, bright_cb);

    /* 音量 */
    info_row(p->screen, sys_ui_str(SYS_UI_STR_SET_VOLUME), &p->vol_value);
    int v = 60;
    sys_settings_get_volume(&v);
    p->vol_slider = add_slider(p, v, p->vol_value, vol_cb);

    /* 按键音效 (单行: 名称 + 开关) */
    lv_obj_t *sfx_row = lv_obj_create(p->screen);
    lv_obj_set_width(sfx_row, LV_PCT(100));
    lv_obj_set_height(sfx_row, 40);
    lv_obj_clear_flag(sfx_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sfx_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sfx_row, 0, 0);
    lv_obj_t *sfx_nm = lv_label_create(sfx_row);
    lv_label_set_text(sfx_nm, sys_ui_str(SYS_UI_STR_SET_SFX));
    lv_obj_set_style_text_color(sfx_nm, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(sfx_nm, LV_ALIGN_LEFT_MID, 0, 0);
    p->sfx_switch = lv_switch_create(sfx_row);
    bool sfx = true;
    sys_settings_get_sfx(&sfx);
    lv_switch_set_state(p->sfx_switch, sfx, LV_ANIM_OFF);
    lv_obj_align(p->sfx_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(p->sfx_switch, sfx_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 系统信息 */
    lv_obj_t *sec = lv_label_create(p->screen);
    lv_label_set_text(sec, sys_ui_str(SYS_UI_STR_SET_INFO));
    lv_obj_set_style_text_color(sec, lv_color_hex(0x9AA5B1), 0);

    info_row(p->screen, sys_ui_str(SYS_UI_STR_SET_PSRAM), &p->psram_label);
    info_row(p->screen, sys_ui_str(SYS_UI_STR_SET_UPTIME), &p->uptime_label);
    lv_obj_t *lang_val;
    info_row(p->screen, sys_ui_str(SYS_UI_STR_SET_LANG), &lang_val);
    lv_label_set_text(lang_val,
                      sys_ui_lang() == SYS_UI_LANG_ZH ? "中文" : "English");
    lv_obj_t *font_val;
    info_row(p->screen, sys_ui_str(SYS_UI_STR_SET_FONT), &font_val);
    lv_label_set_text(font_val,
                      sys_ui_lang() == SYS_UI_LANG_ZH ? "SD TTF" : "Built-in");
    /* 立即刷新一次数值 (此处已持有显示锁, 不能走 on_time_tick 的加锁路径) */
    {
        uint32_t sec = sys_time_uptime_sec();
        lv_label_set_text_fmt(p->uptime_label, "%02u:%02u:%02u",
                              (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
        lv_label_set_text_fmt(p->psram_label, "%u / %u KB",
                              (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                              (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
    }

    /* 返回按钮 (固定底部) */
    lv_obj_t *back = lv_button_create(p->screen);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, sys_ui_str(SYS_UI_STR_BTN_BACK));
    lv_obj_center(back_label);
    sys_ui_unlock();
    ESP_LOGI(TAG, "settings ui built");
    return ESP_OK;
}

static esp_err_t on_init(sys_app_t *app)
{
    settings_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;

    sys_event_subscribe(SYS_EVENT_TIME_TICK, on_time_tick, app, &p->tick_sub);
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    settings_priv_t *p = app->priv;
    if (!p->ui_built) {
        if (build_ui(app) != ESP_OK) {
            return ESP_FAIL;
        }
        p->ui_built = true;
    }
    if (!sys_ui_lock()) {
        return ESP_OK;
    }
    sys_ui_set_root(p->screen);
    sys_ui_unlock();
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
    sys_event_unsubscribe(p->tick_sub);
    if (sys_ui_lock()) {
        if (p->screen) {
            lv_obj_delete(p->screen);
        }
        sys_ui_unlock();
    }
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

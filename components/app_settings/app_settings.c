/**
 * @file app_settings.c
 * @brief 设置 APP —— 亮度/音量/音效 (NVS 持久化) + 系统信息 (Vela 卡片风格)
 *
 * Vela 交互逻辑:
 *   - 顶部边缘下拉 → 控制中心;  底部边缘上滑 / 左上返回键 → 返回
 *   - 返回栈为空时回表盘 (主页), 不再回应用列表
 */
#include <stdlib.h>
#include <string.h>
#include "app_settings/app_settings.h"
#include "app_watch/watch_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_core/sys_time_service.h"
#include "sys_core/sys_settings.h"
#include "sys_hal/sys_hal.h"
#include "sys_ui/sys_ui.h"
#include "sys_ui/sys_ui_theme.h"
#include "sys_ui/sys_ui_gesture.h"
#include "sys_ui/sys_ui_quickpanel.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "settings";
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

/* ---------- 导航 ---------- */

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (sys_app_back() != ESP_OK) {
        sys_app_launch(WATCH_APP_ID);  /* 栈底时回表盘 (主页) */
    }
}

static void back_edge_cb(void *ctx)
{
    (void)ctx;
    back_cb(NULL);
}

static void qc_open_cb(void *ctx)
{
    (void)ctx;
    if (!sys_ui_quickpanel_active()) {
        sys_ui_quickpanel_show();
    }
}

/* ---------- 控件回调 (LVGL 事件上下文, 已持显示锁) ---------- */

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
    /* LVGL 9: switch 继承 checkbox, 状态 = LV_STATE_CHECKED */
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    sys_settings_set_sfx(on);
    if (on) {
        sys_hal_audio_beep(880, 120); /* 打开时试听 */
    }
}

/* 每秒刷新系统信息 (运行时间/内存余量) —— sys_evt 任务上下文, 需取锁 */
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

/* ---------- UI 构件 ---------- */

static lv_obj_t *make_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    sys_ui_theme_card(card);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    return card;
}

/* "名称(左) + 数值(右)" 信息行 */
static lv_obj_t *info_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 32);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    lv_obj_t *nm = lv_label_create(row);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_color(nm, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(nm, sys_ui_theme_font(22), 0);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_color(val, lv_color_hex(SYS_UI_THEME_ACCENT), 0);
    lv_obj_set_style_text_font(val, sys_ui_theme_font(20), 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
    *value_out = val;
    return row;
}

/* 滑杆行: 标题行(名称+数值) + 滑杆 */
static void add_slider_row(lv_obj_t *card, const char *name, int value,
                           lv_obj_t **slider_out, lv_obj_t **value_out,
                           lv_event_cb_t cb, void *ctx)
{
    info_row(card, name, value_out);
    lv_obj_t *s = lv_slider_create(card);
    lv_obj_set_width(s, LV_PCT(100));
    lv_obj_set_height(s, 6);
    lv_slider_set_range(s, 0, 100);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_label_set_text_fmt(*value_out, "%d%%", value);
    *slider_out = s;
}

static esp_err_t build_ui(sys_app_t *app)
{
    settings_priv_t *p = app->priv;
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }
    p->screen = lv_obj_create(NULL);
    sys_ui_theme_screen(p->screen);
    lv_obj_clear_flag(p->screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 边缘手势: 顶=控制中心, 底=返回 */
    sys_ui_top_edge_create(p->screen, qc_open_cb, app);
    sys_ui_bottom_edge_create(p->screen, back_edge_cb, app);

    /* 顶部条: 返回 + 标题 */
    lv_obj_t *bar = lv_obj_create(p->screen);
    lv_obj_set_size(bar, LV_PCT(100), 52);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back = lv_button_create(bar);
    lv_obj_set_size(back, 88, 40);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(back, 20, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(SYS_UI_THEME_CARD), 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text_fmt(back_lbl, "← %s", sys_ui_str(SYS_UI_STR_BTN_BACK));
    lv_obj_set_style_text_font(back_lbl, sys_ui_theme_font(20), 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_center(back_lbl);
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, sys_ui_str(SYS_UI_STR_APP_SETTINGS));
    lv_obj_set_style_text_color(title, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(title, sys_ui_theme_font(30), 0);
    lv_obj_align(title, LV_ALIGN_RIGHT_MID, -8, 0);

    /* 滚动区 + 卡片 */
    lv_obj_t *scroll = lv_obj_create(p->screen);
    lv_obj_set_size(scroll, LV_PCT(100), LV_PCT(100) - 78);
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 16, 0);
    lv_obj_set_style_pad_row(scroll, 16, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);

    /* 卡 1: 亮度 */
    lv_obj_t *c1 = make_card(scroll);
    int b = 100;
    sys_settings_get_brightness(&b);
    add_slider_row(c1, sys_ui_str(SYS_UI_STR_SET_BRIGHTNESS), b,
                   &p->bright_slider, &p->bright_value, bright_cb, p);

    /* 卡 2: 声音 (音量 + 音效) */
    lv_obj_t *c2 = make_card(scroll);
    int v = 60;
    sys_settings_get_volume(&v);
    add_slider_row(c2, sys_ui_str(SYS_UI_STR_SET_VOLUME), v,
                   &p->vol_slider, &p->vol_value, vol_cb, p);
    lv_obj_t *sfx_row = lv_obj_create(c2);
    lv_obj_set_size(sfx_row, LV_PCT(100), 48);
    lv_obj_clear_flag(sfx_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sfx_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sfx_row, 0, 0);
    lv_obj_t *sfx_nm = lv_label_create(sfx_row);
    lv_label_set_text(sfx_nm, sys_ui_str(SYS_UI_STR_SET_SFX));
    lv_obj_set_style_text_color(sfx_nm, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(sfx_nm, sys_ui_theme_font(22), 0);
    lv_obj_align(sfx_nm, LV_ALIGN_LEFT_MID, 0, 0);
    p->sfx_switch = lv_switch_create(sfx_row);
    lv_obj_set_size(p->sfx_switch, 76, 40);
    bool sfx = true;
    sys_settings_get_sfx(&sfx);
    if (sfx) {
        lv_obj_add_state(p->sfx_switch, LV_STATE_CHECKED);
    }
    lv_obj_align(p->sfx_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(p->sfx_switch, sfx_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 卡 3: 系统信息 */
    lv_obj_t *c3 = make_card(scroll);
    lv_obj_t *sec = lv_label_create(c3);
    lv_label_set_text(sec, sys_ui_str(SYS_UI_STR_SET_INFO));
    lv_obj_set_style_text_color(sec, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(sec, sys_ui_theme_font(20), 0);
    info_row(c3, sys_ui_str(SYS_UI_STR_SET_PSRAM), &p->psram_label);
    info_row(c3, sys_ui_str(SYS_UI_STR_SET_UPTIME), &p->uptime_label);
    lv_obj_t *lang_val;
    info_row(c3, sys_ui_str(SYS_UI_STR_SET_LANG), &lang_val);
    lv_label_set_text(lang_val,
                      sys_ui_lang() == SYS_UI_LANG_ZH ? "中文" : "English");
    lv_obj_t *font_val;
    info_row(c3, sys_ui_str(SYS_UI_STR_SET_FONT), &font_val);
    lv_label_set_text(font_val,
                      sys_ui_lang() == SYS_UI_LANG_ZH ? "SD TTF" : "Built-in");

    /* 立即刷新一次数值 (此处已持有显示锁, 不走 on_time_tick 的加锁路径) */
    {
        uint32_t sec = sys_time_uptime_sec();
        lv_label_set_text_fmt(p->uptime_label, "%02u:%02u:%02u",
                              (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
        lv_label_set_text_fmt(p->psram_label, "%u / %u KB",
                              (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                              (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
    }

    sys_ui_unlock();
    ESP_LOGI(TAG, "settings ui built");
    return ESP_OK;
}

/* ---------- 生命周期 ---------- */

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
    p->ui_built = false;
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

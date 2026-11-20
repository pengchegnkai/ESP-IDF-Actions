/**
 * @file sys_ui_quickpanel.c
 * @brief 控制中心 (Vela 快捷面板) 实现
 *
 * 结构: lv_layer_top 上的全屏暗色背景 + 顶部圆角面板。
 * 面板: 标题 / WiFi / ESP-NOW / 音效 三个开关行 + 亮度、音量滑块。
 * 收起: 点背景 或 底部手柄上滑。
 *
 * 开关状态与系统状态双向同步:
 *   WiFi 开关 ↔ sys_net_wifi_enable; ESP-NOW 开关开启时若 WiFi 未开则先开 WiFi
 *   (射频共用); 音效/亮度/音量 走 sys_settings 持久化 (write-through)。
 */
#include <string.h>
#include "sys_ui/sys_ui_quickpanel.h"
#include "sys_ui/sys_ui.h"
#include "sys_ui/sys_ui_theme.h"
#include "sys_ui/sys_ui_gesture.h"
#include "sys_core/sys_event_bus.h"
#include "sys_core/sys_settings.h"
#include "sys_hal/sys_hal.h"
#include "sys_net/sys_net.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "quickpanel";

static lv_obj_t *s_bg;          /* 暗色背景 (lv_layer_top 子对象) */
static lv_obj_t *s_wifi_lbl;    /* WiFi 状态文字 */
static lv_obj_t *s_wifi_sw;
static lv_obj_t *s_now_sw;
static lv_obj_t *s_sfx_sw;
static lv_obj_t *s_bright_bar;
static lv_obj_t *s_vol_bar;
static sys_event_handle_t s_wifi_sub;
static bool s_wifi_sub_on;

/* ---------- 开关回调 (LVGL 事件上下文, 已持显示锁) ---------- */

static void wifi_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (sys_net_wifi_is_enabled() == on) {
        return;
    }
    esp_err_t err = sys_net_wifi_enable(on);
    if (err != ESP_OK) {
        /* 失败回退开关位置 */
        if (on) {
            lv_obj_remove_state(lv_event_get_target(e), LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(lv_event_get_target(e), LV_STATE_CHECKED);
        }
        ESP_LOGW(TAG, "wifi enable(%d) failed: %s", on, esp_err_to_name(err));
    }
}

static void now_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (on) {
        /* ESP-NOW 与 station 共用 C6 射频, 未开 WiFi 先开 */
        if (!sys_net_wifi_is_enabled()) {
            sys_net_wifi_enable(true);
        }
        if (sys_net_espnow_start() != ESP_OK) {
            lv_obj_remove_state(lv_event_get_target(e), LV_STATE_CHECKED);
            ESP_LOGW(TAG, "esp-now start failed");
        }
    } else {
        sys_net_espnow_stop();
    }
}

static void sfx_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    sys_settings_set_sfx(on);
    if (on) {
        sys_hal_audio_beep(1200, 60);  /* 开启时试听一下 */
    }
}

static void bright_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    int v = lv_slider_get_value(lv_event_get_target(e));
    sys_settings_set_brightness(v);
    sys_hal_display_set_brightness(v);
}

static void vol_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    int v = lv_slider_get_value(lv_event_get_target(e));
    sys_settings_set_volume(v);
    sys_hal_audio_t *a = sys_hal_audio_get();
    if (a) {
        sys_hal_audio_set_volume(a, v);
    }
}

/* ---------- WiFi 状态事件 (sys_evt 任务上下文) ---------- */

static void on_wifi_status(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id; (void)user_ctx;
    if (!s_bg) {
        return;
    }
    const sys_net_wifi_status_t *st = event_data;
    if (!sys_ui_lock()) {
        return;
    }
    if (s_wifi_lbl && st) {
        switch (st->state) {
        case SYS_NET_WIFI_CONNECTED:
            lv_label_set_text_fmt(s_wifi_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_CONNECTED),
                                  st->ssid);
            break;
        case SYS_NET_WIFI_CONNECTING:
            lv_label_set_text(s_wifi_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_CONNECTING));
            break;
        case SYS_NET_WIFI_ERROR:
            lv_label_set_text(s_wifi_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_CONNECTING));
            break;
        default:
            lv_label_set_text(s_wifi_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_OFF));
            break;
        }
    }
    /* 开关与真实状态对齐 (如连接失败不改变"开启"语义, 保持用户意图) */
    sys_ui_unlock();
}

/* ---------- 收起 ---------- */

static void dismiss_cb(void *ctx)
{
    (void)ctx;
    sys_ui_quickpanel_dismiss();
}

static void on_bg_click(lv_event_t *e)
{
    (void)e;
    sys_ui_quickpanel_dismiss();
}

void sys_ui_quickpanel_dismiss(void)
{
    if (s_wifi_sub_on) {
        sys_event_unsubscribe(s_wifi_sub);
        s_wifi_sub_on = false;
    }
    if (s_bg) {
        if (sys_ui_lock()) {
            lv_obj_delete(s_bg);
            sys_ui_unlock();
        }
        s_bg = NULL;
        s_wifi_lbl = s_wifi_sw = s_now_sw = s_sfx_sw = NULL;
        s_bright_bar = s_vol_bar = NULL;
        ESP_LOGI(TAG, "quickpanel dismissed");
    }
}

bool sys_ui_quickpanel_active(void)
{
    return s_bg != NULL;
}

/* ---------- 一行开关: 标签 (+ 状态) + 右侧 switch; 返回行对象供定位 ---------- */
static lv_obj_t *add_toggle_row(lv_obj_t *parent, lv_obj_t **out_sw,
                                const char *title, lv_obj_t **out_sub_lbl,
                                bool on, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 72);
    lv_obj_set_style_bg_color(row, lv_color_hex(SYS_UI_THEME_CARD_ALT), 0);
    lv_obj_set_style_radius(row, 18, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(lbl, sys_ui_theme_font(26), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 18, 0);

    if (out_sub_lbl) {
        lv_obj_t *sub = lv_label_create(row);
        lv_obj_set_style_text_color(sub, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
        lv_obj_set_style_text_font(sub, sys_ui_theme_font(18), 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 18, 30);
        *out_sub_lbl = sub;
    }

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 76, 40);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -18, 0);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    *out_sw = sw;
    return row;
}

/* ---------- 显示 ---------- */
esp_err_t sys_ui_quickpanel_show(void)
{
    if (s_bg) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }

    s_bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_bg);
    lv_obj_set_size(s_bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_bg, LV_OPA_50, 0);
    lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_bg, on_bg_click, LV_EVENT_CLICKED, NULL);

    /* 顶部圆角面板 (Vela 控制中心贴顶), 内部手动定位 */
    lv_obj_t *panel = lv_obj_create(s_bg);
    lv_obj_set_size(panel, 464, 500);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x121826), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 28, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, sys_ui_str(SYS_UI_STR_QC_TITLE));
    lv_obj_set_style_text_color(title, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(title, sys_ui_theme_font(30), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

    /* 三个开关行: y = 48 / 130 / 212 (行高 72, 间距 10) */
    lv_obj_t *wifi_sub = NULL;
    lv_obj_t *row1 = add_toggle_row(panel, &s_wifi_sw, sys_ui_str(SYS_UI_STR_NET_WIFI),
                                    &wifi_sub, sys_net_wifi_is_enabled(), wifi_sw_cb);
    s_wifi_lbl = wifi_sub;
    if (s_wifi_lbl) {
        lv_label_set_text(s_wifi_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_OFF));
    }
    lv_obj_align(row1, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *row2 = add_toggle_row(panel, &s_now_sw, sys_ui_str(SYS_UI_STR_NET_ESPNOW),
                                    NULL, sys_net_espnow_is_active(), now_sw_cb);
    lv_obj_align(row2, LV_ALIGN_TOP_MID, 0, 130);

    bool sfx = false;
    sys_settings_get_sfx(&sfx);
    lv_obj_t *row3 = add_toggle_row(panel, &s_sfx_sw, sys_ui_str(SYS_UI_STR_SET_SFX),
                                    NULL, sfx, sfx_sw_cb);
    lv_obj_align(row3, LV_ALIGN_TOP_MID, 0, 212);

    /* 亮度 */
    lv_obj_t *bl = lv_label_create(panel);
    lv_label_set_text(bl, sys_ui_str(SYS_UI_STR_SET_BRIGHTNESS));
    lv_obj_set_style_text_color(bl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(bl, sys_ui_theme_font(24), 0);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 0, 298);
    s_bright_bar = lv_slider_create(panel);
    lv_obj_set_width(s_bright_bar, LV_PCT(100));
    lv_obj_set_height(s_bright_bar, 6);
    lv_obj_align(s_bright_bar, LV_ALIGN_TOP_MID, 0, 330);
    lv_obj_add_event_cb(s_bright_bar, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 音量 */
    lv_obj_t *vl = lv_label_create(panel);
    lv_label_set_text(vl, sys_ui_str(SYS_UI_STR_SET_VOLUME));
    lv_obj_set_style_text_color(vl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(vl, sys_ui_theme_font(24), 0);
    lv_obj_align(vl, LV_ALIGN_TOP_LEFT, 0, 358);
    s_vol_bar = lv_slider_create(panel);
    lv_obj_set_width(s_vol_bar, LV_PCT(100));
    lv_obj_set_height(s_vol_bar, 6);
    lv_obj_align(s_vol_bar, LV_ALIGN_TOP_MID, 0, 390);
    lv_obj_add_event_cb(s_vol_bar, vol_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 底部手柄 (视觉) + 面板底部上滑区 (收起) */
    lv_obj_t *handle = lv_obj_create(panel);
    lv_obj_set_size(handle, 120, 8);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, 6);
    lv_obj_set_style_bg_color(handle, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_40, 0);
    lv_obj_set_style_radius(handle, 4, 0);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    sys_ui_bottom_edge_create(panel, dismiss_cb, NULL);

    /* 初始值 */
    int bright = 100, vol = 60;
    sys_settings_get_brightness(&bright);
    sys_settings_get_volume(&vol);
    lv_slider_set_value(s_bright_bar, bright, LV_ANIM_OFF);
    lv_slider_set_value(s_vol_bar, vol, LV_ANIM_OFF);

    sys_ui_unlock();

    /* WiFi 状态事件 (面板存活期间) */
    if (sys_event_subscribe(SYS_EVENT_WIFI_STATUS, on_wifi_status, NULL,
                            &s_wifi_sub) == ESP_OK) {
        s_wifi_sub_on = true;
        /* 首帧立即刷新一次 (当前状态快照) */
        sys_net_wifi_status_t st;
        sys_net_wifi_get_status(&st);
        on_wifi_status(SYS_EVENT_WIFI_STATUS, &st, NULL);
    }

    ESP_LOGI(TAG, "quickpanel shown");
    return ESP_OK;
}

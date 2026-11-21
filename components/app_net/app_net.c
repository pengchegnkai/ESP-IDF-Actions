/**
 * @file app_net.c
 * @brief 网络中心 APP —— WiFi 扫描/连接(带密码键盘) / ESP-NOW / 蓝牙(占位) / 校时
 *                   / 天气 (wttr.in) / OTA 固件升级
 *
 * Vela 交互逻辑:
 *   - 顶部边缘下拉 → 控制中心;  底部边缘上滑 / 左上返回键 → 返回
 *
 * 线程模型: 扫描回调与事件回调都运行在非 LVGL 任务, 更新 UI 前必须
 * sys_ui_lock() 成功; 失败(适配任务持锁)直接放弃本帧, 下帧事件会补。
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "app_net/app_net.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_core/sys_time_service.h"
#include "sys_hal/sys_hal.h"
#include "sys_net/sys_net.h"
#include "sys_ota/sys_ota.h"
#include "sys_ui/sys_ui.h"
#include "sys_ui/sys_ui_theme.h"
#include "sys_ui/sys_ui_gesture.h"
#include "sys_ui/sys_ui_quickpanel.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "app_net";

static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

/* ========================= 私有状态 ========================= */

#define NET_INPUT_MAX 64

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *scroll;       /* 滚动容器 */
    /* WiFi 卡 */
    lv_obj_t *wifi_dot;
    lv_obj_t *wifi_state_lbl;
    lv_obj_t *scan_btn_lbl;
    lv_obj_t *ap_list;      /* 扫描结果容器 (flex column) */
    /* ESP-NOW 卡 */
    lv_obj_t *now_sw;
    lv_obj_t *now_peer_lbl;
    /* 时间卡 */
    lv_obj_t *time_state_lbl;
    /* 天气卡 */
    lv_obj_t *wx_lbl;
    /* OTA 卡 */
    lv_obj_t *ota_state_lbl;
    lv_obj_t *ota_btn;
    bool ui_built;
    sys_event_handle_t wifi_sub;
    sys_event_handle_t now_sub;
    sys_event_handle_t time_sub;
    sys_event_handle_t tick_sub;
    sys_event_handle_t wx_sub;
    sys_event_handle_t ota_sub;

    /* 扫描结果缓存 (UI 行的 user_data 是索引) */
    sys_net_ap_info_t aps[SYS_NET_SCAN_MAX];
    int ap_count;
    /* 当前选择的 SSID (键盘对话框用) */
    char sel_ssid[SYS_NET_SSID_MAX];
} net_priv_t;

/* ========================= 键盘对话框 ========================= */

typedef enum { KBD_MODE_BASE = 0, KBD_MODE_UPPER, KBD_MODE_SYM } kbd_mode_t;

typedef struct {
    lv_obj_t *content;      /* overlay 内容 */
    lv_obj_t *input_lbl;    /* 输入回显 */
    lv_obj_t *keys[4][10];  /* 4x10 字母/数字键 */
    lv_obj_t *key_space;
    lv_obj_t *key_del;
    lv_obj_t *key_connect;
    lv_obj_t *key_cancel;
    lv_obj_t *key_shift;    /* Aa */
    lv_obj_t *key_sym;      /* #+ */
    kbd_mode_t mode;
    char buf[NET_INPUT_MAX];
    int  len;
    char ssid[SYS_NET_SSID_MAX];  /* 要连接的网络 */
} kbd_priv_t;

static kbd_priv_t *s_kbd = NULL;  /* 全局唯一 (对话框互斥) */
static esp_err_t kbd_show(const char *ssid);
static void kbd_priv_free(void);

/* 三种模式下的键位 (4 行 x 10 列; 短行右侧键隐藏) */
static const char *kbd_rows[KBD_MODE_SYM + 1][4][10] = {
    [KBD_MODE_BASE] = {
        {"1","2","3","4","5","6","7","8","9","0"},
        {"q","w","e","r","t","y","u","i","o","p"},
        {"a","s","d","f","g","h","j","k","l",""},
        {"z","x","c","v","b","n","m","","",""},
    },
    [KBD_MODE_UPPER] = {
        {"1","2","3","4","5","6","7","8","9","0"},
        {"Q","W","E","R","T","Y","U","I","O","P"},
        {"A","S","D","F","G","H","J","K","L",""},
        {"Z","X","C","V","B","N","M","","",""},
    },
    [KBD_MODE_SYM] = {
        {"!","@","#","$","%","^","&","*","(",")"},
        {"-","_","+","=","[","]","{","}","\\","/"},
        {";",":","'","\"",",",".","<",">","?",""},
        {"~","`","(",")","\"","'","|","","",""},
    },
};

static void kbd_refresh_keys(kbd_priv_t *k)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 10; c++) {
            const char *t = kbd_rows[k->mode][r][c];
            if (!t || !t[0]) {
                lv_obj_add_flag(k->keys[r][c], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(k->keys[r][c], LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lv_obj_get_child(k->keys[r][c], 0), t);
            }
        }
    }
    /* Aa / #+ 高亮当前模式 */
    lv_obj_set_style_bg_color(k->key_shift,
        lv_color_hex(k->mode == KBD_MODE_UPPER ? SYS_UI_THEME_PRIMARY
                                               : SYS_UI_THEME_CARD_ALT), 0);
    lv_obj_set_style_bg_color(k->key_sym,
        lv_color_hex(k->mode == KBD_MODE_SYM ? SYS_UI_THEME_PRIMARY
                                             : SYS_UI_THEME_CARD_ALT), 0);
}

static void kbd_refresh_input(kbd_priv_t *k)
{
    char disp[NET_INPUT_MAX + 4];
    memcpy(disp, k->buf, (size_t)k->len);
    disp[k->len] = '\0';
    /* 光标 */
    lv_label_set_text_fmt(k->input_lbl, "%s▌", disp);
}

static void kbd_set_mode(kbd_priv_t *k, kbd_mode_t m)
{
    k->mode = m;
    kbd_refresh_keys(k);
}

static void kbd_connect(void)
{
    if (!s_kbd) {
        return;
    }
    kbd_priv_t *k = s_kbd;
    k->buf[k->len] = '\0';
    char ssid[SYS_NET_SSID_MAX];
    char pass[NET_INPUT_MAX];
    memcpy(ssid, k->ssid, sizeof(ssid));
    memcpy(pass, k->buf, sizeof(pass));
    sys_ui_overlay_dismiss();   /* 先销毁对话框 UI */
    kbd_priv_free();            /* 再释放私有结构 */
    if (ssid[0]) {
        sys_net_wifi_connect(ssid, pass[0] ? pass : NULL);
    }
}

static void kbd_key_cb(lv_event_t *e)
{
    kbd_priv_t *k = s_kbd;
    if (!k) {
        return;
    }
    const char *id = lv_event_get_user_data(e);
    if (!id) {
        return;
    }
    if (id[0] == 0) {
        return;
    }
    if (strcmp(id, "space") == 0) {
        if (k->len < NET_INPUT_MAX - 1) {
            k->buf[k->len++] = ' ';
        }
    } else if (strcmp(id, "del") == 0) {
        if (k->len > 0) {
            k->len--;
        }
    } else if (strcmp(id, "shift") == 0) {
        kbd_set_mode(k, k->mode == KBD_MODE_UPPER ? KBD_MODE_BASE : KBD_MODE_UPPER);
    } else if (strcmp(id, "sym") == 0) {
        kbd_set_mode(k, k->mode == KBD_MODE_SYM ? KBD_MODE_BASE : KBD_MODE_SYM);
    } else {
        if (k->len < NET_INPUT_MAX - 1) {
            k->buf[k->len++] = id[0];
        }
    }
    kbd_refresh_input(k);
}

static void kbd_cancel_cb(lv_event_t *e)
{
    (void)e;
    sys_ui_overlay_dismiss();
    kbd_priv_free();
}

static void kbd_connect_cb(lv_event_t *e)
{
    (void)e;
    kbd_connect();
}

/* ---------- 键盘 UI 构建 ---------- */

#define KBD_W 448
#define KBD_H 500
#define KBD_KEY_W 39
#define KBD_KEY_H 44
#define KBD_GAP 4

static lv_obj_t *kbd_make_key(lv_obj_t *parent, int x, int y, int w,
                              const char *text, const char *id, lv_event_cb_t cb)
{
    lv_obj_t *key = lv_button_create(parent);
    lv_obj_set_size(key, w, KBD_KEY_H);
    lv_obj_align(key, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_radius(key, 10, 0);
    lv_obj_set_style_bg_color(key, lv_color_hex(SYS_UI_THEME_CARD_ALT), 0);
    lv_obj_set_style_bg_color(key, lv_color_hex(SYS_UI_THEME_CARD),
                              LV_STATE_PRESSED);
    lv_obj_add_event_cb(key, cb, LV_EVENT_CLICKED, (void *)id);
    lv_obj_t *lbl = lv_label_create(key);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, sys_ui_theme_font(22), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_center(lbl);
    return key;
}

esp_err_t kbd_show(const char *ssid)
{
    /* 调用方 (点击回调) 必须已持有显示锁 */
    if (!ssid || !ssid[0] || s_kbd) {
        return s_kbd ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
    }

    kbd_priv_t *k = calloc(1, sizeof(*k));
    if (!k) {
        return ESP_ERR_NO_MEM;
    }
    strncpy(k->ssid, ssid, sizeof(k->ssid) - 1);

    lv_obj_t *content = lv_obj_create(NULL);
    lv_obj_set_size(content, KBD_W, KBD_H);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x121826), 0);
    lv_obj_set_style_radius(content, 28, 0);
    lv_obj_set_style_pad_all(content, 12, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 + 取消 */
    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, sys_ui_str(SYS_UI_STR_NET_WIFI_PASSWORD));
    lv_obj_set_style_text_color(title, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(title, sys_ui_theme_font(24), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 2);

    lv_obj_t *cancel = kbd_make_key(content, KBD_W - 34 - 84, 0, 84,
                                    sys_ui_str(SYS_UI_STR_BTN_CANCEL),
                                    NULL, kbd_cancel_cb);
    (void)cancel;

    lv_obj_t *ssid_lbl = lv_label_create(content);
    lv_label_set_text_fmt(ssid_lbl, "%s", ssid);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(SYS_UI_THEME_ACCENT), 0);
    lv_obj_set_style_text_font(ssid_lbl, sys_ui_theme_font(22), 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 4, 44);
    lv_obj_set_width(ssid_lbl, KBD_W - 8);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);

    /* 输入回显 */
    lv_obj_t *input_bg = lv_obj_create(content);
    lv_obj_set_size(input_bg, KBD_W - 8, 48);
    lv_obj_align(input_bg, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_bg_color(input_bg, lv_color_hex(SYS_UI_THEME_CARD), 0);
    lv_obj_set_style_radius(input_bg, 12, 0);
    lv_obj_set_style_border_width(input_bg, 1, 0);
    lv_obj_set_style_border_color(input_bg, lv_color_hex(SYS_UI_THEME_BORDER), 0);
    lv_obj_clear_flag(input_bg, LV_OBJ_FLAG_SCROLLABLE);
    k->input_lbl = lv_label_create(input_bg);
    lv_label_set_text(k->input_lbl, "▌");
    lv_obj_set_style_text_font(k->input_lbl, sys_ui_theme_font(22), 0);
    lv_obj_set_style_text_color(k->input_lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_align(k->input_lbl, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_width(k->input_lbl, KBD_W - 40);
    lv_label_set_long_mode(k->input_lbl, LV_LABEL_LONG_SCROLL);

    /* 4 行字母/数字键 */
    int kx0 = (KBD_W - (10 * KBD_KEY_W + 9 * KBD_GAP)) / 2;
    int ky0 = 138;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 10; c++) {
            const char *t = kbd_rows[KBD_MODE_BASE][r][c];
            k->keys[r][c] = kbd_make_key(content, kx0 + c * (KBD_KEY_W + KBD_GAP),
                                         ky0 + r * (KBD_KEY_H + KBD_GAP),
                                         KBD_KEY_W, t ? t : "", t, kbd_key_cb);
        }
    }
    /* 底行: Aa | #+ | space(4) | ⌫ | 连接(2) —— 按列宽分配 */
    int by = ky0 + 4 * (KBD_KEY_H + KBD_GAP) + 4;
    k->key_shift = kbd_make_key(content, kx0, by, 2 * KBD_KEY_W + KBD_GAP,
                                "Aa", "shift", kbd_key_cb);
    k->key_sym = kbd_make_key(content, kx0 + (2 * KBD_KEY_W + KBD_GAP), by,
                              2 * KBD_KEY_W + KBD_GAP, "#+", "sym", kbd_key_cb);
    k->key_space = kbd_make_key(content, kx0 + 4 * (KBD_KEY_W + KBD_GAP), by,
                                4 * KBD_KEY_W + 3 * KBD_GAP, "␣", "space",
                                kbd_key_cb);
    k->key_del = kbd_make_key(content, kx0 + 8 * (KBD_KEY_W + KBD_GAP), by,
                              KBD_KEY_W, "⌫", "del", kbd_key_cb);
    k->key_connect = kbd_make_key(content, kx0 + 9 * (KBD_KEY_W + KBD_GAP), by,
                                  KBD_KEY_W, "↵", "connect", kbd_connect_cb);
    lv_obj_set_style_bg_color(k->key_connect, lv_color_hex(SYS_UI_THEME_PRIMARY), 0);

    kbd_priv_t *saved = k;
    k->content = content;
    kbd_refresh_keys(k);
    kbd_refresh_input(k);

    esp_err_t err = sys_ui_overlay_show(content, NULL);
    if (err != ESP_OK) {
        lv_obj_delete(content);
        free(saved);
        return err;
    }
    s_kbd = k;
    return ESP_OK;
}

void kbd_priv_free(void)
{
    if (!s_kbd) {
        return;
    }
    free(s_kbd);
    s_kbd = NULL;
}

/* ========================= WiFi 列表 ========================= */

/* 刷新 WiFi 卡状态。调用方必须已持有显示锁
   (LVGL 事件回调上下文天然持有; 事件总线回调需先 sys_ui_lock) */
static void wifi_card_refresh(net_priv_t *p)
{
    if (!p || !p->wifi_dot) {
        return;
    }
    sys_net_wifi_status_t st;
    sys_net_wifi_get_status(&st);
    lv_color_t c;
    const char *txt;
    switch (st.state) {
    case SYS_NET_WIFI_CONNECTED:
        c = lv_color_hex(SYS_UI_THEME_OK);
        txt = sys_ui_str(SYS_UI_STR_NET_WIFI_CONNECTED);
        break;
    case SYS_NET_WIFI_CONNECTING:
        c = lv_color_hex(SYS_UI_THEME_WARN);
        txt = sys_ui_str(SYS_UI_STR_NET_WIFI_CONNECTING);
        break;
    case SYS_NET_WIFI_ERROR:
        c = lv_color_hex(SYS_UI_THEME_ERR);
        txt = sys_ui_str(SYS_UI_STR_NET_WIFI_CONNECTING);
        break;
    default:
        c = lv_color_hex(SYS_UI_THEME_DISABLED);
        txt = sys_ui_str(SYS_UI_STR_NET_WIFI_OFF);
        break;
    }
    lv_obj_set_style_bg_color(p->wifi_dot, c, 0);
    if (st.state == SYS_NET_WIFI_CONNECTED && st.ssid[0]) {
        lv_label_set_text_fmt(p->wifi_state_lbl, "%s", st.ssid);
    } else {
        lv_label_set_text(p->wifi_state_lbl, txt);
    }
}

static void ap_row_cb(lv_event_t *e)
{
    net_priv_t *p = lv_event_get_user_data(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (!p || idx < 0 || idx >= p->ap_count) {
        return;
    }
    const sys_net_ap_info_t *ap = &p->aps[idx];
    if (ap->auth == 0) {  /* WIFI_AUTH_OPEN */
        sys_net_wifi_connect(ap->ssid, NULL);
    } else {
        /* 加密网络 → 密码键盘 */
        kbd_show(ap->ssid);
    }
}

static void rebuild_ap_list(net_priv_t *p)
{
    if (!p->ap_list) {
        return;
    }
    lv_obj_clean(p->ap_list);
    for (int i = 0; i < p->ap_count; i++) {
        lv_obj_t *row = lv_obj_create(p->ap_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 46);
        lv_obj_set_style_bg_color(row, lv_color_hex(SYS_UI_THEME_CARD_ALT), 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, ap_row_cb, LV_EVENT_CLICKED, p);

        lv_obj_t *ssid = lv_label_create(row);
        lv_label_set_text(ssid, p->aps[i].ssid);
        lv_obj_set_style_text_color(ssid, lv_color_hex(SYS_UI_THEME_TEXT), 0);
        lv_obj_set_style_text_font(ssid, sys_ui_theme_font(20), 0);
        lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_set_width(ssid, 240);
        lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);

        lv_obj_t *rssi = lv_label_create(row);
        lv_label_set_text_fmt(rssi, "%d dBm", p->aps[i].rssi);
        lv_obj_set_style_text_color(rssi, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
        lv_obj_set_style_text_font(rssi, sys_ui_theme_font(18), 0);
        lv_obj_align(rssi, LV_ALIGN_RIGHT_MID, -14, 0);

        lv_obj_t *lock = lv_label_create(row);
        lv_label_set_text(lock, p->aps[i].auth == 0
                                 ? sys_ui_str(SYS_UI_STR_NET_WIFI_OPEN)
                                 : sys_ui_str(SYS_UI_STR_NET_WIFI_LOCKED));
        lv_obj_set_style_text_color(lock, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
        lv_obj_set_style_text_font(lock, sys_ui_theme_font(16), 0);
        lv_obj_align(lock, LV_ALIGN_RIGHT_MID, -110, 0);
    }
    if (p->ap_count == 0) {
        lv_obj_t *empty = lv_label_create(p->ap_list);
        lv_label_set_text(empty, sys_ui_str(SYS_UI_STR_NET_WIFI_EMPTY));
        lv_obj_set_style_text_color(empty, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
        lv_obj_set_style_text_font(empty, sys_ui_theme_font(18), 0);
    }
}

/* 扫描完成回调 (sys_net 任务上下文)。
   注意: 先拷贝结果, 再取显示锁更新 UI —— 锁内不得有任何可能阻塞的调用 */
static void on_scan_done(const sys_net_ap_info_t *aps, int count,
                         esp_err_t err, void *ctx)
{
    net_priv_t *p = ctx;
    if (!p || !p->ui_built) {
        return;
    }
    /* 锁外拷贝 */
    int n = 0;
    sys_net_ap_info_t local[SYS_NET_SCAN_MAX];
    if (err == ESP_OK && aps) {
        n = count > SYS_NET_SCAN_MAX ? SYS_NET_SCAN_MAX : count;
        memcpy(local, aps, sizeof(local[0]) * (size_t)n);
    }
    if (!sys_ui_lock()) {
        return; /* 丢失本帧, 下次扫描会刷新 */
    }
    p->ap_count = n;
    memcpy(p->aps, local, sizeof(p->aps[0]) * (size_t)n);
    lv_label_set_text(p->scan_btn_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_SCAN));
    rebuild_ap_list(p);
    wifi_card_refresh(p);
    sys_ui_unlock();
}

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    net_priv_t *p = lv_event_get_user_data(e);
    if (!p || s_kbd) {
        return;
    }
    /* 点击回调上下文已持有显示锁, 直接操作 LVGL */
    lv_label_set_text(p->scan_btn_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_SCANNING));
    /* WiFi 未开先开 (队列保序: 扫描命令排在开启命令之后) */
    if (!sys_net_wifi_is_enabled()) {
        sys_net_wifi_enable(true);
    }
    if (sys_net_wifi_scan(on_scan_done, p) != ESP_OK) {
        lv_label_set_text(p->scan_btn_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_SCAN));
    }
}

/* ========================= ESP-NOW / 时间 ========================= */

static void now_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (on) {
        if (!sys_net_wifi_is_enabled()) {
            sys_net_wifi_enable(true);
        }
        if (sys_net_espnow_start() != ESP_OK) {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
    } else {
        sys_net_espnow_stop();
    }
}

static void time_sync_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = sys_net_time_sync();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "time sync failed: %s", esp_err_to_name(err));
    }
}

/* ========================= 天气 / OTA ========================= */

/* 天气卡内容刷新。调用方必须已持有显示锁; w 无效时显示离线提示 */
static void weather_lbl_render(lv_obj_t *lbl, const sys_net_weather_t *w)
{
    if (!lbl) {
        return;
    }
    if (w && w->valid) {
        lv_label_set_text_fmt(lbl, "%d°C %s", w->temp_c, w->desc);
    } else {
        lv_label_set_text(lbl, sys_ui_str(SYS_UI_STR_WD_WEATHER_OFFLINE));
    }
}

static void wx_refresh_cb(lv_event_t *e)
{
    (void)e;
    /* 异步: 天气任务内执行, 不阻塞 UI; 结果经 SYS_EVENT_WEATHER 回来 */
    esp_err_t err = sys_net_weather_refresh();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "weather refresh rejected: %s", esp_err_to_name(err));
    }
}

static void ota_check_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = sys_ota_check_update();
    if (err != ESP_OK) {
        /* 检查未启动 (如 WiFi 未连), 不会有 SYS_EVENT_OTA, 按钮保持可用 */
        ESP_LOGW(TAG, "ota check failed: %s", esp_err_to_name(err));
    }
}

/* ========================= 事件总线 (非 LVGL 上下文) ========================= */

static void on_wifi_status(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id; (void)event_data;
    net_priv_t *p = user_ctx;
    if (!p || !p->ui_built) {
        return;
    }
    if (!sys_ui_lock()) {
        return;
    }
    wifi_card_refresh(p);
    sys_ui_unlock();
}

static void on_now_peer(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id;
    net_priv_t *p = user_ctx;
    if (!p || !p->now_peer_lbl) {
        return;
    }
    int n = event_data ? *(int *)event_data : 0;
    if (!sys_ui_lock()) {
        return;
    }
    lv_label_set_text_fmt(p->now_peer_lbl, sys_ui_str(SYS_UI_STR_NET_ESPNOW_PEERS), n);
    sys_ui_unlock();
}

static void on_time_synced(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id; (void)event_data;
    net_priv_t *p = user_ctx;
    if (!p || !p->time_state_lbl) {
        return;
    }
    if (!sys_ui_lock()) {
        return;
    }
    lv_label_set_text(p->time_state_lbl, sys_ui_str(SYS_UI_STR_NET_TIME_OK));
    sys_ui_unlock();
}

static void on_weather(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id;
    net_priv_t *p = user_ctx;
    if (!p || !p->wx_lbl) {
        return;
    }
    const sys_net_weather_t *w = event_data;
    if (!w) {
        return;
    }
    /* 回调内拷贝 payload (指针虽稳定, 仍按事件总线约定立即读取) */
    sys_net_weather_t local = *w;
    if (!sys_ui_lock()) {
        return;
    }
    weather_lbl_render(p->wx_lbl, &local);
    sys_ui_unlock();
}

static void on_time_tick(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id; (void)event_data;
    sys_app_t *self = user_ctx;
    if (self->state != SYS_APP_STATE_ACTIVE || !self->priv) {
        return;
    }
    net_priv_t *p = self->priv;
    sys_net_time_check_and_report();
    if (!p->time_state_lbl) {
        return;
    }
    bool synced = sys_net_time_is_synced();
    if (!sys_ui_lock()) {
        return;
    }
    lv_label_set_text(p->time_state_lbl,
                      synced ? sys_ui_str(SYS_UI_STR_NET_TIME_OK)
                             : sys_ui_str(SYS_UI_STR_NET_TIME_NOSYNC));
    sys_ui_unlock();
}

/* OTA 阶段事件: payload const sys_ota_evt_t* {int stage; int percent;}
   stage 1-4 (检查/发现/下载/安装) 期间禁用按钮, 5 (最新) / 6 (失败) 恢复 */
static void on_ota(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id;
    net_priv_t *p = user_ctx;
    if (!p || !p->ui_built || !p->ota_state_lbl || !p->ota_btn) {
        return;
    }
    const sys_ota_evt_t *ev = event_data;
    if (!ev) {
        return;
    }
    if (!sys_ui_lock()) {
        return;
    }
    if (ev->stage >= 1 && ev->stage <= 4) {
        lv_obj_add_state(p->ota_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(p->ota_btn, LV_STATE_DISABLED);
    }
    switch (ev->stage) {
    case 1:
        lv_label_set_text(p->ota_state_lbl, sys_ui_str(SYS_UI_STR_OTA_CHECKING));
        break;
    case 2:
        lv_label_set_text(p->ota_state_lbl, sys_ui_str(SYS_UI_STR_OTA_NEW));
        break;
    case 3:
        lv_label_set_text_fmt(p->ota_state_lbl,
                              sys_ui_str(SYS_UI_STR_OTA_DOWNLOADING), ev->percent);
        break;
    case 4:
        lv_label_set_text(p->ota_state_lbl, sys_ui_str(SYS_UI_STR_OTA_INSTALLING));
        break;
    case 5:
        lv_label_set_text(p->ota_state_lbl, sys_ui_str(SYS_UI_STR_OTA_LATEST));
        break;
    case 6:
        lv_label_set_text(p->ota_state_lbl, sys_ui_str(SYS_UI_STR_OTA_FAILED));
        break;
    default:
        break;
    }
    sys_ui_unlock();
}

/* ========================= 导航 ========================= */

static void back_cb(lv_event_t *e)
{
    (void)e;
    sys_app_back();
}

static void back_edge_cb(void *ctx)
{
    (void)ctx;
    sys_app_back();
}

static void qc_open_cb(void *ctx)
{
    (void)ctx;
    if (!sys_ui_quickpanel_active()) {
        sys_ui_quickpanel_show();
    }
}

/* ========================= UI 构建 ========================= */

/* 顶部条: 返回键 + 标题 */
static lv_obj_t *build_topbar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 52);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 26);  /* 避开顶部 24px 边缘手势区 */
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
    lv_label_set_text(title, sys_ui_str(SYS_UI_STR_APP_NET));
    lv_obj_set_style_text_color(title, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(title, sys_ui_theme_font(30), 0);
    lv_obj_align(title, LV_ALIGN_RIGHT_MID, -8, 0);
    return bar;
}

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

static esp_err_t build_ui(sys_app_t *app)
{
    net_priv_t *p = app->priv;
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }
    p->screen = lv_obj_create(NULL);
    sys_ui_theme_screen(p->screen);
    lv_obj_clear_flag(p->screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 边缘手势: 顶=控制中心, 底=返回 */
    sys_ui_top_edge_create(p->screen, qc_open_cb, app);
    sys_ui_bottom_edge_create(p->screen, back_edge_cb, app);

    build_topbar(p->screen);

    p->scroll = lv_obj_create(p->screen);
    lv_obj_set_width(p->scroll, LV_PCT(100));
    lv_obj_set_height(p->scroll, LV_PCT(100) - 78);
    lv_obj_align(p->scroll, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_opa(p->scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p->scroll, 0, 0);
    lv_obj_set_style_pad_all(p->scroll, 16, 0);
    lv_obj_set_style_pad_row(p->scroll, 16, 0);
    lv_obj_set_flex_flow(p->scroll, LV_FLEX_FLOW_COLUMN);

    /* ---------- WiFi 卡 ---------- */
    lv_obj_t *wcard = make_card(p->scroll);

    lv_obj_t *wrow = lv_obj_create(wcard);
    lv_obj_set_size(wrow, LV_PCT(100), 36);
    lv_obj_set_style_bg_opa(wrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrow, 0, 0);
    lv_obj_clear_flag(wrow, LV_OBJ_FLAG_SCROLLABLE);
    p->wifi_dot = lv_obj_create(wrow);
    lv_obj_set_size(p->wifi_dot, 12, 12);
    lv_obj_align(p->wifi_dot, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(p->wifi_dot, lv_color_hex(SYS_UI_THEME_DISABLED), 0);
    lv_obj_set_style_radius(p->wifi_dot, 6, 0);
    lv_obj_set_style_border_width(p->wifi_dot, 0, 0);
    lv_obj_clear_flag(p->wifi_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *wtitle = lv_label_create(wrow);
    lv_label_set_text(wtitle, sys_ui_str(SYS_UI_STR_NET_WIFI));
    lv_obj_set_style_text_color(wtitle, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(wtitle, sys_ui_theme_font(26), 0);
    lv_obj_align(wtitle, LV_ALIGN_LEFT_MID, 24, 0);
    p->wifi_state_lbl = lv_label_create(wrow);
    lv_label_set_text(p->wifi_state_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_OFF));
    lv_obj_set_style_text_color(p->wifi_state_lbl, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(p->wifi_state_lbl, sys_ui_theme_font(18), 0);
    lv_obj_align(p->wifi_state_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(p->wifi_state_lbl, 220);
    lv_label_set_long_mode(p->wifi_state_lbl, LV_LABEL_LONG_DOT);

    lv_obj_t *scan_btn = lv_button_create(wcard);
    lv_obj_set_size(scan_btn, 160, 48);
    lv_obj_align(scan_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(scan_btn, 24, 0);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(SYS_UI_THEME_PRIMARY), 0);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, p);
    p->scan_btn_lbl = lv_label_create(scan_btn);
    lv_label_set_text(p->scan_btn_lbl, sys_ui_str(SYS_UI_STR_NET_WIFI_SCAN));
    lv_obj_set_style_text_color(p->scan_btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(p->scan_btn_lbl, sys_ui_theme_font(22), 0);
    lv_obj_center(p->scan_btn_lbl);

    p->ap_list = lv_obj_create(wcard);
    lv_obj_set_width(p->ap_list, LV_PCT(100));
    lv_obj_set_height(p->ap_list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(p->ap_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p->ap_list, 0, 0);
    lv_obj_set_style_pad_row(p->ap_list, 8, 0);
    lv_obj_set_flex_flow(p->ap_list, LV_FLEX_FLOW_COLUMN);

    /* ---------- ESP-NOW 卡 ---------- */
    lv_obj_t *ncard = make_card(p->scroll);
    lv_obj_t *nrow = lv_obj_create(ncard);
    lv_obj_set_size(nrow, LV_PCT(100), 56);
    lv_obj_set_style_bg_opa(nrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nrow, 0, 0);
    lv_obj_clear_flag(nrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ntitle = lv_label_create(nrow);
    lv_label_set_text(ntitle, sys_ui_str(SYS_UI_STR_NET_ESPNOW));
    lv_obj_set_style_text_color(ntitle, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(ntitle, sys_ui_theme_font(26), 0);
    lv_obj_align(ntitle, LV_ALIGN_LEFT_MID, 0, 0);
    p->now_sw = lv_switch_create(nrow);
    lv_obj_set_size(p->now_sw, 76, 40);
    lv_obj_align(p->now_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(p->now_sw, now_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *nhint = lv_label_create(ncard);
    lv_label_set_text(nhint, sys_ui_str(SYS_UI_STR_NET_ESPNOW_HINT));
    lv_obj_set_style_text_color(nhint, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(nhint, sys_ui_theme_font(18), 0);
    lv_obj_set_width(nhint, LV_PCT(55));
    lv_label_set_long_mode(nhint, LV_LABEL_LONG_WRAP);
    p->now_peer_lbl = lv_label_create(ncard);
    lv_label_set_text_fmt(p->now_peer_lbl, sys_ui_str(SYS_UI_STR_NET_ESPNOW_PEERS), 0);
    lv_obj_set_style_text_color(p->now_peer_lbl, lv_color_hex(SYS_UI_THEME_ACCENT), 0);
    lv_obj_set_style_text_font(p->now_peer_lbl, sys_ui_theme_font(20), 0);
    lv_obj_align(p->now_peer_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ---------- 蓝牙卡 (硬件不支持, 压暗占位) ---------- */
    lv_obj_t *bcard = make_card(p->scroll);
    sys_ui_theme_card_dim(bcard);
    lv_obj_t *btitle = lv_label_create(bcard);
    lv_label_set_text(btitle, sys_ui_str(SYS_UI_STR_NET_BT));
    lv_obj_set_style_text_color(btitle, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(btitle, sys_ui_theme_font(26), 0);
    lv_obj_t *bhint = lv_label_create(bcard);
    lv_label_set_text(bhint, sys_ui_str(SYS_UI_STR_NET_BT_UNSUPPORTED));
    lv_obj_set_style_text_color(bhint, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(bhint, sys_ui_theme_font(18), 0);
    lv_obj_set_width(bhint, LV_PCT(100));
    lv_label_set_long_mode(bhint, LV_LABEL_LONG_WRAP);

    /* ---------- 时间卡 ---------- */
    lv_obj_t *tcard = make_card(p->scroll);
    lv_obj_t *trow = lv_obj_create(tcard);
    lv_obj_set_size(trow, LV_PCT(100), 48);
    lv_obj_set_style_bg_opa(trow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trow, 0, 0);
    lv_obj_clear_flag(trow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ttitle = lv_label_create(trow);
    lv_label_set_text(ttitle, sys_ui_str(SYS_UI_STR_NET_TIME));
    lv_obj_set_style_text_color(ttitle, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(ttitle, sys_ui_theme_font(26), 0);
    lv_obj_align(ttitle, LV_ALIGN_LEFT_MID, 0, 0);
    p->time_state_lbl = lv_label_create(trow);
    lv_label_set_text(p->time_state_lbl, sys_ui_str(SYS_UI_STR_NET_TIME_NOSYNC));
    lv_obj_set_style_text_color(p->time_state_lbl, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(p->time_state_lbl, sys_ui_theme_font(18), 0);
    lv_obj_align(p->time_state_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *sync_btn = lv_button_create(tcard);
    lv_obj_set_size(sync_btn, 160, 48);
    lv_obj_align(sync_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(sync_btn, 24, 0);
    lv_obj_set_style_bg_color(sync_btn, lv_color_hex(SYS_UI_THEME_CARD_ALT), 0);
    lv_obj_add_event_cb(sync_btn, time_sync_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sync_lbl = lv_label_create(sync_btn);
    lv_label_set_text(sync_lbl, sys_ui_str(SYS_UI_STR_BTN_SYNC));
    lv_obj_set_style_text_color(sync_lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(sync_lbl, sys_ui_theme_font(22), 0);
    lv_obj_center(sync_lbl);

    /* ---------- 天气卡 (校时卡下方) ---------- */
    lv_obj_t *wxcard = make_card(p->scroll);
    lv_obj_t *wxrow = lv_obj_create(wxcard);
    lv_obj_set_size(wxrow, LV_PCT(100), 48);
    lv_obj_set_style_bg_opa(wxrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wxrow, 0, 0);
    lv_obj_clear_flag(wxrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *wxtitle = lv_label_create(wxrow);
    lv_label_set_text(wxtitle, sys_ui_str(SYS_UI_STR_WD_WEATHER));
    lv_obj_set_style_text_color(wxtitle, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(wxtitle, sys_ui_theme_font(26), 0);
    lv_obj_align(wxtitle, LV_ALIGN_LEFT_MID, 0, 0);
    p->wx_lbl = lv_label_create(wxrow);
    lv_label_set_text(p->wx_lbl, sys_ui_str(SYS_UI_STR_WD_WEATHER_OFFLINE));
    lv_obj_set_style_text_color(p->wx_lbl, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(p->wx_lbl, sys_ui_theme_font(18), 0);
    lv_obj_align(p->wx_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(p->wx_lbl, 260);
    lv_label_set_long_mode(p->wx_lbl, LV_LABEL_LONG_DOT);
    lv_obj_t *wx_btn = lv_button_create(wxcard);
    lv_obj_set_size(wx_btn, 160, 48);
    lv_obj_align(wx_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(wx_btn, 24, 0);
    lv_obj_set_style_bg_color(wx_btn, lv_color_hex(SYS_UI_THEME_CARD_ALT), 0);
    lv_obj_add_event_cb(wx_btn, wx_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wx_btn_lbl = lv_label_create(wx_btn);
    lv_label_set_text(wx_btn_lbl, sys_ui_str(SYS_UI_STR_BTN_SYNC));
    lv_obj_set_style_text_color(wx_btn_lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(wx_btn_lbl, sys_ui_theme_font(22), 0);
    lv_obj_center(wx_btn_lbl);

    /* ---------- OTA 卡 (天气卡下方) ---------- */
    lv_obj_t *ocard = make_card(p->scroll);
    lv_obj_t *orow = lv_obj_create(ocard);
    lv_obj_set_size(orow, LV_PCT(100), 48);
    lv_obj_set_style_bg_opa(orow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(orow, 0, 0);
    lv_obj_clear_flag(orow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *otitle = lv_label_create(orow);
    lv_label_set_text(otitle, sys_ui_str(SYS_UI_STR_OTA_TITLE));
    lv_obj_set_style_text_color(otitle, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(otitle, sys_ui_theme_font(26), 0);
    lv_obj_align(otitle, LV_ALIGN_LEFT_MID, 0, 0);
    p->ota_state_lbl = lv_label_create(orow);
    lv_label_set_text(p->ota_state_lbl, "");
    lv_obj_set_style_text_color(p->ota_state_lbl, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(p->ota_state_lbl, sys_ui_theme_font(18), 0);
    lv_obj_align(p->ota_state_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(p->ota_state_lbl, 260);
    lv_label_set_long_mode(p->ota_state_lbl, LV_LABEL_LONG_DOT);
    lv_obj_t *over = lv_label_create(ocard);
    lv_label_set_text_fmt(over, sys_ui_str(SYS_UI_STR_OTA_CURRENT),
                          sys_ota_current_version());
    lv_obj_set_style_text_color(over, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(over, sys_ui_theme_font(18), 0);
    lv_obj_align(over, LV_ALIGN_LEFT_MID, 0, 0);
    p->ota_btn = lv_button_create(ocard);
    lv_obj_set_size(p->ota_btn, 160, 48);
    lv_obj_align(p->ota_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(p->ota_btn, 24, 0);
    lv_obj_set_style_bg_color(p->ota_btn, lv_color_hex(SYS_UI_THEME_PRIMARY), 0);
    lv_obj_add_event_cb(p->ota_btn, ota_check_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *obtn_lbl = lv_label_create(p->ota_btn);
    lv_label_set_text(obtn_lbl, sys_ui_str(SYS_UI_STR_OTA_CHECK));
    lv_obj_set_style_text_color(obtn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(obtn_lbl, sys_ui_theme_font(22), 0);
    lv_obj_center(obtn_lbl);

    sys_ui_unlock();

    /* 事件订阅 (无需显示锁) */
    sys_event_subscribe(SYS_EVENT_WIFI_STATUS, on_wifi_status, p, &p->wifi_sub);
    sys_event_subscribe(SYS_EVENT_ESPNOW_PEER, on_now_peer, p, &p->now_sub);
    sys_event_subscribe(SYS_EVENT_TIME_SYNCED, on_time_synced, p, &p->time_sub);
    sys_event_subscribe(SYS_EVENT_TIME_TICK, on_time_tick, app, &p->tick_sub);
    sys_event_subscribe(SYS_EVENT_WEATHER, on_weather, p, &p->wx_sub);
    sys_event_subscribe(SYS_EVENT_OTA, on_ota, p, &p->ota_sub);

    int n = 0;
    sys_net_espnow_peers(&n);
    ESP_LOGI(TAG, "net ui built");
    return ESP_OK;
}

/* ========================= 生命周期 ========================= */

static esp_err_t on_init(sys_app_t *app)
{
    net_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    net_priv_t *p = app->priv;
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
    wifi_card_refresh(p);
    /* 天气卡首帧: 有缓存显示缓存, 否则离线提示 (后续 SYS_EVENT_WEATHER 更新) */
    sys_net_weather_t wx;
    if (sys_net_weather_snapshot(&wx) == ESP_OK) {
        weather_lbl_render(p->wx_lbl, &wx);
    }
    sys_ui_unlock();
    return ESP_OK;
}

static void on_suspend(sys_app_t *app)
{
    (void)app;
}

static void on_stop(sys_app_t *app)
{
    net_priv_t *p = app->priv;
    if (!p) {
        return;
    }
    sys_event_unsubscribe(p->wifi_sub);
    sys_event_unsubscribe(p->now_sub);
    sys_event_unsubscribe(p->time_sub);
    sys_event_unsubscribe(p->tick_sub);
    sys_event_unsubscribe(p->wx_sub);
    sys_event_unsubscribe(p->ota_sub);
    if (s_kbd) {
        /* 对话框还开着: overlay 挂在 lv_layer_top, 先关再释放 (持锁操作) */
        if (sys_ui_lock()) {
            sys_ui_overlay_dismiss();
            sys_ui_unlock();
        }
        kbd_priv_free();
    }
    if (sys_ui_lock()) {
        if (p->screen) {
            lv_obj_delete(p->screen);
        }
        sys_ui_unlock();
    }
    p->ui_built = false;
    free(p);
    app->priv = NULL;
}

static sys_app_t s_net = {
    .id         = NET_APP_ID,
    .name       = "Network",
    .icon       = 0,
    .on_init    = on_init,
    .on_start   = on_start,
    .on_suspend = on_suspend,
    .on_stop    = on_stop,
    .on_event   = NULL,
};

sys_app_t *net_app_get(void)
{
    return &s_net;
}

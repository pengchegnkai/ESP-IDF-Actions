/**
 * @file app_security.c
 * @brief 安全密钥 APP —— FIDO2 手动激活 (阶段三骨架)
 *
 * 结构 (Vela 卡片风格):
 *   - "USB 状态"卡: 订阅 SYS_EVENT_USB_IN/OUT 刷新 (事件源 = sys_hal FIDO 层
 *     的 TinyUSB attach/detach 回调)
 *   - "激活"卡: 开关 + 状态 + 提示; 变更 → sys_hal_usb_fido_set_active() +
 *     发布 SYS_EVENT_SEC_ACTIVE。开机默认 OFF (手动激活模式), 不持久化。
 *
 * CTAP2 处理 (v1 最简, 回调运行在 TinyUSB 任务上下文):
 *   帧格式: [channel, ccid, params...]; 本设备只处理 channel 0, 第二字节为 CCID:
 *     0x04 GetInfo        → 直接自动应答 (静态占位 CBOR, TODO 阶段二)
 *     0x05 MakeCredential → 弹窗 (SEC_MAKE_CRED) → 确认签名/取消
 *     0x06 GetAssertion   → 弹窗 (SEC_ASSERT)    → 确认签名/取消
 *   确认 → secure_link_fido_sign() (v1 stub 必失败) → 组最简 CTAP2 应答
 *         (失败 = 错误码帧) → sys_hal_usb_fido_send();  取消 → USER_ACTION 错误帧。
 *   TODO(阶段二): 多报告消息拼装 (CTAP2 消息可 >64B)、完整 CBOR 应答组装、
 *                 GetInfo 真实能力表、PIN/UvAuth 流程。
 */
#include <stdlib.h>
#include <string.h>
#include "app_security/app_security.h"
#include "app_watch/watch_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_hal/sys_hal.h"
#include "sys_ui/sys_ui.h"
#include "sys_ui/sys_ui_theme.h"
#include "sys_ui/sys_ui_gesture.h"
#include "sys_ui/sys_ui_quickpanel.h"
#include "sys_ui/sys_ui_secure_dialog.h"
#include "secure_link/secure_link.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "security";

/* ---------- CTAP2 常量 ---------- */

#define CTAP_MAX_REPORT 64  /* 单报告最大长度 (FIDO HID 报告 64B) */

/* CCID (channel 0) */
#define CTAP_CCID_GET_INFO        0x04
#define CTAP_CCID_MAKE_CRED       0x05
#define CTAP_CCID_GET_ASSERTION   0x06

/* CTAP2 错误码 (FIDO2 spec) */
#define CTAP2_ERR_INVALID_CMD  0x01  /* 未知命令 */
#define CTAP2_ERR_INTERNAL     0x0D  /* v1 占位 (官方为 UNSUPPORTED_ALGORITHM); 阶段二细化 */
#define CTAP2_ERR_USER_ACTION  0x27  /* 用户未确认 (取消) */

/* GetInfo 静态占位应答: CBOR map { 1:"FIDO_2.1", 4:64, 5:aaguid(16B) }
 * TODO(阶段二): 换真实能力表 (extensions/pinProtocols/maxPinRetries/...) 与真 AAGUID */
static const uint8_t S_GETINFO_RESP[] = {
    0x00, 0x00,                               /* channel 0 + 应答域 0x00 */
    0xA3,                                     /* CBOR map(3) */
    0x01, 0x08, 'F', 'I', 'D', 'O', '_', '2', '.', '1',  /* versionU2F = "FIDO_2.1" */
    0x04, 0x40,                               /* maxMessageSize = 64 */
    0x05, 0x40,                               /* aaguid: bytes(16) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *usb_lbl;    /* USB 状态值 (SEC_USB_IN/_OUT) */
    lv_obj_t *state_lbl;  /* 激活状态值 (SEC_ACTIVE/_INACTIVE) */
    lv_obj_t *sw;         /* 激活开关 */
    bool ui_built;
    bool active;          /* 手动激活状态 (开机默认 OFF) */
    bool processing;      /* 有 CTAP 请求在处理中 (确认弹窗打开期间) */
    int pending_op;       /* CTAP_CCID_MAKE_CRED / CTAP_CCID_GET_ASSERTION */
    uint8_t ctap_buf[CTAP_MAX_REPORT];
    int ctap_len;
    sys_event_handle_t usb_in_sub;
    sys_event_handle_t usb_out_sub;
} sec_priv_t;

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

/* ---------- USB 状态 (事件总线任务上下文, 需显示锁) ---------- */

static void on_usb_evt(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_data;
    sys_app_t *self = user_ctx;
    sec_priv_t *p = self ? self->priv : NULL;
    if (!p || !p->usb_lbl || self->state != SYS_APP_STATE_ACTIVE) {
        return;
    }
    if (!sys_ui_lock()) {
        return;
    }
    lv_label_set_text(p->usb_lbl,
                      sys_ui_str(event_id == SYS_EVENT_USB_IN ? SYS_UI_STR_USB_IN
                                                              : SYS_UI_STR_USB_OUT));
    sys_ui_unlock();
}

/* ---------- 激活开关 (LVGL 事件上下文, 已持显示锁) ---------- */

static void sec_sw_cb(lv_event_t *e)
{
    sec_priv_t *p = lv_event_get_user_data(e);
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    p->active = on;
    sys_hal_usb_fido_set_active(on);   /* HAL 路由开关: 停用 = 丢弃 CTAP 数据不响应 */
    int v = on ? 1 : 0;
    sys_event_publish(SYS_EVENT_SEC_ACTIVE, &v);
    if (p->state_lbl) {
        lv_label_set_text(p->state_lbl,
                          sys_ui_str(on ? SYS_UI_STR_SEC_ACTIVE : SYS_UI_STR_SEC_INACTIVE));
    }
    ESP_LOGI(TAG, "security key %s", on ? "ACTIVE" : "INACTIVE");
}

/* ---------- CTAP2 处理 ---------- */

/* 确认弹窗结果: 由 sys_ui_secure_dialog 在 LVGL 上下文调用 (弹窗已关闭, 持显示锁) */
static bool on_dialog_result(bool confirmed, void *ctx)
{
    sys_app_t *app = ctx;
    sec_priv_t *p = app ? app->priv : NULL;
    if (!p) {
        return false;  /* APP 已退出 */
    }
    uint8_t resp[3];
    int n;
    ESP_LOGI(TAG, "ctap op=0x%02X result=%s", p->pending_op,
             confirmed ? "confirm" : "cancel");

    if (!confirmed) {
        /* 取消 → 错误帧 [channel, 被处理命令 CCID, 错误码] */
        resp[0] = 0x00;
        resp[1] = (uint8_t)p->pending_op;
        resp[2] = CTAP2_ERR_USER_ACTION;
        n = 3;
        ESP_LOGI(TAG, "ctap: user cancelled");
    } else {
        /* 确认 → 安全链路签名 (阶段二: SDIO AES-GCM/PSK 真实现; v1 stub 必失败) */
        uint8_t sig[CTAP_MAX_REPORT + 2];
        int sig_len = sizeof(sig);
        esp_err_t err = secure_link_fido_sign(p->ctap_buf, p->ctap_len, sig, &sig_len);
        if (err == ESP_OK) {
            /* TODO(阶段二): 组完整 CTAP2 CBOR 应答后发送:
             *  MakeCredential → {1:credId, 2:clientDataJSON, 3:authData, 4:attstn}
             *  GetAssertion   → {1:credId, 2:clientDataJSON, 3:authData, 4:signature} */
            ESP_LOGW(TAG, "sign ok but v1 has no response assembly yet (TODO phase 2)");
        } else {
            ESP_LOGW(TAG, "secure_link_fido_sign failed: %s -> error frame",
                     esp_err_to_name(err));
        }
        /* v1: 签名不可用 → 内部错误帧 [channel, 命令 CCID, 错误码] */
        resp[0] = 0x00;
        resp[1] = (uint8_t)p->pending_op;
        resp[2] = CTAP2_ERR_INTERNAL;
        n = 3;
    }

    sys_hal_usb_fido_send(resp, n);
    p->processing = false;
    p->pending_op = 0;
    ESP_LOGI(TAG, "ctap response sent (%d bytes)", n);
    return true;
}

/* CTAP2 帧接收回调: 运行在 TinyUSB 任务上下文 (非 LVGL, 非 ISR) */
static void ctap_rx_cb(const uint8_t *data, int len, void *ctx)
{
    sys_app_t *app = ctx;
    sec_priv_t *p = app ? app->priv : NULL;
    if (!p || !p->active) {
        return;  /* 停用: HAL 已丢弃, 此处双保险 */
    }
    if (p->processing) {
        ESP_LOGW(TAG, "ctap busy (dialog open), drop");
        return;
    }
    if (len < 1 || len > CTAP_MAX_REPORT) {
        ESP_LOGW(TAG, "ctap frame len %d invalid, drop", len);
        return;
    }

    if (len < 2 || data[0] != 0) {
        /* 真实 CTAPHID 帧: 首字节 channel, 次字节 CCID; 本设备只处理 channel 0 */
        ESP_LOGW(TAG, "ctap bad frame (len=%d ch=%02x), drop", len,
                 len >= 1 ? data[0] : 0xFF);
        return;
    }
    uint8_t ccid = data[1];
    switch (ccid) {
    case CTAP_CCID_GET_INFO:
        /* 自动应答: 静态占位 CBOR (TODO 阶段二: 真实 GetInfo) */
        ESP_LOGI(TAG, "ctap GetInfo -> placeholder response");
        sys_hal_usb_fido_send(S_GETINFO_RESP, sizeof(S_GETINFO_RESP));
        return;
    case CTAP_CCID_MAKE_CRED:
    case CTAP_CCID_GET_ASSERTION:
        break;
    default: {
        uint8_t err[] = { 0x00, ccid, CTAP2_ERR_INVALID_CMD };
        ESP_LOGW(TAG, "ctap unknown ccid 0x%02x -> INVALID_CMD", ccid);
        sys_hal_usb_fido_send(err, sizeof(err));
        return;
    }
    }

    /* MakeCredential / GetAssertion: 缓存请求, 请求用户确认 (顶层弹窗) */
    memcpy(p->ctap_buf, data, len);
    p->ctap_len = len;
    p->pending_op = ccid;
    p->processing = true;

    const char *op_text = sys_ui_str(ccid == CTAP_CCID_MAKE_CRED
                                     ? SYS_UI_STR_SEC_MAKE_CRED
                                     : SYS_UI_STR_SEC_ASSERT);
    ESP_LOGI(TAG, "ctap %s request -> confirm dialog", op_text);
    /* detail_text 传 NULL: v1 无 origin 文案 ID (阶段二补); 弹窗内部自取显示锁 */
    esp_err_t err = sys_ui_secure_dialog_show(op_text, NULL, on_dialog_result, app);
    if (err != ESP_OK) {
        /* 弹窗不可用 (已有其他弹窗等): 直接回错误帧, 恢复空闲 */
        p->processing = false;
        p->pending_op = 0;
        uint8_t errf[] = { CTAP_CCID_GET_INFO, CTAP2_ERR_INTERNAL };
        ESP_LOGW(TAG, "dialog show failed: %s -> error frame", esp_err_to_name(err));
        sys_hal_usb_fido_send(errf, sizeof(errf));
    }
}

/* ---------- UI 构建 ---------- */

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

static esp_err_t build_ui(sys_app_t *app)
{
    sec_priv_t *p = app->priv;
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
    lv_label_set_text(title, sys_ui_str(SYS_UI_STR_SEC_TITLE));
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

    /* 卡 1: USB 状态 */
    lv_obj_t *c1 = make_card(scroll);
    info_row(c1, sys_ui_str(SYS_UI_STR_SEC_USB_STATE), &p->usb_lbl);
    /* 初始状态: 直接读 HAL 的挂载状态 (避免"已插线却显示拔出") */
    lv_label_set_text(p->usb_lbl,
                      sys_ui_str(sys_hal_usb_fido_connected() ? SYS_UI_STR_USB_IN
                                                              : SYS_UI_STR_USB_OUT));

    /* 卡 2: 激活 (手动激活模式, 开机默认 OFF) */
    lv_obj_t *c2 = make_card(scroll);
    info_row(c2, sys_ui_str(SYS_UI_STR_SEC_ACTIVATE), &p->state_lbl);
    lv_label_set_text(p->state_lbl,
                      sys_ui_str(p->active ? SYS_UI_STR_SEC_ACTIVE
                                           : SYS_UI_STR_SEC_INACTIVE));
    lv_obj_t *row2 = lv_obj_create(c2);
    lv_obj_set_width(row2, LV_PCT(100));
    lv_obj_set_height(row2, 56);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row2, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_t *hint = lv_label_create(row2);
    lv_label_set_text(hint, sys_ui_str(SYS_UI_STR_SEC_HINT));
    lv_obj_set_style_text_color(hint, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint, sys_ui_theme_font(18), 0);
    lv_obj_align(hint, LV_ALIGN_LEFT_MID, 0, 0);
    p->sw = lv_switch_create(row2);
    lv_obj_set_size(p->sw, 76, 40);
    lv_obj_align(p->sw, LV_ALIGN_RIGHT_MID, 0, 0);
    if (p->active) {
        lv_obj_add_state(p->sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(p->sw, sec_sw_cb, LV_EVENT_VALUE_CHANGED, p);

    sys_ui_unlock();
    ESP_LOGI(TAG, "security ui built");
    return ESP_OK;
}

/* ---------- 生命周期 ---------- */

static esp_err_t on_init(sys_app_t *app)
{
    sec_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;

    /* 手动激活模式: 开机默认 OFF (不持久化, 重启归 OFF) */
    p->active = false;
    sys_hal_usb_fido_set_active(false);
    /* CTAP2 路由: 收到帧 → ctap_rx_cb (TinyUSB 任务上下文); 在 on_stop 注销 */
    sys_hal_usb_fido_register_rx_cb(ctap_rx_cb, app);

    /* USB 插拔事件 (事件源 = sys_hal FIDO 层 TinyUSB attach/detach) */
    sys_event_subscribe(SYS_EVENT_USB_IN, on_usb_evt, app, &p->usb_in_sub);
    sys_event_subscribe(SYS_EVENT_USB_OUT, on_usb_evt, app, &p->usb_out_sub);
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    sec_priv_t *p = app->priv;
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
    /* 回前台刷新一次 USB 状态 (期间可能已插拔) */
    if (p->usb_lbl) {
        lv_label_set_text(p->usb_lbl,
                          sys_ui_str(sys_hal_usb_fido_connected() ? SYS_UI_STR_USB_IN
                                                                  : SYS_UI_STR_USB_OUT));
    }
    sys_ui_unlock();
    return ESP_OK;
}

static void on_suspend(sys_app_t *app)
{
    (void)app;
    ESP_LOGI(TAG, "security to background");
}

static void on_stop(sys_app_t *app)
{
    sec_priv_t *p = app->priv;
    if (!p) {
        return;
    }
    sys_event_unsubscribe(p->usb_in_sub);
    sys_event_unsubscribe(p->usb_out_sub);
    sys_hal_usb_fido_register_rx_cb(NULL, NULL);  /* 注销 CTAP 回调 */
    if (sys_ui_lock()) {
        if (p->screen) {
            lv_obj_delete(p->screen);
        }
        sys_ui_unlock();
    }
    p->ui_built = false;
    free(p);
    app->priv = NULL;
    ESP_LOGI(TAG, "security stopped");
}

static sys_app_t s_security = {
    .id         = SECURITY_APP_ID,
    .name       = "Security",
    .icon       = 2,
    .on_init    = on_init,
    .on_start   = on_start,
    .on_suspend = on_suspend,
    .on_stop    = on_stop,
    .on_event   = NULL,
};

sys_app_t *security_app_get(void)
{
    return &s_security;
}

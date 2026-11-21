/**
 * @file sys_ui_secure_dialog.c
 * @brief 安全认证弹窗实现 —— 基于 sys_ui_overlay_show 的顶层弹窗
 *
 * 流程:
 *   show()  (任意 FreeRTOS 任务, 内部取锁建卡)
 *     点确认 → 详情行变 SEC_SIGNING, 按钮禁用 → 100ms 后 (LVGL timer) 关弹窗
 *              → on_result(true, ctx)   —— 让"正在签名"至少渲染一帧
 *     点取消 → 立即关弹窗 → on_result(false, ctx)
 *
 * on_result 运行在 LVGL 渲染上下文 (持显示锁): 回调内不要碰 LVGL 对象
 * (弹窗已销毁), 不要长期阻塞 (阶段二签名异步化后此约束自然解除)。
 */
#include <string.h>
#include <stdint.h>
#include "sys_ui/sys_ui.h"
#include "sys_ui/sys_ui_theme.h"
#include "sys_ui/sys_ui_secure_dialog.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "sys_ui_sec";

/* 弹窗互斥 (sys_ui_overlay 单实例) → 静态单份上下文安全 */
typedef struct {
    sys_ui_secure_dialog_result_cb_t on_result;
    void *user_ctx;
} dlg_cb_ctx_t;

static dlg_cb_ctx_t s_cb;
static lv_obj_t  *s_detail_lbl;    /* 详情/状态行 (确认后变 SEC_SIGNING) */
static lv_obj_t  *s_btn_confirm;   /* 确认按钮 (区分两个按钮用) */
static lv_obj_t  *s_btn_row;       /* 按钮行 (确认后整行禁用防连点) */
static lv_timer_t *s_timer;        /* 确认后的延时收尾 (一次性) */

#define SEC_SIGN_DELAY_MS 100  /* "正在签名" 的最小可见时长 */

static void finish(bool confirmed)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    /* 先关弹窗 (LVGL 上下文, 已持显示锁), 再回调 (launcher 弹窗同款模式) */
    sys_ui_overlay_dismiss();
    s_detail_lbl  = NULL;
    s_btn_confirm = NULL;
    s_btn_row     = NULL;
    if (s_cb.on_result) {
        sys_ui_secure_dialog_result_cb_t cb  = s_cb.on_result;
        void *ctx                            = s_cb.user_ctx;
        s_cb.on_result = NULL;
        cb(confirmed, ctx);
    }
}

/* LVGL timer 回调 (LVGL 任务上下文, 持显示锁) */
static void finish_timer_cb(lv_timer_t *tmr)
{
    bool confirmed = (bool)(intptr_t)lv_timer_get_user_data(tmr);
    s_timer = NULL;
    finish(confirmed);
}

/* 按钮回调 (LVGL 事件上下文, 持显示锁) */
static void btn_cb(lv_event_t *e)
{
    bool confirmed = (lv_event_get_target_obj(e) == s_btn_confirm);
    if (confirmed) {
        /* 先显示"正在签名", 禁用按钮防连点, 稍后收尾 */
        if (s_detail_lbl) {
            /* 详情行原本隐藏 (无 detail_text) 时也解除隐藏, 让"正在签名"可见 */
            lv_obj_clear_flag(s_detail_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_detail_lbl, sys_ui_str(SYS_UI_STR_SEC_SIGNING));
        }
        if (s_btn_row) {
            lv_obj_add_state(s_btn_row, LV_STATE_DISABLED);
        }
        s_timer = lv_timer_create(finish_timer_cb, SEC_SIGN_DELAY_MS,
                                  (void *)(intptr_t)true);
    } else {
        if (s_btn_row) {
            lv_obj_add_state(s_btn_row, LV_STATE_DISABLED);
        }
        finish(false);
    }
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, bool accent)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 180, 48);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (accent) {
        lv_obj_set_style_bg_color(b, lv_color_hex(SYS_UI_THEME_PRIMARY), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(SYS_UI_THEME_CARD_ALT), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(b, lv_color_white(), 0);
    } else {
        lv_obj_set_style_bg_color(b, lv_color_hex(SYS_UI_THEME_CARD), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(SYS_UI_THEME_BORDER), 0);
        lv_obj_set_style_text_color(b, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, sys_ui_theme_font(22), 0);
    lv_obj_center(l);
    return b;
}

esp_err_t sys_ui_secure_dialog_show(const char *op_text, const char *detail_text,
                                    sys_ui_secure_dialog_result_cb_t on_result, void *ctx)
{
    if (!on_result) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (sys_ui_overlay_active()) {
        /* 已有顶层弹窗, 互斥 */
        goto unlock;
    }

    /* 卡片 (挂 lv_layer_top, 与 launcher 弹窗同款; overlay_show 会挂进遮罩) */
    lv_obj_t *card = lv_obj_create(lv_layer_top());
    if (!card) {
        err = ESP_ERR_NO_MEM;
        goto unlock;
    }
    lv_obj_set_size(card, 440, 252);
    sys_ui_theme_card(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题: SEC_DIALOG_TITLE + op_text */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text_fmt(title, "%s  %s", sys_ui_str(SYS_UI_STR_SEC_DIALOG_TITLE),
                          op_text ? op_text : "");
    lv_obj_set_style_text_color(title, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(title, sys_ui_theme_font(28), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    /* 详情行 (origin 占位; 确认后变为 SEC_SIGNING) */
    s_detail_lbl = lv_label_create(card);
    lv_obj_set_style_text_color(s_detail_lbl, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_set_style_text_font(s_detail_lbl, sys_ui_theme_font(20), 0);
    if (detail_text && *detail_text) {
        lv_label_set_text(s_detail_lbl, detail_text);
    } else {
        lv_obj_add_flag(s_detail_lbl, LV_OBJ_FLAG_HIDDEN);  /* 无详情时先隐藏 */
    }
    lv_obj_align(s_detail_lbl, LV_ALIGN_TOP_MID, 0, 76);

    /* 按钮行: 取消 + 确认 */
    s_btn_row = lv_obj_create(card);
    lv_obj_set_size(s_btn_row, LV_PCT(100), 56);
    lv_obj_clear_flag(s_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_btn_row, 0, 0);
    lv_obj_align(s_btn_row, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_t *cancel  = make_btn(s_btn_row, sys_ui_str(SYS_UI_STR_SEC_CANCEL), false);
    s_btn_confirm = make_btn(s_btn_row, sys_ui_str(SYS_UI_STR_SEC_CONFIRM), true);
    lv_obj_align(cancel, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_align(s_btn_confirm, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_add_event_cb(cancel, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_btn_confirm, btn_cb, LV_EVENT_CLICKED, NULL);

    s_cb.on_result = on_result;
    s_cb.user_ctx  = ctx;

    err = sys_ui_overlay_show(card, NULL);
    if (err != ESP_OK) {
        lv_obj_delete(card);
        s_cb.on_result = NULL;
        s_detail_lbl   = NULL;
        s_btn_confirm  = NULL;
        s_btn_row      = NULL;
    } else {
        ESP_LOGI(TAG, "secure dialog shown (op=%s)", op_text ? op_text : "(null)");
    }

unlock:
    sys_ui_unlock();
    return err;
}

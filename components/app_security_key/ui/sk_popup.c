/* sk_popup.c — FIDO2 全局确认弹窗 ([纯逻辑·可仿真])
 *
 * 红线①: 覆盖层挂在 lv_layer_top() 上, 强行覆盖原厂任何界面.
 * LVGL 9.2+ API (工厂基座为 LVGL 9.5).
 *
 * 无 lvgl 头文件时 (pc_sim run_tests.sh 纯 gcc 路径) 编译为空桩 —
 * 弹窗本身的行为测试在 pc_sim CMake (PC_SIM_HAS_LVGL) / 真机路径验证.
 *
 * 真机调用约定: hal 层/APP 注册层在 esp_brookesia LvLockGuard 内调用
 * sk_popup_show/hide (LvLock 不进 ui/). */
#if __has_include("lvgl.h")

#include "sk_popup.h"
#include "lvgl.h"

#include <string.h>

static lv_obj_t *s_mask;
static lv_obj_t *s_box;
static sk_confirm_cb_t s_cb;
static void *s_user;
static char s_origin[96];

static void cleanup(void)
{
    if (s_mask) {
        lv_obj_delete(s_mask);
        s_mask = NULL;
        s_box = NULL;
    }
}

static void btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    bool allow = (bool)(intptr_t)lv_obj_get_user_data(btn);
    sk_confirm_cb_t cb = s_cb;
    void *user = s_user;
    cleanup(); /* 先收弹窗再回调, 回调里可立刻再 show */
    if (cb) cb(allow, user);
}

void sk_popup_show(const char *origin, sk_confirm_cb_t cb, void *user_data)
{
    /* 重建: 旧覆盖层直接删 (幂等 + 更新文案) */
    cleanup();
    s_cb = cb;
    s_user = user_data;
    if (origin) {
        strncpy(s_origin, origin, sizeof(s_origin) - 1);
        s_origin[sizeof(s_origin) - 1] = '\0';
    } else {
        s_origin[0] = '\0';
    }

    /* 红线①: 挂在顶层 layer, 全屏不透明遮罩, 覆盖原厂任何界面 */
    s_mask = lv_layer_top();
    lv_obj_set_size(s_mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_mask, LV_OPA_70, 0);
    lv_obj_clear_flag(s_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mask, LV_OBJ_FLAG_CLICKABLE); /* 吃掉点击, 不穿透 */

    s_box = lv_obj_create(s_mask);
    lv_obj_set_size(s_box, 360, 240);
    lv_obj_center(s_box);
    lv_obj_clear_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_box, 16, 0);

    lv_obj_t *title = lv_label_create(s_box);
    lv_label_set_text_fmt(title, "%s 网站请求使用你的安全密钥", s_origin);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *btn_ok = lv_btn_create(s_box);
    lv_obj_set_size(btn_ok, 140, 48);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_user_data(btn_ok, (void *)(intptr_t)1);
    lv_obj_add_event_cb(btn_ok, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_ok = lv_label_create(btn_ok);
    lv_label_set_text(l_ok, "允许");
    lv_obj_center(l_ok);

    lv_obj_t *btn_no = lv_btn_create(s_box);
    lv_obj_set_size(btn_no, 140, 48);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_user_data(btn_no, (void *)(intptr_t)0);
    lv_obj_add_event_cb(btn_no, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_no = lv_label_create(btn_no);
    lv_label_set_text(l_no, "拒绝");
    lv_obj_center(l_no);
}

void sk_popup_hide(void)
{
    cleanup();
}

bool sk_popup_is_visible(void)
{
    return s_mask != NULL;
}

#else /* 无 lvgl: 空桩 (ui 行为测试走 CMake + LVGL 路径) */

#include "sk_popup.h"

void sk_popup_show(const char *origin, sk_confirm_cb_t cb, void *user_data)
{
    (void)origin; (void)cb; (void)user_data;
}

void sk_popup_hide(void) {}

bool sk_popup_is_visible(void)
{
    return false;
}

#endif /* __has_include("lvgl.h") */

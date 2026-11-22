/* hk_ui.c — [纯逻辑·可仿真] App_HackerKit 纯 LVGL 界面实现
 *
 * 只允许 lvgl.h + core/ 头; 文件/USB 经 backend 接口.
 * 真机上本文件的所有回调由 APP 注册层在 LvLockGuard 内触发
 * (锁的获取放 hal 层/APP 注册层, 不进 ui 纯逻辑).
 */
#include "hk_ui.h"

#include <string.h>

#include "ui_theme.h"

#define HK_PAYLOADS_DIR "/sdcard/payloads"
#define HK_UI_MAX_PAYLOADS 32

static const hk_fs_ops_t *s_fs;
static const hk_usb_ops_t *s_usb;
static hk_delay_fn_t s_delay_fn;
static hk_ui_exec_fn_t s_exec_fn;

static lv_obj_t *s_list;
static lv_obj_t *s_status_label;
static char s_selected[HK_PAYLOAD_NAME_MAX];

void hk_ui_set_executor(hk_ui_exec_fn_t fn)
{
    s_exec_fn = fn;
}

void hk_ui_set_status(hk_ui_status_t status)
{
    const char *text = "Idle";
    switch (status) {
    case HK_UI_STATUS_IDLE:    text = "Idle"; break;
    case HK_UI_STATUS_RUNNING: text = "Running..."; break;
    case HK_UI_STATUS_DONE:    text = "Done"; break;
    case HK_UI_STATUS_FAILED:  text = "Failed"; break;
    case HK_UI_STATUS_BUSY:    text = "USB busy (FIDO2)"; break;
    }
    if (s_status_label) {
        lv_label_set_text(s_status_label, text);
    }
}

static void exec_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!s_exec_fn || s_selected[0] == '\0') {
        return;
    }
    hk_ui_set_status(HK_UI_STATUS_RUNNING);
    int rc = s_exec_fn(s_selected);
    hk_ui_set_status(rc == 0 ? HK_UI_STATUS_DONE : HK_UI_STATUS_FAILED);
}

static void list_item_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *name = lv_list_get_button_text(s_list, btn);
    if (name) {
        strncpy(s_selected, name, sizeof(s_selected) - 1);
        s_selected[sizeof(s_selected) - 1] = '\0';
    }
}

void hk_ui_create(lv_obj_t *parent, const hk_fs_ops_t *fs,
                  const hk_usb_ops_t *usb, hk_delay_fn_t delay_fn)
{
    s_fs = fs;
    s_usb = usb;
    s_delay_fn = delay_fn;
    s_selected[0] = '\0';

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_theme_page(parent);

    lv_obj_t *title = ui_theme_title(parent, "HackerKit (BadUSB)", UI_ACCENT_HK);

    /* payload 列表 (卡片化) */
    s_list = lv_list_create(parent);
    lv_obj_set_size(s_list, 300, 240);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_radius(s_list, UI_RADIUS, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);

    char names[HK_UI_MAX_PAYLOADS][HK_PAYLOAD_NAME_MAX];
    int n = 0;
    if (fs && fs->list_payloads) {
        n = fs->list_payloads(HK_PAYLOADS_DIR, names, HK_UI_MAX_PAYLOADS);
    }
    if (n <= 0) {
        lv_obj_t *item = lv_list_add_button(s_list, LV_SYMBOL_FILE, "(no payloads)");
        lv_obj_add_state(item, LV_STATE_DISABLED);
    } else {
        for (int i = 0; i < n; i++) {
            lv_obj_t *item = lv_list_add_button(s_list, LV_SYMBOL_FILE, names[i]);
            lv_obj_add_event_cb(item, list_item_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    /* 执行按钮 (强调色圆角) */
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_ACCENT_HK), 0);
    lv_obj_set_style_radius(btn, UI_RADIUS, 0);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Run");
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(btn, exec_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 状态显示 (执行中/完成/失败/USB 占用) */
    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "Idle");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(UI_TEXT_DIM), 0);
}

/* s_usb / s_delay_fn 供后续扩展 (如内联单步执行) 使用, 避免未用告警 */
void hk_ui_keep_refs(void)
{
    (void)s_usb;
    (void)s_delay_fn;
}

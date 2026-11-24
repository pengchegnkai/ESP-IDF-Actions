/* adb_ui.c — ADB 工具命令行界面 ([纯逻辑·可仿真])
 *
 * 标准 LVGL flex 列布局 (参考 LVGL 官方 flex 示例), 深色主题沿用 app_shared/ui_theme.h.
 * widget 指针为文件级静态, 由 adb_ui_create 重建, APP run()/close() 生命周期内有效.
 */
#include "adb_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "ui_theme.h"

/* ADB 工具强调色: 复用 SecurityKey 青 */
#define UI_ACCENT_ADB   UI_ACCENT_SK

#define ADB_UI_OUT_MAX     8192   /* 输出区文本上限 (防长会话内存膨胀) */
#define ADB_UI_INPUT_MAX   256

static lv_obj_t *_status = NULL;      /* 状态行 label */
static lv_obj_t *_output = NULL;      /* 输出滚动区 (lv_textarea 只读) */
static lv_obj_t *_input = NULL;       /* 单行输入 */
static char _input_buf[ADB_UI_INPUT_MAX];

static adb_ui_action_cb_t _connect_cb = NULL;
static adb_ui_action_cb_t _disconnect_cb = NULL;
static void *_action_user = NULL;

/* ---------------- 回调 ---------------- */
static void send_event_cb(lv_event_t *e)
{
    (void)e;
    /* 发送动作由 APP 层轮询 get_input() 处理 (UI 层不感知 adb core) */
}

static void connect_event_cb(lv_event_t *e)
{
    (void)e;
    if (_connect_cb) _connect_cb(_action_user);
}

static void disconnect_event_cb(lv_event_t *e)
{
    (void)e;
    if (_disconnect_cb) _disconnect_cb(_action_user);
}

/* ---------------- 对外 ---------------- */
void adb_ui_set_actions(adb_ui_action_cb_t connect, adb_ui_action_cb_t disconnect, void *user)
{
    _connect_cb = connect;
    _disconnect_cb = disconnect;
    _action_user = user;
}

void adb_ui_create(lv_obj_t *parent)
{
    _status = NULL;
    _output = NULL;
    _input = NULL;

    ui_theme_page(parent);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, UI_PAD, 0);

    /* 标题 */
    lv_obj_t *title = ui_theme_title(parent, "ADB 工具", UI_ACCENT_ADB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    /* 状态行 */
    _status = lv_label_create(parent);
    lv_label_set_text(_status, "未连接");
    lv_obj_set_style_text_color(_status, lv_color_hex(UI_TEXT_DIM), 0);

    /* 连接/断开 按钮行 */
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_PAD, 0);

    lv_obj_t *btn_conn = lv_button_create(row);
    lv_obj_set_size(btn_conn, 120, 40);
    lv_obj_set_style_bg_color(btn_conn, lv_color_hex(UI_ACCENT_ADB), 0);
    lv_obj_add_event_cb(btn_conn, connect_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_conn = lv_label_create(btn_conn);
    lv_label_set_text(lbl_conn, "连接手机");

    lv_obj_t *btn_dis = lv_button_create(row);
    lv_obj_set_size(btn_dis, 120, 40);
    lv_obj_set_style_bg_color(btn_dis, lv_color_hex(UI_DANGER), 0);
    lv_obj_add_event_cb(btn_dis, disconnect_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_dis = lv_label_create(btn_dis);
    lv_label_set_text(lbl_dis, "断开");

    /* 输出滚动区 (只读 textarea, 等宽风格) */
    _output = lv_textarea_create(parent);
    lv_textarea_set_one_line(_output, false);
    lv_textarea_set_text(_output, "");
    lv_obj_set_width(_output, lv_pct(100));
    lv_obj_set_flex_grow(_output, 1);           /* flex 列中吃满剩余高度 */
    lv_obj_set_style_bg_color(_output, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_text_color(_output, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_border_width(_output, 0, 0);
    lv_obj_set_style_radius(_output, UI_RADIUS, 0);
    lv_obj_clear_flag(_output, LV_OBJ_FLAG_SCROLL_ELASTIC);

    /* 底部输入行: 单行输入 + 发送按钮 */
    lv_obj_t *in_row = lv_obj_create(parent);
    lv_obj_set_size(in_row, lv_pct(100), 48);
    lv_obj_clear_flag(in_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(in_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(in_row, 0, 0);
    lv_obj_set_style_pad_all(in_row, 0, 0);
    lv_obj_set_flex_flow(in_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(in_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(in_row, UI_PAD, 0);

    _input = lv_textarea_create(in_row);
    lv_textarea_set_one_line(_input, true);
    lv_textarea_set_placeholder_text(_input, "输入 shell 命令...");
    lv_obj_set_flex_grow(_input, 1);
    lv_obj_set_style_bg_color(_input, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_text_color(_input, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_border_width(_input, 0, 0);
    lv_obj_set_style_radius(_input, UI_RADIUS, 0);

    lv_obj_t *btn_send = lv_button_create(in_row);
    lv_obj_set_size(btn_send, 88, 40);
    lv_obj_set_style_bg_color(btn_send, lv_color_hex(UI_ACCENT_ADB), 0);
    lv_obj_add_event_cb(btn_send, send_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_send = lv_label_create(btn_send);
    lv_label_set_text(lbl_send, "发送");
}

void adb_ui_set_status(const char *s)
{
    if (_status && s) lv_label_set_text(_status, s);
}

void adb_ui_append_output(const char *line)
{
    if (!_output || !line) return;

    /* 追加一行; 超长则截断头部 (保留尾部 ADB_UI_OUT_MAX-64 字节) */
    const char *cur = lv_textarea_get_text(_output);
    size_t clen = cur ? strlen(cur) : 0;
    size_t add = strlen(line) + 1;   /* + '\n' */
    if (clen + add > ADB_UI_OUT_MAX) {
        const char *tail = cur + (clen + add - ADB_UI_OUT_MAX);
        if (tail > cur) {
            /* 跳到下一行, 避免半行截断 */
            const char *nl = strchr(tail, '\n');
            if (nl) tail = nl + 1;
        }
        lv_textarea_set_text(_output, tail);
    }
    lv_textarea_add_text(_output, line);
    lv_textarea_add_char(_output, '\n');
    /* 滚到底部 */
    lv_obj_scroll_to_y(_output, LV_COORD_MAX, LV_ANIM_OFF);
}

const char *adb_ui_get_input(void)
{
    if (!_input) return "";
    const char *t = lv_textarea_get_text(_input);
    snprintf(_input_buf, sizeof(_input_buf), "%s", t ? t : "");
    return _input_buf;
}

void adb_ui_clear_input(void)
{
    if (_input) lv_textarea_set_text(_input, "");
}

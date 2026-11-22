/* fm_ui.c — 文件管理 APP 纯 LVGL UI
 * [纯逻辑·可仿真] ui 层: 只允许 lvgl.h + core/ 头, 文件系统全走 fm_backend_t.
 * 借鉴 (参考 espressif/esp-ui 文件管理器的目录浏览/分页预览/编辑对话框思路).
 *
 * 结构:
 *   浏览页  — 标题 (当前路径) + [New] + lv_list (.. / 目录 / .txt 文件)
 *   预览页  — 文本分页 label + [上一页][下一页][关闭]
 *   编辑页  — lv_textarea + [保存]
 * 真机上调用方 (APP 注册层/hal) 须已持 LVGL 锁 (见 pc_sim/CONVENTIONS.md).
 */
#include "fm_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "fm_payload.h"
#include "fm_text.h"

#define FM_ROOT_DIR     "/sdcard"
#define FM_PAGE_CHARS   1024        /* 每页字符数 */
#define FM_READ_BUF_MAX (32 * 1024) /* 预览/编辑最大读入 */
#define FM_PATH_MAX     512

/* UI 状态 (单实例) */
typedef struct {
    const fm_backend_t *be;
    char cwd[FM_PATH_MAX];

    lv_obj_t *root;          /* parent 下的容器 */
    lv_obj_t *browser;       /* 浏览页 */
    lv_obj_t *list;          /* 目录列表 */
    lv_obj_t *title;         /* 当前路径 label */

    lv_obj_t *preview;       /* 预览页 */
    lv_obj_t *preview_label;
    long     preview_page;

    lv_obj_t *editor;        /* 编辑页 */
    lv_obj_t *textarea;
    char     edit_name[FM_PAYLOAD_NAME_MAX]; /* 空串 = 新建 */

    char     *tx;            /* 预览/编辑载入的文件内容 */
    long     tx_len;
    long     tx_pos;         /* fm_text 读取回调游标 */
} fm_ui_t;

static fm_ui_t s_ui;

/* list 事件回调用的条目表 (槽位索引作为 user_data) */
typedef struct {
    char name[FM_NAME_MAX];
    bool is_dir;
} fm_row_t;

#define FM_ROWS_MAX 32
static fm_row_t s_rows[FM_ROWS_MAX];

/* ---------------- 工具 ---------------- */

static void join_path(char *dst, size_t size, const char *dir, const char *name)
{
    snprintf(dst, size, "%s/%s", dir, name);
}

static void tx_free(void)
{
    free(s_ui.tx);
    s_ui.tx = NULL;
    s_ui.tx_len = 0;
    s_ui.tx_pos = 0;
}

static int tx_load(const char *path)
{
    const fm_backend_t *be = s_ui.be;
    long size;

    tx_free();
    if (be == NULL || be->file_size == NULL || be->read_file == NULL) {
        return FM_ERR;
    }
    size = be->file_size(be->ctx, path);
    if (size < 0) {
        return FM_ERR;
    }
    if (size > FM_READ_BUF_MAX - 1) {
        size = FM_READ_BUF_MAX - 1;
    }
    s_ui.tx = (char *)malloc((size_t)size + 1);
    if (s_ui.tx == NULL) {
        return FM_ERR_NOMEM;
    }
    s_ui.tx_len = be->read_file(be->ctx, path, s_ui.tx, size);
    if (s_ui.tx_len < 0) {
        s_ui.tx_len = 0;
    }
    s_ui.tx[s_ui.tx_len] = '\0';
    return FM_OK;
}

static void show_only(lv_obj_t *page)
{
    lv_obj_add_flag(s_ui.browser, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.preview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.editor, LV_OBJ_FLAG_HIDDEN);
    if (page != NULL) {
        lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------- 预览页 ---------------- */

/* fm_text 读取回调: 从已载入的 s_ui.tx 顺序取块 */
static long tx_read_cb(void *user, char *buf, long buf_size)
{
    long *pos = (long *)user;
    long remain = s_ui.tx_len - *pos;

    if (remain <= 0) {
        return 0;
    }
    if (buf_size > remain) {
        buf_size = remain;
    }
    memcpy(buf, s_ui.tx + *pos, (size_t)buf_size);
    *pos += buf_size;
    return buf_size;
}

static void preview_refresh(void)
{
    char *page_buf = (char *)malloc(FM_PAGE_CHARS + 1);
    long pages;

    if (page_buf == NULL) {
        return;
    }
    s_ui.tx_pos = 0;
    pages = fm_text_extract(tx_read_cb, &s_ui.tx_pos, FM_PAGE_CHARS,
                            s_ui.preview_page, page_buf, FM_PAGE_CHARS + 1);
    if (pages <= 0) {
        snprintf(page_buf, FM_PAGE_CHARS + 1, "(empty)");
        s_ui.preview_page = 0;
    } else if (s_ui.preview_page >= pages) {
        /* 越界: 回到最后一页再取一次 */
        s_ui.preview_page = pages - 1;
        s_ui.tx_pos = 0;
        fm_text_extract(tx_read_cb, &s_ui.tx_pos, FM_PAGE_CHARS,
                        s_ui.preview_page, page_buf, FM_PAGE_CHARS + 1);
    }
    lv_label_set_text_fmt(s_ui.preview_label, "%s\n\n[%ld/%ld]",
                          page_buf, s_ui.preview_page + 1, pages);
    free(page_buf);
}

static void preview_close_cb(lv_event_t *e)
{
    (void)e;
    tx_free();
    show_only(s_ui.browser);
}

static void preview_prev_cb(lv_event_t *e)
{
    (void)e;
    if (s_ui.preview_page > 0) {
        s_ui.preview_page--;
        preview_refresh();
    }
}

static void preview_next_cb(lv_event_t *e)
{
    (void)e;
    s_ui.preview_page++;
    preview_refresh(); /* 越界时自动回退到最后一页 */
}

static void preview_open(const char *path)
{
    if (tx_load(path) != FM_OK) {
        lv_label_set_text(s_ui.title, "read failed");
        return;
    }
    s_ui.preview_page = 0;
    show_only(s_ui.preview);
    preview_refresh();
}

/* ---------------- 浏览页 ---------------- */

static void browser_open(const char *dir);

static void browser_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const fm_backend_t *be = s_ui.be;
    char path[FM_PATH_MAX];
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

    if (be == NULL) {
        return;
    }
    if (idx < 0) { /* [New] 按钮 → 打开编辑器 (新建) */
        s_ui.edit_name[0] = '\0';
        lv_textarea_set_text(s_ui.textarea, "");
        show_only(s_ui.editor);
        return;
    }
    if (idx >= FM_ROWS_MAX) {
        return;
    }
    if (strcmp(s_rows[idx].name, "..") == 0) { /* 返回上级 */
        char parent[FM_PATH_MAX];
        char *slash;
        snprintf(parent, sizeof(parent), "%s", s_ui.cwd);
        slash = strrchr(parent, '/');
        if (slash != NULL && slash != parent) {
            *slash = '\0';
        } else {
            snprintf(parent, sizeof(parent), "%s", FM_ROOT_DIR);
        }
        browser_open(parent);
        return;
    }
    join_path(path, sizeof(path), s_ui.cwd, s_rows[idx].name);
    if (s_rows[idx].is_dir) {
        browser_open(path);
    } else {
        preview_open(path);
    }
}

static void new_btn_cb(lv_event_t *e)
{
    (void)e;
    s_ui.edit_name[0] = '\0';
    lv_textarea_set_text(s_ui.textarea, "");
    show_only(s_ui.editor);
}

static void list_add_row(const fm_entry_t *entry, int slot, const char *dir)
{
    const fm_backend_t *be = s_ui.be;
    lv_obj_t *btn;

    btn = lv_list_add_button(s_ui.list,
                             entry->is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
                             entry->name);
    lv_obj_add_event_cb(btn, browser_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)slot);
    if (!entry->is_dir) {
        lv_obj_t *lbl = lv_obj_get_child(btn, 1);
        char path[FM_PATH_MAX];
        long size;
        join_path(path, sizeof(path), dir, entry->name);
        size = (be != NULL && be->file_size != NULL)
               ? be->file_size(be->ctx, path) : 0;
        lv_label_set_text_fmt(lbl, "%s (%ld B)", entry->name, size);
    }
}

static void browser_open(const char *dir)
{
    const fm_backend_t *be = s_ui.be;
    fm_entry_t entries[FM_ROWS_MAX];
    int n, i, j, slot = 0;

    if (be == NULL || be->list_dir == NULL) {
        return;
    }
    n = be->list_dir(be->ctx, dir, entries, FM_ROWS_MAX);
    if (n < 0) {
        lv_label_set_text_fmt(s_ui.title, "%s (list failed)", dir);
        return;
    }
    /* 排序: 目录在前, 名称升序 (冒泡, 条目少) */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n - 1 - i; j++) {
            bool swap;
            if (entries[j].is_dir != entries[j + 1].is_dir) {
                swap = !entries[j].is_dir; /* 目录在前 */
            } else {
                swap = strcmp(entries[j].name, entries[j + 1].name) > 0;
            }
            if (swap) {
                fm_entry_t tmp = entries[j];
                entries[j] = entries[j + 1];
                entries[j + 1] = tmp;
            }
        }
    }
    snprintf(s_ui.cwd, sizeof(s_ui.cwd), "%s", dir);
    lv_label_set_text(s_ui.title, s_ui.cwd);
    lv_obj_clean(s_ui.list);
    memset(s_rows, 0, sizeof(s_rows));

    if (strcmp(s_ui.cwd, FM_ROOT_DIR) != 0) { /* 返回上级 ".." 占槽 0 */
        lv_obj_t *btn = lv_list_add_button(s_ui.list, LV_SYMBOL_UP, "..");
        lv_obj_add_event_cb(btn, browser_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
        snprintf(s_rows[0].name, sizeof(s_rows[0].name), "..");
        s_rows[0].is_dir = true;
        slot = 1;
    }
    for (i = 0; i < n && slot < FM_ROWS_MAX; i++, slot++) {
        snprintf(s_rows[slot].name, sizeof(s_rows[slot].name), "%s", entries[i].name);
        s_rows[slot].is_dir = entries[i].is_dir;
        list_add_row(&entries[i], slot, dir);
    }
    show_only(s_ui.browser);
}

/* ---------------- 编辑页 ---------------- */

static void editor_save_cb(lv_event_t *e)
{
    (void)e;
    {
        const char *content = lv_textarea_get_text(s_ui.textarea);
        long len = (long)strlen(content);
        int ret;

        if (s_ui.edit_name[0] == '\0') {
            /* 新建: 自动命名 payload_N.txt (取第一个不存在的编号) */
            int i;
            for (i = 1; i < 999; i++) {
                char cand[FM_PAYLOAD_NAME_MAX];
                char path[FM_PATH_MAX];
                snprintf(cand, sizeof(cand), "payload_%d.txt", i);
                join_path(path, sizeof(path), FM_PAYLOAD_DIR, cand);
                if (s_ui.be->file_exists(s_ui.be->ctx, path) != FM_OK) {
                    snprintf(s_ui.edit_name, sizeof(s_ui.edit_name), "%s", cand);
                    break;
                }
            }
        }
        ret = fm_payload_write(s_ui.be, s_ui.edit_name, content, len);
        if (ret == FM_OK) {
            browser_open(FM_PAYLOAD_DIR); /* 保存后回到 payloads 目录刷新 */
            return;
        }
        lv_label_set_text(s_ui.title, "save failed");
    }
}

/* ---------------- 创建 ---------------- */

static void build_pages(lv_obj_t *parent)
{
    lv_obj_t *row, *btn, *lbl, *nav;

    /* 浏览页 */
    s_ui.browser = lv_obj_create(parent);
    lv_obj_set_size(s_ui.browser, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_ui.browser, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_ui.browser, 4, 0);

    row = lv_obj_create(s_ui.browser);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    s_ui.title = lv_label_create(row);
    lv_obj_set_flex_grow(s_ui.title, 1);
    lv_label_set_text(s_ui.title, FM_ROOT_DIR);
    btn = lv_button_create(row);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_PLUS " New");
    lv_obj_add_event_cb(btn, new_btn_cb, LV_EVENT_CLICKED, NULL);

    s_ui.list = lv_list_create(s_ui.browser);
    lv_obj_set_size(s_ui.list, lv_pct(100), lv_pct(100));

    /* 预览页 */
    s_ui.preview = lv_obj_create(parent);
    lv_obj_set_size(s_ui.preview, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_ui.preview, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_ui.preview, LV_OBJ_FLAG_HIDDEN);
    s_ui.preview_label = lv_label_create(s_ui.preview);
    lv_obj_set_size(s_ui.preview_label, lv_pct(100), lv_pct(100));
    lv_label_set_long_mode(s_ui.preview_label, LV_LABEL_LONG_WRAP);

    nav = lv_obj_create(s_ui.preview);
    lv_obj_set_size(nav, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    {
        lv_obj_t *prev = lv_button_create(nav);
        lv_obj_t *next = lv_button_create(nav);
        lv_obj_t *close = lv_button_create(nav);
        lbl = lv_label_create(prev);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT);
        lbl = lv_label_create(next);
        lv_label_set_text(lbl, LV_SYMBOL_RIGHT);
        lbl = lv_label_create(close);
        lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
        lv_obj_add_event_cb(prev, preview_prev_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(next, preview_next_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(close, preview_close_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 编辑页 */
    s_ui.editor = lv_obj_create(parent);
    lv_obj_set_size(s_ui.editor, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_ui.editor, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_ui.editor, LV_OBJ_FLAG_HIDDEN);
    s_ui.textarea = lv_textarea_create(s_ui.editor);
    lv_obj_set_size(s_ui.textarea, lv_pct(100), lv_pct(100));
    lv_textarea_set_one_line(s_ui.textarea, false);
    btn = lv_button_create(s_ui.editor);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Save");
    lv_obj_add_event_cb(btn, editor_save_cb, LV_EVENT_CLICKED, NULL);
}

void fm_ui_create(lv_obj_t *parent, const fm_backend_t *be)
{
    memset(&s_ui, 0, sizeof(s_ui));
    memset(s_rows, 0, sizeof(s_rows));
    s_ui.be = be;
    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, lv_pct(100), lv_pct(100));
    build_pages(s_ui.root);
    browser_open(FM_ROOT_DIR);
}

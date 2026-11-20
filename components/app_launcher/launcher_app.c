/**
 * @file launcher_app.c
 * @brief 应用列表 (Vela 应用抽屉) —— 卡片网格
 *
 * Vela 交互逻辑:
 *   - 下滑 (全屏空白区)  → 表盘 (主页)
 *   - 顶部边缘下拉        → 控制中心 (sys_ui_quickpanel)
 *   - 点卡片             → 启动对应 APP
 */
#include <string.h>
#include <stdlib.h>
#include "app_launcher/launcher_app.h"
#include "app_settings/app_settings.h"
#include "app_net/app_net.h"
#include "app_watch/watch_app.h"
#include "app_splash/splash_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_ui/sys_ui.h"
#include "sys_ui/sys_ui_theme.h"
#include "sys_ui/sys_ui_gesture.h"
#include "sys_ui/sys_ui_quickpanel.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "launcher";

static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *grid;
    bool ui_built;
} launcher_priv_t;

/* ---------- 弹窗演示 (系统基座能力展示, 保留) ---------- */
static void overlay_ok_cb(lv_event_t *e)
{
    (void)e;
    sys_ui_overlay_dismiss();
}

static void show_demo_overlay(void)
{
    if (sys_ui_overlay_active()) {
        return;
    }
    lv_obj_t *content = lv_obj_create(lv_layer_top());
    lv_obj_set_size(content, 320, 180);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(content, SYS_UI_THEME_RADIUS, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(SYS_UI_THEME_CARD), 0);
    lv_font_t *f = sys_ui_cjk_font(SYS_UI_FONT_DEFAULT_PX);
    if (f) {
        lv_obj_set_style_text_font(content, f, LV_STATE_DEFAULT);
    }

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, sys_ui_str(SYS_UI_STR_OVERLAY_TITLE));
    lv_obj_set_style_text_color(title, lv_color_hex(SYS_UI_THEME_ERR), 0);

    lv_obj_t *body = lv_label_create(content);
    lv_label_set_text(body, sys_ui_str(SYS_UI_STR_OVERLAY_BODY));

    lv_obj_t *btn = lv_button_create(content);
    lv_obj_add_event_cb(btn, overlay_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, sys_ui_str(SYS_UI_STR_OVERLAY_OK));
    lv_obj_center(btn_label);

    esp_err_t err = sys_ui_overlay_show(content, NULL);
    if (err != ESP_OK) {
        lv_obj_delete(content);
    }
}

static void demo_btn_cb(lv_event_t *e)
{
    (void)e;
    show_demo_overlay();
}

/* ---------- Vela 手势 ---------- */

static void qc_open_cb(void *ctx)
{
    (void)ctx;
    if (!sys_ui_quickpanel_active()) {
        sys_ui_quickpanel_show();
    }
}

static void on_swipe(sys_ui_swipe_dir_t dir, void *ctx)
{
    (void)ctx;
    if (dir == SYS_UI_SWIPE_DOWN) {
        sys_app_launch(WATCH_APP_ID);  /* 下滑 → 表盘 */
    }
}

/* ---------- APP 卡片 ---------- */

static void app_btn_cb(lv_event_t *e)
{
    const char *app_id = lv_event_get_user_data(e);
    if (app_id) {
        sys_app_launch(app_id);
    }
}

/* APP 显示名: 已知 APP 走 i18n, 未知 APP 回退 name 字段 */
static const char *app_display_name(const sys_app_t *app)
{
    if (strcmp(app->id, LAUNCHER_APP_ID) == 0) {
        return sys_ui_str(SYS_UI_STR_APP_LAUNCHER);
    }
    if (strcmp(app->id, SETTINGS_APP_ID) == 0) {
        return sys_ui_str(SYS_UI_STR_APP_SETTINGS);
    }
    if (strcmp(app->id, NET_APP_ID) == 0) {
        return sys_ui_str(SYS_UI_STR_APP_NET);
    }
    if (strcmp(app->id, WATCH_APP_ID) == 0) {
        return app->name ? app->name : app->id;
    }
    return app->name ? app->name : app->id;
}

/* 图标底色: 按 id 哈希取色 (Vela 风格多彩圆标) */
static uint32_t app_icon_color(const char *id)
{
    static const uint32_t palette[] = {
        0x4C8DFF, 0x34C759, 0xFF9F0A, 0xBF5AF2,
        0x35C4F5, 0xFF453A, 0xFFD60A, 0x64D2FF,
    };
    uint32_t h = 0;
    for (const char *s = id; *s; s++) {
        h = h * 31 + (uint8_t)*s;
    }
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

static void build_grid(launcher_priv_t *p)
{
    for (size_t i = 0; i < sys_app_count(); i++) {
        sys_app_t *app = sys_app_get(i);
        /* 桌面/表盘/启动画面不进网格 */
        if (!app || strcmp(app->id, LAUNCHER_APP_ID) == 0 ||
            strcmp(app->id, WATCH_APP_ID) == 0 ||
            strcmp(app->id, SPLASH_APP_ID) == 0) {
            continue;
        }
        lv_obj_t *card = lv_obj_create(p->grid);
        lv_obj_set_size(card, 212, 150);
        sys_ui_theme_card(card);
        lv_obj_add_event_cb(card, app_btn_cb, LV_EVENT_CLICKED, (void *)app->id);
        lv_obj_add_state(card, LV_STATE_DEFAULT);

        /* 图标圆标 (APP 名首字符) */
        lv_obj_t *icon = lv_obj_create(card);
        lv_obj_set_size(icon, 64, 64);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_bg_color(icon, lv_color_hex(app_icon_color(app->id)), 0);
        lv_obj_set_style_radius(icon, 32, 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon_lbl = lv_label_create(icon);
        lv_label_set_text_fmt(icon_lbl, "%c",
                              (int)(app->name && app->name[0] ? app->name[0]
                                                              : app->id[0]));
        lv_obj_set_style_text_font(icon_lbl, sys_ui_theme_font(30), 0);
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_center(icon_lbl);

        lv_obj_t *name_lbl = lv_label_create(card);
        lv_label_set_text(name_lbl, app_display_name(app));
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
        lv_obj_set_style_text_font(name_lbl, sys_ui_theme_font(24), 0);
        lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, -18);
    }

    /* 系统弹窗演示入口 */
    lv_obj_t *demo = lv_obj_create(p->grid);
    lv_obj_set_size(demo, 212, 150);
    sys_ui_theme_card(demo);
    lv_obj_add_event_cb(demo, demo_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *demo_icon = lv_obj_create(demo);
    lv_obj_set_size(demo_icon, 64, 64);
    lv_obj_align(demo_icon, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(demo_icon, lv_color_hex(0xFFD60A), 0);
    lv_obj_set_style_radius(demo_icon, 32, 0);
    lv_obj_set_style_border_width(demo_icon, 0, 0);
    lv_obj_clear_flag(demo_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *demo_icon_lbl = lv_label_create(demo_icon);
    lv_label_set_text(demo_icon_lbl, "i");
    lv_obj_set_style_text_font(demo_icon_lbl, sys_ui_theme_font(30), 0);
    lv_obj_set_style_text_color(demo_icon_lbl, lv_color_hex(0x1B2129), 0);
    lv_obj_center(demo_icon_lbl);

    lv_obj_t *demo_lbl = lv_label_create(demo);
    lv_label_set_text(demo_lbl, sys_ui_str(SYS_UI_STR_BTN_DEMO));
    lv_obj_set_style_text_color(demo_lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_set_style_text_font(demo_lbl, sys_ui_theme_font(24), 0);
    lv_obj_align(demo_lbl, LV_ALIGN_BOTTOM_MID, 0, -18);
}

/* ---------- UI 构建 ---------- */

static esp_err_t build_ui(sys_app_t *app)
{
    launcher_priv_t *p = app->priv;
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }
    p->screen = lv_obj_create(NULL);
    sys_ui_theme_screen(p->screen);
    lv_obj_clear_flag(p->screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 手势层: 第一个子对象 (垫底) */
    sys_ui_swipe_layer_create(p->screen, on_swipe, app);
    sys_ui_top_edge_create(p->screen, qc_open_cb, app);

    /* 应用卡片网格 (2 列, 自动换行) */
    p->grid = lv_obj_create(p->screen);
    lv_obj_set_size(p->grid, LV_PCT(100), LV_PCT(100));
    lv_obj_align(p->grid, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_flex_flow(p->grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(p->grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(p->grid, 20, 0);
    lv_obj_set_style_pad_column(p->grid, 20, 0);
    lv_obj_set_style_bg_opa(p->grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p->grid, 0, 0);
    lv_obj_set_style_pad_all(p->grid, 16, 0);

    sys_ui_unlock();
    ESP_LOGI(TAG, "launcher (app drawer) ui built");
    return ESP_OK;
}

/* ---------- 生命周期 ---------- */

static esp_err_t on_init(sys_app_t *app)
{
    launcher_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    launcher_priv_t *p = app->priv;
    if (!p->ui_built) {
        if (build_ui(app) != ESP_OK) {
            return ESP_FAIL;
        }
        p->ui_built = true;
    }
    if (!sys_ui_lock()) {
        ESP_LOGW(TAG, "on_start: no display lock, grid refresh skipped");
        return ESP_OK;
    }
    lv_obj_clean(p->grid);   /* 每次进前台刷新 APP 列表 (可能新增/注销) */
    build_grid(p);
    sys_ui_set_root(p->screen);
    sys_ui_unlock();
    return ESP_OK;
}

static void on_suspend(sys_app_t *app)
{
    (void)app;
}

static void on_stop(sys_app_t *app)
{
    launcher_priv_t *p = app->priv;
    if (!p) {
        return;
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

static sys_app_t s_launcher = {
    .id         = LAUNCHER_APP_ID,
    .name       = "Apps",
    .icon       = 0,
    .on_init    = on_init,
    .on_start   = on_start,
    .on_suspend = on_suspend,
    .on_stop    = on_stop,
    .on_event   = NULL,
};

sys_app_t *launcher_app_get(void)
{
    return &s_launcher;
}

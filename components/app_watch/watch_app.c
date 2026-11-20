/**
 * @file watch_app.c
 * @brief 表盘 APP (Vela 风格主屏/主页)
 *
 * Vela 交互逻辑:
 *   - 上滑 (全屏空白区)  → 应用列表 (launcher)
 *   - 顶部边缘下拉        → 控制中心 (sys_ui_quickpanel)
 *
 * 内容: 大时钟 HH:MM + 秒环 + 秒数 + 日期(周X M/D)。
 * 已校时 (NTP) 显示真实时间; 未校时显示运行时长并加提示。
 */
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "app_watch/watch_app.h"
#include "app_launcher/launcher_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "sys_core/sys_time_service.h"
#include "sys_net/sys_net.h"
#include "sys_ui/sys_ui.h"
#include "sys_ui/sys_ui_theme.h"
#include "sys_ui/sys_ui_gesture.h"
#include "sys_ui/sys_ui_quickpanel.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "watch";

static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *time_lbl;   /* 大时钟 */
    lv_obj_t *date_lbl;   /* 周X M/D */
    lv_obj_t *sec_lbl;    /* 秒数 */
    lv_obj_t *sec_arc;    /* 秒环 */
    lv_obj_t *nosync_lbl; /* 未校时提示 */
    bool ui_built;
    sys_event_handle_t tick_sub;
} watch_priv_t;

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
    if (dir == SYS_UI_SWIPE_UP) {
        sys_app_launch(LAUNCHER_APP_ID);  /* 上滑 → 应用列表 */
    }
}

/* ---------- 时钟刷新 (1Hz) ---------- */

static const char *weekday_str(int tm_wday)
{
    /* tm_wday: 0=周日 .. 6=周六; i18n 表按 周一..周日 排列 */
    static const sys_ui_str_id_t map[7] = {
        SYS_UI_STR_WD_SUN, SYS_UI_STR_WD_MON, SYS_UI_STR_WD_TUE,
        SYS_UI_STR_WD_WED, SYS_UI_STR_WD_THU, SYS_UI_STR_WD_FRI,
        SYS_UI_STR_WD_SAT,
    };
    return sys_ui_str(map[tm_wday]);
}

static void update_clock(watch_priv_t *p)
{
    if (!sys_ui_lock()) {
        return;
    }
    bool synced = sys_net_time_is_synced();
    if (synced) {
        time_t t = (time_t)sys_net_time_epoch();
        struct tm tm;
        localtime_r(&t, &tm);
        lv_label_set_text_fmt(p->time_lbl, "%02d:%02d", tm.tm_hour, tm.tm_min);
        lv_label_set_text_fmt(p->date_lbl, "%s %d/%02d",
                              weekday_str(tm.tm_wday), tm.tm_mon + 1, tm.tm_mday);
        lv_label_set_text_fmt(p->sec_lbl, "%02d", tm.tm_sec);
        lv_arc_set_value(p->sec_arc, (uint16_t)tm.tm_sec);
    } else {
        uint32_t sec = sys_time_uptime_sec();
        lv_label_set_text_fmt(p->time_lbl, "%02lu:%02lu:%02lu",
                              (unsigned long)(sec / 3600),
                              (unsigned long)(sec / 60 % 60),
                              (unsigned long)(sec % 60));
        lv_label_set_text(p->date_lbl, "");
        lv_label_set_text_fmt(p->sec_lbl, "%02lu",
                              (unsigned long)(sec % 60));
        lv_arc_set_value(p->sec_arc, (uint16_t)(sec % 60));
    }
    if (synced) {
        lv_obj_add_flag(p->nosync_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(p->nosync_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    sys_ui_unlock();
}

static void on_time_tick(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id; (void)event_data;
    sys_app_t *self = user_ctx;
    if (self->state != SYS_APP_STATE_ACTIVE || !self->priv) {
        return;
    }
    /* 顺带推进 NTP 同步状态事件 (幂等, 成功后只发一次) */
    sys_net_time_check_and_report();
    update_clock(self->priv);
}

/* ---------- UI 构建 ---------- */

static esp_err_t build_ui(sys_app_t *app)
{
    watch_priv_t *p = app->priv;
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }
    p->screen = lv_obj_create(NULL);
    sys_ui_theme_screen(p->screen);
    lv_obj_clear_flag(p->screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 手势层必须是第一个子对象 (垫底, 只捕获空白区滑动) */
    sys_ui_swipe_layer_create(p->screen, on_swipe, app);

    /* 顶部边缘下拉 → 控制中心 */
    sys_ui_top_edge_create(p->screen, qc_open_cb, app);

    /* 中央时钟组: 大时间 + 右侧秒环, 下方日期 + 未校时提示 */
    lv_obj_t *clock_box = lv_obj_create(p->screen);
    lv_obj_set_size(clock_box, 440, 220);
    lv_obj_align(clock_box, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_opa(clock_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_box, 0, 0);
    lv_obj_clear_flag(clock_box, LV_OBJ_FLAG_SCROLLABLE);

    p->time_lbl = lv_label_create(clock_box);
    lv_label_set_text(p->time_lbl, "--:--");
    lv_obj_set_style_text_font(p->time_lbl, sys_ui_theme_font(96), 0);
    lv_obj_set_style_text_color(p->time_lbl, lv_color_hex(SYS_UI_THEME_TEXT), 0);
    lv_obj_align(p->time_lbl, LV_ALIGN_LEFT_MID, 0, -10);

    /* 秒环 + 秒数 (Vela 表盘标志性元素) */
    p->sec_arc = lv_arc_create(clock_box);
    lv_obj_set_size(p->sec_arc, 96, 96);
    lv_obj_align(p->sec_arc, LV_ALIGN_RIGHT_MID, -8, -18);
    lv_arc_set_range(p->sec_arc, 0, 59);
    lv_arc_set_value(p->sec_arc, 0);
    lv_obj_set_style_arc_width(p->sec_arc, 4, 0);
    lv_obj_set_style_arc_color(p->sec_arc, lv_color_hex(SYS_UI_THEME_PRIMARY), 0);
    lv_obj_set_style_arc_rounded(p->sec_arc, true, 0);
    lv_obj_set_style_line_rounded(p->sec_arc, true, 0);
    lv_obj_set_style_arc_opa(p->sec_arc, LV_OPA_80, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(p->sec_arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(p->sec_arc, lv_color_hex(SYS_UI_THEME_TEXT), LV_PART_KNOB);
    lv_obj_set_style_size(p->sec_arc, 6, 6, LV_PART_KNOB);
    lv_obj_clear_flag(p->sec_arc, LV_OBJ_FLAG_CLICKABLE);

    p->sec_lbl = lv_label_create(clock_box);
    lv_label_set_text(p->sec_lbl, "00");
    lv_obj_set_style_text_font(p->sec_lbl, sys_ui_theme_font(24), 0);
    lv_obj_set_style_text_color(p->sec_lbl, lv_color_hex(SYS_UI_THEME_ACCENT), 0);
    lv_obj_align(p->sec_lbl, LV_ALIGN_RIGHT_MID, -38, 44);

    p->date_lbl = lv_label_create(p->screen);
    lv_label_set_text(p->date_lbl, "");
    lv_obj_set_style_text_font(p->date_lbl, sys_ui_theme_font(28), 0);
    lv_obj_set_style_text_color(p->date_lbl, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_align(p->date_lbl, LV_ALIGN_CENTER, 0, 66);

    p->nosync_lbl = lv_label_create(p->screen);
    lv_label_set_text(p->nosync_lbl, sys_ui_str(SYS_UI_STR_LAUNCHER_NOSYNC));
    lv_obj_set_style_text_font(p->nosync_lbl, sys_ui_theme_font(18), 0);
    lv_obj_set_style_text_color(p->nosync_lbl, lv_color_hex(SYS_UI_THEME_TEXT_DIM), 0);
    lv_obj_align(p->nosync_lbl, LV_ALIGN_CENTER, 0, 104);
    lv_obj_add_flag(p->nosync_lbl, LV_OBJ_FLAG_HIDDEN);

    sys_ui_unlock();

    sys_event_subscribe(SYS_EVENT_TIME_TICK, on_time_tick, app, &p->tick_sub);
    ESP_LOGI(TAG, "watch ui built");
    return ESP_OK;
}

/* ---------- 生命周期 ---------- */

static esp_err_t on_init(sys_app_t *app)
{
    watch_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    app->priv = p;
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    watch_priv_t *p = app->priv;
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
    update_clock(p);
    sys_ui_unlock();
    return ESP_OK;
}

static void on_suspend(sys_app_t *app)
{
    (void)app;
}

static void on_stop(sys_app_t *app)
{
    watch_priv_t *p = app->priv;
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
}

static sys_app_t s_watch = {
    .id         = WATCH_APP_ID,
    .name       = "Watch Face",
    .icon       = 0,
    .on_init    = on_init,
    .on_start   = on_start,
    .on_suspend = on_suspend,
    .on_stop    = on_stop,
    .on_event   = NULL,
};

sys_app_t *watch_app_get(void)
{
    return &s_watch;
}

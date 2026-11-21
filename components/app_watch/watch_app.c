/**
 * @file watch_app.c
 * @brief 表盘 APP (Vela 深空大数字主屏)
 *
 * Vela 交互逻辑:
 *   - 上滑 (全屏空白区)  → 应用列表 (launcher)
 *   - 顶部边缘下拉        → 控制中心 (sys_ui_quickpanel)
 *
 * 布局 (480x800 竖屏):
 *   顶部状态行(校时状态) → 大时钟 HH:MM (蓝色发光) + 背后光晕
 *   → 整圆秒环 (12 点起、顺时针扫) 环心秒数 → 日期(周X M/D) → 底部天气卡。
 * 已校时 (NTP) 显示真实时间; 未校时显示运行时长并加提示。
 *
 * LVGL 9.3 适配说明:
 *   - 9.3 无 bg_blur_size 样式 → 规格"光晕 blur 80"用"淡色圆盘 + width 80
 *     的柔和阴影"近似 (SHADOW 样式在 LV_DRAW_SW_COMPLEX 下支持)。
 *   - 9.x 无 LV_OPA_15 宏 → 15% 透明度用字面量 38。
 *   - 整圆秒环: lv_arc_set_bg_angles(0, 360) (9.3 SW 渲染对 start+360==end
 *     有专门整环路径) + lv_arc_set_rotation(270) (9.x 弧 0°=3 点方向,
 *     旋转 270° 使 0 秒位于 12 点)。
 *   - lv_obj_set_style_bg_grad 只存 dsc 指针 → 渐变描述符保存在 priv 里
 *     (lv_color_hex() 是函数调用, 不能用于静态初始化)。
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
#include "sys_ui/sys_ui_gesture.h"
#include "sys_ui/sys_ui_quickpanel.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "watch";

static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

/* ---------- 定稿色值 (Vela 深空大数字表盘) ---------- */
#define WATCH_BG_TOP        0x05070F  /* 屏幕渐变顶: 深空黑 */
#define WATCH_BG_BOTTOM     0x0D1526  /* 屏幕渐变底 */
#define WATCH_GLOW          0x2E5BFF  /* 光晕 / 秒环指示 / 时间发光 */
#define WATCH_ARC_TRACK     0x1A2440  /* 秒环轨道 */
#define WATCH_TEXT          0xF2F6FF  /* 主文字: 大数字 / 秒数 / 天气值 */
#define WATCH_TEXT_DIM      0x8FA3C8  /* 次级文字: 状态行 / 日期 / 天气标题 */
#define WATCH_CARD_BG       0x0F1830  /* 天气卡底色 */
#define WATCH_CARD_BORDER   0x1E2A4A  /* 天气卡描边 */
#define WATCH_NOSYNC_HINT   0xFFB020  /* 未校时提示 */

#define WATCH_GLOW_OPA      38        /* ≈15% (9.x 无 LV_OPA_15 宏) */
#define WX_FETCH_INTERVAL   300       /* 天气拉取节流: invalid 时每 5 分钟最多重试一次 */

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *glow;         /* 时间背后光晕 */
    lv_obj_t *status_lbl;   /* 顶部: 校时状态 */
    lv_obj_t *time_lbl;     /* 大时钟 HH:MM */
    lv_obj_t *sec_arc;      /* 秒环 (整圆) */
    lv_obj_t *sec_lbl;      /* 秒数 (环心) */
    lv_obj_t *date_lbl;     /* 日期 (周X M/D) */
    lv_obj_t *nosync_lbl;   /* 未校时提示 */
    lv_obj_t *wx_lbl;       /* 天气卡数值行 */
    lv_grad_dsc_t bg_grad;  /* 背景渐变 dsc (样式只存指针, 生命周期随 priv) */
    sys_net_weather_t weather;   /* 天气缓存副本 (SYS_EVENT_WEATHER / 快照) */
    uint32_t wx_fetch_try;  /* 上次请求天气拉取的 uptime 秒 */
    bool ui_built;
    sys_event_handle_t tick_sub;
    sys_event_handle_t wx_sub;
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

/* ---------- 字体 ---------- */

/* 字体: 优先 CJK TTF (懒缓存); 英文模式返回 NULL → 退回最近的内置 Montserrat */
static void set_font(lv_obj_t *obj, uint32_t cjk_px, const lv_font_t *fallback)
{
    lv_font_t *f = sys_ui_cjk_font(cjk_px);
    lv_obj_set_style_text_font(obj, f ? f : fallback, 0);
}

/* ---------- 日期 ---------- */

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

/* 英文月份名 (3 字母缩写) */
static const char *month_en_str(int tm_mon)
{
    static const char *names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    return names[tm_mon];
}

/* 日期行: 中文 "11月21日 周六" / 英文 "Nov 21, Sat" */
static void set_date_label(watch_priv_t *p, const struct tm *tm)
{
    if (sys_ui_lang() == SYS_UI_LANG_ZH) {
        lv_label_set_text_fmt(p->date_lbl, "%d月%d日 %s",
                              tm->tm_mon + 1, tm->tm_mday, weekday_str(tm->tm_wday));
    } else {
        lv_label_set_text_fmt(p->date_lbl, "%s %d, %s",
                              month_en_str(tm->tm_mon), tm->tm_mday, weekday_str(tm->tm_wday));
    }
}

/* ---------- 天气 ---------- */

/* 更新天气卡 (调用方须持显示锁):
 * valid → "%d°  %s"; WiFi 已连 → 获取中; 未连 → 离线。
 * 同时节流触发后台拉取: invalid 期间每 WX_FETCH_INTERVAL 秒最多一次。 */
static void refresh_weather(watch_priv_t *p)
{
    sys_net_wifi_status_t st;
    sys_net_wifi_get_status(&st);
    bool connected = (st.state == SYS_NET_WIFI_CONNECTED);

    if (connected && !p->weather.valid &&
        (uint32_t)(sys_time_uptime_sec() - p->wx_fetch_try) >= WX_FETCH_INTERVAL) {
        sys_net_weather_refresh();  /* 异步后台拉取, 结果经 SYS_EVENT_WEATHER 到达 */
        p->wx_fetch_try = sys_time_uptime_sec();
    }

    if (p->weather.valid) {
        lv_label_set_text_fmt(p->wx_lbl, "%d°  %s",
                              (int)p->weather.temp_c, p->weather.desc);
    } else if (connected) {
        lv_label_set_text(p->wx_lbl, sys_ui_str(SYS_UI_STR_WD_WEATHER_FETCH));
    } else {
        lv_label_set_text(p->wx_lbl, sys_ui_str(SYS_UI_STR_WD_WEATHER_OFFLINE));
    }
}

static void on_weather(int32_t event_id, void *event_data, void *user_ctx)
{
    (void)event_id;
    sys_app_t *self = user_ctx;
    if (self->state != SYS_APP_STATE_ACTIVE || !self->priv) {
        return;
    }
    if (!event_data) {
        return;
    }
    watch_priv_t *p = self->priv;
    /* 事件回调跑在 sys_evt 任务 (不持显示锁) → 显式加锁 (同 app_net 各事件回调) */
    if (!sys_ui_lock()) {
        return;
    }
    /* 先拷贝到 priv, 保证后续 1Hz 读取状态一致 */
    p->weather = *(const sys_net_weather_t *)event_data;
    refresh_weather(p);
    sys_ui_unlock();
}

/* ---------- 时钟刷新 (1Hz) ---------- */

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
        set_date_label(p, &tm);
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
    lv_label_set_text(p->status_lbl,
                      synced ? sys_ui_str(SYS_UI_STR_NET_TIME_OK)
                             : sys_ui_str(SYS_UI_STR_NET_TIME_NOSYNC));
    if (synced) {
        lv_obj_add_flag(p->nosync_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(p->nosync_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_weather(p);  /* 顺 1Hz 心跳更新天气卡 (锁已持有) */
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
    lv_obj_clear_flag(p->screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 屏幕背景: 竖向深空渐变 (dsc 存 priv, 见文件头说明) */
    memset(&p->bg_grad, 0, sizeof(p->bg_grad));
    p->bg_grad.dir         = LV_GRAD_DIR_VER;
    p->bg_grad.stops_count = 2;
    p->bg_grad.stops[0].color = lv_color_hex(WATCH_BG_TOP);
    p->bg_grad.stops[0].opa   = LV_OPA_COVER;
    p->bg_grad.stops[0].frac  = 0;
    p->bg_grad.stops[1].color = lv_color_hex(WATCH_BG_BOTTOM);
    p->bg_grad.stops[1].opa   = LV_OPA_COVER;
    p->bg_grad.stops[1].frac  = 255;
    lv_obj_set_style_bg_grad(p->screen, &p->bg_grad, 0);
    lv_obj_set_style_bg_opa(p->screen, LV_OPA_COVER, 0);

    /* 手势层必须是第一个子对象 (垫底, 只捕获空白区滑动) */
    sys_ui_swipe_layer_create(p->screen, on_swipe, app);

    /* 顶部边缘下拉 → 控制中心 */
    sys_ui_top_edge_create(p->screen, qc_open_cb, app);

    /* 时间背后光晕: 大圆盘 + 柔和外扩 (9.3 无 bg_blur_size, 见文件头) */
    p->glow = lv_obj_create(p->screen);
    lv_obj_set_size(p->glow, 360, 360);
    lv_obj_set_style_radius(p->glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p->glow, lv_color_hex(WATCH_GLOW), 0);
    lv_obj_set_style_bg_opa(p->glow, WATCH_GLOW_OPA, 0);
    lv_obj_set_style_shadow_width(p->glow, 80, 0);
    lv_obj_set_style_shadow_spread(p->glow, 0, 0);
    lv_obj_set_style_shadow_color(p->glow, lv_color_hex(WATCH_GLOW), 0);
    lv_obj_set_style_shadow_opa(p->glow, LV_OPA_40, 0);
    lv_obj_clear_flag(p->glow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(p->glow, LV_ALIGN_CENTER, 0, -180);

    /* 顶部状态行: 校时状态 (避开顶部 24px 边缘手势区) */
    p->status_lbl = lv_label_create(p->screen);
    lv_label_set_text(p->status_lbl, sys_ui_str(SYS_UI_STR_NET_TIME_NOSYNC));
    set_font(p->status_lbl, 20, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(p->status_lbl, lv_color_hex(WATCH_TEXT_DIM), 0);
    lv_obj_align(p->status_lbl, LV_ALIGN_TOP_MID, 0, 48);

    /* 主时间 HH:MM (大数字 + 蓝色发光) */
    p->time_lbl = lv_label_create(p->screen);
    lv_label_set_text(p->time_lbl, "--:--");
    set_font(p->time_lbl, 96, &lv_font_montserrat_48);
    lv_obj_set_style_text_color(p->time_lbl, lv_color_hex(WATCH_TEXT), 0);
    lv_obj_set_style_shadow_color(p->time_lbl, lv_color_hex(WATCH_GLOW), 0);
    lv_obj_set_style_shadow_width(p->time_lbl, 60, 0);
    lv_obj_set_style_shadow_opa(p->time_lbl, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(p->time_lbl, 0, 0);
    lv_obj_align(p->time_lbl, LV_ALIGN_CENTER, 0, -180);

    /* 秒环: 整圆, 12 点起顺时针扫 (实现细节见文件头说明) */
    p->sec_arc = lv_arc_create(p->screen);
    lv_obj_set_size(p->sec_arc, 160, 160);
    lv_obj_align(p->sec_arc, LV_ALIGN_CENTER, 0, -40);
    lv_arc_set_range(p->sec_arc, 0, 59);
    lv_arc_set_value(p->sec_arc, 0);
    lv_arc_set_bg_angles(p->sec_arc, 0, 360);
    lv_arc_set_rotation(p->sec_arc, 270);
    /* 轨道 (LV_PART_MAIN) */
    lv_obj_set_style_arc_width(p->sec_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(p->sec_arc, lv_color_hex(WATCH_ARC_TRACK), LV_PART_MAIN);
    /* 指示 (LV_PART_INDICATOR): 圆角端点 */
    lv_obj_set_style_arc_width(p->sec_arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(p->sec_arc, lv_color_hex(WATCH_GLOW), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(p->sec_arc, true, LV_PART_INDICATOR);
    /* 不需要 knob */
    lv_obj_set_style_bg_opa(p->sec_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(p->sec_arc, LV_OBJ_FLAG_CLICKABLE);

    /* 秒数 (环心) */
    p->sec_lbl = lv_label_create(p->screen);
    lv_label_set_text(p->sec_lbl, "00");
    set_font(p->sec_lbl, 28, &lv_font_montserrat_28);
    lv_obj_set_style_text_color(p->sec_lbl, lv_color_hex(WATCH_TEXT), 0);
    lv_obj_align(p->sec_lbl, LV_ALIGN_CENTER, 0, -40);

    /* 日期行: 中文 "11月21日 周六" / 英文 "Nov 21, Sat" */
    p->date_lbl = lv_label_create(p->screen);
    lv_label_set_text(p->date_lbl, "");
    set_font(p->date_lbl, 28, &lv_font_montserrat_28);
    lv_obj_set_style_text_color(p->date_lbl, lv_color_hex(WATCH_TEXT_DIM), 0);
    lv_obj_align(p->date_lbl, LV_ALIGN_CENTER, 0, 80);

    /* 未校时提示 (日期下方) */
    p->nosync_lbl = lv_label_create(p->screen);
    lv_label_set_text(p->nosync_lbl, sys_ui_str(SYS_UI_STR_LAUNCHER_NOSYNC));
    set_font(p->nosync_lbl, 16, &lv_font_montserrat_16);
    lv_obj_set_style_text_color(p->nosync_lbl, lv_color_hex(WATCH_NOSYNC_HINT), 0);
    lv_obj_align(p->nosync_lbl, LV_ALIGN_CENTER, 0, 112);
    lv_obj_add_flag(p->nosync_lbl, LV_OBJ_FLAG_HIDDEN);

    /* 天气卡 (底部居中, y=580) */
    lv_obj_t *wx_card = lv_obj_create(p->screen);
    lv_obj_set_size(wx_card, 400, 150);
    lv_obj_align(wx_card, LV_ALIGN_TOP_MID, 0, 580);
    lv_obj_set_style_bg_color(wx_card, lv_color_hex(WATCH_CARD_BG), 0);
    lv_obj_set_style_radius(wx_card, 28, 0);
    lv_obj_set_style_border_width(wx_card, 1, 0);
    lv_obj_set_style_border_color(wx_card, lv_color_hex(WATCH_CARD_BORDER), 0);
    lv_obj_clear_flag(wx_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wx_title = lv_label_create(wx_card);
    lv_label_set_text(wx_title, sys_ui_str(SYS_UI_STR_WD_WEATHER));
    set_font(wx_title, 20, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(wx_title, lv_color_hex(WATCH_TEXT_DIM), 0);
    lv_obj_align(wx_title, LV_ALIGN_TOP_LEFT, 24, 22);

    p->wx_lbl = lv_label_create(wx_card);
    lv_label_set_text(p->wx_lbl, sys_ui_str(SYS_UI_STR_WD_WEATHER_OFFLINE));
    /* 规格 32px 不在允许字号集 {96,28,24,20,16} 内 → 用最接近的 28px */
    set_font(p->wx_lbl, 28, &lv_font_montserrat_28);
    lv_obj_set_style_text_color(p->wx_lbl, lv_color_hex(WATCH_TEXT), 0);
    lv_obj_align(p->wx_lbl, LV_ALIGN_BOTTOM_LEFT, 24, -26);

    sys_ui_unlock();

    sys_event_subscribe(SYS_EVENT_TIME_TICK, on_time_tick, app, &p->tick_sub);
    sys_event_subscribe(SYS_EVENT_WEATHER, on_weather, app, &p->wx_sub);
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
    p->wx_fetch_try = UINT32_MAX;  /* WiFi 连上后首次立即尝试拉天气 */
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
    /* 先读当前天气缓存 (线程安全, 无需显示锁); 之后经 SYS_EVENT_WEATHER 更新 */
    sys_net_weather_snapshot(&p->weather);
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
    sys_event_unsubscribe(p->wx_sub);
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

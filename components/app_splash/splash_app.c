/**
 * @file splash_app.c
 * @brief 启动画面 (Splash) —— 品牌 + 进度条
 *
 * 职责: 掩盖上电白屏。它在显示点亮后的最早时机就用 LVGL 内置字体
 * (Montserrat) 画出来, 不依赖 SD 卡/中文字库; 系统完成全部初始化
 * (字库/触摸/音频, 由 app_main 调 splash_set_ready 通知) 后才切入桌面。
 *
 * 时间线:
 *   上电 → 显示+LVGL → 进入本画面 (内置字体, 英文文案)
 *        → 字库加载完成 → 自动升级为中文字体/文案 (若 splash 仍在显示)
 *        → splash_set_ready() → 进度条充满 → 自动切入 Launcher
 *
 * 自动切换在 LVGL timer 上下文执行 (与按钮回调同上下文),
 * 经调度器互斥锁保护, 线程安全。
 */
#include <stdlib.h>
#include <string.h>
#include "app_splash/splash_app.h"
#include "app_launcher/launcher_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_ui/sys_ui.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "splash";

#define SPLASH_TIMER_PERIOD     30    /* 进度刷新周期 */
#define SPLASH_PRE_READY_MAX_MS 6000  /* ready 前进度条 6 秒内走到 90% 后停走 */
#define SPLASH_READY_HOLD_MS    600   /* ready 后短暂停留再切入 */

static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

typedef struct {
    lv_obj_t    *screen;
    lv_obj_t    *bar;
    lv_obj_t    *brand;   /* 字库就绪后升级中文字体用 */
    lv_obj_t    *sub;
    lv_obj_t    *status;
    lv_timer_t  *timer;
    uint32_t     elapsed_ms;
    uint32_t     hold_ms;
    bool         ui_built;
    bool         cjk_applied;
    bool         alive;   /* on_stop 置 false; timer 在 LVGL 上下文自删 */
} splash_priv_t;

/* app_main 全部初始化完成后置位; LVGL 任务读取 (单向锁存标志, 无需加锁) */
static volatile bool s_ready;

/* 升级为中文字体/文案 (字库在 splash 显示期间才加载完成) */
static void apply_cjk(splash_priv_t *p)
{
    if (!sys_ui_lock()) {
        return;
    }
    const lv_font_t *f48 = sys_ui_cjk_font(48);
    if (f48) {
        lv_obj_set_style_text_font(p->brand, f48, 0);
    }
    lv_label_set_text(p->sub, sys_ui_str(SYS_UI_STR_SPLASH_SUBTITLE));
    lv_label_set_text(p->status, sys_ui_str(SYS_UI_STR_SPLASH_STARTING));
    lv_font_t *f24 = sys_ui_cjk_font(SYS_UI_FONT_DEFAULT_PX);
    if (f24) {
        lv_obj_set_style_text_font(p->sub, f24, 0);
        lv_obj_set_style_text_font(p->status, f24, 0);
    }
    sys_ui_unlock();
}

/* LVGL timer 上下文: 进度条动画 / 字库自动升级 / ready 后切入桌面 */
static void splash_tick(lv_timer_t *t)
{
    splash_priv_t *p = lv_timer_get_user_data(t);
    if (!p || !p->alive) {
        lv_timer_delete(t);
        return;
    }
    p->elapsed_ms += SPLASH_TIMER_PERIOD;

    /* 字库就绪 (早期画面用内置字体画的) → 升级中文 */
    if (!p->cjk_applied && sys_ui_cjk_font(48)) {
        apply_cjk(p);
        p->cjk_applied = true;
    }

    if (!s_ready) {
        /* 系统初始化未完成: 进度条走到 90% 后停走, 等 ready */
        uint32_t v = p->elapsed_ms * 90 / SPLASH_PRE_READY_MAX_MS;
        if (v > 90) {
            v = 90;
        }
        lv_bar_set_value(p->bar, (int32_t)v, LV_ANIM_OFF);
        return;
    }

    /* ready: 充满, 短暂停留后切入桌面 */
    lv_bar_set_value(p->bar, 100, LV_ANIM_OFF);
    p->hold_ms += SPLASH_TIMER_PERIOD;
    if (p->hold_ms >= SPLASH_READY_HOLD_MS) {
        lv_timer_delete(t);
        p->timer = NULL;
        p->alive = false;
        ESP_LOGI(TAG, "boot sequence done, launch %s", LAUNCHER_APP_ID);
        sys_app_launch(LAUNCHER_APP_ID);
    }
}

static esp_err_t build_ui(sys_app_t *app)
{
    splash_priv_t *p = app->priv;
    if (!sys_ui_lock()) {
        return ESP_FAIL;
    }
    p->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(p->screen, lv_color_hex(0x0B0F14), 0);

    /* 品牌 (48px; 有中文字库用 TTF 字体, 否则内置 Montserrat 48) */
    p->brand = lv_label_create(p->screen);
    lv_label_set_text(p->brand, "ESP32-P4");
    lv_obj_set_style_text_color(p->brand, lv_color_hex(0xE0E0E0), 0);
    const lv_font_t *bf = sys_ui_cjk_font(48);
    if (!bf) {
        bf = &lv_font_montserrat_48; /* LVGL9 字体宏是 const 对象, 取地址 */
    } else {
        p->cjk_applied = true;
    }
    lv_obj_set_style_text_font(p->brand, bf, 0);
    lv_obj_align(p->brand, LV_ALIGN_CENTER, 0, -140);

    /* 副标题 (i18n: 早期启动为英文文案, 字库加载后自动切中文) */
    p->sub = lv_label_create(p->screen);
    lv_label_set_text(p->sub, sys_ui_str(SYS_UI_STR_SPLASH_SUBTITLE));
    lv_obj_set_style_text_color(p->sub, lv_color_hex(0x7EC8E3), 0);
    lv_font_t *sf = sys_ui_cjk_font(SYS_UI_FONT_DEFAULT_PX);
    if (sf) {
        lv_obj_set_style_text_font(p->sub, sf, 0);
    }
    lv_obj_align(p->sub, LV_ALIGN_CENTER, 0, -80);

    /* 进度条 */
    p->bar = lv_bar_create(p->screen);
    lv_obj_set_size(p->bar, 280, 12);
    lv_bar_set_range(p->bar, 0, 100);
    lv_bar_set_value(p->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(p->bar, lv_color_hex(0x1B2129), LV_PART_MAIN);
    lv_obj_set_style_bg_color(p->bar, lv_color_hex(0x2C6E9B), LV_PART_INDICATOR);
    lv_obj_set_style_radius(p->bar, 6, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_align(p->bar, LV_ALIGN_CENTER, 0, 40);

    /* 状态文字 (i18n) */
    p->status = lv_label_create(p->screen);
    lv_label_set_text(p->status, sys_ui_str(SYS_UI_STR_SPLASH_STARTING));
    lv_obj_set_style_text_color(p->status, lv_color_hex(0x9AA5B1), 0);
    lv_obj_align(p->status, LV_ALIGN_CENTER, 0, 80);
    sys_ui_unlock();
    return ESP_OK;
}

static esp_err_t on_init(sys_app_t *app)
{
    splash_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    p->alive = true;
    app->priv = p;
    return ESP_OK;
}

static esp_err_t on_start(sys_app_t *app)
{
    splash_priv_t *p = app->priv;
    if (!p->ui_built) {
        if (build_ui(app) != ESP_OK) {
            return ESP_FAIL;
        }
        p->ui_built = true;
    }
    /* 显示锁内: 屏幕切换 + 创建自动切换 timer (与渲染适配任务互斥) */
    if (!sys_ui_lock()) {
        return ESP_OK;
    }
    sys_ui_set_root(p->screen);
    p->elapsed_ms = 0;
    p->hold_ms = 0;
    p->alive = true;
    lv_bar_set_value(p->bar, 0, LV_ANIM_OFF);
    if (!p->timer) {
        p->timer = lv_timer_create(splash_tick, SPLASH_TIMER_PERIOD, p);
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
    splash_priv_t *p = app->priv;
    if (!p) {
        return;
    }
    p->alive = false;
    if (p->ui_built && sys_ui_lock()) {
        if (p->timer) {
            lv_timer_delete(p->timer);
            p->timer = NULL;
        }
        if (p->screen) {
            lv_obj_delete(p->screen);
        }
        sys_ui_unlock();
        p->ui_built = false;
    }
    free(p);
    app->priv = NULL;
}

static sys_app_t s_splash = {
    .id         = SPLASH_APP_ID,
    .name       = "Splash",
    .icon       = 0,
    .on_init    = on_init,
    .on_start   = on_start,
    .on_suspend = on_suspend,
    .on_stop    = on_stop,
    .on_event   = NULL,
};

sys_app_t *splash_app_get(void)
{
    return &s_splash;
}

/**
 * @brief 系统初始化全部完成后由 app_main 调用:
 *        启动画面进度条充满, 短暂停留后切入桌面
 */
void splash_set_ready(void)
{
    s_ready = true;
}

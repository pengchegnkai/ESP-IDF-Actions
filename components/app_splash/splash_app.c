/**
 * @file splash_app.c
 * @brief 启动画面 (Splash) —— 品牌 + 进度条, 结束后自动切入 Launcher
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

#define SPLASH_HOLD_MS      3000  /* 总停留时长 */
#define SPLASH_BAR_MS       2500  /* 进度条充满时长 */
#define SPLASH_TIMER_PERIOD 30    /* 进度刷新周期 */

static esp_err_t on_init(sys_app_t *app);
static esp_err_t on_start(sys_app_t *app);
static void on_suspend(sys_app_t *app);
static void on_stop(sys_app_t *app);

typedef struct {
    lv_obj_t    *screen;
    lv_obj_t    *bar;
    lv_timer_t  *timer;
    uint32_t     elapsed_ms;
    bool         ui_built;
    bool         alive;  /* on_stop 置 false; timer 在 LVGL 上下文自删 */
} splash_priv_t;

/* LVGL timer 上下文: 刷新进度, 到期自动切入 Launcher */
static void splash_tick(lv_timer_t *t)
{
    splash_priv_t *p = lv_timer_get_user_data(t);
    if (!p || !p->alive) {
        lv_timer_delete(t);
        return;
    }
    p->elapsed_ms += SPLASH_TIMER_PERIOD;
    if (p->elapsed_ms >= SPLASH_HOLD_MS) {
        lv_timer_delete(t);
        p->timer = NULL;
        p->alive = false;
        ESP_LOGI(TAG, "boot sequence done, launch %s", LAUNCHER_APP_ID);
        sys_app_launch(LAUNCHER_APP_ID);
        return;
    }
    if (p->bar) {
        uint32_t v = p->elapsed_ms * 100 / SPLASH_BAR_MS;
        if (v > 100) {
            v = 100;
        }
        lv_bar_set_value(p->bar, (int32_t)v, LV_ANIM_OFF);
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

    /* 品牌 (48px; 中文模式下用 TTF 字体, 英文模式用内置 Montserrat 48) */
    lv_obj_t *brand = lv_label_create(p->screen);
    lv_label_set_text(brand, "ESP32-P4");
    lv_obj_set_style_text_color(brand, lv_color_hex(0xE0E0E0), 0);
    const lv_font_t *bf = sys_ui_cjk_font(48);
    if (!bf) {
        bf = &lv_font_montserrat_48; /* LVGL9 字体宏是 const 对象, 取地址 */
    }
    lv_obj_set_style_text_font(brand, bf, 0);
    lv_obj_align(brand, LV_ALIGN_CENTER, 0, -140);

    /* 副标题 (i18n) */
    lv_obj_t *sub = lv_label_create(p->screen);
    lv_label_set_text(sub, sys_ui_str(SYS_UI_STR_SPLASH_SUBTITLE));
    lv_obj_set_style_text_color(sub, lv_color_hex(0x7EC8E3), 0);
    lv_font_t *sf = sys_ui_cjk_font(SYS_UI_FONT_DEFAULT_PX);
    if (sf) {
        lv_obj_set_style_text_font(sub, sf, 0);
    }
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -80);

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
    lv_obj_t *status = lv_label_create(p->screen);
    lv_label_set_text(status, sys_ui_str(SYS_UI_STR_SPLASH_STARTING));
    lv_obj_set_style_text_color(status, lv_color_hex(0x9AA5B1), 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 80);
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
    p->alive = true;
    lv_bar_set_value(p->bar, 0, LV_ANIM_OFF);
    p->timer = lv_timer_create(splash_tick, SPLASH_TIMER_PERIOD, p);
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

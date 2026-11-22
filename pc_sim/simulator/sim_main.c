/* sim_main.c — 交互式 LVGL 模拟器入口 (pc_sim_app)
 *
 * [纯逻辑·可仿真] SDL2 窗口渲染 480×800 (与 P4 真机同分辨率), 鼠标/手指 = 触摸输入。
 * 在 Termux 运行 ./run_simulator.sh 即可亲手测试三个 APP 的 UI —
 * 同一份 ui/ 源码在真机上由工厂 LVGL port (LvLock) 驱动, 此处仅替换输入/显示后端。
 *
 * APP 接线用 __has_include 增量检测: 成员交付 ui/ 头文件后自动点亮, 无需改本文件。
 * 后端: fm=POSIX 目录 (sim_fs/), hk=fake USB + 同目录, sk=确定性 fake 加密 (stubs/)。
 */
/* 双保险: 无论 lv_conf.h 装载链如何, 强制 SDL 后端生效 */
#ifndef LV_USE_SDL
#define LV_USE_SDL 1
#endif

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include <SDL2/SDL.h>
#include <stdio.h>

/* ---- 成员交付后自动可用的 APP 头 ---- */
#if __has_include("fm_ui.h")
#include "fm_ui.h"
#define SIM_HAS_FM 1
#endif
#if __has_include("hk_ui.h")
#include "hk_ui.h"
#define SIM_HAS_HK 1
#endif
#if __has_include("sk_popup.h")
#include "sk_popup.h"
#define SIM_HAS_SK 1
#endif

/* ---- host 后端 (stubs/, 与单元测试共用) ---- */
#if defined(SIM_HAS_FM) && __has_include("fm_backend.h") && __has_include("fm_backend_host.h")
#include "fm_backend.h"
#include "fm_backend_host.h"
static fm_backend_t s_fm_be;
#define FM_BACKEND_AVAILABLE 1
#endif

static lv_obj_t *s_launcher; /* 启动器屏幕 */

static void back_to_launcher(lv_event_t *e)
{
    (void)e;
    lv_screen_load_anim(s_launcher, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

/* APP 屏幕容器: 顶部 home 条 + 内容区 (真机由工厂 Phone 系统提供同样结构) */
static lv_obj_t *make_app_screen(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 480, 56);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2b3240), 0);

    lv_obj_t *home = lv_button_create(bar);
    lv_obj_set_size(home, 110, 40);
    lv_obj_align(home, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text(hl, "< Home");
    lv_obj_center(hl);
    lv_obj_add_event_cb(home, back_to_launcher, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
    return scr;
}

static void open_fm(lv_event_t *e)
{
    (void)e;
    lv_obj_t *scr = make_app_screen("FileManager");
#if defined(FM_BACKEND_AVAILABLE)
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_size(body, 480, 744);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    fm_ui_create(body, &s_fm_be); /* 同一份纯 LVGL ui/ 源码 */
#endif
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

static void open_hk(lv_event_t *e)
{
    (void)e;
    lv_obj_t *scr = make_app_screen("HackerKit");
#if defined(SIM_HAS_HK)
    /* TODO(集成): hk_ui_create(body, &s_hk_fs, &s_hk_usb, delay_noop) — hk3 交付后接线 */
#endif
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, "HackerKit UI: awaiting member delivery");
    lv_obj_center(l);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

static void open_sk(lv_event_t *e)
{
    (void)e;
    lv_obj_t *scr = make_app_screen("SecurityKey");
#if defined(SIM_HAS_SK)
    /* TODO(集成): 手动激活开关 + sk_popup_show() 全局弹窗演示 */
#endif
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, "SecurityKey UI: awaiting member delivery");
    lv_obj_center(l);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

int main(void)
{
    lv_init();
    lv_sdl_window_create(480, 800); /* 与真机 480x800 MIPI-DSI 同分辨率 */
    lv_sdl_mouse_create();          /* 鼠标/手指 → LVGL 触摸 indev */

#if defined(FM_BACKEND_AVAILABLE)
    fm_backend_host_init(&s_fm_be, "sim_fs");
#endif

    /* 启动器: 三个 APP 入口 (模拟工厂 launcher 图标网格) */
    s_launcher = lv_obj_create(NULL);
    lv_obj_set_size(s_launcher, 480, 800);

    lv_obj_t *title = lv_label_create(s_launcher);
    lv_label_set_text(title, "Brookesia pc_sim");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    struct { const char *name; lv_event_cb_t cb; } apps[] = {
        { "FileManager", open_fm },
        { "HackerKit",   open_hk },
        { "SecurityKey", open_sk },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_button_create(s_launcher);
        lv_obj_set_size(b, 300, 90);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 180 + i * 140);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, apps[i].name);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, apps[i].cb, LV_EVENT_CLICKED, NULL);
    }

    lv_screen_load(s_launcher);

    /* 主循环 */
    while (1) {
        uint32_t next = lv_timer_handler();
        SDL_Delay(next < 5 ? 5 : (next > 100 ? 100 : next));
    }
    return 0;
}

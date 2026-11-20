/**
 * @file sys_ui_gesture.c
 * @brief Vela 式手势框架实现
 *
 * 原理: LVGL 的 LV_EVENT_RELEASED 始终发回"按下时命中的对象",
 * 所以即使手指滑出对象范围也能在按下点收到抬起事件 —— 用它算
 * 按下→抬起的位移, 按主导轴方向触发回调。不依赖 LVGL 内置
 * gesture 子系统, 行为确定、与 indev 配置无关。
 */
#include "sys_ui/sys_ui_gesture.h"
#include <stdlib.h>

typedef struct {
    int   start_x, start_y;
    bool  pressed;
    uint8_t mode;   /* 0 = 全方向 (全屏层); 1 = 只下滑 (顶边); 2 = 只上滑 (底边) */
    sys_ui_swipe_cb_t cb;
    void *ctx;      /* 单方向区时指向 one_dir_priv_t */
} gesture_priv_t;

typedef struct {
    void (*cb)(void *);
    void *ctx;
} one_dir_priv_t;

static void on_press(lv_event_t *e)
{
    gesture_priv_t *g = lv_obj_get_user_data(lv_event_get_target(e));
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!g || !indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);   /* LVGL 9.3: void 返回, 恒成功 */
    g->start_x = p.x;
    g->start_y = p.y;
    g->pressed = true;
}

static void on_release(lv_event_t *e)
{
    gesture_priv_t *g = lv_obj_get_user_data(lv_event_get_target(e));
    if (!g || !g->pressed) {
        return;
    }
    g->pressed = false;
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dx = p.x - g->start_x;
    int dy = p.y - g->start_y;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    if (ax < SYS_UI_SWIPE_THRESHOLD && ay < SYS_UI_SWIPE_THRESHOLD) {
        return; /* 普通点击 */
    }
    sys_ui_swipe_dir_t dir;
    if (ax >= ay) {
        dir = (p.x > g->start_x) ? SYS_UI_SWIPE_RIGHT : SYS_UI_SWIPE_LEFT;
    } else {
        dir = (p.y > g->start_y) ? SYS_UI_SWIPE_DOWN : SYS_UI_SWIPE_UP;
    }
    if (g->mode == 1 && dir != SYS_UI_SWIPE_DOWN) { return; }
    if (g->mode == 2 && dir != SYS_UI_SWIPE_UP)   { return; }
    if (g->cb) {
        g->cb(dir, g->ctx);
    }
}

static void one_dir_trampoline(sys_ui_swipe_dir_t dir, void *gctx)
{
    (void)dir;
    one_dir_priv_t *od = gctx;
    if (od && od->cb) {
        od->cb(od->ctx);
    }
}

/* zone 删除时释放 priv (屏幕销毁连带释放, 防进出 APP 反复泄漏) */
static void on_zone_delete(lv_event_t *e)
{
    gesture_priv_t *g = lv_obj_get_user_data(lv_event_get_target(e));
    if (g) {
        if (g->mode != 0) {
            free(g->ctx);  /* one_dir_priv_t */
        }
        free(g);
    }
}

static lv_obj_t *make_zone(lv_obj_t *parent, int32_t height, uint8_t mode,
                           bool top_align, sys_ui_swipe_cb_t cb, void *ctx)
{
    lv_obj_t *zone = lv_obj_create(parent);
    lv_obj_set_size(zone, LV_PCT(100), height);
    lv_obj_align(zone, top_align ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_set_style_pad_all(zone, 0, 0);
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE);

    gesture_priv_t *g = calloc(1, sizeof(*g));
    if (!g) {
        lv_obj_delete(zone);
        return NULL;
    }
    g->mode = mode;
    g->cb   = cb;
    g->ctx  = ctx;
    lv_obj_set_user_data(zone, g);
    lv_obj_add_event_cb(zone, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(zone, on_release, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(zone, on_zone_delete, LV_EVENT_DELETE, NULL);
    return zone;
}

lv_obj_t *sys_ui_swipe_layer_create(lv_obj_t *parent, sys_ui_swipe_cb_t cb, void *ctx)
{
    return make_zone(parent, LV_PCT(100), 0, true, cb, ctx);
}

lv_obj_t *sys_ui_top_edge_create(lv_obj_t *parent, void (*cb)(void *ctx), void *ctx)
{
    if (!cb) {
        return NULL;
    }
    one_dir_priv_t *od = calloc(1, sizeof(*od));
    if (!od) {
        return NULL;
    }
    od->cb  = cb;
    od->ctx = ctx;
    lv_obj_t *zone = make_zone(parent, 24, 1, true, one_dir_trampoline, od);
    if (!zone) {
        free(od);
    }
    return zone;
}

lv_obj_t *sys_ui_bottom_edge_create(lv_obj_t *parent, void (*cb)(void *ctx), void *ctx)
{
    if (!cb) {
        return NULL;
    }
    one_dir_priv_t *od = calloc(1, sizeof(*od));
    if (!od) {
        return NULL;
    }
    od->cb  = cb;
    od->ctx = ctx;
    lv_obj_t *zone = make_zone(parent, 24, 2, false, one_dir_trampoline, od);
    if (!zone) {
        free(od);
    }
    return zone;
}

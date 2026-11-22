/* test_lvgl_headless.c — LVGL 无头渲染冒烟测试 (UI 层可仿真的证明)
 *
 * [纯逻辑·可仿真] 不依赖 SDL/显示设备: 软件渲染到内存 buffer (LV_USE_DRAW_SW),
 * 证明 ui/ 层代码 (纯 LVGL C) 可在 PC 上脱离 P4 硬件运行.
 * 真机侧同一份 ui/ 源码由工厂 LVGL port (LvLock 保护) 驱动 — 模拟先行分层.
 */
#include "test_framework.h"

#if defined(PC_SIM_HAS_LVGL)

#include "lvgl.h"

static uint8_t s_buf[480 * 80 * 4];

static int s_btn_click_count = 0;

static void on_btn_clicked(lv_event_t *e)
{
    (void)e;
    s_btn_click_count++;
}

static void lvgl_sim_init(void)
{
    static int inited = 0;
    if (inited) return;
    lv_init();
    lv_display_t *d = lv_display_create(480, 800);
    lv_display_set_buffers(d, s_buf, NULL, sizeof(s_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    inited = 1;
}

TEST(lvgl_headless_display_and_label)
{
    lvgl_sim_init();
    lv_obj_t *scr = lv_screen_active();
    CHECK(scr != NULL);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "hello pc sim");
    /* 跑一帧渲染 */
    lv_timer_handler();
    /* 标签应有非零布局 (字体渲染生效) */
    CHECK(lv_obj_get_width(label) > 0);
    CHECK(lv_obj_get_height(label) > 0);
}

TEST(lvgl_headless_list_and_events)
{
    lvgl_sim_init();
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_center(list);
    /* lv_list_add_button(list, icon, text) — icon 可 NULL (符号字体在 lv_conf 默认关闭);
     * 返回值即按钮对象, 直接挂事件回调 */
    static int s_clicked = 0;
    lv_obj_t *btn  = lv_list_add_button(list, NULL, "payloads");
    lv_obj_t *btn2 = lv_list_add_button(list, NULL, "scripts");
    (void)btn2;
    (void)s_clicked;
    CHECK(btn != NULL);
    lv_obj_add_event_cb(btn, on_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    CHECK(lv_obj_get_width(list) > 0);

    /* 事件注入: 等价于触摸完成的最终效果 (真机由底层 lv_indev 产生同样的事件链) */
    lv_obj_send_event(btn, LV_EVENT_CLICKED, NULL);
    CHECK(s_btn_click_count == 1);
}

#endif /* PC_SIM_HAS_LVGL */

/**
 * @file sys_ui_gesture.h
 * @brief Vela 式手势框架 —— 全屏滑动层 + 屏幕边缘滑动区
 *
 * 交互约定 (移植自 OpenHarmony Vela 手表系统):
 *   - 表盘(主屏)  上滑 → 应用列表
 *   - 应用列表    下滑 → 表盘
 *   - 任意屏幕    顶部边缘下拉 → 控制中心 (快捷面板)
 *   - 应用内      底部边缘上滑 → 返回
 *   - 控制中心    上滑任意处 / 点背景 → 收起
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 滑动方向 */
typedef enum {
    SYS_UI_SWIPE_UP = 0,
    SYS_UI_SWIPE_DOWN,
    SYS_UI_SWIPE_LEFT,
    SYS_UI_SWIPE_RIGHT,
} sys_ui_swipe_dir_t;

/** 全屏滑动层回调 (按下→抬起位移 ≥ 阈值时按主方向触发) */
typedef void (*sys_ui_swipe_cb_t)(sys_ui_swipe_dir_t dir, void *ctx);

/** 边缘滑动阈值 px */
#define SYS_UI_SWIPE_THRESHOLD 28

/**
 * @brief 全屏手势层 (透明, 不画任何东西)
 * @note  必须作为屏幕的**第一个子对象**创建 (垫底):
 *        只有落在空白区域的滑动才由它捕获, 上层控件(按钮/滑块)优先。
 * @return 手势层对象 (随屏幕销毁)
 */
lv_obj_t *sys_ui_swipe_layer_create(lv_obj_t *parent, sys_ui_swipe_cb_t cb, void *ctx);

/**
 * @brief 顶部边缘下拉区 (屏顶 24px 高, 最上层)
 * @note  下拉 (dy ≥ 阈值) 时触发 cb; 状态栏内容请避开顶部 24px。
 */
lv_obj_t *sys_ui_top_edge_create(lv_obj_t *parent, void (*cb)(void *ctx), void *ctx);

/**
 * @brief 底部边缘上滑区 (屏底 24px 高, 最上层)
 * @note  上滑 (dy ≤ -阈值) 时触发 cb; 应用内用作"返回"手势。
 */
lv_obj_t *sys_ui_bottom_edge_create(lv_obj_t *parent, void (*cb)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

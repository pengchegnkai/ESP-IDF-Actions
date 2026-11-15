/**
 * @file sys_ui.h
 * @brief UI 框架 —— LVGL 封装: 初始化、APP 屏幕切换、顶层弹窗(Overlay)
 *
 * 线程模型: 所有 LVGL 对象操作必须在 sys_ui_lock()/sys_ui_unlock() 之间
 * 进行 (通常在 APP 生命周期回调内部, 回调本身就持有锁)。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** LVGL 心跳周期 ms */
#define SYS_UI_TICK_MS 10

/**
 * @brief 初始化 UI 子系统: LVGL 核心 + 渲染任务
 * @note  依赖 sys_hal_display_init() 已成功
 */
esp_err_t sys_ui_init(void);

/** LVGL 互斥锁 (生命周期回调外操作 LVGL 对象时必须持有) */
bool sys_ui_lock(void);   /* 返回 false = 未获锁, 禁止执行 LVGL 操作 */
void sys_ui_unlock(void);

/**
 * @brief 切换到指定 APP 的屏幕 (转发到调度器 sys_app_launch)
 * @param app_id APP 唯一 ID
 */
esp_err_t sys_ui_switch_to(const char *app_id);

/**
 * @brief APP 在 on_start 中调用: 把自己的根 screen 对象交给系统显示
 * @note  screen 必须是 lv_obj_create(NULL) 创建的屏幕对象
 */
void sys_ui_set_root(lv_obj_t *screen);

/**
 * @brief 顶层弹窗: 无论当前哪个 APP 在前台, content 永远压在所有内容之上
 * @param content 任意 LVGL 控件容器 (调用方负责填充样式与回调)
 * @return 系统生成的 overlay 背景对象; 关闭时调用 sys_ui_overlay_dismiss()
 * @note  弹窗互斥: 已有弹窗时后一次调用返回 ESP_ERR_INVALID_STATE
 */
esp_err_t sys_ui_overlay_show(lv_obj_t *content, lv_obj_t **out_bg);

/** 关闭当前顶层弹窗 */
void sys_ui_overlay_dismiss(void);

/** 当前是否有顶层弹窗 */
bool sys_ui_overlay_active(void);

#ifdef __cplusplus
}
#endif

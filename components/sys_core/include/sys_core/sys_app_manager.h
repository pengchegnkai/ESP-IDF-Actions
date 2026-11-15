/**
 * @file sys_app_manager.h
 * @brief APP 调度器 —— 注册/启动/挂起应用, 维护全局前台应用
 */
#pragma once

#include <stdint.h>
#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册一个 APP (不启动)
 * @note  注册后立即回调 on_init; 若 on_init 失败, 返回错误且 APP 处于 STOPPED
 */
esp_err_t sys_app_register(sys_app_t *app);

/**
 * @brief 启动/切换到指定 APP
 *
 * 调度顺序:
 *   1. 目标 APP 未 init 则先 on_init
 *   2. 若当前有前台 APP 且不是目标 -> on_suspend (状态 ACTIVE -> SUSPENDED)
 *   3. 目标 APP on_start, 状态置 ACTIVE
 *   4. 广播 SYS_EVENT_APP_STATE
 *
 * @param app_id 在 sys_app_t.id 中定义的唯一 ID
 */
esp_err_t sys_app_launch(const char *app_id);

/** @brief 获取当前前台 APP (无则为 NULL) */
sys_app_t *sys_app_current(void);

/** @brief 按 ID 查找已注册的 APP (未注册返回 NULL) */
sys_app_t *sys_app_find(const char *app_id);

#ifdef __cplusplus
}
#endif

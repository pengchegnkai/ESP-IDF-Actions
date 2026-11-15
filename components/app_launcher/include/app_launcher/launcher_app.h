/**
 * @file launcher_app.h
 * @brief Launcher (桌面) APP —— 系统默认启动的应用, 也是新 APP 的参考模板
 */
#pragma once

#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Launcher 的唯一 ID, 系统启动流程用它拉起 */
#define LAUNCHER_APP_ID "launcher"

/** 获取 Launcher 的 APP 对象 (供 main 注册) */
sys_app_t *launcher_app_get(void);

#ifdef __cplusplus
}
#endif

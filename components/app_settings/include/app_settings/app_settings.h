/**
 * @file app_settings.h
 * @brief 设置 APP —— 第二个演示 APP, 用于验证多 APP 调度与返回栈
 */
#pragma once

#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_APP_ID "settings"

sys_app_t *settings_app_get(void);

#ifdef __cplusplus
}
#endif

/**
 * @file splash_app.h
 * @brief 启动画面 APP —— 品牌 + 进度条, 数秒后自动切入 Launcher
 */
#pragma once

#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPLASH_APP_ID "splash"

sys_app_t *splash_app_get(void);

#ifdef __cplusplus
}
#endif

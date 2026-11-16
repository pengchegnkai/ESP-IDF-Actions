/**
 * @file splash_app.h
 * @brief 启动画面 APP —— 品牌 + 进度条, 掩盖上电白屏,
 *        系统初始化完成后自动切入 Launcher
 */
#pragma once

#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPLASH_APP_ID "splash"

sys_app_t *splash_app_get(void);

/**
 * @brief 系统初始化全部完成后由 app_main 调用:
 *        启动画面进度条充满, 短暂停留后切入桌面
 */
void splash_set_ready(void);

#ifdef __cplusplus
}
#endif

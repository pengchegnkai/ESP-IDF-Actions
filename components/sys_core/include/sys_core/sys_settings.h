/**
 * @file sys_settings.h
 * @brief 用户设置 —— NVS 持久化 (亮度/音量/音效), 设置项跨重启保留
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化设置子系统 (必须在 NVS init 之后、UI 之前调用)
 * @note  从 NVS 读回持久化值; 首次运行使用默认值
 */
esp_err_t sys_settings_init(void);

/** 屏幕亮度 0-100 (默认 100) */
esp_err_t sys_settings_get_brightness(int *v);
esp_err_t sys_settings_set_brightness(int v);

/** 音频音量 0-100 (默认 60) */
esp_err_t sys_settings_get_volume(int *v);
esp_err_t sys_settings_set_volume(int v);

/** 按键音效开关 (默认开) */
esp_err_t sys_settings_get_sfx(bool *v);
esp_err_t sys_settings_set_sfx(bool v);

#ifdef __cplusplus
}
#endif

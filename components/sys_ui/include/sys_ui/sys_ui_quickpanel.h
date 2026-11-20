/**
 * @file sys_ui_quickpanel.h
 * @brief 控制中心 (Vela 快捷面板) —— 全局顶层, 任意屏幕顶部下拉唤出
 *
 * 内容: WiFi / ESP-NOW / 音效 三个快捷开关 + 亮度/音量滑块。
 * 收起: 点暗色背景 或 面板底部手柄上滑。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示控制中心
 * @return ESP_OK / ESP_ERR_INVALID_STATE (已显示) / ESP_FAIL (未取到显示锁)
 */
esp_err_t sys_ui_quickpanel_show(void);

/** 收起控制中心 (幂等) */
void sys_ui_quickpanel_dismiss(void);

/** 当前是否显示 */
bool sys_ui_quickpanel_active(void);

#ifdef __cplusplus
}
#endif

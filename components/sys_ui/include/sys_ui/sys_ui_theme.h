/**
 * @file sys_ui_theme.h
 * @brief Vela 风格主题 —— 深空暗色底 + 大圆角卡片 + 蓝色强调
 *
 * 所有 APP 统一走这里的色板与辅助函数, 保持桌面/设置/网络一致观感。
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 色板 (Vela 风格) ---- */
#define SYS_UI_THEME_BG         0x0B0F1A  /* 屏幕底: 深空黑 */
#define SYS_UI_THEME_CARD       0x161E2E  /* 卡片底 */
#define SYS_UI_THEME_CARD_ALT   0x1D2739  /* 卡片高亮/按下 */
#define SYS_UI_THEME_BORDER     0x2A3550  /* 卡片描边 */
#define SYS_UI_THEME_PRIMARY    0x4C8DFF  /* 主色蓝 */
#define SYS_UI_THEME_ACCENT     0x35C4F5  /* 强调青 (时钟/数值) */
#define SYS_UI_THEME_TEXT       0xF2F5FA  /* 主文字 */
#define SYS_UI_THEME_TEXT_DIM   0x8A94A6  /* 次级文字 */
#define SYS_UI_THEME_OK         0x34C759  /* 成功/在线 */
#define SYS_UI_THEME_WARN       0xFF9F0A  /* 进行中/警告 */
#define SYS_UI_THEME_ERR        0xFF453A  /* 错误 */
#define SYS_UI_THEME_DISABLED   0x3A4152  /* 不可用 */

#define SYS_UI_THEME_RADIUS     24        /* 卡片圆角 */

/** 屏幕底色 (调用方建好 lv_obj_create(NULL) 后使用) */
void sys_ui_theme_screen(lv_obj_t *scr);

/** 标准卡片样式 (底色/圆角/内边距/描边), 卡片上不再设边框 */
void sys_ui_theme_card(lv_obj_t *card);

/** 不可用卡片 (半透明压暗) */
void sys_ui_theme_card_dim(lv_obj_t *card);

/**
 * @brief 主题字体: 中文模式取 TTF 中文字体(任意 px), 英文模式回退最近的
 *        内置 Montserrat 字号 (≥44 用 48px, 依此类推)
 */
const lv_font_t *sys_ui_theme_font(uint32_t px);

#ifdef __cplusplus
}
#endif

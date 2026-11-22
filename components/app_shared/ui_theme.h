/* ui_theme.h — 三 APP 共享深色主题 ([纯逻辑·可仿真])
 *
 * 设计语言: 深蓝黑底 + 卡片式布局 + 各 APP 独立强调色,
 * 圆角 10px / 标题 22px / 正文 16px, 与 480x800 启动器风格衔接.
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 调色板 */
#define UI_BG          0x141A24 /* 页面背景 */
#define UI_CARD        0x1E2836 /* 卡片/列表项 */
#define UI_CARD_PRESS  0x2A3750 /* 按下态 */
#define UI_TEXT        0xE8EEF7 /* 主文本 */
#define UI_TEXT_DIM    0x8FA3BF /* 次文本 */
#define UI_ACCENT_FM   0x3D9BFF /* FileManager 蓝 */
#define UI_ACCENT_HK   0xB45AFF /* HackerKit 紫 */
#define UI_ACCENT_SK   0x35C4C4 /* SecurityKey 青 */
#define UI_DANGER      0xE05252 /* 危险操作 */

/* 半径/间距 */
#define UI_RADIUS      10
#define UI_PAD         12

/* 常用样式: 页面背景 */
static inline void ui_theme_page(lv_obj_t *page)
{
    lv_obj_set_style_bg_color(page, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(page, UI_PAD, 0);
}

/* 常用样式: 标题 (app 名, 强调色) */
static inline lv_obj_t *ui_theme_title(lv_obj_t *parent, const char *text, uint32_t accent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(accent), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return label;
}

/* 常用样式: 卡片容器 */
static inline lv_obj_t *ui_theme_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_RADIUS, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, UI_PAD, 0);
    return card;
}

#ifdef __cplusplus
}
#endif

#endif /* UI_THEME_H */

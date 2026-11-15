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

/* ========================= 语言与字库 (i18n) ========================= */

/** 系统语言: 由 SD 卡字库可用性决定 (有字库=中文, 无=英文) */
typedef enum {
    SYS_UI_LANG_EN = 0,
    SYS_UI_LANG_ZH,
} sys_ui_lang_t;

/** 界面文案 ID (中/英对照表见 sys_ui_font.c) */
typedef enum {
    SYS_UI_STR_APP_LAUNCHER = 0,  /* APP 名: 桌面 */
    SYS_UI_STR_APP_SETTINGS,      /* APP 名: 设置 */
    SYS_UI_STR_SYSTEM_BASE_TITLE, /* Launcher 标题栏 */
    SYS_UI_STR_LAST_EVENT,        /* Launcher 事件提示前缀 */
    SYS_UI_STR_BTN_DEMO,          /* Launcher 弹窗演示按钮 */
    SYS_UI_STR_OVERLAY_TITLE,     /* 系统弹窗标题 */
    SYS_UI_STR_OVERLAY_BODY,      /* 系统弹窗正文 */
    SYS_UI_STR_OVERLAY_OK,        /* 系统弹窗确认按钮 */
    SYS_UI_STR_BTN_BACK,          /* 返回按钮 */
    SYS_UI_STR_USB_IN,            /* USB 已插入 */
    SYS_UI_STR_USB_OUT,           /* USB 已拔出 */
    SYS_UI_STR_SWITCH_LABEL,      /* 演示开关 */
    SYS_UI_STR_COUNT
} sys_ui_str_id_t;

/** 中文字体默认字号 (px) */
#define SYS_UI_FONT_DEFAULT_PX 24

/**
 * @brief 从 SD 卡字库目录 (挂载点下的 fonts 目录, 第一个 ttf 文件) 加载中文字库并确定界面语言
 * @note  必须在 sys_ui_init() 之后、任何 APP 构建 UI 之前调用;
 *        无 SD 卡/无字库时返回错误码, 系统降级为英文界面
 */
esp_err_t sys_ui_font_init(void);

/** 当前界面语言 */
sys_ui_lang_t sys_ui_lang(void);

/** 按语言取界面文案 */
const char *sys_ui_str(sys_ui_str_id_t id);

/**
 * @brief 按字号获取中文字体 (懒创建, 内部缓存; 共用同一 TTF 数据缓冲)
 * @return 字体对象; 英文模式或字库缺失时返回 NULL (调用方不设置字体即可)
 */
lv_font_t *sys_ui_cjk_font(uint32_t px);

#ifdef __cplusplus
}
#endif

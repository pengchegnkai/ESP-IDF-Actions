/**
 * @file sys_app.h
 * @brief APP 接口规范 —— 所有应用必须遵循的生命周期契约
 *
 * 生命周期: register -> init -> (start <-> suspend) -> stop
 *   on_init    注册后调用一次, 做一次性初始化(分配资源/创建 UI 根对象)
 *   on_start   进入前台, 恢复/显示自己的屏幕
 *   on_suspend 退到后台, 必须停止耗时工作, 保留现场
 *   on_stop    退出, 释放 on_init 中分配的全部资源
 */
#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

/** APP 状态机 */
typedef enum {
    SYS_APP_STATE_STOPPED   = 0,  /* 已注册, 未 init */
    SYS_APP_STATE_READY     = 1,  /* 已 init, 未启动 */
    SYS_APP_STATE_ACTIVE    = 2,  /* 前台运行 */
    SYS_APP_STATE_SUSPENDED = 3,  /* 后台挂起 */
} sys_app_state_t;

/** APP 对象: 每个应用静态定义一个, 通过 sys_app_register() 注册 */
typedef struct sys_app sys_app_t;

struct sys_app {
    /* ---- 标识 ---- */
    const char    *id;     /**< 唯一 ID, 如 "launcher"、"settings" */
    const char    *name;   /**< 显示名称 */
    const uint8_t  icon;   /**< 图标资源 ID (0 = 无图标) */

    /* ---- 运行时 ---- */
    void          *priv;   /**< APP 私有数据, 框架不解释 */
    sys_app_state_t state; /**< 由调度器维护, APP 不要手改 */

    /* ---- 生命周期回调 ---- */
    esp_err_t (*on_init)(sys_app_t *app);
    esp_err_t (*on_start)(sys_app_t *app);
    void      (*on_suspend)(sys_app_t *app);
    void      (*on_stop)(sys_app_t *app);

    /* ---- 事件总线回调 (可选): 收到自己订阅的系统事件时被调用 ---- */
    void      (*on_event)(sys_app_t *app, int32_t event_id, void *event_data);
};

/** 注册表容量: 最多同时注册的 APP 数 */
#define SYS_APP_MAX  8

#ifdef __cplusplus
}
#endif

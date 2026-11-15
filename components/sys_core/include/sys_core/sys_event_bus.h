/**
 * @file sys_event_bus.h
 * @brief 全局事件总线 —— 基于 esp_event 的系统级发布/订阅封装
 */
#pragma once

#include <esp_err.h>
#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 事件总线名称 (esp_event base) */
ESP_EVENT_DECLARE_BASE(SYS_EVENT);

/** 系统事件枚举: 系统状态变化统一走这里广播 */
typedef enum {
    SYS_EVENT_TIME_TICK   = 0,  /**< 每秒心跳, data: NULL */
    SYS_EVENT_USB_IN      = 1,  /**< USB 插入 */
    SYS_EVENT_USB_OUT     = 2,  /**< USB 拔出 */
    SYS_EVENT_NET_UP      = 3,  /**< 网络连接建立 */
    SYS_EVENT_NET_DOWN    = 4,  /**< 网络断开 */
    SYS_EVENT_BATTERY_LOW = 5,  /**< 低电量, data: (int)百分比 */
    SYS_EVENT_APP_STATE   = 6,  /**< APP 状态变化, data: sys_app_state_evt_t* */
} sys_event_id_t;

/** SYS_EVENT_APP_STATE 携带的数据 */
typedef struct {
    const char     *app_id;
    sys_app_state_t new_state;
} sys_app_state_evt_t;

typedef void (*sys_event_handler_t)(int32_t event_id, void *event_data, void *user_ctx);

/**
 * @brief 初始化事件总线(内部创建 esp_event loop)
 * @note  必须最先调用, 早于其他子系统
 */
esp_err_t sys_event_bus_init(void);

/**
 * @brief 订阅事件
 * @param handler 回调运行在事件循环任务上下文, 不要阻塞
 * @param out_handle 返回句柄, 传给 sys_event_unsubscribe 使用
 */
esp_err_t sys_event_subscribe(int32_t event_id,
                              sys_event_handler_t handler,
                              void *user_ctx,
                              esp_event_handler_t *out_handle);

/** 取消订阅 */
esp_err_t sys_event_unsubscribe(int32_t event_id, esp_event_handler_t handle);

/**
 * @brief 发布事件 (异步, 拷贝 event_data 浅层指针)
 * @note  指针指向的数据在回调完成前必须保持有效; 无数据传 NULL
 */
esp_err_t sys_event_publish(int32_t event_id, void *event_data);

#ifdef __cplusplus
}
#endif

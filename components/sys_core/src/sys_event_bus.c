/**
 * @file sys_event_bus.c
 * @brief 全局事件总线实现 —— 封装 esp_event, 提供系统级发布/订阅
 */
#include <string.h>
#include "sys_core/sys_event_bus.h"

ESP_EVENT_DEFINE_BASE(SYS_EVENT);

static esp_event_loop_handle_t s_loop;

esp_err_t sys_event_bus_init(void)
{
    if (s_loop) {
        return ESP_OK; /* 已初始化 */
    }

    esp_event_loop_args_t args = {
        .queue_size      = 16,
        .task_name       = "sys_evt",
        .task_priority   = 5,
        .task_stack_size = 4096,
        .task_core_id    = tskNO_AFFINITY,
    };
    esp_err_t err = esp_event_loop_create(&args, &s_loop);
    return err;
}

esp_err_t sys_event_subscribe(int32_t event_id,
                              sys_event_handler_t handler,
                              void *user_ctx,
                              esp_event_handler_t *out_handle)
{
    if (!s_loop || !handler || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 包一层适配器: 把 esp_event 的 base/id 剥掉, 只暴露系统事件语义 */
    esp_err_t err = esp_event_handler_register_with(s_loop, SYS_EVENT, event_id,
                                                    (esp_event_handler_t)handler, user_ctx);
    if (err == ESP_OK) {
        *out_handle = (esp_event_handler_t)handler;
    }
    return err;
}

esp_err_t sys_event_unsubscribe(int32_t event_id, esp_event_handler_t handle)
{
    if (!s_loop || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_event_handler_unregister_with(s_loop, SYS_EVENT, event_id, handle);
}

esp_err_t sys_event_publish(int32_t event_id, void *event_data)
{
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_post_to(s_loop, SYS_EVENT, event_id, event_data, sizeof(void *), 0);
}

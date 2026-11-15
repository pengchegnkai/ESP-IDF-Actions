/**
 * @file sys_event_bus.c
 * @brief 全局事件总线实现 —— esp_event 之上的轻量适配层
 *
 * 每个订阅占用一个静态槽位, esp_event 注册的总是同一个 4 参数适配函数
 * bus_dispatch, 槽位指针作为 handler_arg 传入, 从而避免函数指针强转。
 */
#include <string.h>
#include <stdbool.h>
#include "sys_core/sys_event_bus.h"

ESP_EVENT_DEFINE_BASE(SYS_EVENT);

#define SYS_EVENT_MAX_SUBS 16

typedef struct {
    bool                used;
    sys_event_handler_t cb;
    void               *user_ctx;
} sub_slot_t;

static esp_event_loop_handle_t s_loop;
static sub_slot_t s_slots[SYS_EVENT_MAX_SUBS];

/* esp_event -> 订阅者 的适配器 (签名与 esp_event_handler_t 完全一致) */
static void bus_dispatch(void *handler_arg, esp_event_base_t base,
                         int32_t event_id, void *event_data)
{
    (void)base;
    sub_slot_t *slot = handler_arg;
    if (slot && slot->used && slot->cb) {
        slot->cb(event_id, event_data, slot->user_ctx);
    }
}

esp_err_t sys_event_bus_init(void)
{
    if (s_loop) {
        return ESP_OK;
    }
    esp_event_loop_args_t args = {
        .queue_size      = 16,
        .task_name       = "sys_evt",
        .task_priority   = 5,
        .task_stack_size = 4096,
        .task_core_id    = tskNO_AFFINITY,
    };
    return esp_event_loop_create(&args, &s_loop);
}

esp_err_t sys_event_subscribe(int32_t event_id,
                              sys_event_handler_t handler,
                              void *user_ctx,
                              sys_event_handle_t *out_handle)
{
    if (!s_loop || !handler || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    sub_slot_t *slot = NULL;
    for (int i = 0; i < SYS_EVENT_MAX_SUBS; i++) {
        if (!s_slots[i].used) {
            slot = &s_slots[i];
            break;
        }
    }
    if (!slot) {
        return ESP_ERR_NO_MEM;
    }

    slot->cb       = handler;
    slot->user_ctx = user_ctx;
    slot->used     = true;

    esp_err_t err = esp_event_handler_register_with(s_loop, SYS_EVENT, event_id,
                                                    bus_dispatch, slot);
    if (err != ESP_OK) {
        slot->used = false;
        return err;
    }
    *out_handle = slot;
    return ESP_OK;
}

esp_err_t sys_event_unsubscribe(sys_event_handle_t handle)
{
    if (!s_loop || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    sub_slot_t *slot = (sub_slot_t *)handle;
    if (!slot->used) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 解注册时按 handler_arg 匹配, event_id 用 ESP_EVENT_ANY_ID 通配 */
    esp_err_t err = esp_event_handler_unregister_with(s_loop, SYS_EVENT,
                                                      ESP_EVENT_ANY_ID, bus_dispatch);
    slot->used = false;
    return err;
}

esp_err_t sys_event_publish(int32_t event_id, void *event_data)
{
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_post_to(s_loop, SYS_EVENT, event_id,
                             &event_data, sizeof(void *), 0);
}

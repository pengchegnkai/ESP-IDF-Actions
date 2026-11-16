/**
 * @file sys_event_bus.c
 * @brief 全局事件总线实现 —— esp_event 之上的轻量适配层
 *
 * 每个 event_id 占一个组: esp_event 只注册一次 bus_dispatch (组指针作
 * handler_arg, 避免函数指针强转); 组内可挂多个订阅者 —— 否则同一事件的
 * 第二次订阅会触发 esp_event "handler already registered, overwriting",
 * 把第一个订阅者静默顶掉 (launcher/settings 都听 TIME_TICK 时必现)。
 */
#include <string.h>
#include <stdbool.h>
#include "sys_core/sys_event_bus.h"

ESP_EVENT_DEFINE_BASE(SYS_EVENT);

#define SYS_EVENT_MAX_GROUPS       8
#define SYS_EVENT_MAX_SUBS_PER_GRP 4

typedef struct {
    bool                used;
    sys_event_handler_t cb;
    void               *user_ctx;
} sub_slot_t;

typedef struct {
    int32_t    event_id;
    bool       in_use;
    sub_slot_t subs[SYS_EVENT_MAX_SUBS_PER_GRP];
} event_group_t;

static esp_event_loop_handle_t s_loop;
static event_group_t s_groups[SYS_EVENT_MAX_GROUPS];

/* esp_event -> 订阅者组 的适配器 (签名与 esp_event_handler_t 完全一致) */
static void bus_dispatch(void *handler_arg, esp_event_base_t base,
                         int32_t event_id, void *event_data)
{
    (void)base;
    event_group_t *g = handler_arg;
    for (int i = 0; i < SYS_EVENT_MAX_SUBS_PER_GRP; i++) {
        sub_slot_t *s = &g->subs[i];
        if (s->used && s->cb) {
            s->cb(event_id, event_data, s->user_ctx);
        }
    }
}

static event_group_t *find_group(int32_t event_id)
{
    for (int i = 0; i < SYS_EVENT_MAX_GROUPS; i++) {
        if (s_groups[i].in_use && s_groups[i].event_id == event_id) {
            return &s_groups[i];
        }
    }
    return NULL;
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

    event_group_t *g = find_group(event_id);
    if (!g) {
        /* 新事件: 先占一个组, esp_event 处理器只注册这一次 */
        for (int i = 0; i < SYS_EVENT_MAX_GROUPS; i++) {
            if (!s_groups[i].in_use) {
                g = &s_groups[i];
                break;
            }
        }
        if (!g) {
            return ESP_ERR_NO_MEM;
        }
        memset(g, 0, sizeof(*g));
        g->event_id = event_id;
        g->in_use   = true;

        esp_err_t err = esp_event_handler_register_with(s_loop, SYS_EVENT, event_id,
                                                        bus_dispatch, g);
        if (err != ESP_OK) {
            g->in_use = false;
            return err;
        }
    }

    for (int i = 0; i < SYS_EVENT_MAX_SUBS_PER_GRP; i++) {
        if (!g->subs[i].used) {
            g->subs[i].cb       = handler;
            g->subs[i].user_ctx = user_ctx;
            g->subs[i].used     = true;
            *out_handle = &g->subs[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t sys_event_unsubscribe(sys_event_handle_t handle)
{
    if (!s_loop || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    sub_slot_t *slot = (sub_slot_t *)handle;

    /* 按指针范围定位所属组 (槽位都住在 s_groups 内); 组处理器保持注册,
     * 空组 dispatch 是空操作, 省去再注册/解注册的分支 */
    bool found = false;
    for (int i = 0; i < SYS_EVENT_MAX_GROUPS; i++) {
        sub_slot_t *first = &s_groups[i].subs[0];
        sub_slot_t *last  = &s_groups[i].subs[SYS_EVENT_MAX_SUBS_PER_GRP];
        if (slot >= first && slot < last) {
            found = true;
            break;
        }
    }
    if (!found || !slot->used) {
        return ESP_ERR_INVALID_STATE;
    }
    slot->used = false;
    return ESP_OK;
}

esp_err_t sys_event_publish(int32_t event_id, void *event_data)
{
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_post_to(s_loop, SYS_EVENT, event_id,
                             &event_data, sizeof(void *), 0);
}

/* ---------- 系统事件源辅助 ---------- */
void sys_event_report_usb(bool inserted)
{
    sys_event_publish(inserted ? SYS_EVENT_USB_IN : SYS_EVENT_USB_OUT, NULL);
}

void sys_event_report_net(bool connected)
{
    sys_event_publish(connected ? SYS_EVENT_NET_UP : SYS_EVENT_NET_DOWN, NULL);
}

void sys_event_report_battery(int percent)
{
    sys_event_publish(SYS_EVENT_BATTERY_LOW, percent <= 10 ? &percent : NULL);
}

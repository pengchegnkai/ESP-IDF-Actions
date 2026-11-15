/**
 * @file sys_time_service.c
 * @brief 系统时间服务实现 —— esp_timer 周期回调发布 TIME_TICK 事件
 */
#include "sys_core/sys_time_service.h"
#include "sys_core/sys_event_bus.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "sys_time";

static esp_timer_handle_t s_tick_timer;
static volatile uint32_t  s_uptime_sec;

static void tick_cb(void *arg)
{
    (void)arg;
    s_uptime_sec++;
    sys_event_publish(SYS_EVENT_TIME_TICK, NULL);
}

esp_err_t sys_time_service_init(void)
{
    if (s_tick_timer) {
        return ESP_OK;
    }
    const esp_timer_create_args_t args = {
        .callback = tick_cb,
        .arg      = NULL,
        .name     = "sys_tick",
    };
    esp_err_t err = esp_timer_create(&args, &s_tick_timer);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_timer_start_periodic(s_tick_timer, 1000 * 1000); /* 1s */
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "time service started (1Hz)");
    }
    return err;
}

uint32_t sys_time_uptime_sec(void)
{
    return s_uptime_sec;
}

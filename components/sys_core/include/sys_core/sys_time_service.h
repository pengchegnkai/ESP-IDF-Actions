/**
 * @file sys_time_service.h
 * @brief 系统时间服务 —— 1Hz 心跳, 驱动 SYS_EVENT_TIME_TICK
 */
#pragma once

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 启动 1 秒心跳定时器 (幂等) */
esp_err_t sys_time_service_init(void);

/** 开机以来的秒数 (由心跳维护, 低功耗场景下可能被冻结) */
uint32_t sys_time_uptime_sec(void);

#ifdef __cplusplus
}
#endif

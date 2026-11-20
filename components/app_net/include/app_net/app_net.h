#pragma once

#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_APP_ID "net"

/** 网络中心 APP —— WiFi / ESP-NOW / 蓝牙 / 时间同步 */
sys_app_t *net_app_get(void);

#ifdef __cplusplus
}
#endif

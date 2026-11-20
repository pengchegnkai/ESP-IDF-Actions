#pragma once

#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_APP_ID "watch"

/** 表盘 APP (Vela 风格主屏/主页) */
sys_app_t *watch_app_get(void);

#ifdef __cplusplus
}
#endif

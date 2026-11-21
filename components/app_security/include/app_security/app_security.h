/**
 * @file app_security.h
 * @brief 安全密钥 APP —— FIDO2 手动激活 + USB 状态 + CTAP2 最简处理
 *
 * 手动激活模式: 开机默认 OFF, 用户在本 APP 内拨开关后才响应 USB CTAP2 请求;
 * 激活/停用不持久化 (重启归 OFF)。签名走 secure_link (v1 stub, 阶段二实现)。
 */
#pragma once

#include "sys_core/sys_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SECURITY_APP_ID "security"

sys_app_t *security_app_get(void);

#ifdef __cplusplus
}
#endif

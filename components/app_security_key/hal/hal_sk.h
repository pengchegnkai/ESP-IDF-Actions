/* hal_sk.h — App_SecurityKey hal 层对外接口 ([硬件相关·需真机验证])
 *
 * 被 app_security_key.cpp (Brookesia App 子类) 调用;
 * 实现在 hal/hal_c6_crypto.c 与 hal/hal_usb_ctap.c, 只进固件构建.
 * 红线②: 纯软件安全层, 绝不烧写 eFuse. */
#ifndef HAL_SK_H
#define HAL_SK_H

#include <stddef.h>
#include <stdint.h>

#include "sk_crypto_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- hal_c6_crypto.c: C6 签名通道 (AES-GCM/PSK over SDIO peer_data) ---------- */

/* 初始化 C6 安全通道 (幂等). 成功后可用 sk_c6_backend() 取得后端. */
int hal_c6_crypto_init(void);

/* C6 运算后端 (generate_keypair_id / sign / derive_session_key) */
const sk_crypto_backend_t *hal_c6_backend(void);

/* 通道健康查询 (UI 显示 "C6 已连接" 用) */
bool hal_c6_link_ok(void);

/* ---------- hal_usb_ctap.c: TinyUSB CTAP over HID ---------- */

/* USB (CTAP-HID) 子系统挂载/卸载 — 由 usb_arbiter 状态切换回调驱动:
 * FIDO2 获得 USB → attach; 失去 (BadUSB 抢占/关闭开关) → detach. */
int hal_usb_ctap_attach(void);
void hal_usb_ctap_detach(void);

/* 设置 CTAP2 完整消息到达回调 (消息已去 CTAP-HID 帧, buf 指向内部缓冲,
 * 回调运行在 TinyUSB 任务上下文 — 调用方负责转到 LvLockGuard 再碰 UI). */
typedef void (*hal_usb_ctap_msg_cb_t)(uint32_t cid, const uint8_t *msg, size_t len, void *user);
int hal_usb_ctap_set_msg_cb(hal_usb_ctap_msg_cb_t cb, void *user);

/* 发送一条 CTAP2 应答 (内部自动切 CTAP-HID 帧). */
int hal_usb_ctap_send(uint32_t cid, const uint8_t *msg, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SK_H */

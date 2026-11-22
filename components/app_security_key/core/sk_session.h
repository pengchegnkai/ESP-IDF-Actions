/* sk_session.h — FIDO2 会话状态机 ([纯逻辑·可仿真])
 *
 * 手动激活模式:
 *   Idle → (手动激活: APP 前台且 usb_arbiter 已授予 FIDO2)
 *        → Active → (电脑 CTAP2 请求) → Auth_pending
 *        → (触摸确认 allow/deny) → Signing → (签名完成) → Done → Active
 *
 * 规则:
 *   - 未激活时收到的 CTAP2 请求一律回 Not_Allowed (Ctap2_Sw_NotAllowed);
 *   - BadUsb 抢占 (arbiter Busy, FIDO2 失去 USB) → 自动回 Idle (挂起语义,
 *     进行中的请求作废, 语义等同取消);
 *   - 签名运算经 sk_crypto_backend_t vtable, 本层不做任何密码学.
 */
#ifndef SK_SESSION_H
#define SK_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sk_crypto_backend.h"
#include "ctap2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SK_ST_IDLE = 0,       /* USB 未激活 (默认) */
    SK_ST_ACTIVE,         /* 前台 + arbiter 已授予, 可接收 CTAP2 请求 */
    SK_ST_AUTH_PENDING,   /* 请求待用户触摸确认 (全局弹窗展示中) */
    SK_ST_SIGNING,        /* 用户确认, 正在经 backend 签名 */
    SK_ST_DONE,           /* 本条请求完成 (状态保持 Active, 可接下一条) */
} sk_state_t;

/* 一次会话处理的请求上下文 (session → 调用方回传弹窗/签名所需信息) */
typedef struct {
    uint8_t cmd;                       /* CTAP2 命令字 */
    uint8_t payload[CTAPHID_MSG_MAX];
    size_t  payload_len;
    int     sign_count;                /* 本次请求中签名调用次数 (测试断言用) */
} sk_req_t;

typedef struct {
    /* [纯逻辑] 运算后端 (pc_sim: fake; 真机: C6 通道), 可为 NULL (无签名能力) */
    const sk_crypto_backend_t *backend;
    void *backend_ctx;
} sk_session_cfg_t;

typedef struct {
    sk_state_t state;
    sk_session_cfg_t cfg;
    sk_req_t req;            /* 当前待处理请求 */
    int sign_count_total;    /* 累计签名调用次数 (测试断言用) */
} sk_session_t;

/* 初始化 (cfg 可在激活前随时注入). */
void sk_session_init(sk_session_t *s, const sk_session_cfg_t *cfg);

/* 手动激活: 仅当 APP 前台 (caller 保证) 且 arbiter 已授予 (caller 已
 * usb_arb_request(FIDO2) 成功) 时调用. 成功 → Active. */
bool sk_session_activate(sk_session_t *s);

/* 关闭开关/离开前台 → 回 Idle (释放 arbiter 由调用方负责). */
void sk_session_deactivate(sk_session_t *s);

/* BadUsb 抢占 (arbiter Busy, 失去 USB) → 自动回 Idle, 挂起语义. */
void sk_session_preempt(sk_session_t *s);

/* 收到一条 CTAP2 请求 (完整消息, 已去帧). 返回 CTAP2 状态码:
 *   - 未激活: Ctap2_Sw_NotAllowed (请求被忽略);
 *   - 激活: 进 Auth_pending, 弹出确认 (弹窗由调用方经 ui/sk_popup 驱动),
 *     返回 Ctap2_Sw_UserActionPending 表示等触摸; */
uint8_t sk_session_on_request(sk_session_t *s, uint32_t cid, const uint8_t *msg, size_t len);

/* 触摸结果: allow=true → Signing (经 backend 签名) → Done;
 * false → 回 Active (请求以 UserActionTimeout 拒绝). */
uint8_t sk_session_confirm(sk_session_t *s, bool allow);

/* 状态与上次结果查询 */
sk_state_t sk_session_state(const sk_session_t *s);
bool sk_session_is_done(const sk_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* SK_SESSION_H */

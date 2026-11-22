/* sk_session.c — FIDO2 会话状态机实现 ([纯逻辑·可仿真]) */
#include "sk_session.h"

#include <string.h>

void sk_session_init(sk_session_t *s, const sk_session_cfg_t *cfg)
{
    memset(s, 0, sizeof(*s));
    if (cfg) s->cfg = *cfg;
    s->state = SK_ST_IDLE;
}

sk_state_t sk_session_state(const sk_session_t *s)
{
    return s->state;
}

bool sk_session_is_done(const sk_session_t *s)
{
    return s->state == SK_ST_DONE;
}

bool sk_session_activate(sk_session_t *s)
{
    /* 手动激活门禁: 只有 Idle 可激活 (Auth_pending 期间失焦等由调用方先取消) */
    if (s->state != SK_ST_IDLE) return false;
    s->state = SK_ST_ACTIVE;
    return true;
}

void sk_session_deactivate(sk_session_t *s)
{
    s->state = SK_ST_IDLE;
    memset(&s->req, 0, sizeof(s->req));
}

void sk_session_preempt(sk_session_t *s)
{
    /* 挂起语义: BadUSB 抢占, USB 已不是我们的 — 进行中的请求作废回 Idle */
    sk_session_deactivate(s);
}

uint8_t sk_session_on_request(sk_session_t *s, uint32_t cid, const uint8_t *msg, size_t len)
{
    (void)cid;
    if (s->state != SK_ST_ACTIVE && s->state != SK_ST_DONE) {
        /* 未激活/正忙: 一律拒绝, 请求不进入弹窗流程 (DONE 保持激活可接下一条) */
        return Ctap2_Sw_NotAllowed;
    }
    if (!msg || len == 0 || len > CTAPHID_MSG_MAX) return Ctap2_Sw_InvalidParam;
    s->req.cmd = msg[0];
    s->req.payload_len = (len > 1) ? (len - 1) : 0;
    if (s->req.payload_len) memcpy(s->req.payload, msg + 1, s->req.payload_len);
    s->req.sign_count = 0;
    s->state = SK_ST_AUTH_PENDING;
    return Ctap2_Sw_UserActionPending;
}

uint8_t sk_session_confirm(sk_session_t *s, bool allow)
{
    if (s->state != SK_ST_AUTH_PENDING) return Ctap2_Sw_InvalidCmd;

    if (!allow) {
        /* 用户点拒绝: 回 Active, 请求以 UserActionTimeout 语义拒绝 */
        s->state = SK_ST_ACTIVE;
        return Ctap2_Sw_UserActionTimeout;
    }

    s->state = SK_ST_SIGNING;
    uint8_t status = Ctap2_Sw_Ok;

    if (s->req.cmd == CTAP2_CMD_MAKE_CREDENTIAL || s->req.cmd == CTAP2_CMD_GET_ASSERTION) {
        /* 需硬件签名: MakeCredential 额外生成密钥对, 之后对 clientDataHash 签名 */
        const sk_crypto_backend_t *be = s->cfg.backend;
        uint8_t hash[64] = { 0 };
        uint8_t sig[SK_SIG_MAX];
        size_t sig_len = sizeof(sig);
        uint8_t id[SK_KEY_ID_LEN] = { 0 }; /* GetAssertion: 无新密钥生成, 以全零 id 签名 */
        size_t id_len = sizeof(id);

        if (!be) {
            status = Ctap2_Sw_UnsupportedAlg;
        } else {
            if (s->req.cmd == CTAP2_CMD_MAKE_CREDENTIAL) {
                if (be->generate_keypair_id(s->cfg.backend_ctx, id, &id_len) != 0) {
                    status = Ctap2_Sw_Other;
                }
            }
            if (status == Ctap2_Sw_Ok) {
                /* hash 输入 = clientDataHash (不足 32B 时零填充; 协议层只做变换调度) */
                size_t hl = s->req.payload_len < sizeof(hash) ? s->req.payload_len : sizeof(hash);
                if (hl) memcpy(hash, s->req.payload, hl);
                if (be->sign(s->cfg.backend_ctx, id, id_len,
                             hash, sizeof(hash), sig, &sig_len) != 0) {
                    status = Ctap2_Sw_Other;
                } else {
                    s->req.sign_count++;
                    s->sign_count_total++;
                }
            }
        }
    }
    /* GetInfo / ClientPin 不签名, 直接完成 */

    s->state = SK_ST_DONE;   /* 语义: 完成本条, 会话保持激活可接下一条 */
    return status;
}

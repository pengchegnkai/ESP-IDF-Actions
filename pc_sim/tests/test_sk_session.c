/* test_sk_session.c — FIDO2 会话状态机测试 */
#include "test_framework.h"
#include "sk_session.h"
#include "sk_crypto_host.h"

#include <string.h>

static sk_session_t s;
static sk_session_cfg_t cfg;

/* 每个用例独立初始化 (带确定性 fake backend) */
static void fresh(void)
{
    cfg.backend = sk_crypto_host_backend();
    cfg.backend_ctx = NULL;
    sk_session_init(&s, &cfg);
}

/* 手工 CBOR 组 GetAssertion 请求 (rpId + hash) */
static size_t mk_getassert(uint8_t *buf, size_t cap)
{
    ctap2_getassert_req_t r = {
        .rp_id = "example.com",
        .client_data_hash = (const uint8_t *)"\x01\x02\x03\x04",
        .client_data_hash_len = 4,
    };
    int n = ctap2_getassert_req_encode(&r, buf + 1, cap - 1);
    if (n < 0) return 0;
    buf[0] = CTAP2_CMD_GET_ASSERTION;
    return (size_t)n + 1;
}

TEST(sk_session_initial_idle)
{
    fresh();
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_IDLE);
    CHECK(!sk_session_is_done(&s));
}

TEST(sk_session_inactive_request_denied)
{
    fresh();
    uint8_t msg[256];
    CHECK(mk_getassert(msg, sizeof(msg)) > 0);
    /* 未激活 (Idle): CTAP 请求一律 Not_Allowed, 不进弹窗流程 */
    CHECK_EQ_INT(sk_session_on_request(&s, 1, msg, sizeof(msg)), Ctap2_Sw_NotAllowed);
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_IDLE);
}

TEST(sk_session_activate_allow_request)
{
    fresh();
    uint8_t msg[256];
    size_t n = mk_getassert(msg, sizeof(msg));
    CHECK(sk_session_activate(&s));
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_ACTIVE);
    /* 激活后: 请求 → UserActionPending (等触摸) */
    CHECK_EQ_INT(sk_session_on_request(&s, 1, msg, n), Ctap2_Sw_UserActionPending);
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_AUTH_PENDING);
    /* 重复请求被拒 (正忙) */
    CHECK_EQ_INT(sk_session_on_request(&s, 2, msg, n), Ctap2_Sw_NotAllowed);
}

TEST(sk_session_confirm_signs_and_done)
{
    fresh();
    uint8_t msg[256];
    size_t n = mk_getassert(msg, sizeof(msg));
    CHECK(sk_session_activate(&s));
    CHECK_EQ_INT(sk_session_on_request(&s, 1, msg, n), Ctap2_Sw_UserActionPending);

    CHECK_EQ_INT(sk_session_confirm(&s, true), Ctap2_Sw_Ok); /* 触摸确认 → 签名成功 */
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_DONE);
    CHECK(sk_session_is_done(&s));
    CHECK_EQ_INT(s.sign_count_total, 1); /* fake backend 被调 1 次 */
    CHECK_EQ_INT(s.req.sign_count, 1);
}

TEST(sk_session_deny_returns_active)
{
    fresh();
    uint8_t msg[256];
    size_t n = mk_getassert(msg, sizeof(msg));
    sk_session_activate(&s);
    sk_session_on_request(&s, 1, msg, n);
    /* 拒绝 → 回 Active, 无签名 */
    CHECK_EQ_INT(sk_session_confirm(&s, false), Ctap2_Sw_UserActionTimeout);
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_ACTIVE);
    CHECK_EQ_INT(s.sign_count_total, 0);
}

TEST(sk_session_badusb_preempt_suspends)
{
    fresh();
    uint8_t msg[256];
    size_t n = mk_getassert(msg, sizeof(msg));
    sk_session_activate(&s);
    sk_session_on_request(&s, 1, msg, n); /* Auth_pending 中 */
    /* BadUSB 抢占: 自动回 Idle (挂起语义), 后续请求 Not_Allowed */
    sk_session_preempt(&s);
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_IDLE);
    CHECK_EQ_INT(sk_session_on_request(&s, 1, msg, n), Ctap2_Sw_NotAllowed);
    /* 抢占后确认无效 */
    CHECK_EQ_INT(sk_session_confirm(&s, true), Ctap2_Sw_InvalidCmd);
}

TEST(sk_session_deactivate_releases)
{
    fresh();
    sk_session_activate(&s);
    sk_session_deactivate(&s);
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_IDLE);
    /* 关闭后请求拒绝 */
    uint8_t msg[256];
    size_t n = mk_getassert(msg, sizeof(msg));
    CHECK_EQ_INT(sk_session_on_request(&s, 1, msg, n), Ctap2_Sw_NotAllowed);
}

TEST(sk_session_getinfo_no_sign)
{
    fresh();
    CHECK(sk_session_activate(&s));
    const uint8_t msg[] = { CTAP2_CMD_GET_INFO };
    CHECK_EQ_INT(sk_session_on_request(&s, 1, msg, sizeof(msg)), Ctap2_Sw_UserActionPending);
    CHECK_EQ_INT(sk_session_confirm(&s, true), Ctap2_Sw_Ok); /* 不需签名 */
    CHECK_EQ_INT(s.sign_count_total, 0);
}

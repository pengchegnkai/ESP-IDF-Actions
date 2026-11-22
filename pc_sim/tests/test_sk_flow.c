/* test_sk_flow.c — 端到端流程测试: fake 电脑 → CTAP-HID 帧 → demux →
 * session → fake 加密 → 应答帧. 断言状态码与签名调用次数. */
#include "test_framework.h"
#include "ctap2.h"
#include "sk_session.h"
#include "sk_crypto_host.h"

#include <string.h>

static sk_session_t s;
static sk_session_cfg_t cfg;

/* fake 电脑: 组一条完整 CTAP-HID 消息帧 (report 流) */
static size_t host_frame(uint32_t cid, uint8_t cmd, const uint8_t *pl, size_t len,
                         uint8_t *out, size_t cap)
{
    return (size_t)ctap2_frame_msg(cid, cmd, pl, len, out, cap);
}

TEST(sk_flow_end_to_end_makecred)
{
    static const uint8_t hash[32] = { 0 }; /* clientDataHash (确定性) */
    ctap2_makecred_req_t req = {
        .rp_id = "example.com",
        .rp_name = "Example",
        .user_id = (const uint8_t *)"\x42",
        .user_id_len = 1,
        .user_name = "alice",
        .client_data_hash = hash,
        .client_data_hash_len = 32,
    };
    uint8_t cbor[512], framed[512];
    int n = ctap2_makecred_req_encode(&req, cbor, sizeof(cbor));
    CHECK(n > 0);

    cfg.backend = sk_crypto_host_backend();
    cfg.backend_ctx = NULL;
    sk_session_init(&s, &cfg);
    CHECK(sk_session_activate(&s));

    /* fake 电脑发 MakeCredential (载荷 = CBOR, 2 个 report 覆盖) */
    size_t flen = host_frame(0x11223344, CTAP2_CMD_MAKE_CREDENTIAL, cbor, (size_t)n,
                             framed, sizeof(framed));
    CHECK(flen > 0);

    /* USB 收 → demux → 完整 CTAP2 消息 */
    ctap2_msg_t msg;
    ctap2_demux_t demux;
    ctap2_demux_init(&demux);
    int rc = 0;
    for (size_t off = 0; off < flen && rc == 0; off += CTAPHID_REPORT_SZ) {
        rc = ctap2_demux_push(&demux, framed + off, CTAPHID_REPORT_SZ, &msg);
    }
    CHECK_EQ_INT(rc, 1);
    CHECK_EQ_INT(msg.cmd, CTAP2_CMD_MAKE_CREDENTIAL);

    /* session 收 → 触摸确认 → 签名成功 (session 契约: msg[0] = CTAP2 命令字) */
    uint8_t full[CTAPHID_MSG_MAX];
    full[0] = (uint8_t)CTAP2_CMD_MAKE_CREDENTIAL;
    memcpy(full + 1, msg.payload, msg.len);
    CHECK_EQ_INT(sk_session_on_request(&s, msg.cid, full, msg.len + 1),
                 Ctap2_Sw_UserActionPending);
    CHECK_EQ_INT(sk_session_confirm(&s, true), Ctap2_Sw_Ok);
    CHECK_EQ_INT(s.sign_count_total, 1); /* 签名恰好被调 1 次 */

    /* 设备回状态帧 → fake 电脑侧 demux 还原 (状态码 OK) */
    uint8_t resp = Ctap2_Sw_Ok;
    uint8_t rfr[64];
    CHECK_EQ_INT(host_frame(msg.cid, CTAP2_CMD_MAKE_CREDENTIAL, &resp, 1, rfr, sizeof(rfr)), 64);
    ctap2_msg_t rmsg;
    ctap2_demux_init(&demux);
    CHECK_EQ_INT(ctap2_demux_push(&demux, rfr, 64, &rmsg), 1);
    CHECK_EQ_INT(rmsg.payload[0], Ctap2_Sw_Ok);

    /* 完成后可接下一条 (Done → Active 语义) */
    CHECK_EQ_INT(sk_session_on_request(&s, 1, rfr + 7, 1), Ctap2_Sw_UserActionPending);
}

TEST(sk_flow_inactive_host_gets_not_allowed)
{
    cfg.backend = sk_crypto_host_backend();
    sk_session_init(&s, &cfg);
    const uint8_t msg[] = { CTAP2_CMD_GET_ASSERTION };
    /* 电脑在未激活时发请求 → Not_Allowed 状态帧回给电脑 */
    CHECK_EQ_INT(sk_session_on_request(&s, 7, msg, sizeof(msg)), Ctap2_Sw_NotAllowed);
}

TEST(sk_flow_fake_signature_deterministic)
{
    const sk_crypto_backend_t *be = sk_crypto_host_backend();
    uint8_t id[SK_KEY_ID_LEN];
    size_t id_len = sizeof(id);
    CHECK_EQ_INT(be->generate_keypair_id(NULL, id, &id_len), 0);

    const uint8_t hash[32] = { 0 };
    uint8_t sig1[SK_SIG_MAX], sig2[SK_SIG_MAX];
    size_t l1 = sizeof(sig1), l2 = sizeof(sig2);
    CHECK_EQ_INT(be->sign(NULL, id, id_len, hash, sizeof(hash), sig1, &l1), 0);
    CHECK_EQ_INT(be->sign(NULL, id, id_len, hash, sizeof(hash), sig2, &l2), 0);
    CHECK_EQ_INT(l1, SK_SIG_MAX);
    /* 确定性: 同输入 → 同签名 (断言可复现) */
    CHECK_EQ_INT(memcmp(sig1, sig2, SK_SIG_MAX), 0);
    /* 固定变换: hash 前 32 字节 XOR 0xA5 */
    CHECK_EQ_INT(sig1[0], 0xA5);
    CHECK_EQ_INT(sig1[31], (uint8_t)(0 ^ 0xA5));
    /* 会话密钥派生也确定 */
    uint8_t k1[16], k2[16];
    CHECK_EQ_INT(be->derive_session_key(NULL, hash, 32, k1, sizeof(k1)), 0);
    CHECK_EQ_INT(be->derive_session_key(NULL, hash, 32, k2, sizeof(k2)), 0);
    CHECK_EQ_INT(memcmp(k1, k2, sizeof(k1)), 0);
}

TEST(sk_flow_badusb_preempt_mid_flow)
{
    const uint8_t hash[32] = { 1 };
    ctap2_getassert_req_t req = {
        .rp_id = "example.com",
        .client_data_hash = hash,
        .client_data_hash_len = 32,
    };
    uint8_t cbor[128], framed[128];
    int n = ctap2_getassert_req_encode(&req, cbor, sizeof(cbor));
    CHECK(n > 0);

    cfg.backend = sk_crypto_host_backend();
    sk_session_init(&s, &cfg);
    sk_session_activate(&s);

    /* 请求已到 (等触摸) 时 BadUSB 抢占 → 挂起回 Idle */
    size_t flen = host_frame(0x55, CTAP2_CMD_GET_ASSERTION, cbor, (size_t)n,
                             framed, sizeof(framed));
    ctap2_msg_t msg;
    ctap2_demux_t demux;
    ctap2_demux_init(&demux);
    int rc = 0;
    for (size_t off = 0; off < flen && rc == 0; off += CTAPHID_REPORT_SZ) {
        rc = ctap2_demux_push(&demux, framed + off, CTAPHID_REPORT_SZ, &msg);
    }
    CHECK_EQ_INT(rc, 1);
    CHECK_EQ_INT(sk_session_on_request(&s, msg.cid, msg.payload, msg.len),
                 Ctap2_Sw_UserActionPending);

    sk_session_preempt(&s); /* arbiter 被.BadUSB 抢走 */
    CHECK_EQ_INT(sk_session_state(&s), SK_ST_IDLE);
    CHECK_EQ_INT(s.sign_count_total, 0); /* 未签名 */
    /* 后续请求拒绝 */
    CHECK_EQ_INT(sk_session_on_request(&s, 1, msg.payload, msg.len), Ctap2_Sw_NotAllowed);
}

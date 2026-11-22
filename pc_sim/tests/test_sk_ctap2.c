/* test_sk_ctap2.c — CTAP2 编解码往返 + CTAP-HID 分包测试 */
#include "test_framework.h"
#include "ctap2.h"

#include <string.h>

/* ---------------- 状态码表 ---------------- */
TEST(ctap2_status_table)
{
    CHECK_EQ_INT(Ctap2_Sw_Ok, 0x00);
    CHECK_EQ_INT(Ctap2_Sw_InvalidCmd, 0x01);
    CHECK_EQ_INT(Ctap2_Sw_NotAllowed, 0x1B);
    CHECK_EQ_INT(Ctap2_Sw_UserActionTimeout, 0x27);
    CHECK_STREQ(ctap2_status_str(Ctap2_Sw_Ok), "OK");
    CHECK_STREQ(ctap2_status_str(0xEE), "UNKNOWN");
}

/* ---------------- GetInfo ---------------- */
TEST(ctap2_getinfo_roundtrip)
{
    uint8_t req[8];
    CHECK_EQ_INT(ctap2_getinfo_req_encode(req, sizeof(req)), 0); /* 空请求 */

    /* 手工组应答: map{1:["FIDO_2_1_PRE"], 5:16B aaguid} */
    static const uint8_t aaguid[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
    uint8_t resp[128];
    uint8_t *p = resp;
    p += cbor_head(5, 2, p);
    p += cbor_head(0, 1, p);
    p += cbor_head(4, 1, p);
    p += cbor_put_text("FIDO_2_1_PRE", p);
    p += cbor_head(0, 5, p);
    p += cbor_put_bytes(aaguid, sizeof(aaguid), p);

    ctap2_getinfo_resp_t gi;
    CHECK_EQ_INT(ctap2_getinfo_resp_decode(resp, p - resp, &gi), 0);
    CHECK_EQ_INT(gi.version_count, 1);
    /* 解码为零拷贝指针 (不 NUL 结尾) — 用定长 memcmp 断言 */
    CHECK(memcmp(gi.versions[0], "FIDO_2_1_PRE", 12) == 0);
    CHECK(gi.aaguid != NULL);
    CHECK_EQ_INT(memcmp(gi.aaguid, aaguid, 16), 0);
    /* 错 major type → 拒绝 */
    CHECK_EQ_INT(ctap2_getinfo_resp_decode(req, 1, &gi), -1);
}

/* ---------------- MakeCredential ---------------- */
TEST(ctap2_makecred_roundtrip)
{
    ctap2_makecred_req_t req = {
        .rp_id = "example.com",
        .rp_name = "Example",
        .user_id = (const uint8_t *)"\x01\x02\x03",
        .user_id_len = 3,
        .user_name = "alice",
        .client_data_hash = (const uint8_t *)"\xAA\xBB",
        .client_data_hash_len = 2,
    };
    uint8_t buf[512];
    int n = ctap2_makecred_req_encode(&req, buf, sizeof(buf));
    CHECK(n > 0);

    ctap2_makecred_req_t out;
    CHECK_EQ_INT(ctap2_makecred_req_decode(buf, (size_t)n, &out), 0);
    CHECK(memcmp(out.rp_id, "example.com", 11) == 0);
    CHECK(memcmp(out.rp_name, "Example", 7) == 0);
    CHECK(memcmp(out.user_name, "alice", 5) == 0);
    CHECK_EQ_INT(out.user_id_len, 3);
    CHECK_EQ_INT(out.user_id[0], 0x01);
    CHECK_EQ_INT(out.client_data_hash_len, 2);
    CHECK_EQ_INT(out.client_data_hash[0], 0xAA);
    /* 截断 → 拒绝 */
    CHECK_EQ_INT(ctap2_makecred_req_decode(buf, (size_t)n / 2, &out), -1);
    /* 缺参 → 拒绝 */
    CHECK_EQ_INT(ctap2_makecred_req_encode(NULL, buf, sizeof(buf)), -1);
}

/* ---------------- GetAssertion ---------------- */
TEST(ctap2_getassert_roundtrip)
{
    ctap2_getassert_req_t req = {
        .rp_id = "site.org",
        .client_data_hash = (const uint8_t *)"\x11\x22\x33\x44",
        .client_data_hash_len = 4,
    };
    uint8_t buf[128];
    int n = ctap2_getassert_req_encode(&req, buf, sizeof(buf));
    CHECK(n > 0);

    ctap2_getassert_req_t out;
    CHECK_EQ_INT(ctap2_getassert_req_decode(buf, (size_t)n, &out), 0);
    CHECK(memcmp(out.rp_id, "site.org", 8) == 0);
    CHECK_EQ_INT(out.client_data_hash_len, 4);
    CHECK_EQ_INT(out.client_data_hash[2], 0x33);
}

/* ---------------- ClientPin (预留) ---------------- */
TEST(ctap2_clientpin_reserved)
{
    uint8_t buf[16];
    CHECK_EQ_INT(ctap2_clientpin_req_encode(0x01, NULL, 0, buf, sizeof(buf)), 1);
    CHECK_EQ_INT(buf[0], 0x01); /* subCmd = getPinRetries */
    CHECK_EQ_INT(ctap2_clientpin_req_encode(0x04, (const uint8_t *)"\x01", 1,
                                            buf, sizeof(buf)), 2);
    CHECK_EQ_INT(ctap2_clientpin_req_encode(0x01, NULL, 0, NULL, 0), -1);
}

/* ---------------- CTAP-HID 分包 ---------------- */
/* 组一条最小 init 帧 (单 report) 辅助 */
static size_t mk_frame(uint32_t cid, uint8_t cmd, const uint8_t *pl, size_t len,
                       uint8_t *out, size_t cap)
{
    return (size_t)ctap2_frame_msg(cid, cmd, pl, len, out, cap);
}

TEST(ctaphid_single_frame)
{
    ctap2_demux_t d; ctap2_msg_t msg;
    ctap2_demux_init(&d);
    const uint8_t pl[] = { CTAP2_CMD_GET_INFO }; /* 空载荷 GetInfo */
    uint8_t fr[64];
    CHECK_EQ_INT(mk_frame(0x01020304, CTAP2_CMD_GET_INFO, pl, 1, fr, sizeof(fr)), 64);

    CHECK_EQ_INT(ctap2_demux_push(&d, fr, sizeof(fr), &msg), 1);
    CHECK_EQ_INT(msg.cid, 0x01020304);
    CHECK_EQ_INT(msg.cmd, CTAP2_CMD_GET_INFO);
    CHECK_EQ_INT(msg.len, 1);
    CHECK_EQ_INT(msg.payload[0], CTAP2_CMD_GET_INFO);
}

TEST(ctaphid_half_and_sticky)
{
    ctap2_demux_t d; ctap2_msg_t msg;
    ctap2_demux_init(&d);

    /* 半包: 一条跨 2 个 report 的消息 (载荷 > 57B) */
    uint8_t payload[100];
    for (int i = 0; i < 100; i++) payload[i] = (uint8_t)i;
    uint8_t fr[128];
    CHECK_EQ_INT(mk_frame(0x01, CTAP2_CMD_MAKE_CREDENTIAL, payload, sizeof(payload), fr, sizeof(fr)), 128);

    CHECK_EQ_INT(ctap2_demux_push(&d, fr, 64, &msg), 0);          /* 只有前半 */
    CHECK_EQ_INT(ctap2_demux_push(&d, fr + 64, 64, &msg), 1);     /* 后半 → 完成 */
    CHECK_EQ_INT(msg.len, 100);
    CHECK_EQ_INT(memcmp(msg.payload, payload, 100), 0);

    /* 粘包: 一次 push 两条完整单 report 消息 */
    ctap2_demux_init(&d);
    const uint8_t pl[] = { 0x77 };
    uint8_t two[128];
    CHECK_EQ_INT(mk_frame(0x0A, CTAP2_CMD_GET_INFO, pl, 1, two, sizeof(two)), 64);
    CHECK_EQ_INT(mk_frame(0x0B, CTAP2_CMD_GET_INFO, pl, 1, two + 64, sizeof(two) - 64), 64);
    CHECK_EQ_INT(ctap2_demux_push(&d, two, sizeof(two), &msg), 1);
    CHECK_EQ_INT(msg.cid, 0x0A);
    CHECK_EQ_INT(ctap2_demux_push(&d, NULL, 0, &msg), 1);         /* carry 里取第二条 */
    CHECK_EQ_INT(msg.cid, 0x0B);
}

TEST(ctaphid_bad_bcn_and_seq)
{
    ctap2_demux_t d; ctap2_msg_t msg;
    ctap2_demux_init(&d);

    /* 错 bcnt: 超过上限 → 错误并复位 */
    uint8_t bad[64] = { 0 };
    bad[4] = 0x80 | CTAP2_CMD_GET_INFO;
    bad[5] = 0xFF; bad[6] = 0xFF;                 /* bcnt = 65535 */
    CHECK_EQ_INT(ctap2_demux_push(&d, bad, 64, &msg), -1);

    /* 错 seq: continuation 序号不连续 → 错误 */
    ctap2_demux_init(&d);
    uint8_t payload[100];
    for (int i = 0; i < 100; i++) payload[i] = (uint8_t)i;
    uint8_t fr[128];
    CHECK_EQ_INT(mk_frame(0x01, CTAP2_CMD_GET_ASSERTION, payload, sizeof(payload), fr, sizeof(fr)), 128);
    fr[64 + 4] = 0x09;                            /* 应为 seq 0 */
    CHECK_EQ_INT(ctap2_demux_push(&d, fr, 128, &msg), -1);

    /* 错 CID: continuation 与 init CID 不一致 → 错误 */
    ctap2_demux_init(&d);
    CHECK_EQ_INT(mk_frame(0x01, CTAP2_CMD_GET_ASSERTION, payload, sizeof(payload), fr, sizeof(fr)), 128);
    fr[64] = 0x99;                                /* CID 首字节改掉 */
    CHECK_EQ_INT(ctap2_demux_push(&d, fr, 128, &msg), -1);

    /* 复位后可重新同步 */
    const uint8_t pl[] = { 0x04 };
    uint8_t ok[64];
    CHECK_EQ_INT(mk_frame(0x02, CTAP2_CMD_GET_INFO, pl, 1, ok, sizeof(ok)), 64);
    CHECK_EQ_INT(ctap2_demux_push(&d, ok, 64, &msg), 1);
}

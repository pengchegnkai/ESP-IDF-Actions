/* ctap2.h — CTAP2 消息层 ([纯逻辑·可仿真])
 *
 * App_SecurityKey FIDO2 协议层 (借鉴 pico-fido 的 CTAP2 协议栈分层思路,
 * 行为遵循 FIDO Client-to-Authenticator Protocol v2.1 规范).
 *
 * 职责 (只做协议层, 密钥运算全部经 sk_crypto_backend.h 接口, 不碰 mbedtls):
 *   - CTAP2 命令编解码: authenticatorGetInfo / authenticatorClientPin(预留) /
 *     authenticatorMakeCredential / authenticatorGetAssertion
 *   - 状态码表 (Ctap2_Sw_*, FIDO2 spec §6.2 错误码)
 *   - CTAP-HID 字节流分包 (report 64B, init 帧 header: CID/0x80|cmd/bcnt,
 *     continuation 帧: CID/seq) — 粘包/半包/错序恢复
 *
 * 无 ESP-IDF 依赖, pc_sim 覆盖.
 */
#ifndef SK_CTAP2_H
#define SK_CTAP2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- CTAP2 命令字 (FIDO2 spec §6.1) ---------------- */
#define CTAP2_CMD_MAKE_CREDENTIAL 0x01
#define CTAP2_CMD_GET_ASSERTION   0x02
#define CTAP2_CMD_GET_INFO        0x04
#define CTAP2_CMD_CLIENT_PIN      0x06

/* ---------------- 状态码表 (FIDO2 spec §6.2) ----------------
 * 命名沿用 pico-fido 的 Ctap2_Sw_* 风格. */
typedef enum {
    Ctap2_Sw_Ok                = 0x00,
    Ctap2_Sw_InvalidCmd        = 0x01,
    Ctap2_Sw_InvalidParam      = 0x02,
    Ctap2_Sw_InvalidParamLen   = 0x03,
    Ctap2_Sw_InvalidChannel    = 0x04,
    Ctap2_Sw_InvalidSeq        = 0x05,
    Ctap2_Sw_Timeout           = 0x06,
    Ctap2_Sw_ChannelBusy       = 0x07,
    Ctap2_Sw_LockRequired      = 0x08,
    Ctap2_Sw_OperationDenied   = 0x0A,
    Ctap2_Sw_UnsupportedAlg    = 0x0D,
    Ctap2_Sw_UserActionPending = 0x14,
    Ctap2_Sw_OperationPending  = 0x15,
    Ctap2_Sw_NoPIN             = 0x16,
    Ctap2_Sw_PINInvalid        = 0x17,
    Ctap2_Sw_PINAuthInvalid    = 0x18,
    Ctap2_Sw_PINRequired       = 0x19,
    Ctap2_Sw_NotAllowed        = 0x1B, /* 本实现: 会话未激活/被禁止时的统一拒绝码 */
    Ctap2_Sw_UserActionTimeout = 0x27, /* 用户取消/超时 (弹窗点拒绝) */
    Ctap2_Sw_Other             = 0x2F,
} ctap2_status_t;

/* 状态码的可读名 (测试/日志用), 未知码返回 "UNKNOWN" */
const char *ctap2_status_str(uint8_t status);

/* ---------------- CTAP-HID 分包 ---------------- */
#define CTAPHID_REPORT_SZ   64   /* FIDO HID 报告定长 */
#define CTAPHID_MSG_MAX   1400   /* 单条 CTAP2 消息上限 (含 CBOR 载荷) */

/* 解析出的一条完整 CTAP2 消息 (cmd 已去掉 0x80 位; payload 指向内部缓冲) */
typedef struct {
    uint32_t cid;
    uint8_t cmd;
    const uint8_t *payload;
    size_t len;
} ctap2_msg_t;

/* CTAP-HID 字节流分包器: 以 64B report 为单位喂入 (每次 push 可含多个
 * 连续 report = 粘包; 一条消息跨多个 report = 半包), 拼出完整 CTAP2 消息.
 * [纯逻辑·可仿真] */
typedef struct {
    int      state;      /* 内部状态: 0=idle 1=等 continuation */
    uint8_t  hdr[6];     /* init 帧头累积 (半包头) */
    size_t   hdr_have;
    uint32_t cid;
    uint8_t  cmd;
    uint16_t bcnt;
    size_t   have;
    int      next_seq;
    uint8_t  buf[CTAPHID_MSG_MAX];
    uint8_t  carry[CTAPHID_REPORT_SZ * 8]; /* 未消费输入 (粘包时暂存) */
    size_t   carry_len;
} ctap2_demux_t;

void ctap2_demux_init(ctap2_demux_t *d);

/* �入原始字节流 (report 对齐, 可多 report 连发). 返回:
 *   1  *out 填好一条完整消息 (继续 push 可取下一条)
 *   0  还需更多数据
 *  -1  协议错误 (错 bcnt/错 seq/CID 错序) — 内部已复位, 可重新同步 */
int ctap2_demux_push(ctap2_demux_t *d, const uint8_t *data, size_t len, ctap2_msg_t *out);

/* 反向: 把一条 CTAP2 消息切成 CTAP-HID init+continuation 帧 (报告流),
 * 返回写入 out 的长度; 超过 cap 返回负值 (所需容量). [纯逻辑·可仿真] */
int ctap2_frame_msg(uint32_t cid, uint8_t cmd, const uint8_t *payload, size_t len,
                    uint8_t *out, size_t cap);

/* ---------------- CBOR (最小子集, 仅本协议所需) ----------------
 * 借鉴 pico-fido 的极简 CBOR 编解码, 不引第三方库. */
size_t cbor_head(uint8_t major, uint64_t val, uint8_t *out);        /* head 编码 */
size_t cbor_put_text(const char *s, uint8_t *out);                   /* text string */
size_t cbor_put_bytes(const uint8_t *b, size_t n, uint8_t *out);     /* byte string */

/* ---------------- authenticatorGetInfo ---------------- */
/* 请求: 空载荷 */
int ctap2_getinfo_req_encode(uint8_t *out, size_t cap);

/* 应答解码 (设备端不产生, 测试往返用): 解出版本串与 AAGUID.
 * 各指针借用 out 缓冲 (原 CBOR), 不拷贝. */
typedef struct {
    const char *versions[4];   /* "FIDO_2_1_PRE" 等, 借用指针 */
    size_t version_count;
    const uint8_t *aaguid;     /* 16 字节, 借用指针 */
} ctap2_getinfo_resp_t;
int ctap2_getinfo_resp_decode(const uint8_t *in, size_t len, ctap2_getinfo_resp_t *out);

/* ---------------- authenticatorMakeCredential ---------------- */
typedef struct {
    const char    *rp_id;         /* rp.id */
    const char    *rp_name;
    const uint8_t *user_id;       /* user.id (bytes) */
    size_t         user_id_len;
    const char    *user_name;
    const uint8_t *client_data_hash; /* 32B */
    size_t         client_data_hash_len;
} ctap2_makecred_req_t;

/* 编码: map{1:rp, 2:user, 3:pubKeyCredParams(ES256), 4:clientDataHash} */
int ctap2_makecred_req_encode(const ctap2_makecred_req_t *r, uint8_t *out, size_t cap);
/* 解码 (roundtrip 用), 字符串/字节借用 out 缓冲 */
int ctap2_makecred_req_decode(const uint8_t *in, size_t len, ctap2_makecred_req_t *r);

/* ---------------- authenticatorGetAssertion ---------------- */
typedef struct {
    const char    *rp_id;
    const uint8_t *client_data_hash;
    size_t         client_data_hash_len;
} ctap2_getassert_req_t;

int ctap2_getassert_req_encode(const ctap2_getassert_req_t *r, uint8_t *out, size_t cap);
int ctap2_getassert_req_decode(const uint8_t *in, size_t len, ctap2_getassert_req_t *r);

/* ---------------- authenticatorClientPin (预留, 未实现流程) ----------------
 * v1 只编解码子命令头 (pinProtocol=1, subCmd), 完整 PIN/uvAuth 流程预留. */
int ctap2_clientpin_req_encode(uint8_t sub_cmd, const uint8_t *payload, size_t len,
                               uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* SK_CTAP2_H */

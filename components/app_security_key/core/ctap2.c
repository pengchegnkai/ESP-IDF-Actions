/* ctap2.c — CTAP2 消息层实现 ([纯逻辑·可仿真])
 * 借鉴 pico-fido CTAP2 协议栈; 密钥运算不经本文件 (见 sk_crypto_backend.h). */
#include "ctap2.h"

#include <string.h>

/* ---------------- 状态码 ---------------- */
const char *ctap2_status_str(uint8_t status)
{
    switch (status) {
    case Ctap2_Sw_Ok:                return "OK";
    case Ctap2_Sw_InvalidCmd:        return "INVALID_CMD";
    case Ctap2_Sw_InvalidParam:      return "INVALID_PARAM";
    case Ctap2_Sw_InvalidParamLen:   return "INVALID_PARAM_LEN";
    case Ctap2_Sw_InvalidChannel:    return "INVALID_CHANNEL";
    case Ctap2_Sw_InvalidSeq:        return "INVALID_SEQ";
    case Ctap2_Sw_Timeout:           return "TIMEOUT";
    case Ctap2_Sw_ChannelBusy:       return "CHANNEL_BUSY";
    case Ctap2_Sw_LockRequired:      return "LOCK_REQUIRED";
    case Ctap2_Sw_OperationDenied:   return "OPERATION_DENIED";
    case Ctap2_Sw_UnsupportedAlg:    return "UNSUPPORTED_ALG";
    case Ctap2_Sw_UserActionPending: return "USER_ACTION_PENDING";
    case Ctap2_Sw_OperationPending:  return "OPERATION_PENDING";
    case Ctap2_Sw_NoPIN:             return "NO_PIN";
    case Ctap2_Sw_PINInvalid:        return "PIN_INVALID";
    case Ctap2_Sw_PINAuthInvalid:    return "PIN_AUTH_INVALID";
    case Ctap2_Sw_PINRequired:       return "PIN_REQUIRED";
    case Ctap2_Sw_NotAllowed:        return "NOT_ALLOWED";
    case Ctap2_Sw_UserActionTimeout: return "USER_ACTION_TIMEOUT";
    case Ctap2_Sw_Other:             return "OTHER";
    default:                         return "UNKNOWN";
    }
}

/* ---------------- 最小 CBOR (major type RFC 8949) ---------------- */
size_t cbor_head(uint8_t major, uint64_t val, uint8_t *out)
{
    size_t i = 0;
    out[0] = (uint8_t)(major << 5);
    if (val < 24) {
        out[0] |= (uint8_t)val;
        i = 1;
    } else if (val <= 0xFF) {
        out[0] |= 24; out[1] = (uint8_t)val; i = 2;
    } else if (val <= 0xFFFF) {
        out[0] |= 25; out[1] = (uint8_t)(val >> 8); out[2] = (uint8_t)val; i = 3;
    } else {
        out[0] |= 26;
        for (int k = 3; k >= 0; k--) out[1 + (3 - k)] = (uint8_t)(val >> (8 * k));
        i = 5;
    }
    return i;
}

static size_t cbor_read_head(const uint8_t *in, size_t len, uint8_t *major, uint64_t *val, size_t *adv)
{
    if (len < 1) return 1;
    uint8_t b = in[0];
    *major = b >> 5;
    uint8_t info = b & 0x1F;
    size_t need = 0;
    if (info < 24)      { *val = info; need = 1; }
    else if (info == 24){ *val = in[1]; need = 2; }
    else if (info == 25){ *val = (uint64_t)in[1] << 8 | in[2]; need = 3; }
    else if (info == 26){ *val = 0; for (int k = 0; k < 4; k++) *val = (*val << 8) | in[1 + k]; need = 5; }
    else return 1;
    if (len < need) return 1;
    *adv = need;
    return 0;
}

size_t cbor_put_text(const char *s, uint8_t *out)
{
    size_t n = strlen(s);
    size_t h = cbor_head(3, n, out);
    memcpy(out + h, s, n);
    return h + n;
}

size_t cbor_put_bytes(const uint8_t *b, size_t n, uint8_t *out)
{
    size_t h = cbor_head(2, n, out);
    memcpy(out + h, b, n);
    return h + n;
}

/* 读一个 text/byte string 的内容指针与长度 */
static int cbor_read_string(const uint8_t *in, size_t len, uint8_t want_major,
                            const uint8_t **data, size_t *dlen, size_t *adv)
{
    uint8_t major; uint64_t v; size_t h;
    if (cbor_read_head(in, len, &major, &v, &h) != 0 || major != want_major) return -1;
    if (len - h < v) return -1;
    if (data) *data = in + h;
    if (dlen) *dlen = (size_t)v;
    *adv = h + (size_t)v;
    return 0;
}

/* 跳过一个任意 CBOR item (text/bytes/整数/数组/地图, 数组与地图递归) */
static int cbor_skip_item(const uint8_t *in, size_t len, size_t *adv)
{
    uint8_t major; uint64_t v; size_t h;
    if (cbor_read_head(in, len, &major, &v, &h) != 0) return -1;
    if (major == 2 || major == 3) {
        if (len - h < v) return -1;
        *adv = h + (size_t)v;
        return 0;
    }
    if (major == 4 || major == 5) {
        /* 地图每条目 = key + value 两个 item; 数组每元素 = 一个 item */
        size_t off = h;
        uint64_t items = (major == 5) ? 2 * v : v;
        for (uint64_t k = 0; k < items; k++) {
            size_t a;
            if (cbor_skip_item(in + off, len - off, &a) != 0) return -1;
            off += a;
        }
        *adv = off;
        return 0;
    }
    *adv = h; /* 整数 (major 0/1) 头即全部 */
    return 0;
}

/* ---------------- GetInfo ---------------- */
int ctap2_getinfo_req_encode(uint8_t *out, size_t cap)
{
    (void)cap;
    if (!out) return -1;
    return 0; /* authenticatorGetInfo 请求无参数 */
}

int ctap2_getinfo_resp_decode(const uint8_t *in, size_t len, ctap2_getinfo_resp_t *out)
{
    memset(out, 0, sizeof(*out));
    uint8_t major; uint64_t n; size_t h;
    if (cbor_read_head(in, len, &major, &n, &h) != 0 || major != 5) return -1;
    in += h; len -= h;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t key; size_t adv; uint64_t cnt;
        if (cbor_read_head(in, len, &major, &key, &adv) != 0 || major != 0) return -1;
        in += adv; len -= adv;
        if (key == 1) { /* versions: array of text */
            if (cbor_read_head(in, len, &major, &cnt, &adv) != 0 || major != 4) return -1;
            in += adv; len -= adv;
            for (uint64_t k = 0; k < cnt && out->version_count < 4; k++) {
                size_t a;
                if (cbor_read_string(in, len, 3, (const uint8_t **)&out->versions[out->version_count],
                                     NULL, &a) != 0) return -1;
                out->version_count++;
                in += a; len -= a;
            }
        } else if (key == 5) { /* aaguid: bytes(16) */
            size_t a;
            if (cbor_read_string(in, len, 2, &out->aaguid, NULL, &a) != 0) return -1;
            in += a; len -= a;
        } else { /* 跳过未知键 (前向兼容) */
            size_t a;
            uint64_t v;
            if (cbor_read_string(in, len, 2, NULL, NULL, &a) != 0 &&
                cbor_read_string(in, len, 3, NULL, NULL, &a) != 0 &&
                cbor_read_head(in, len, &major, &v, &a) != 0) return -1;
            in += a; len -= a;
        }
        if (len == 0 && i + 1 < n) return -1;
    }
    return 0;
}

/* ---------------- MakeCredential ---------------- */
int ctap2_makecred_req_encode(const ctap2_makecred_req_t *r, uint8_t *out, size_t cap)
{
    uint8_t *p = out;
    if (!r || !out || !r->rp_id || !r->user_id || !r->client_data_hash) return -1;
    if (cap < 512) return -1;
    p += cbor_head(5, 4, p);                          /* map(4) */
    p += cbor_head(0, 1, p);                          /* 1: rp */
    p += cbor_head(5, 2, p);                          /*   map(2) */
    p += cbor_put_text("id", p);
    p += cbor_put_text(r->rp_id, p);
    p += cbor_put_text("name", p);
    p += cbor_put_text(r->rp_name ? r->rp_name : "", p);
    p += cbor_head(0, 2, p);                          /* 2: user */
    p += cbor_head(5, 2, p);
    p += cbor_put_text("id", p);
    p += cbor_put_bytes(r->user_id, r->user_id_len, p);
    p += cbor_put_text("name", p);
    p += cbor_put_text(r->user_name ? r->user_name : "", p);
    p += cbor_head(0, 3, p);                          /* 3: pubKeyCredParams */
    p += cbor_head(4, 1, p);                          /*   array(1) */
    p += cbor_head(5, 2, p);                          /*   map(2): ES256 */
    p += cbor_put_text("type", p);
    p += cbor_put_text("public-key", p);
    p += cbor_put_text("alg", p);
    p += cbor_head(0, 7, p);                          /*   alg = -7 → 7-1=6? 见下: 负数头 0x20|6 */
    /* 注: 上行 alg 用正数头占位后覆写为负数 -7 */
    p[-1] = (uint8_t)(0x20 | 6);                      /* CBOR 负整数 -7 = head(1, 6) */
    p += cbor_head(0, 4, p);                          /* 4: clientDataHash */
    p += cbor_put_bytes(r->client_data_hash, r->client_data_hash_len, p);
    return (int)(p - out);
}

int ctap2_makecred_req_decode(const uint8_t *in, size_t len, ctap2_makecred_req_t *r)
{
    memset(r, 0, sizeof(*r));
    uint8_t major; uint64_t n; size_t h;
    if (cbor_read_head(in, len, &major, &n, &h) != 0 || major != 5) return -1;
    in += h; len -= h;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t key; size_t adv;
        if (cbor_read_head(in, len, &major, &key, &adv) != 0 || major != 0) return -1;
        in += adv; len -= adv;
        switch (key) {
        case 1: { /* rp: map{ "id", "name" } */
            uint64_t m;
            if (cbor_read_head(in, len, &major, &m, &adv) != 0 || major != 5) return -1;
            in += adv; len -= adv;
            for (uint64_t k = 0; k < m; k++) {
                const uint8_t *s; size_t sl, a;
                if (cbor_read_string(in, len, 3, &s, &sl, &a) != 0) return -1;
                in += a; len -= a;
                if (sl == 2 && memcmp(s, "id", 2) == 0) {
                    if (cbor_read_string(in, len, 3, (const uint8_t **)&r->rp_id, NULL, &a) != 0) return -1;
                } else if (sl == 4 && memcmp(s, "name", 4) == 0) {
                    if (cbor_read_string(in, len, 3, (const uint8_t **)&r->rp_name, NULL, &a) != 0) return -1;
                } else if (cbor_read_string(in, len, 3, NULL, NULL, &a) != 0) return -1;
                in += a; len -= a;
            }
            break;
        }
        case 2: { /* user: map{ "id"(bytes), "name" } */
            uint64_t m;
            if (cbor_read_head(in, len, &major, &m, &adv) != 0 || major != 5) return -1;
            in += adv; len -= adv;
            for (uint64_t k = 0; k < m; k++) {
                const uint8_t *s; size_t sl, a;
                if (cbor_read_string(in, len, 3, &s, &sl, &a) != 0) return -1;
                in += a; len -= a;
                if (sl == 2 && memcmp(s, "id", 2) == 0) {
                    if (cbor_read_string(in, len, 2, &r->user_id, &r->user_id_len, &a) != 0) return -1;
                } else if (sl == 4 && memcmp(s, "name", 4) == 0) {
                    if (cbor_read_string(in, len, 3, (const uint8_t **)&r->user_name, NULL, &a) != 0) return -1;
                } else if (cbor_read_string(in, len, 3, NULL, NULL, &a) != 0) return -1;
                in += a; len -= a;
            }
            break;
        }
        case 3: { /* pubKeyCredParams: array of map — 整体跳过 */
            size_t a;
            if (cbor_skip_item(in, len, &a) != 0) return -1;
            in += a; len -= a;
            break;
        }
        case 4:
            if (cbor_read_string(in, len, 2, &r->client_data_hash,
                                 &r->client_data_hash_len, &adv) != 0) return -1;
            in += adv; len -= adv;
            break;
        default:
            if (cbor_read_head(in, len, &major, &n, &adv) != 0) return -1;
            in += adv; len -= adv;
            break;
        }
    }
    return 0;
}

/* ---------------- GetAssertion ---------------- */
int ctap2_getassert_req_encode(const ctap2_getassert_req_t *r, uint8_t *out, size_t cap)
{
    if (!r || !out || !r->rp_id || !r->client_data_hash) return -1;
    if (cap < 128) return -1;
    uint8_t *p = out;
    p += cbor_head(5, 2, p);                          /* map(2) */
    p += cbor_head(0, 1, p);                          /* 1: rpId */
    p += cbor_put_text(r->rp_id, p);
    p += cbor_head(0, 2, p);                          /* 2: clientDataHash */
    p += cbor_put_bytes(r->client_data_hash, r->client_data_hash_len, p);
    return (int)(p - out);
}

int ctap2_getassert_req_decode(const uint8_t *in, size_t len, ctap2_getassert_req_t *r)
{
    memset(r, 0, sizeof(*r));
    uint8_t major; uint64_t n; size_t h;
    if (cbor_read_head(in, len, &major, &n, &h) != 0 || major != 5) return -1;
    in += h; len -= h;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t key; size_t adv;
        if (cbor_read_head(in, len, &major, &key, &adv) != 0 || major != 0) return -1;
        in += adv; len -= adv;
        if (key == 1) {
            if (cbor_read_string(in, len, 3, (const uint8_t **)&r->rp_id, NULL, &adv) != 0) return -1;
            in += adv; len -= adv;
        } else if (key == 2) {
            if (cbor_read_string(in, len, 2, &r->client_data_hash,
                                 &r->client_data_hash_len, &adv) != 0) return -1;
            in += adv; len -= adv;
        } else {
            uint64_t skip;
            if (cbor_read_string(in, len, 2, NULL, NULL, &adv) != 0 &&
                cbor_read_head(in, len, &major, &skip, &adv) != 0) return -1;
            in += adv; len -= adv;
        }
    }
    return 0;
}

/* ---------------- ClientPin (预留) ---------------- */
int ctap2_clientpin_req_encode(uint8_t sub_cmd, const uint8_t *payload, size_t len,
                               uint8_t *out, size_t cap)
{
    if (!out || cap < 1u + len) return -1;
    /* 预留: pinProtocol=1 版本协商与 uvAuth 参数留待实现 */
    out[0] = sub_cmd;
    if (len) memcpy(out + 1, payload, len);
    return (int)(1 + len);
}

/* ---------------- CTAP-HID 分包 ---------------- */
enum { D_IDLE = 0, D_CONT = 1 };

void ctap2_demux_init(ctap2_demux_t *d)
{
    memset(d, 0, sizeof(*d));
}

/* 处理单个 64B report; 返回同 demux_push, *consumed 传出是否产生消息 */
static int demux_report(ctap2_demux_t *d, const uint8_t *rep, ctap2_msg_t *out)
{
    if (d->state == D_IDLE) {
        if (rep[4] & 0x80) {                        /* init 帧 */
            uint32_t cid = ((uint32_t)rep[0] << 24) | ((uint32_t)rep[1] << 16) |
                           ((uint32_t)rep[2] << 8) | rep[3];
            uint16_t bcnt = (uint16_t)((rep[5] << 8) | rep[6]);
            uint8_t cmd = (uint8_t)(rep[4] & 0x7F);
            if (bcnt > CTAPHID_MSG_MAX) return -1;  /* 错 bcnt → 复位重新同步 */
            d->cid = cid; d->cmd = cmd; d->bcnt = bcnt; d->have = 0; d->next_seq = 0;
            size_t chunk = bcnt;
            if (chunk > CTAPHID_REPORT_SZ - 7) chunk = CTAPHID_REPORT_SZ - 7;
            memcpy(d->buf, rep + 7, chunk);
            d->have = chunk;
            if (d->have >= bcnt) {
                out->cid = cid; out->cmd = cmd; out->payload = d->buf; out->len = bcnt;
                return 1;
            }
            d->state = D_CONT;
            return 0;
        }
        return 0; /* 非 init 帧 (如 ping/hid 命令) 丢弃 */
    }
    /* continuation 帧: [CID][seq][payload] */
    uint32_t cid = ((uint32_t)rep[0] << 24) | ((uint32_t)rep[1] << 16) |
                   ((uint32_t)rep[2] << 8) | rep[3];
    if (cid != d->cid) return -1;                   /* CID 错序 */
    if ((rep[4] & 0x80) || rep[4] != d->next_seq) return -1; /* 错 seq */
    d->next_seq++;
    size_t chunk = d->bcnt - d->have;
    if (chunk > CTAPHID_REPORT_SZ - 5) chunk = CTAPHID_REPORT_SZ - 5;
    memcpy(d->buf + d->have, rep + 5, chunk);
    d->have += chunk;
    if (d->have >= d->bcnt) {
        d->state = D_IDLE;
        out->cid = d->cid; out->cmd = d->cmd; out->payload = d->buf; out->len = d->bcnt;
        return 1;
    }
    return 0;
}

int ctap2_demux_push(ctap2_demux_t *d, const uint8_t *data, size_t len, ctap2_msg_t *out)
{
    /* 粘包: 输入先进 carry, 循环逐 report 消费; 每次最多产出一条消息,
     * 剩余留在 carry 里等下一次 push 调用取出. */
    if (len > sizeof(d->carry) - d->carry_len) return -1;
    memcpy(d->carry + d->carry_len, data, len);
    d->carry_len += len;

    while (d->carry_len >= CTAPHID_REPORT_SZ) {
        int rc = demux_report(d, d->carry, out);
        if (rc < 0) {
            d->carry_len = 0; /* 错误复位 */
            d->state = D_IDLE;
            return -1;
        }
        memmove(d->carry, d->carry + CTAPHID_REPORT_SZ,
                d->carry_len - CTAPHID_REPORT_SZ);
        d->carry_len -= CTAPHID_REPORT_SZ;
        if (rc == 1) return 1; /* 剩余留在 carry, 下次 push 取下一条 */
    }
    /* 半包: 不足一个 report 的尾部留待后续 push (真机按 64B report 送达) */
    return 0;
}

int ctap2_frame_msg(uint32_t cid, uint8_t cmd, const uint8_t *payload, size_t len,
                    uint8_t *out, size_t cap)
{
    if (len > CTAPHID_MSG_MAX) return -1;
    size_t need = ((len + CTAPHID_REPORT_SZ - 1) / CTAPHID_REPORT_SZ) * CTAPHID_REPORT_SZ;
    if (need == 0) need = CTAPHID_REPORT_SZ;
    if (cap < need) return (int)need;
    size_t off = 0;
    for (size_t i = 0; i < need; i += CTAPHID_REPORT_SZ) {
        uint8_t *r = out + i;
        memset(r, 0, CTAPHID_REPORT_SZ);
        r[0] = (uint8_t)(cid >> 24); r[1] = (uint8_t)(cid >> 16);
        r[2] = (uint8_t)(cid >> 8);  r[3] = (uint8_t)cid;
        if (i == 0) {
            r[4] = (uint8_t)(0x80 | cmd);
            r[5] = (uint8_t)(len >> 8); r[6] = (uint8_t)len;
            size_t chunk = len;
            if (chunk > CTAPHID_REPORT_SZ - 7) chunk = CTAPHID_REPORT_SZ - 7;
            memcpy(r + 7, payload, chunk);
            off = chunk;
        } else {
            r[4] = (uint8_t)(i / CTAPHID_REPORT_SZ - 1); /* seq 从 0 起 */
            size_t chunk = len - off;
            if (chunk > CTAPHID_REPORT_SZ - 5) chunk = CTAPHID_REPORT_SZ - 5;
            memcpy(r + 5, payload + off, chunk);
            off += chunk;
        }
    }
    return (int)need;
}

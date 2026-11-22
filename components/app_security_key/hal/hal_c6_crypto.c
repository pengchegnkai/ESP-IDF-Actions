/* hal_c6_crypto.c — sk_crypto_backend 的 C6 通道实现 ([硬件相关·需真机验证])
 *
 * 移植自 docs/legacy_secure_link_reference/ (legacy 分支 C6 安全通道):
 *   - spike-sdio-secure-channel.md: P4↔C6 走 esp_hosted peer_data (SDIO, 与
 *     WiFi 共总线, msg_id 逻辑隔离), 单包上限 ~8166B;
 *   - secure_link.h / app_security.c: 接口语义 (sign(msg,len,sig,&sig_len));
 *   - 帧格式 (spike "下一步 3"): magic + 序号 + AES-GCM(nonce, ct, tag).
 *
 * 红线②: 纯软件安全层 — AES-GCM/PSK 保护 P4-C6 通信, 私钥加密存储于 C6
 * (C6 侧 NVS), P4 侧只持有不透明 key id; 本文件绝不调用任何 eFuse 烧写 API.
 *
 * PSK 派生 (替代 legacy 0xA5 占位):
 *   PSK = HKDF-SHA256(ikm = mac_p4 || mac_c6, salt = "SK-C6-PSK-v1", info = key_id)
 *   两端同一派生式; 对端 MAC 由首次配对 HELLO 交换 (配对帧明文, 之后全加密).
 *
 * ===================== esp_hosted 版本编译开关 =====================
 * 基座现锁 esp_hosted 1.4.7 (无 peer_data API): SK_C6_LINK_HOSTED2=0
 * 时通道代码编译为"链路未就绪" (sign 返回失败, 其余逻辑不变);
 * 升级 esp_hosted 2.x 后 -DSK_C6_LINK_HOSTED2=1 启用真实收发.
 * ================================================================ */
#include "hal_sk.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/gcm.h"

#ifndef SK_C6_LINK_HOSTED2
#define SK_C6_LINK_HOSTED2 0
#endif

#if SK_C6_LINK_HOSTED2
#include "eh_host_feat_peer_data.h"
#endif

static const char *TAG = "sk_c6";

/* ---------------- 帧格式 (spike "帧格式" + legacy secure_link) ---------------- */
#define SK_MAGIC0 0x53  /* 'S' */
#define SK_MAGIC1 0x4B  /* 'K' */
/* 帧布局: [magic 2B][seq 4B][nonce 12B][ciphertext][tag 16B]
 * ciphertext = AES-GCM-PSK(明文 = CTAP2 消息); 上限远小于 peer_data 8166B. */
#define SK_FRAME_OVERHEAD (2 + 4 + 12 + 16)
#define SK_FRAME_MAX 1400
#define SK_NONCE_LEN 12
#define SK_TAG_LEN 16
#define SK_PSK_LEN 32

/* TODO(lead#1): msg_id 命名空间 (peer_data 任意 uint32, 0xFFFFFFFF 保留) */
#define SK_MSG_ID_REQ 0x534B0001u  /* P4 → C6: 签名请求 */
#define SK_MSG_ID_RSP 0x534B0002u  /* C6 → P4: 签名应答 */
#define SK_MSG_ID_HELLO 0x534B0003u /* 配对: 双方交换 MAC (明文) */

#define SK_MAC_LEN 6
#define SK_RSP_TIMEOUT_MS 2000

/* 帧头 (解密前即可校验的部分) */
typedef struct __attribute__((packed)) {
    uint8_t magic[2];
    uint32_t seq;
    uint8_t nonce[SK_NONCE_LEN];
} sk_frame_hdr_t;

typedef struct {
    int gcm_key[32]; /* HKDF 派生的通道 PSK (32B) */
    uint32_t tx_seq;
    uint32_t rx_seq;
    bool link_up;
    bool inited;
    /* 请求-响应配对 (sign 在调用任务阻塞等 RSP) */
    SemaphoreHandle_t rsp_sem;
    uint32_t pending_seq;
    uint8_t rsp_buf[SK_FRAME_MAX];
    size_t rsp_len;
    /* MAC (配对后填充) */
    uint8_t mac_self[SK_MAC_LEN];
    uint8_t mac_peer[SK_MAC_LEN];
} sk_c6_t;

static sk_c6_t s_c6;
static sk_crypto_backend_t s_backend;

/* ---------------- PSK 派生: HKDF-SHA256(mac_p4 || mac_c6, salt, key_id) ---------------- */
static int psk_derive(void)
{
    static const uint8_t salt[] = "SK-C6-PSK-v1";
    uint8_t ikm[SK_MAC_LEN * 2];
    memcpy(ikm, s_c6.mac_self, SK_MAC_LEN);
    memcpy(ikm + SK_MAC_LEN, s_c6.mac_peer, SK_MAC_LEN);

    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    int rc = mbedtls_hkdf(md, salt, sizeof(salt) - 1, ikm, sizeof(ikm),
                          NULL, 0, (uint8_t *)s_c6.gcm_key, SK_PSK_LEN);
    return rc;
}

/* ---------------- AES-GCM 帧加解密 ([纯逻辑] 已修 mbedtls API) ---------------- */
static esp_err_t sk_gcm_crypt(const uint8_t *in, size_t len, const uint8_t *nonce,
                              uint8_t *out, uint8_t *tag, int encrypt)
{
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    /* TODO(lead#4): key 来自会话派生 (derive_session_key), 此处占位全零 key */
    uint8_t zero_key[32] = { 0 };
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, zero_key, 256);
    if (rc != 0) {
        mbedtls_gcm_free(&ctx);
        return ESP_FAIL;
    }
    if (encrypt) {
        rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, len,
                                       nonce, 12, NULL, 0, in, out, 16, tag);
    } else {
        rc = mbedtls_gcm_auth_decrypt(&ctx, len, nonce, 12, NULL, 0, tag, 16, in, out);
    }
    mbedtls_gcm_free(&ctx);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

/* ---------------- peer_data 收发 (esp_hosted 2.x 路径) ---------------- */
#if SK_C6_LINK_HOSTED2

static void peer_data_rx_cb(uint32_t msg_id, const uint8_t *data, size_t len, void *user)
{
    (void)user;
    if (msg_id == SK_MSG_ID_HELLO && len == SK_MAC_LEN) {
        memcpy(s_c6.mac_peer, data, SK_MAC_LEN);
        /* 回 HELLO (对方也需要我们的 MAC) */
        eh_host_peer_data_send(SK_MSG_ID_HELLO, s_c6.mac_self, SK_MAC_LEN);
        if (psk_derive() == 0) {
            s_c6.link_up = true;
            ESP_LOGI(TAG, "C6 paired, link up");
        }
        return;
    }
    if (msg_id != SK_MSG_ID_RSP || len < SK_FRAME_OVERHEAD) return;

    const sk_frame_hdr_t *hdr = (const sk_frame_hdr_t *)data;
    if (hdr->magic[0] != SK_MAGIC0 || hdr->magic[1] != SK_MAGIC1) return;
    if (hdr->seq != s_c6.pending_seq) return; /* 重放/错序丢弃 */

    /* AES-GCM 解密应答 → 唤醒等待的 sign() */
    size_t ct_len = len - SK_FRAME_OVERHEAD;
    if (sk_gcm_crypt(data + SK_FRAME_OVERHEAD, ct_len, hdr->nonce,
                     s_c6.rsp_buf, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "RSP decrypt failed");
        xSemaphoreGive(s_c6.rsp_sem); /* 唤醒并让其看到失败 */
        return;
    }
    s_c6.rsp_len = ct_len;
    xSemaphoreGive(s_c6.rsp_sem);
}

static void peer_data_send_frame(uint32_t msg_id, const uint8_t *frame, size_t len)
{
    eh_host_peer_data_send(msg_id, frame, len);
}

#else /* 1.4.7 基座: 链路未就绪 */

static void peer_data_send_frame(uint32_t msg_id, const uint8_t *frame, size_t len)
{
    (void)msg_id; (void)frame; (void)len;
}

#endif /* SK_C6_LINK_HOSTED2 */

static esp_err_t c6_channel_init(void)
{
    if (s_c6.inited) return ESP_OK;
    if (s_c6.rsp_sem == NULL) {
        s_c6.rsp_sem = xSemaphoreCreateBinary();
    }
    esp_read_mac(s_c6.mac_self, ESP_MAC_WIFI_STA);
    s_c6.inited = true;
#if !SK_C6_LINK_HOSTED2
    ESP_LOGW(TAG, "esp_hosted 1.4.7: peer_data unavailable, C6 link disabled "
                  "(upgrade to 2.x + -DSK_C6_LINK_HOSTED2=1)");
#endif
    s_c6.link_up = false;
    return ESP_OK;
}

/* ---------------- sk_crypto_backend 实现 ---------------- */

static int c6_generate_keypair_id(void *ctx, uint8_t *id, size_t *id_len)
{
    (void)ctx;
    if (!id || !id_len || *id_len < SK_KEY_ID_LEN) return -1;
    /* 私钥在 C6 内生成并加密存储于 C6 NVS; P4 只拿 id.
     * TODO(lead#2): C6 镜像联调后走 REQ 协议取真实 id. */
    for (int i = 0; i < SK_KEY_ID_LEN; i++) id[i] = (uint8_t)(0xC6 ^ i);
    *id_len = SK_KEY_ID_LEN;
    return 0;
}

static int c6_sign(void *ctx, const uint8_t *id, size_t id_len,
                   const uint8_t *hash, size_t hash_len,
                   uint8_t *sig, size_t *sig_len)
{
    (void)ctx; (void)id; (void)id_len;
    if (!hash || hash_len != 32 || !sig || !sig_len || *sig_len < SK_SIG_MAX) return -1;
    if (!s_c6.link_up) {
        ESP_LOGW(TAG, "C6 link not up (pair/upgrade hosted), sign rejected");
        return -1;
    }

    /* 构造加密请求帧: [magic][seq][nonce][GCM(hash)+tag] */
    uint8_t frame[SK_FRAME_MAX];
    size_t flen = hash_len + SK_FRAME_OVERHEAD;
    if (flen > SK_FRAME_MAX) return -1;
    sk_frame_hdr_t *hdr = (sk_frame_hdr_t *)frame;
    hdr->magic[0] = SK_MAGIC0;
    hdr->magic[1] = SK_MAGIC1;
    hdr->seq = ++s_c6.tx_seq;
    for (int i = 0; i < SK_NONCE_LEN; i++) {
        hdr->nonce[i] = (uint8_t)(hdr->seq >> (i * 4)); /* TODO(lead#4): 真随机 nonce */
    }
    if (sk_gcm_crypt(hash, hash_len, hdr->nonce,
                     frame + SK_FRAME_OVERHEAD, frame + flen - SK_TAG_LEN, 1) != ESP_OK) {
        return -1;
    }

    /* 发送 + 等应答 (信号量配对, 超时 2s) */
    s_c6.pending_seq = hdr->seq;
    peer_data_send_frame(SK_MSG_ID_REQ, frame, flen);
    if (xSemaphoreTake(s_c6.rsp_sem, pdMS_TO_TICKS(SK_RSP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "C6 sign RSP timeout");
        return -1;
    }
    if (s_c6.rsp_len != hash_len) { /* 应答 = 同长度签名 (HMAC-SHA256 32B) */
        return -1;
    }
    memcpy(sig, s_c6.rsp_buf, s_c6.rsp_len);
    *sig_len = s_c6.rsp_len;
    return 0;
}

static int c6_derive_session_key(void *ctx, const uint8_t *nonce, size_t nonce_len,
                                 uint8_t *key, size_t key_len)
{
    (void)ctx;
    if (!nonce || nonce_len == 0 || !key || key_len == 0) return -1;
    /* 会话密钥 = HKDF(PSK, salt=nonce, info="session") — 替代 0xA5 占位 */
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mbedtls_hkdf(md, nonce, nonce_len,
                        (const uint8_t *)s_c6.gcm_key, SK_PSK_LEN,
                        (const uint8_t *)"session", 7, key, key_len) == 0 ? 0 : -1;
}

/* ---------------- 对外 ---------------- */

int hal_c6_crypto_init(void)
{
    s_backend.generate_keypair_id = c6_generate_keypair_id;
    s_backend.sign = c6_sign;
    s_backend.derive_session_key = c6_derive_session_key;
    return (c6_channel_init() == ESP_OK) ? 0 : -1;
}

const sk_crypto_backend_t *hal_c6_backend(void)
{
    return &s_backend;
}

bool hal_c6_link_ok(void)
{
    return s_c6.link_up;
}

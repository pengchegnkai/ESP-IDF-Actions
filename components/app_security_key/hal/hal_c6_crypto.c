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
 * PSK 派生 (spike R3): 出厂唯一 MAC (只读, 不熔断) + 固定盐, 不写死在固件.
 *
 * ===================== 集成点 TODO (lead 处理) =====================
 * TODO(lead#1) esp_hosted 版本: 工厂 Brookesia 基座 (esp-idf v5.5.5) 用
 *   esp_hosted 1.4.7, 其 host API 是 esp_hosted_tx/esp_hosted_init 旧接口,
 *   本文件按 spike 文档写的是 esp_hosted 2.x (unified_release) 的
 *   eh_host_feat_peer_data API (eh_host_peer_data_send/register).
 *   两条路线二选一:
 *     a) 升级 esp_hosted 到 2.x + Kconfig: CONFIG_ESP_HOSTED_HOST_FEAT_PEER_DATA=y
 *        (依赖 RPC_EXT_V2; MAX_CUSTOM_MSG_HANDLERS 默认 3, 本通道用 2 个 msg_id 够用);
 *     b) 保持 1.4.7: 用 ESP_HOSTED legacy API + 自定义 ctrl msg 承载本通道
 *        (需改 coprocessor 侧, 工作量更大).
 *   建议 a. peer_data P4 侧正确头文件: host/features/eh_host_feat_peer_data/
 *   include/eh_host_feat_peer_data.h.
 * TODO(lead#2) C6 侧镜像: 出厂 stock C6 固件无我们的 peer_data handler,
 *   必须按 spike §4 重编 (模板 examples/peer_data_transfer/cp/, target=esp32c6,
 *   CONFIG_ESP_HOSTED_CP_FEAT_PEER_DATA=y, 私钥+AES-GCM 进 C6 NVS 工程内实现).
 *   首次烧录路径未闭环 (spike R1).
 * TODO(lead#3) PSK 预置: 本文件 TODO_位置 占位; 按 spike R3 用
 *   esp_efuse_mac_get_default (只读 MAC, 非熔断) + 固定盐 HKDF 派生;
 *   需要与 C6 镜像侧约定同一派生式.
 * TODO(lead#4) mbedtls 版本: esp-idf v5.5.5 的 mbedtls AES-GCM API
 *   (mbedtls_gcm_*) 已按 v3.x 写; 若基座换组件版本需复核.
 * ================================================================ */
#include "hal_sk.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "mbedtls/gcm.h"

/* TODO(lead#1): esp_hosted 1.4.7 无此头 — 升级 2.x 后再放开
#include "eh_host_feat_peer_data.h" */

static const char *TAG = "sk_c6";

/* ---------------- 帧格式 (spike "帧格式" + legacy secure_link) ---------------- */
#define SK_MAGIC0 0x53  /* 'S' */ 
#define SK_MAGIC1 0x4B  /* 'K' */
/* 帧布局: [magic 2B][seq 4B][nonce 12B][ciphertext][tag 16B]
 * ciphertext = AES-GCM-PSK(明文 = CTAP2 消息); 上限远小于 peer_data 8166B. */
#define SK_FRAME_OVERHEAD (2 + 4 + 12 + 16)
#define SK_FRAME_MAX 1400

/* TODO(lead#1): msg_id 命名空间 (peer_data 任意 uint32, 0xFFFFFFFF 保留) */
#define SK_MSG_ID_REQ 0x534B0001u  /* P4 → C6: 签名请求 */
#define SK_MSG_ID_RSP 0x534B0002u  /* C6 → P4: 签名应答 */

typedef struct {
    int gcm_key[32]; /* TODO(lead#3): 会话密钥缓冲占位 (HKDF 派生后填入) */
    uint32_t tx_seq;
    uint32_t rx_seq;
    bool link_up;
    bool inited;
} sk_c6_t;

static sk_c6_t s_c6;
static sk_crypto_backend_t s_backend;

/* ---------------- AES-GCM (mbedtls, esp-idf v5.5.5 v3.x API) ---------------- */
static esp_err_t sk_gcm_crypt(const uint8_t *in, size_t len, const uint8_t *nonce,
                              uint8_t *out, uint8_t *tag, int encrypt)
{
    /* TODO(lead#3): key 来自会话派生 (derive_session_key), 此处占位:
     * 真实实现保存 mbedtls_gcm_context/句柄并 setkey 一次. */
    mbedtls_gcm_ctx ctx;
    mbedtls_gcm_init(&ctx);
    /* TODO(lead#4): key 来自会话派生 (derive_session_key), 此处占位全零 key */
    uint8_t zero_key[32] = { 0 };
    int rc = mbedtls_gcm_setkey(&ctx, "aes", zero_key, 256);
    if (rc != 0) {
        mbedtls_gcm_free(&ctx);
        return ESP_FAIL;
    }
    if (encrypt) {
        rc = mbedtls_gcm_crypt_and_tag(NULL, &ctx, MBEDTLS_GCM_ENCRYPT, len,
                                       nonce, 12, NULL, 0, in, out, 16, tag);
    } else {
        rc = mbedtls_gcm_auth_decrypt(&ctx, len, nonce, 12, NULL, 0, tag, 16, in, out);
    }
    mbedtls_gcm_free(&ctx);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

/* ---------------- peer_data 收发 (esp_hosted 2.x, 见 TODO(lead#1)) ---------------- */
/* TODO(lead#1): 放开注解后启用
static void peer_data_rx_cb(uint32_t msg_id, const uint8_t *data, size_t len, void *user)
{
    if (msg_id != SK_MSG_ID_RSP || len < SK_FRAME_OVERHEAD) return;
    // 校验 magic / 重放序号 / AES-GCM 解密 / tag 校验 → 唤醒等待的 sign()
    s_c6.link_up = true;
    // TODO(lead#1): 完成请求-响应配对 (信号量 + seq 匹配), 参考 legacy app_security.c
}
*/

static esp_err_t c6_channel_init(void)
{
    if (s_c6.inited) return ESP_OK;
    /* TODO(lead#1): esp_hosted 2.x 启用:
     *   eh_host_peer_data_register(SK_MSG_ID_RSP, peer_data_rx_cb, NULL);
     * esp_hosted 1.4.7 (现基座): 无 peer_data — 等升级或走 legacy ctrl msg.
     */
    s_c6.inited = true;
    s_c6.link_up = false; /* 真机联调 (TODO(lead#2)) 前恒 false → sign 返回失败 */
    return ESP_OK;
}

/* ---------------- sk_crypto_backend 实现 ---------------- */

static int c6_generate_keypair_id(void *ctx, uint8_t *id, size_t *id_len)
{
    (void)ctx;
    if (!id || !id_len || *id_len < SK_KEY_ID_LEN) return -1;
    /* 私钥在 C6 内生成并加密存储于 C6 NVS (TODO(lead#2) C6 镜像), P4 只拿 id. */
    /* TODO(lead#1/2): 请求-响应经加密通道; 联调前用确定性占位 id */
    for (int i = 0; i < SK_KEY_ID_LEN; i++) id[i] = (uint8_t)(0xC6 ^ i);
    *id_len = SK_KEY_ID_LEN;
    return 0;
}

static int c6_sign(void *ctx, const uint8_t *id, size_t id_len,
                   const uint8_t *hash, size_t hash_len,
                   uint8_t *sig, size_t *sig_len)
{
    (void)ctx; (void)id; (void)id_len;
    if (!hash || hash_len != 32 || !sig || !sig_len) return -1;

    uint8_t frame[SK_FRAME_MAX];
    size_t flen = hash_len + SK_FRAME_OVERHEAD;
    if (flen > SK_FRAME_MAX) return -1;
    frame[0] = SK_MAGIC0; frame[1] = SK_MAGIC1;
    memcpy(frame + 2, &s_c6.tx_seq, 4);
    s_c6.tx_seq++;
    /* TODO(lead#3): nonce/派生密钥/tag 接线; 联调前通道未 up 直接失败 */
    if (!s_c6.link_up) {
        ESP_LOGW(TAG, "C6 link not up (TODO lead#2 C6 image), sign rejected");
        return -1;
    }
    if (sk_gcm_crypt(hash, hash_len, frame + 6, frame + SK_FRAME_OVERHEAD, NULL, 1) != ESP_OK)
        return -1;
    /* TODO(lead#1): eh_host_peer_data_send(SK_MSG_ID_REQ, frame, flen) + 等应答 */
    (void)frame;
    if (*sig_len < SK_SIG_MAX) return -1;
    *sig_len = SK_SIG_MAX;
    return 0;
}

static int c6_derive_session_key(void *ctx, const uint8_t *nonce, size_t nonce_len,
                                 uint8_t *key, size_t key_len)
{
    (void)ctx;
    if (!nonce || nonce_len == 0 || !key || key_len == 0) return -1;
    /* TODO(lead#3): HKDF(出厂 MAC + 盐) → 通道 PSK; 两端约定同一派生式. */
    memset(key, 0xA5, key_len); /* 占位 (联调前), 不可用于真安全 */
    return 0;
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

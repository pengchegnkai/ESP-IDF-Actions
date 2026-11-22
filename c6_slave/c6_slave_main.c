/* c6_slave_main.c — ESP32-C6 从机侧签名服务 ([C6 固件, 独立工程编译])
 *
 * 协议与 P4 侧 hal_c6_crypto.c 完全对偶:
 *   HELLO 配对 (明文 MAC 交换) → PSK 派生 (两端同一 HKDF) →
 *   REQ (加密帧) → GCM 解密 → HMAC-SHA256 签名 (NVS 私钥) → RSP (加密帧).
 *
 * 红线②: 私钥只在 C6 侧生成一次并加密存 NVS, 绝不出 C6; 无 eFuse 操作.
 *
 * 编译 (esp_hosted slave SDK, 独立于主工程):
 *   见同目录 README.md
 */
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

#if SK_C6_LINK_HOSTED2
#include "esp_hosted.h" /* slave 侧: esp_hosted_init + custom msg 注册 */
#endif

static const char *TAG = "c6_slave";

/* 与 P4 侧一致的帧协议 */
#define SK_MAGIC0 0x53
#define SK_MAGIC1 0x4B
#define SK_FRAME_OVERHEAD (2 + 4 + 12 + 16)
#define SK_FRAME_MAX 1400
#define SK_NONCE_LEN 12
#define SK_TAG_LEN 16
#define SK_PSK_LEN 32
#define SK_MAC_LEN 6
#define SK_MSG_ID_REQ 0x534B0001u
#define SK_MSG_ID_RSP 0x534B0002u
#define SK_MSG_ID_HELLO 0x534B0003u

typedef struct {
    uint8_t psk[SK_PSK_LEN];     /* HKDF 派生的通道密钥 */
    uint8_t mac_self[SK_MAC_LEN];
    uint8_t mac_peer[SK_MAC_LEN]; /* HELLO 配对获得 */
    uint8_t priv_key[32];         /* 签名私钥 (NVS 加密存储, 启动时载入) */
    bool paired;
    uint32_t tx_seq;
} c6_state_t;

static c6_state_t s_st;

/* ---------------- 私钥管理: 首次生成, 之后从 NVS 读 ---------------- */
static int priv_key_load_or_create(void)
{
    nvs_handle_t h;
    if (nvs_open("sksec", NVS_READWRITE, &h) != ESP_OK) return -1;
    size_t len = sizeof(s_st.priv_key);
    esp_err_t err = nvs_get_blob(h, "priv", s_st.priv_key, &len);
    if (err != ESP_OK || len != 32) {
        /* 首次: 由内部 efuse 随机数派生 32B 私钥 */
        for (int i = 0; i < 32; i += 4) {
            esp_fill_random(s_st.priv_key + i, 4);
        }
        nvs_set_blob(h, "priv", s_st.priv_key, 32);
        nvs_commit(h);
        ESP_LOGI(TAG, "private key generated (32B) and stored in NVS");
    }
    nvs_close(h);
    return 0;
}

/* ---------------- PSK 派生 (与 P4 侧同一公式) ---------------- */
static int psk_derive(void)
{
    static const uint8_t salt[] = "SK-C6-PSK-v1";
    uint8_t ikm[SK_MAC_LEN * 2];
    memcpy(ikm, s_st.mac_peer, SK_MAC_LEN);   /* P4 MAC */
    memcpy(ikm + SK_MAC_LEN, s_st.mac_self, SK_MAC_LEN); /* 自己 MAC 在后 */
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mbedtls_hkdf(md, salt, sizeof(salt) - 1, ikm, sizeof(ikm),
                        NULL, 0, s_st.psk, SK_PSK_LEN);
}

/* ---------------- GCM ---------------- */
static int gcm_crypt(const uint8_t *in, size_t len, const uint8_t *nonce,
                     uint8_t *out, uint8_t *tag, int encrypt)
{
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, s_st.psk, 256);
    if (rc != 0) {
        mbedtls_gcm_free(&ctx);
        return -1;
    }
    if (encrypt) {
        rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, len, nonce, 12,
                                       NULL, 0, in, out, 16, tag);
    } else {
        rc = mbedtls_gcm_auth_decrypt(&ctx, len, nonce, 12, NULL, 0, tag, 16, in, out);
    }
    mbedtls_gcm_free(&ctx);
    return rc;
}

/* ---------------- 签名: HMAC-SHA256(priv_key, hash) — ECDSA 联调后切换 ---------------- */
static int sign_hash(const uint8_t *hash, size_t hash_len, uint8_t *sig, size_t *sig_len)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mbedtls_md_hmac(md, s_st.priv_key, 32, hash, hash_len, sig) == 0
               ? (*sig_len = 32, 0) : -1;
}

/* ---------------- peer_data 收发 (slave 侧 API) ---------------- */
#if SK_C6_LINK_HOSTED2

static void on_peer_msg(uint32_t msg_id, const uint8_t *data, size_t len, void *user)
{
    (void)user;
    if (msg_id == SK_MSG_ID_HELLO && len == SK_MAC_LEN) {
        if (!s_st.paired) {
            memcpy(s_st.mac_peer, data, SK_MAC_LEN);
            psk_derive();
            s_st.paired = true;
            ESP_LOGI(TAG, "paired with P4, link up");
            esp_hosted_slave_send_to_host(SK_MSG_ID_HELLO, s_st.mac_self, SK_MAC_LEN);
        }
        return;
    }
    if (!s_st.paired || msg_id != SK_MSG_ID_REQ || len < SK_FRAME_OVERHEAD) return;

    /* 解 REQ 帧 */
    const uint8_t *hdr = data;
    uint32_t seq;
    memcpy(&seq, hdr + 2, 4);
    const uint8_t *nonce = hdr + 6;
    const uint8_t *ct = data + SK_FRAME_OVERHEAD;
    size_t ct_len = len - SK_FRAME_OVERHEAD;
    uint8_t plain[SK_FRAME_MAX - SK_FRAME_OVERHEAD];
    if (gcm_crypt(ct, ct_len, nonce, plain, NULL, 0) != 0) {
        ESP_LOGW(TAG, "REQ decrypt failed");
        return;
    }

    /* 签名并构造 RSP 帧 (同 seq, 新 nonce) */
    uint8_t sig[32];
    size_t sig_len = 32;
    if (sign_hash(plain, ct_len, sig, &sig_len) != 0) return;

    uint8_t frame[SK_FRAME_MAX];
    frame[0] = SK_MAGIC0; frame[1] = SK_MAGIC1;
    memcpy(frame + 2, &seq, 4);
    for (int i = 0; i < SK_NONCE_LEN; i++) frame[6 + i] = (uint8_t)(seq >> (i * 4));
    size_t flen = sig_len + SK_FRAME_OVERHEAD;
    gcm_crypt(sig, sig_len, frame + 6, frame + SK_FRAME_OVERHEAD,
              frame + flen - SK_TAG_LEN, 1);
    esp_hosted_slave_send_to_host(SK_MSG_ID_RSP, frame, flen);
}

#endif /* SK_C6_LINK_HOSTED2 */

void app_main(void)
{
    esp_read_mac(s_st.mac_self, ESP_MAC_WIFI_STA);
    nvs_flash_init();
    priv_key_load_or_create();
#if SK_C6_LINK_HOSTED2
    /* slave SDK 初始化 + peer_data 注册 (具体 API 以 esp_hosted slave 版本为准) */
    esp_hosted_slave_init();
    esp_hosted_slave_register_peer_msg_cb(on_peer_msg, NULL);
    ESP_LOGI(TAG, "C6 slave ready, waiting for P4 HELLO");
#else
    ESP_LOGW(TAG, "compile with esp_hosted slave SDK + SK_C6_LINK_HOSTED2=1");
#endif
    for (;;) vTaskDelay(portMAX_DELAY);
}

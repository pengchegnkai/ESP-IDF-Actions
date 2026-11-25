/* adb_rsa.c — ADB RSA2048 密钥生成/存取/签名 ([纯逻辑·可仿真])
 *
 * 红线②: 纯软件实现, 只用 mbedtls + NVS, 不碰 eFuse/安全区.
 * mbedtls 3.x API (ESP-IDF v5.5).
 */
#include "adb_rsa.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"

static const char *TAG = "adbtool.rsa";

static mbedtls_pk_context s_rsa;
static int s_rsa_ready;

/* libusb 风格随机数回调 (mbedtls 3.x 签名: int f(void*, unsigned char*, size_t)) */
static int mbedtls_rng(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    esp_fill_random(out, len);
    return 0;
}

int adb_rsa_init(void)
{
    if (s_rsa_ready) return 0;

    mbedtls_pk_init(&s_rsa);
    nvs_handle_t h;
    if (nvs_open("adbtool", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('adbtool') failed");
        return -1;
    }

    /* 先尝试从 NVS 恢复私钥 DER */
    uint8_t buf[2048];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_blob(h, "rsakey", buf, &len);
    if (err == ESP_OK && len <= sizeof(buf)) {
        if (mbedtls_pk_parse_key(&s_rsa, buf, len, NULL, 0) == 0) {
            s_rsa_ready = 1;
            nvs_close(h);
            ESP_LOGI(TAG, "RSA2048 key loaded from NVS");
            return 0;
        }
        ESP_LOGW(TAG, "stored key unreadable, regenerating");
    }

    /* 首次: 生成 RSA2048 */
    if (mbedtls_pk_setup(&s_rsa, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0 ||
        mbedtls_rsa_gen_key(mbedtls_pk_rsa(s_rsa), mbedtls_rng, NULL, 2048, 65537) != 0) {
        ESP_LOGE(TAG, "RSA keygen failed");
        nvs_close(h);
        return -1;
    }

    /* 私钥 DER 序列化 (右对齐写入, 返回实际长度) */
    uint8_t der[2048];
    int n = mbedtls_pk_write_key_der(&s_rsa, der, sizeof(der));
    if (n <= 0) {
        nvs_close(h);
        return -1;
    }
    if (nvs_set_blob(h, "rsakey", der + sizeof(der) - n, n) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS store key failed");
        nvs_close(h);
        return -1;
    }
    nvs_close(h);
    s_rsa_ready = 1;
    ESP_LOGI(TAG, "RSA2048 key generated & stored in NVS");
    return 0;
}

int adb_rsa_pubkey_adb(uint8_t *out, size_t cap)
{
    if (!s_rsa_ready && adb_rsa_init() != 0) return -1;

    /* DER SubjectPublicKeyInfo (右对齐写入, 返回实际长度) */
    uint8_t der[1024];
    int n = mbedtls_pk_write_pubkey_der(&s_rsa, der, sizeof(der));
    if (n <= 0) return -1;

    uint8_t b64[1200]; /* 2048bit SPKI DER ≈ 294B → base64 ≈ 392B */
    size_t b64_len = 0;
    if (mbedtls_base64_encode(b64, sizeof(b64), &b64_len,
                              der + sizeof(der) - n, n) != 0) {
        return -1;
    }

    const char *prefix = "host:";
    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + b64_len + 1; /* + '\0' */
    if (cap < total) return -1;

    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, b64, b64_len);
    out[total - 1] = '\0';
    return (int)(total - 1);
}

int adb_rsa_sign(const uint8_t *hash, size_t hash_len, uint8_t *sig, size_t *sig_len)
{
    if (!s_rsa_ready && adb_rsa_init() != 0) return -1;
    if (hash_len != 20 || sig == NULL || *sig_len < 256) return -1;

    /* RSASSA-PKCS1-v1_5 with SHA1 (ADB adbd 用同样参数验签) */
    if (mbedtls_rsa_set_padding(mbedtls_pk_rsa(s_rsa), MBEDTLS_RSA_PKCS_V15,
                                MBEDTLS_MD_SHA1) != 0) {
        return -1;
    }
    int ret = mbedtls_rsa_pkcs1_sign(mbedtls_pk_rsa(s_rsa), mbedtls_rng, NULL,
                                     MBEDTLS_MD_SHA1, 20,
                                     hash, sig);
    if (ret != 0) {
        ESP_LOGE(TAG, "rsa sign failed: -0x%04x", -ret);
        return -1;
    }
    *sig_len = mbedtls_pk_get_len(&s_rsa);
    return 0;
}

/* sk_crypto_host.c — sk_crypto_backend 的 pc_sim 确定性 fake ([纯逻辑·可仿真])
 *
 * sign = hash 前 32 字节逐字节 XOR 0xA5 (简单可验证变换, 输出确定, 断言友好);
 * generate_keypair_id 返回确定性 id (0x10..0x17);
 * derive_session_key = nonce 逐字节 XOR 0x5A 填满 key.
 */
#include "sk_crypto_host.h"

#include <string.h>

static int host_generate_keypair_id(void *ctx, uint8_t *id, size_t *id_len)
{
    (void)ctx;
    if (!id || !id_len || *id_len < SK_KEY_ID_LEN) return -1;
    for (int i = 0; i < SK_KEY_ID_LEN; i++) id[i] = (uint8_t)(0x10 + i);
    *id_len = SK_KEY_ID_LEN;
    return 0;
}

static int host_sign(void *ctx, const uint8_t *id, size_t id_len,
                     const uint8_t *hash, size_t hash_len,
                     uint8_t *sig, size_t *sig_len)
{
    (void)ctx;
    /* 契约: hash_len >= 32 (session 传入 64B 零填充缓冲), 取前 32 字节 */
    if (!id || id_len != SK_KEY_ID_LEN || !hash || hash_len < 32 ||
        !sig || !sig_len || *sig_len < SK_SIG_MAX) return -1;
    for (int i = 0; i < 32; i++) sig[i] = (uint8_t)(hash[i] ^ 0xA5);
    for (int i = 32; i < SK_SIG_MAX; i++) sig[i] = (uint8_t)(id[i % SK_KEY_ID_LEN] ^ i);
    *sig_len = SK_SIG_MAX;
    return 0;
}

static int host_derive_session_key(void *ctx, const uint8_t *nonce, size_t nonce_len,
                                   uint8_t *key, size_t key_len)
{
    (void)ctx;
    if (!nonce || !nonce_len || !key || !key_len) return -1;
    for (size_t i = 0; i < key_len; i++) key[i] = (uint8_t)(nonce[i % nonce_len] ^ 0x5A);
    return 0;
}

static sk_crypto_backend_t s_backend = {
    .generate_keypair_id = host_generate_keypair_id,
    .sign = host_sign,
    .derive_session_key = host_derive_session_key,
};

const sk_crypto_backend_t *sk_crypto_host_backend(void)
{
    return &s_backend;
}

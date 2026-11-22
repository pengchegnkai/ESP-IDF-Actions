/* sk_crypto_backend.h — 签名运算 vtable ([纯逻辑·可仿真])
 *
 * core 协议层与具体密钥运算的解耦接口 (CONVENTIONS.md 后端接口模式):
 *   - pc_sim/stubs/sk_crypto_host.c  → 确定性 fake (签名 = 固定可验证变换)
 *   - hal/hal_c6_crypto.c            → 真机走 P4↔C6 AES-GCM/PSK 通道
 *
 * 红线: 纯软件实现, 绝不熔断 eFuse; 私钥加密存储于 C6, P4 侧只见 key id.
 * 借鉴 pico-fido 的运算抽象层.
 */
#ifndef SK_CRYPTO_BACKEND_H
#define SK_CRYPTO_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回 0 = 成功, 负值 = 错误 */
typedef struct {
    /* 生成/登记密钥对, 返回不透明 key id (P4 侧不接触私钥本体).
     * C6 端私钥加密存储于其 NVS; host fake 返回确定性 id. */
    int (*generate_keypair_id)(void *ctx, uint8_t *id, size_t *id_len);

    /* 用 key id 对 hash (32B SHA-256 常见) 做签名, sig 容量 ≥64 (ES256). */
    int (*sign)(void *ctx, const uint8_t *id, size_t id_len,
                const uint8_t *hash, size_t hash_len,
                uint8_t *sig, size_t *sig_len);

    /* 派生会话密钥 (通道加密用, AES-GCM), 输入 nonce/盐, 输出对称 key. */
    int (*derive_session_key)(void *ctx, const uint8_t *nonce, size_t nonce_len,
                              uint8_t *key, size_t key_len);
} sk_crypto_backend_t;

/* 统一签名长度 (ES256 = 64B raw r||s) */
#define SK_SIG_MAX 64
/* 统一 key id 长度 (host 与 C6 均按 8B id 寻址) */
#define SK_KEY_ID_LEN 8

#ifdef __cplusplus
}
#endif

#endif /* SK_CRYPTO_BACKEND_H */

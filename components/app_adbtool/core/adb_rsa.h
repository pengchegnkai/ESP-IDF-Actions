/* adb_rsa.h — ADB RSA2048 密钥管理 ([纯逻辑·可仿真], 仅依赖 NVS/mbedtls)
 *
 * - 首次调用 adb_rsa_init() 生成 RSA2048 密钥对, 私钥 DER 存 NVS("adbtool","rsakey")
 * - adb_rsa_pubkey_adb(): ADB 公钥格式 "host:" + base64(DER SubjectPublicKeyInfo) + '\0'
 * - adb_rsa_sign(): 对 20B SHA1 摘要做 RSASSA-PKCS1-v1_5 签名 (ADB AUTH SIGNATURE 用)
 */
#ifndef ADB_RSA_H
#define ADB_RSA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 加载或首次生成 RSA2048 密钥 (NVS 幂等); 0 = 成功 */
int adb_rsa_init(void);

/* ADB 公钥串: "host:" + base64(DER SPKI) + '\0'; 返回写入长度(不含'\0'), 负数=失败 */
int adb_rsa_pubkey_adb(uint8_t *out, size_t cap);

/* 对 hash (SHA1, 20B) 做 RSASSA-PKCS1-v1_5 签名; sig 需 >= 256B; 0 = 成功 */
int adb_rsa_sign(const uint8_t *hash, size_t hash_len, uint8_t *sig, size_t *sig_len);

#ifdef __cplusplus
}
#endif
#endif /* ADB_RSA_H */

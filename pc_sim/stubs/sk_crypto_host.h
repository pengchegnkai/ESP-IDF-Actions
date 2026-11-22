/* sk_crypto_host.h — sk_crypto_backend 的 pc_sim 确定性 fake ([纯逻辑·可仿真])
 *
 * 供 test_sk_* 断言签名输出确定: sign = hash 前 32 字节逐字节 XOR 0xA5.
 * 初始化用 sk_crypto_host_init().
 */
#ifndef SK_CRYPTO_HOST_H
#define SK_CRYPTO_HOST_H

#include "sk_crypto_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

const sk_crypto_backend_t *sk_crypto_host_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* SK_CRYPTO_HOST_H */

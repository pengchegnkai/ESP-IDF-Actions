/**
 * @file secure_link_stub.c
 * @brief 安全链路 v1 stub —— 阶段二在此文件位置替换为 SDIO/AES-GCM/PSK 真实现
 *
 * 替换点:
 *   secure_link_init()      ← SDIO 安全通道建立 + 密钥交换
 *   secure_link_fido_sign() ← 经安全链路把 msg 送协处理器签名, 回 P-256 ECDSA
 * 接口签名保持稳定 (见 secure_link.h)。
 */
#include "secure_link/secure_link.h"
#include "esp_log.h"

static const char *TAG = "secure_link";

esp_err_t secure_link_init(void)
{
    /* TODO(阶段二): SDIO 安全通道建立 + AES-GCM/PSK 密钥交换 */
    ESP_LOGI(TAG, "init (v1 stub: no secure channel)");
    return ESP_OK;
}

esp_err_t secure_link_fido_sign(const uint8_t *msg, int len, uint8_t *sig, int *sig_len)
{
    (void)sig;
    (void)sig_len;
    /* TODO(阶段二): 经 SDIO 安全链路送协处理器签名, 回填 P-256 ECDSA (64B) */
    ESP_LOGW(TAG, "fido_sign not implemented (v1 stub): msg=%d bytes", len);
    return ESP_ERR_NOT_SUPPORTED;
}

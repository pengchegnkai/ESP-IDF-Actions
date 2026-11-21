/**
 * @file secure_link.h
 * @brief 安全链路 —— 主控 (ESP32-P4) 与协处理器 (ESP32-C6, SDIO) 之间的安全通道
 *
 * 阶段三 (v1, 本期): stub。
 *   secure_link_init()      无操作, 返回 ESP_OK
 *   secure_link_fido_sign() 记日志, 返回 ESP_ERR_NOT_SUPPORTED
 *   → 安全密钥的签名请求 (MakeCredential/GetAssertion) 一律回 CTAP2 错误帧。
 *
 * 阶段二: 在 SDIO 链路上实现 AES-GCM/PSK 加密通道 + 密钥交换/签名,
 *         接口保持稳定 (调用方 app_security 无需改动)。
 */
#pragma once

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化安全链路
 * @note  阶段二: 建立 SDIO 安全通道 (PSK 交换 / AES-GCM 会话), 建议放后台任务
 * @return v1: 恒 ESP_OK
 */
esp_err_t secure_link_init(void);

/**
 * @brief CTAP2 消息签名
 * @param msg     CTAP2 命令数据 (makeCredential/getAssertion 的待签数据)
 * @param len     msg 长度
 * @param sig     签名输出缓冲 (阶段二: P-256 ECDSA, 64 字节; 调用方按 ≥64 分配)
 * @param sig_len 入: sig 缓冲容量; 出: 实际签名长度
 * @return v1: 恒 ESP_ERR_NOT_SUPPORTED; 阶段二: ESP_OK = 签名成功
 */
esp_err_t secure_link_fido_sign(const uint8_t *msg, int len, uint8_t *sig, int *sig_len);

#ifdef __cplusplus
}
#endif

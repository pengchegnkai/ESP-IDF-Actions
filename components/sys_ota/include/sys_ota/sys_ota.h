/**
 * @file sys_ota.h
 * @brief 网络 OTA —— GitHub Release 检查 / 下载 / 校验 / 安装 / 回滚保护
 *
 * 流程: check_update() 异步执行:
 *   1. GET GitHub API 获取最新版本 tag + demo.bin 下载地址 + sha256 digest
 *   2. 版本门: 远端 build 号 <= 本地 → 结束
 *   3. 流式下载 demo.bin 到 PSRAM, 同时计算 SHA256
 *   4. 校验 (SHA256 或 magic 0xE9) → 写入 OTA 分区 → set_boot → 重启
 *
 * 回滚安全: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
 *   新镜像首次启动后 30s 内若未 crash, sys_ota_boot_check() mark valid 取消回滚。
 *
 * 线程模型: 内部 ota 任务 (prio 3, 栈 16384); 对外接口任意任务可调用。
 * 事件: 每阶段通过 SYS_EVENT_OTA 广播 sys_ota_evt_t*。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** OTA 阶段事件 (SYS_EVENT_OTA 的 data 指针类型) */
typedef struct {
    int stage;    /**< 0=idle 1=checking 2=found 3=downloading 4=installing 5=ok 6=failed */
    int percent;  /**< 0-100, stage=3 时有效 */
} sys_ota_evt_t;

/**
 * @brief 初始化 OTA 子系统 (创建内部任务)
 * @note  协调者在 app_main 的 net 启动任务中调用; 需 WiFi 栈已就绪。
 * @return ESP_OK 成功
 */
esp_err_t sys_ota_init(void);

/**
 * @brief 触发一次 OTA 检查 (异步: 检查→下载→校验→安装→重启)
 * @note  可任意任务调用; 重复调用时若已在执行则忽略 (返回 ESP_ERR_INVALID_STATE)。
 * @return ESP_OK 已入队, ESP_ERR_INVALID_STATE 正在执行中
 */
esp_err_t sys_ota_check_update(void);

/**
 * @brief 当前固件版本字符串 (如 "build-62")
 * @return 指向编译期常量字符串的指针, 永不释放
 */
const char *sys_ota_current_version(void);

/**
 * @brief 启动后回滚保护检查 (协调者在 app_main 中调用)
 * @note  若当前运行的是 OTA 镜像且处于 PENDING_VERIFY 状态,
 *        则延迟 30s 后 mark valid (取消回滚)。
 *        非 OTA 镜像或已 valid 时立即返回, 无副作用。
 */
void sys_ota_boot_check(void);

#ifdef __cplusplus
}
#endif

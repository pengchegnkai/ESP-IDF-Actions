/**
 * @file sys_net.h
 * @brief 网络子系统 —— WiFi 连接 / ESP-NOW 邻居发现 / NTP 校时
 *
 * P4 目标: WiFi 由板载 ESP32-C6 协处理器经 ESP-Hosted (SDIO) 提供,
 *          本组件封装标准 esp_wifi API, 并向上通过事件总线广播状态:
 *            SYS_EVENT_WIFI_STATUS (data: sys_net_wifi_status_t*)
 *            SYS_EVENT_ESPNOW_PEER (data: int* 邻居数)
 *            SYS_EVENT_TIME_SYNCED
 * S3/C6 独立目标: 全部返回 ESP_ERR_NOT_SUPPORTED (stub)。
 *
 * 线程模型: 所有 esp_wifi API 调用集中在内部 net 任务 (命令队列),
 *          对外接口任意任务可调用; 事件回调内不得阻塞。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_NET_SSID_MAX   33
#define SYS_NET_SCAN_MAX   16
#define SYS_NET_PEER_MAX   8

/** WiFi 链路状态 */
typedef enum {
    SYS_NET_WIFI_OFF = 0,        /**< 未开启 */
    SYS_NET_WIFI_CONNECTING,     /**< 连接中 */
    SYS_NET_WIFI_CONNECTED,      /**< 已连接且拿到 IP */
    SYS_NET_WIFI_ERROR,          /**< 初始化/连接失败 */
} sys_net_wifi_state_t;

/** WiFi 状态快照 */
typedef struct {
    sys_net_wifi_state_t state;
    char ssid[SYS_NET_SSID_MAX];
    char ip[16];                 /**< 点分十进制; 未拿到时为空串 */
    int  rssi;                   /**< dBm; 未连接为 0 */
} sys_net_wifi_status_t;

/** 扫描结果项 */
typedef struct {
    char ssid[SYS_NET_SSID_MAX];
    int8_t rssi;
    uint8_t auth;                /**< wifi_auth_mode_t */
    bool hidden;
} sys_net_ap_info_t;

/** ESP-NOW 邻居 (5 秒内听到广播才算在线) */
typedef struct {
    uint8_t mac[6];
    char name[24];               /**< 对端设备名 */
    uint32_t last_seen_ms;       /**< esp_timer_get_time() 毫秒 */
} sys_net_espnow_peer_t;

/**
 * @brief 异步扫描完成回调 (运行在 sys_net 任务上下文; 结果仅回调期间有效)
 * @param err ESP_OK 时 aps/count 有效
 */
typedef void (*sys_net_scan_cb_t)(const sys_net_ap_info_t *aps, int count,
                                  esp_err_t err, void *ctx);

/**
 * @brief 初始化网络子系统
 * @note  内部起 net 任务: 建立 hosted 链路 + WiFi 栈 (耗时数秒, 不阻塞调用方)。
 *        必须在 NVS 初始化之后调用 (WiFi 凭据持久化); 可早于/晚于 UI 就绪。
 */
esp_err_t sys_net_init(void);

/* ---------- WiFi ---------- */

/** 开启/关闭 WiFi (关闭会断开并停止重连) */
esp_err_t sys_net_wifi_enable(bool enable);

/** WiFi 当前是否开启 */
bool sys_net_wifi_is_enabled(void);

/**
 * @brief 连接到指定热点 (异步; 凭据存入 NVS, 下次开机自动重连)
 * @param password 可为 NULL 或 "" (开放网络)
 */
esp_err_t sys_net_wifi_connect(const char *ssid, const char *password);

/**
 * @brief 开始扫描 (异步; 完成后 cb 被调用, 最多 SYS_NET_SCAN_MAX 个, 按信号排序)
 */
esp_err_t sys_net_wifi_scan(sys_net_scan_cb_t cb, void *ctx);

/** 状态快照 (任意任务可调用; 线程安全) */
void sys_net_wifi_get_status(sys_net_wifi_status_t *st);

/* ---------- ESP-NOW ---------- */

/** 启动 ESP-NOW 广播 (需要 WiFi 已开启) */
esp_err_t sys_net_espnow_start(void);

/** 停止 ESP-NOW 广播 */
esp_err_t sys_net_espnow_stop(void);

bool sys_net_espnow_is_active(void);

/**
 * @brief 当前在线邻居 (5 秒内收到过广播)
 * @return 指向内部表的指针 (调用方需立即读取); *count 输出数量
 */
const sys_net_espnow_peer_t *sys_net_espnow_peers(int *count);

/* ---------- 时间同步 (NTP) ---------- */

/** 是否已成功校时 */
bool sys_net_time_is_synced(void);

/** 触发一次 SNTP 校时 (WiFi 拿到 IP 后自动触发; 也可手动) */
esp_err_t sys_net_time_sync(void);

/** 当前 epoch 秒 (未校时返回 0) */
uint32_t sys_net_time_epoch(void);

/**
 * @brief 检查 NTP 同步状态变化, 成功时发布 SYS_EVENT_TIME_SYNCED (幂等)
 * @note  建议每秒调用一次 (APP 的 TIME_TICK 回调里); 同步只报告一次
 */
void sys_net_time_check_and_report(void);

#ifdef __cplusplus
}
#endif

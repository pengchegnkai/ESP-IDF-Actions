/* c6_rf_test.h — C6 全功能共存测试框架 (P4 主机侧)
 *
 * [硬件相关·需真机验证] 依赖 esp_wifi (esp-hosted 代理到 C6) / BT host 栈 /
 * esp_coex API — 不进 pc_sim 仿真 (pc_sim 只测纯逻辑层)。
 *
 * 验证目标 (官方已验证模式, IDFGH-18546): 三栈共存于固件 + 两两组合轮换;
 * 三栈同时收发官方不支持, 框架不设该用例。
 *
 * Kconfig: CONFIG_C6_RF_TEST_ENABLED 默认 n — 不影响出厂主固件;
 *          CI 显式开启时作为编译门禁。
 * 借鉴: espressif/esp-hosted-mcu 示例结构 (wifi_station / hosted_hci bleprph)。
 */
#ifndef C6_RF_TEST_H
#define C6_RF_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    C6_RF_PAIR_WIFI_BLE = 0,   /* Wi-Fi + BLE (官方共存矩阵全绿) */
    C6_RF_PAIR_WIFI_154,       /* Wi-Fi + 802.15.4 (需自定义 CP 服务落地后可用) */
    C6_RF_PAIR_BLE_154,        /* BLE + 802.15.4 (同上) */
    C6_RF_PAIR_MAX,
} c6_rf_pair_t;

typedef struct {
    unsigned wifi_scan_done;   /* Wi-Fi 扫描完成次数 */
    unsigned ble_scan_reports; /* BLE 广播报文接收数 */
    unsigned frames_154;       /* 802.15.4 帧收发数 (自定义服务落地后有效) */
    c6_rf_pair_t active_pair;  /* 当前启用的两两组合 */
} c6_rf_test_stats_t;

/* 初始化: 仅当 CONFIG_C6_RF_TEST_ENABLED 时做实际工作, 否则回 ESP_ERR_NOT_SUPPORTED */
esp_err_t c6_rf_test_init(void);

/* Wi-Fi 扫描 (同步阻塞, 结果经日志输出) — 经 esp-hosted 在 C6 上执行 */
esp_err_t c6_rf_test_wifi_scan(uint32_t timeout_ms);

/* BLE 扫描 (异步启动; 结果计数进 stats) — BT 控制器在 C6 (VHCI over SDIO),
 * 需 CONFIG_C6_RF_TEST_ENABLE_BLE + esp_hosted >= 2.x host BT 功能 */
esp_err_t c6_rf_test_ble_scan_start(uint32_t window_ms);

/* 两两共存轮换: pair = 当前要验证的组合; 每组合观察 window_ms 后自动切换.
 * 内部使用 esp_coex_status_bit_set/clear 与 (Wi-Fi+15.4 时) esp_coex_wifi_i154_enable().
 * 802.15.4 分支在自定义 CP 服务落地前返回 ESP_ERR_NOT_SUPPORTED. */
esp_err_t c6_rf_test_coex_pairwise(c6_rf_pair_t pair, uint32_t window_ms);

/* 统计快照 */
const c6_rf_test_stats_t *c6_rf_test_stats_get(void);

#ifdef __cplusplus
}
#endif

#endif /* C6_RF_TEST_H */

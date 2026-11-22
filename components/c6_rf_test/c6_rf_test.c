/* c6_rf_test.c — C6 全功能共存测试框架实现 (P4 主机侧)
 *
 * [硬件相关·需真机验证] esp_wifi/BT host 栈经 esp-hosted 调度到 C6 执行。
 * 编译门禁: 默认 CONFIG_C6_RF_TEST_ENABLED=n 时整文件退化为桩, 零副作用。
 *
 * 借鉴: espressif/esp-hosted-mcu examples (wifi_station, hosted_hci),
 *       IDF RF 共存编程指南 (esp_coex_status_bit_set / esp_coex_wifi_i154_enable)。
 */
#include "c6_rf_test.h"

#include <esp_log.h>
#include <string.h>

#include "sdkconfig.h"

static const char *TAG = "c6_rf_test";

static c6_rf_test_stats_t s_stats;

#if !CONFIG_C6_RF_TEST_ENABLED

esp_err_t c6_rf_test_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t c6_rf_test_wifi_scan(uint32_t timeout_ms) { (void)timeout_ms; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t c6_rf_test_ble_scan_start(uint32_t window_ms) { (void)window_ms; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t c6_rf_test_coex_pairwise(c6_rf_pair_t pair, uint32_t window_ms) { (void)pair; (void)window_ms; return ESP_ERR_NOT_SUPPORTED; }
const c6_rf_test_stats_t *c6_rf_test_stats_get(void) { return &s_stats; }

#else /* CONFIG_C6_RF_TEST_ENABLED */

#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* esp_coex API: Wi-Fi+15.4 双模共存使能 (IDF 官方推荐, IDFGH-18546) */
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE
#include "esp_coexist.h"
#endif

static bool s_ble_started;

esp_err_t c6_rf_test_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.active_pair = C6_RF_PAIR_MAX;
    return ESP_OK;
}

esp_err_t c6_rf_test_wifi_scan(uint32_t timeout_ms)
{
    wifi_scan_config_t cfg = {
        .show_hidden = false,
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, true /*block*/);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi scan start: %s", esp_err_to_name(err));
        return err;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    /* 只打印数量 — 完整 AP 列表经 esp_wifi_scan_get_ap_records 按需取 */
    ESP_LOGI(TAG, "wifi scan done: %u APs (timeout %ums)", n, (unsigned)timeout_ms);
    s_stats.wifi_scan_done++;
    return ESP_OK;
}

esp_err_t c6_rf_test_ble_scan_start(uint32_t window_ms)
{
#if CONFIG_C6_RF_TEST_ENABLE_BLE
    /* TODO(烧录窗口): esp_hosted 升 2.x 后接入 NimBLE GAP 扫描
     *   nimble_port_init() → ble_gap_disc() → 计数 ble_gap_event 的 DISC 报文。
     * 控制器由 C6 提供 (CONFIG_ESP_HOSTED_HOST_FEAT_BT + VHCI), P4 侧零本地控制器。 */
    ESP_LOGW(TAG, "BLE scan: awaiting esp_hosted 2.x BT host feature (window %ums)", (unsigned)window_ms);
    s_ble_started = true;
    return ESP_OK;
#else
    (void)window_ms;
    ESP_LOGW(TAG, "BLE scan: CONFIG_C6_RF_TEST_ENABLE_BLE=n");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t c6_rf_test_coex_pairwise(c6_rf_pair_t pair, uint32_t window_ms)
{
    if (pair >= C6_RF_PAIR_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_stats.active_pair = pair;
    ESP_LOGI(TAG, "coex pair=%d observe %ums", (int)pair, (unsigned)window_ms);

    switch (pair) {
    case C6_RF_PAIR_WIFI_BLE:
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE
        /* Wi-Fi+BLE 共存: 协议栈内部已调 coex_enable, 无需显式 API。
         * BLE MESH 场景才需要 esp_coex_status_bit_set/clear 手动同步。 */
        ESP_LOGI(TAG, "wifi+ble coex: automatic (stack-managed)");
#endif
        break;

    case C6_RF_PAIR_WIFI_154:
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE
        /* 官方: Wi-Fi+15.4 双模需应用显式使能 15.4 共存状态 */
        esp_err_t err = esp_coex_wifi_i154_enable();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_coex_wifi_i154_enable: %s", esp_err_to_name(err));
            return err;
        }
#endif
        /* TODO(自定义 CP 服务): C6 侧 ieee802154 驱动收发 + esp-hosted 自定义帧代理。
         * 本板 P4↔C6 仅 SDIO, 官方 RCP (spinel over UART) 路径未布线。 */
        ESP_LOGW(TAG, "802.15.4 via custom CP service: not yet wired (see guide §4)");
        return ESP_ERR_NOT_SUPPORTED;

    case C6_RF_PAIR_BLE_154:
        ESP_LOGW(TAG, "ble+154: requires custom CP service (not yet wired)");
        return ESP_ERR_NOT_SUPPORTED;

    default:
        return ESP_ERR_INVALID_ARG;
    }

    vTaskDelay(pdMS_TO_TICKS(window_ms));
    return ESP_OK;
}

const c6_rf_test_stats_t *c6_rf_test_stats_get(void)
{
    return &s_stats;
}

#endif /* CONFIG_C6_RF_TEST_ENABLED */

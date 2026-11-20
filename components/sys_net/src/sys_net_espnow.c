/**
 * @file sys_net_espnow.c
 * @brief ESP-NOW 邻居发现 —— 每秒广播本机名, 维护 5 秒 TTL 的邻居表
 *
 * 用途: 演示/调试 —— 两台同固件的板子互相发现。协议极简:
 *   载荷 = 本机名 (ASCII, 无 NUL), 每 1s 广播一次;
 *   收到后按 MAC 更新邻居表, 超过 5s 未再收到视为离线。
 *
 * 依赖 WiFi 已开启 (esp_wifi_remote 下 ESP-NOW 与 station 共用射频,
 * 不能与关联的 station 同时收 ESP-NOW 包 —— 所以广播在"已关联"时
 * 只能发出不保证收到; 邻居发现建议在未连网/连 AP 空闲时体验最佳)。
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "sys_net/sys_net.h"
#include "sys_core/sys_event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_SYS_NET_ESPNOW_ENABLE
#include "esp_wifi.h"
#include "esp_now.h"
#endif

static const char *TAG = "sys_net_now";

static bool s_active;
static SemaphoreHandle_t s_peer_lock;
static sys_net_espnow_peer_t s_peers[SYS_NET_PEER_MAX];
static int s_peer_count;
static int s_last_reported = -1;
#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_SYS_NET_ESPNOW_ENABLE
static TaskHandle_t s_beacon_task;
static char s_name[24];
#endif

#define PEER_TTL_MS 5000

/* 过期邻居 (net 上下文调用, 持锁) */
static void peers_expire(void)
{
    uint32_t now = esp_timer_get_time();
    int w = 0;
    for (int i = 0; i < s_peer_count; i++) {
        if (now - s_peers[i].last_seen_ms < PEER_TTL_MS) {
            s_peers[w++] = s_peers[i];
        }
    }
    s_peer_count = w;
}

static void report_peer_count(void)
{
    if (s_peer_count != s_last_reported) {
        s_last_reported = s_peer_count;
        static int n;
        n = s_peer_count;
        sys_event_publish(SYS_EVENT_ESPNOW_PEER, &n);
    }
}

const sys_net_espnow_peer_t *sys_net_espnow_peers(int *count)
{
    if (!s_peer_lock) {
        if (count) {
            *count = 0;
        }
        return NULL;
    }
    xSemaphoreTake(s_peer_lock, portMAX_DELAY);
    peers_expire();
    if (count) {
        *count = s_peer_count;
    }
    xSemaphoreGive(s_peer_lock);
    return s_peers;
}

bool sys_net_espnow_is_active(void)
{
    return s_active;
}

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_SYS_NET_ESPNOW_ENABLE

static void on_now_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len <= 0 || len >= (int)sizeof(s_name)) {
        return;
    }
    if (!s_peer_lock) {
        return;
    }
    xSemaphoreTake(s_peer_lock, portMAX_DELAY);
    peers_expire();
    int idx = -1;
    for (int i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, info->src_addr, 6) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (s_peer_count >= SYS_NET_PEER_MAX) {
            idx = 0; /* 表满顶掉最旧的 */
        } else {
            idx = s_peer_count++;
        }
        memcpy(s_peers[idx].mac, info->src_addr, 6);
    }
    memcpy(s_peers[idx].name, data, (size_t)len);
    s_peers[idx].name[len] = '\0';
    s_peers[idx].last_seen_ms = esp_timer_get_time();
    report_peer_count();
    xSemaphoreGive(s_peer_lock);
    ESP_LOGI(TAG, "peer %s (now %d)", s_peers[idx].name, s_peer_count);
}

static void on_now_send(const esp_now_send_info_t *info, esp_now_send_status_t st)
{
    (void)info;
    if (st != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "beacon send failed: %d", st);
    }
}

static void beacon_task(void *arg)
{
    (void)arg;
    const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_active) {
            continue;
        }
        if (!sys_net_wifi_is_enabled()) {
            continue; /* 射频没开, 不发 */
        }
        esp_now_send(bcast, (const uint8_t *)s_name, (size_t)strlen(s_name));
    }
}

esp_err_t sys_net_espnow_start(void)
{
    if (s_active) {
        return ESP_OK;
    }
    if (!s_peer_lock) {
        s_peer_lock = xSemaphoreCreateMutex();
        if (!s_peer_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    /* 首次: 初始化 ESP-NOW + 注册回调 */
    static bool inited;
    if (!inited) {
        esp_err_t err = esp_now_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
            return err;
        }
        esp_now_register_recv_cb(on_now_recv);
        esp_now_register_send_cb(on_now_send);
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(s_name, sizeof(s_name), "vela-p4-%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
        inited = true;
    }
    s_active = true;
    s_last_reported = -1;
    if (!s_beacon_task) {
        if (xTaskCreate(beacon_task, "espnow_bcn", 4096, NULL, 2, &s_beacon_task) != pdPASS) {
            s_active = false;
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "esp-now active, broadcasting \"%s\"", s_name);
    return ESP_OK;
}

esp_err_t sys_net_espnow_stop(void)
{
    if (!s_active) {
        return ESP_OK;
    }
    s_active = false;
    xSemaphoreTake(s_peer_lock, portMAX_DELAY);
    s_peer_count = 0;
    report_peer_count();
    xSemaphoreGive(s_peer_lock);
    ESP_LOGI(TAG, "esp-now stopped");
    return ESP_OK;
}

#else /* stub: S3/C6 独立目标, 或 P4 未启用 SYS_NET_ESPNOW_ENABLE */

esp_err_t sys_net_espnow_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sys_net_espnow_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif

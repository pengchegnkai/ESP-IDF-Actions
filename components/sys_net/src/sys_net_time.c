/**
 * @file sys_net_time.c
 * @brief NTP 校时 —— lwip SNTP 客户端
 *
 * WiFi 拿到 IP 后 (sys_net.c 的 IP_EVENT_STA_GOT_IP) 自动触发, 也可手动
 * (sys_net_time_sync)。校成一次后 lwip SNTP 按自己的退避周期维持同步
 * (默认约 6 小时), 无需额外任务。
 */
#include <time.h>
#include <string.h>
#include "sdkconfig.h"
#include "sys_net/sys_net.h"
#include "sys_core/sys_event_bus.h"
#include "esp_log.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "lwip/apps/sntp.h"
#endif

static const char *TAG = "sys_net_time";
static volatile bool s_synced;
#if CONFIG_IDF_TARGET_ESP32P4
static bool s_sntp_started;
#endif

bool sys_net_time_is_synced(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (s_synced) {
        return true;
    }
    /* 惰性探测: SNTP 可能在后台悄悄同步成功 */
    return s_sntp_started && sntp_get_sync_status() == SNTP_SYNC_STATUS_SYNCED;
#else
    return false;
#endif
}

uint32_t sys_net_time_epoch(void)
{
    if (!sys_net_time_is_synced()) {
        return 0;
    }
    time_t t = time(NULL);
    return (t > 0) ? (uint32_t)t : 0;
}

esp_err_t sys_net_time_sync(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (s_sntp_started) {
        /* 重新触发一次立即同步 */
        sntp_restart();
        return ESP_OK;
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_set_sync_mode(SNTP_SYNC_MODE_ASYNC);
    esp_err_t err = sntp_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp_init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_sntp_started = true;
    ESP_LOGI(TAG, "sntp started (ntp.aliyun.com)");
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* 校成后广播事件: 由调用侧 (app 层) 轮询 is_synced 变化时发布,
   或这里提供一次性检查钩子 */
void sys_net_time_check_and_report(void)
{
    if (!s_synced && sys_net_time_is_synced()) {
        s_synced = true;
        ESP_LOGI(TAG, "time synced: %lu", (unsigned long)sys_net_time_epoch());
        sys_event_publish(SYS_EVENT_TIME_SYNCED, NULL);
    }
}

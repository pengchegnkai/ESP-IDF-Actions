/**
 * @file sys_net_weather.c
 * @brief 天气服务 —— wttr.in (format=j1, lang=zh) 周期获取
 *
 * 独立 FreeRTOS 任务 (prio 3, 栈 8192), 由 net 任务收到 CMD_WX_START 后
 * 创建 (sys_net_weather_start() 经命令队列注册, 见 sys_net.c)。任务循环:
 *   1. 轮询等待 WiFi 拿到 IP (SYS_NET_WIFI_CONNECTED), WiFi 未连上不占网络
 *   2. GET https://wttr.in/?format=j1&lang=zh (esp_http_client + cJSON)
 *   3. 成功: 更新互斥保护的静态缓存 + 发布 SYS_EVENT_WEATHER
 *      (payload 指向静态缓冲, 指针稳定, 订阅者须在回调内立即读取)
 *      失败: 保留旧缓存, 记日志, 下个周期重试
 * 周期 30 分钟; sys_net_weather_refresh() 入队唤醒提前取一次。
 *
 * 非 P4 目标无 WiFi 协处理器: start/refresh 返回 NOT_SUPPORTED,
 * snapshot 返回空缓存 (valid=false)。
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "sys_net/sys_net.h"
#include "sys_core/sys_event_bus.h"
#include "esp_log.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#endif

static const char *TAG = "sys_net_weather";

#define WX_PERIOD_MS        (30 * 60 * 1000)  /* 30 分钟周期 */
#define WX_HTTP_TIMEOUT_MS  20000
#define WX_WIFI_POLL_MS     5000
#define WX_URL              "https://wttr.in/?format=j1&lang=zh"

static SemaphoreHandle_t s_wx_lock;
static sys_net_weather_t s_cache;        /* 最新缓存 (互斥保护) */
static sys_net_weather_t s_evt_payload;  /* 事件 payload: 静态缓冲, 发布前锁内拷贝 */
static TickType_t s_last_ok_tick;        /* 上次成功 tick (age_sec 用, 不依赖 RTC 校时) */

#if CONFIG_IDF_TARGET_ESP32P4
static QueueHandle_t s_wx_q;             /* 唤醒队列: 任意值 = 立即取一次 */
static volatile bool s_started;
static uint32_t s_alloc_caps;            /* 响应缓冲分配域 (有 PSRAM 用 PSRAM) */

/* 描述尾随空白清理 (英文 "Cloudy " 带尾空格; 统一清理) */
static void wx_rstrip(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

/* 取一次 (wx 任务上下文)。成功更新缓存并发布事件; 失败保留旧缓存只记日志。
   响应约 40~100KB: 动态扩容缓冲读全, 优先放 PSRAM 避免挤占内部 DRAM。 */
static void wx_fetch_once(void)
{
    esp_http_client_config_t cfg = {
        .url = WX_URL,
        .timeout_ms = WX_HTTP_TIMEOUT_MS,
    };
    esp_http_client *client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGW(TAG, "http client init failed (keep old cache)");
        return;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;

    char *body = NULL;
    size_t len = 0;
    if (err == ESP_OK && status == 200) {
        size_t cap = 8192;
        body = heap_caps_malloc(cap, s_alloc_caps);
        if (body) {
            for (;;) {
                if (len + 4096 > cap) {
                    size_t ncap = cap * 2;
                    char *nb = heap_caps_realloc(body, ncap, s_alloc_caps);
                    if (!nb) {
                        break;  /* 扩容失败: 用已读部分尽力解析 */
                    }
                    body = nb;
                    cap = ncap;
                }
                int r = esp_http_client_read_response(client, body + len, 4096);
                if (r <= 0) {
                    break;
                }
                len += (size_t)r;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!body || len == 0) {
        heap_caps_free(body);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "fetch failed: %s (keep old cache)", esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "fetch failed: http %d (keep old cache)", status);
        }
        return;
    }

    cJSON *root = cJSON_ParseWithLength(body, len);
    heap_caps_free(body);
    if (!root) {
        ESP_LOGW(TAG, "json parse failed (keep old cache)");
        return;
    }
    cJSON *cc = cJSON_GetObjectItem(root, "current_condition");
    cJSON *cur = (cc && cJSON_IsArray(cc)) ? cJSON_GetArrayItem(cc, 0) : NULL;
    cJSON *temp_s = cur ? cJSON_GetObjectItem(cur, "temp_C") : NULL;
    cJSON *desc_arr = cur ? cJSON_GetObjectItem(cur, "weatherDesc") : NULL;
    cJSON *desc_item = (desc_arr && cJSON_IsArray(desc_arr))
                           ? cJSON_GetArrayItem(desc_arr, 0) : NULL;
    cJSON *desc_s = desc_item ? cJSON_GetObjectItem(desc_item, "value") : NULL;

    if (!cJSON_IsString(temp_s) || !cJSON_IsString(desc_s)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "unexpected json layout (keep old cache)");
        return;
    }
    char desc[sizeof(s_cache.desc)] = {0};
    strncpy(desc, desc_s->valuestring, sizeof(desc) - 1);
    wx_rstrip(desc);
    int temp = atoi(temp_s->valuestring);
    cJSON_Delete(root);
    if (temp > 60 || temp < -60) {
        ESP_LOGW(TAG, "implausible temp %dC (keep old cache)", temp);
        return;
    }

    /* 成功: 锁内更新缓存, 发布事件 (payload 指静态缓冲, 指针稳定) */
    xSemaphoreTake(s_wx_lock, portMAX_DELAY);
    s_cache.valid = true;
    s_cache.temp_c = (int16_t)temp;
    memcpy(s_cache.desc, desc, sizeof(s_cache.desc));
    s_cache.age_sec = 0;
    s_last_ok_tick = xTaskGetTickCount();
    memcpy(&s_evt_payload, &s_cache, sizeof(s_evt_payload));
    xSemaphoreGive(s_wx_lock);
    ESP_LOGI(TAG, "weather updated: %dC %s", temp, desc);
    sys_event_publish(SYS_EVENT_WEATHER, &s_evt_payload);
}

static void wx_task(void *arg)
{
    (void)arg;
    int wake;
    for (;;) {
        /* 1. 等 WiFi 拿到 IP (每 5s 轮询) */
        for (;;) {
            sys_net_wifi_status_t st;
            sys_net_wifi_get_status(&st);
            if (st.state == SYS_NET_WIFI_CONNECTED) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(WX_WIFI_POLL_MS));
        }
        /* 2. 取一次 */
        wx_fetch_once();
        /* 3. 休眠到下个周期; 先排空过期的立即刷新请求 (刚取过),
           新请求会提前唤醒, 超时即周期到 */
        while (xQueueReceive(s_wx_q, &wake, 0) == pdTRUE) {
        }
        xQueueReceive(s_wx_q, &wake, pdMS_TO_TICKS(WX_PERIOD_MS));
    }
}

void sys_net_weather_task_launch(void)
{
    if (s_started) {
        return;  /* 幂等 */
    }
    if (!s_wx_lock) {
        s_wx_lock = xSemaphoreCreateMutex();
        if (!s_wx_lock) {
            ESP_LOGE(TAG, "mutex create failed");
            return;
        }
    }
    if (!s_wx_q) {
        s_wx_q = xQueueCreate(2, sizeof(int));
        if (!s_wx_q) {
            ESP_LOGE(TAG, "queue create failed");
            return;
        }
    }
    /* 响应缓冲分配域: 有 PSRAM 用 PSRAM (j1 响应可达 ~100KB) */
    s_alloc_caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) > 0
                       ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                       : MALLOC_CAP_DEFAULT;
    if (xTaskCreate(wx_task, "wx", 8192, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "weather task started (period 30 min)");
}
#endif /* CONFIG_IDF_TARGET_ESP32P4 */

/* ========================= 对外接口 ========================= */

esp_err_t sys_net_weather_refresh(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (!s_started || !s_wx_q) {
        return ESP_ERR_INVALID_STATE;
    }
    int wake = 1;
    return xQueueSend(s_wx_q, &wake, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sys_net_weather_snapshot(sys_net_weather_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (sys_net_weather_t){0};
    if (s_wx_lock) {
        xSemaphoreTake(s_wx_lock, portMAX_DELAY);
        out->valid = s_cache.valid;
        out->temp_c = s_cache.temp_c;
        memcpy(out->desc, s_cache.desc, sizeof(out->desc));
        out->age_sec = s_cache.valid
                           ? (uint32_t)((xTaskGetTickCount() - s_last_ok_tick) /
                                        configTICK_RATE_HZ)
                           : 0;
        xSemaphoreGive(s_wx_lock);
    }
    return ESP_OK;
}

#if !CONFIG_IDF_TARGET_ESP32P4
/* 非 P4 目标无天气任务: 保持符号存在 (sys_net.h 声明) */
void sys_net_weather_task_launch(void)
{
}
#endif

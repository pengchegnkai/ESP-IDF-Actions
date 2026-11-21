/**
 * @file sys_net.c
 * @brief 网络子系统 —— WiFi (ESP-Hosted 协处理器) + 状态事件 + 凭据持久化
 *
 * P4 上 WiFi 栈实际跑在板载 ESP32-C6 上, P4 端经 ESP-Hosted (SDIO) 透传
 * 标准 esp_wifi API (esp_wifi_remote 组件), 用法与原生芯片一致。
 *
 * 并发模型: 所有 esp_wifi API 调用集中在 net 任务 (命令队列);
 * WIFI_EVENT/IP_EVENT 回调 (默认事件循环上下文) 只更新状态快照并广播
 * 事件总线事件。状态快照用互斥锁保护; 事件 payload 是静态缓冲的指针,
 * 订阅者必须在回调内立即读取, 不得留存。
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "sys_net/sys_net.h"
#include "sys_core/sys_event_bus.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/ip4_addr.h"
#endif

static const char *TAG = "sys_net";

static bool s_inited;
static SemaphoreHandle_t s_status_lock;
static sys_net_wifi_status_t s_status;   /* 状态快照 (互斥保护) */
static bool s_wifi_enabled;              /* 用户开关 (与链路状态分开) */
static bool s_wifi_started;              /* esp_wifi_start() 已成功
                                            (esp_wifi_remote 不导出 esp_wifi_is_started) */
#if CONFIG_IDF_TARGET_ESP32P4
static int s_retry;                      /* 断开重连计数 */
static QueueHandle_t s_cmd_q;            /* net 任务命令队列 */
#endif

/* 事件 payload: 静态缓冲, 发布前在锁内拷贝 */
static sys_net_wifi_status_t s_evt_payload;

/* ========================= 状态更新/广播 ========================= */

/** 更新状态快照并广播 (任何任务可调用) */
static void wifi_set_state(sys_net_wifi_state_t state, const char *ssid)
{
    if (!s_status_lock) {
        return;
    }
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (s_status.state != state || ssid != NULL) {
        s_status.state = state;
        if (ssid) {
            strncpy(s_status.ssid, ssid, sizeof(s_status.ssid) - 1);
        }
    }
    sys_net_wifi_status_t snap = s_status;
    xSemaphoreGive(s_status_lock);

    memcpy(&s_evt_payload, &snap, sizeof(s_evt_payload));
    sys_event_publish(SYS_EVENT_WIFI_STATUS, &s_evt_payload);
}

void sys_net_wifi_get_status(sys_net_wifi_status_t *st)
{
    if (!st) {
        return;
    }
    if (s_status_lock) {
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        *st = s_status;
        xSemaphoreGive(s_status_lock);
    } else {
        *st = (sys_net_wifi_status_t){0};
    }
}

/* ========================= NVS 凭据 (namespace "net") ========================= */

#if CONFIG_IDF_TARGET_ESP32P4
static void nvs_load_profile(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz)
{
    nvs_handle_t h;
    if (nvs_open("net", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    nvs_get_str(h, "ssid", ssid, &ssid_sz);
    nvs_get_str(h, "pass", pass, &pass_sz);
    nvs_close(h);
}

static void nvs_save_profile(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("net", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
}

/* ========================= net 任务: 所有 esp_wifi API 都在这 ========================= */

typedef enum {
    CMD_ENABLE,        /* arg: bool (int) */
    CMD_CONNECT,       /* arg: connect_args_t* (一次性结构, 任务内释放) */
    CMD_SCAN,          /* arg: scan_args_t* */
} cmd_type_t;

typedef struct {
    char ssid[SYS_NET_SSID_MAX];
    char pass[65];
} connect_args_t;

typedef struct {
    sys_net_scan_cb_t cb;
    void *ctx;
} scan_args_t;

typedef struct {
    cmd_type_t type;
    int iarg;
    void *varg;
} cmd_t;

static void net_cmd_enable(bool enable);
static void net_cmd_connect(const char *ssid, const char *pass);
static void net_cmd_scan(const sys_net_scan_cb_t cb, void *ctx);

static void net_task(void *arg)
{
    (void)arg;
    cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (cmd.type) {
        case CMD_ENABLE:
            net_cmd_enable(cmd.iarg != 0);
            break;
        case CMD_CONNECT: {
            connect_args_t *a = cmd.varg;
            net_cmd_connect(a->ssid, a->pass);
            free(a);
            break;
        }
        case CMD_SCAN: {
            scan_args_t *a = cmd.varg;
            net_cmd_scan(a->cb, a->ctx);
            free(a);
            break;
        }
        default:
            break;
        }
    }
}

static esp_err_t post_cmd(cmd_type_t type, int iarg, void *varg)
{
    /* 不能直接把复合字面量塞进 xQueueSend: 逗号会被预处理器当成多个参数 */
    cmd_t c = { .type = type, .iarg = iarg, .varg = varg };
    return xQueueSend(s_cmd_q, &c, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

/* ---------- 命令实现 (net 任务上下文) ---------- */

static void net_cmd_enable(bool enable)
{
    if (enable && !s_wifi_enabled) {
        s_wifi_enabled = true;
        s_retry = 0;
        /* 开启 WiFi: 首次调用才建栈 (hosted 链路在此建立, 耗时数秒) */
        static bool stack_up;
        if (!stack_up) {
            ESP_LOGI(TAG, "wifi stack init (hosted link up)...");
            esp_netif_init();
            /* 默认事件循环已在 sys_net_init 建过 (事件 handler 注册依赖它);
               重复创建返回 INVALID_STATE, 属正常, 忽略即可 */
            esp_event_loop_create_default();
            esp_netif_create_default_wifi_sta();
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            if (esp_wifi_init(&cfg) != ESP_OK) {
                s_wifi_enabled = false;
                wifi_set_state(SYS_NET_WIFI_ERROR, NULL);
                return;
            }
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
            if (esp_wifi_start() != ESP_OK) {
                s_wifi_enabled = false;
                wifi_set_state(SYS_NET_WIFI_ERROR, NULL);
                return;
            }
            stack_up = true;
            s_wifi_started = true;
            ESP_LOGI(TAG, "wifi stack up (C6 coprocessor)");
        }
        /* 有凭据则自动连接 */
        char ssid[SYS_NET_SSID_MAX] = {0}, pass[65] = {0};
        nvs_load_profile(ssid, sizeof(ssid), pass, sizeof(pass));
        if (ssid[0]) {
            net_cmd_connect(ssid, pass);
        } else {
            wifi_set_state(SYS_NET_WIFI_OFF, NULL);
        }
    } else if (!enable && s_wifi_enabled) {
        s_wifi_enabled = false;
        if (s_wifi_started) {
            esp_wifi_disconnect();
            esp_wifi_stop();
            s_wifi_started = false;
        }
        wifi_set_state(SYS_NET_WIFI_OFF, NULL);
        s_status.ssid[0] = '\0';
    }
}

static void net_cmd_connect(const char *ssid, const char *pass)
{
    if (!s_wifi_enabled || !s_wifi_started) {
        return;
    }
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (pass && pass[0]) {
        strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    nvs_save_profile(ssid, pass);
    s_retry = 0;
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        wifi_set_state(SYS_NET_WIFI_ERROR, ssid);
        return;
    }
    ESP_LOGI(TAG, "connecting to \"%s\"...", ssid);
    esp_wifi_connect();
}

static void net_cmd_scan(const sys_net_scan_cb_t cb, void *ctx)
{
    sys_net_ap_info_t aps[SYS_NET_SCAN_MAX];
    int n = 0;
    esp_err_t err = ESP_OK;
    if (!s_wifi_enabled || !s_wifi_started) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        wifi_scan_config_t scan_cfg = {
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        };
        if (esp_wifi_scan_start(&scan_cfg, false) == ESP_OK) {
            uint16_t ap_num = 0;
            static wifi_ap_record_t recs[SYS_NET_SCAN_MAX];
            if (esp_wifi_scan_get_ap_num(&ap_num) == ESP_OK) {
                uint16_t want = ap_num > SYS_NET_SCAN_MAX ? SYS_NET_SCAN_MAX : ap_num;
                if (want == 0 ||
                    esp_wifi_scan_get_ap_records(&want, recs) == ESP_OK) {
                    for (uint16_t i = 0; i < want; i++) {
                        if (!recs[i].ssid[0]) {
                            continue;  /* 隐藏 SSID 跳过 */
                        }
                        aps[n].rssi = recs[i].rssi;
                        aps[n].auth = (uint8_t)recs[i].authmode;
                        strncpy(aps[n].ssid, (const char *)recs[i].ssid,
                                sizeof(aps[n].ssid) - 1);
                        n++;
                    }
                }
            }
        } else {
            err = ESP_ERR_NOT_SUPPORTED; /* scan busy 等 */
        }
    }
    /* 按信号强度降序 (简单插入排序, 数量小) */
    for (int i = 1; i < n; i++) {
        sys_net_ap_info_t key = aps[i];
        int j = i - 1;
        while (j >= 0 && aps[j].rssi < key.rssi) {
            aps[j + 1] = aps[j];
            j--;
        }
        aps[j + 1] = key;
    }
    ESP_LOGI(TAG, "scan done: %d APs", n);
    if (cb) {
        cb(aps, n, err, ctx);
    }
}

/* ---------- WIFI_EVENT / IP_EVENT (默认事件循环上下文) ---------- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            /* 栈刚起来: 有开关状态但还没凭据连接时, 这里补一次 (enable 已触发过) */
            char ssid[SYS_NET_SSID_MAX] = {0}, pass[65] = {0};
            nvs_load_profile(ssid, sizeof(ssid), pass, sizeof(pass));
            if (s_wifi_enabled && ssid[0]) {
                net_cmd_connect(ssid, pass);
            }
        } else if (id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t *ev = data;
            s_retry = 0;
            /* 该事件结构体没有 rssi 字段, 关联后单独取 */
            wifi_ap_record_t ap = {0};
            int rssi = 0;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                rssi = ap.rssi;
            }
            ESP_LOGI(TAG, "associated with \"%s\" (rssi %d)",
                     (const char *)ev->ssid, rssi);
            xSemaphoreTake(s_status_lock, portMAX_DELAY);
            s_status.rssi = (int8_t)rssi;
            xSemaphoreGive(s_status_lock);
            wifi_set_state(SYS_NET_WIFI_CONNECTING, (const char *)ev->ssid);
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            if (!s_wifi_enabled) {
                return;
            }
            s_retry++;
            if (s_retry <= 5) {
                ESP_LOGW(TAG, "disconnected, retry %d/5", s_retry);
                esp_wifi_connect();
                wifi_set_state(SYS_NET_WIFI_CONNECTING, NULL);
            } else {
                wifi_set_state(SYS_NET_WIFI_ERROR, NULL);
                sys_event_report_net(false);
            }
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *ev = data;
            char ip[16] = {0};
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));
            xSemaphoreTake(s_status_lock, portMAX_DELAY);
            strncpy(s_status.ip, ip, sizeof(s_status.ip) - 1);
            xSemaphoreGive(s_status_lock);
            ESP_LOGI(TAG, "GOT IP: %s", ip);
            wifi_set_state(SYS_NET_WIFI_CONNECTED, NULL);
            sys_event_report_net(true);
            /* 拿到 IP 后自动校时 (声明见 sys_net.h) */
            sys_net_time_sync();
        } else if (id == IP_EVENT_STA_LOST_IP) {
            xSemaphoreTake(s_status_lock, portMAX_DELAY);
            s_status.ip[0] = '\0';
            xSemaphoreGive(s_status_lock);
            wifi_set_state(SYS_NET_WIFI_CONNECTING, NULL);
        }
    }
}
#endif /* CONFIG_IDF_TARGET_ESP32P4 */

/* ========================= 对外接口 ========================= */

esp_err_t sys_net_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    s_status_lock = xSemaphoreCreateMutex();
    if (!s_status_lock) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));

#if CONFIG_IDF_TARGET_ESP32P4
    /* 队列稍深一点: UI 线程(持显示锁)会向这里 post 命令,
       队列满时 post 会阻塞 LVGL 适配任务, 8 个槽位足够吸收突发 */
    s_cmd_q = xQueueCreate(8, sizeof(cmd_t));
    if (!s_cmd_q) {
        return ESP_ERR_NO_MEM;
    }
    /* 默认事件循环必须先建: WIFI_EVENT/IP_EVENT 都挂在上面,
       未建时 esp_event_handler_register 返回 INVALID_STATE 导致 abort。
       已存在时返回 INVALID_STATE, 忽略即可 */
    esp_netif_init();
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP | IP_EVENT_STA_LOST_IP,
                                               on_wifi_event, NULL));
    if (xTaskCreate(net_task, "sys_net", 8192, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#else
    ESP_LOGW(TAG, "no WiFi coprocessor on this target (stub)");
#endif

    s_inited = true;
    return ESP_OK;
}

esp_err_t sys_net_wifi_enable(bool enable)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    return post_cmd(CMD_ENABLE, enable ? 1 : 0, NULL);
#else
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool sys_net_wifi_is_enabled(void)
{
    return s_wifi_enabled;
}

esp_err_t sys_net_wifi_connect(const char *ssid, const char *password)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (!s_inited || !ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    connect_args_t *a = malloc(sizeof(*a));
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    strncpy(a->ssid, ssid, sizeof(a->ssid) - 1);
    if (password) {
        strncpy(a->pass, password, sizeof(a->pass) - 1);
    }
    esp_err_t err = post_cmd(CMD_CONNECT, 0, a);
    if (err != ESP_OK) {
        free(a);
    }
    return err;
#else
    (void)ssid; (void)password;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sys_net_wifi_scan(sys_net_scan_cb_t cb, void *ctx)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (!s_inited || !cb) {
        return ESP_ERR_INVALID_ARG;
    }
    scan_args_t *a = malloc(sizeof(*a));
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    a->cb = cb;
    a->ctx = ctx;
    esp_err_t err = post_cmd(CMD_SCAN, 0, a);
    if (err != ESP_OK) {
        free(a);
    }
    return err;
#else
    (void)cb; (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

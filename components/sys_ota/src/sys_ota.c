/**
 * @file sys_ota.c
 * @brief 网络 OTA 实现 —— GitHub Release 检查 / 下载 / SHA256 校验 / OTA 安装
 *
 * 线程模型: 内部 ota 任务 (prio 3, 栈 16384), 通过命令队列异步执行。
 * P4: 完整实现; S3/C6: stub (NOT_SUPPORTED)。
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "sys_ota/sys_ota.h"
#include "sys_core/sys_event_bus.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "psa/crypto.h"   /* mbedtls 4.x: 经典 mbedtls/sha256.h 已移除, 用 PSA API */
#include "cJSON.h"

static const char *TAG = "sys_ota";

const char *sys_ota_current_version(void)
{
    return CONFIG_APP_PROJECT_VER;
}

#if CONFIG_IDF_TARGET_ESP32P4

/* ---------- 常量 ---------- */
#define OTA_API_URL       "https://api.github.com/repos/pengchegnkai/ESP-IDF-Actions/releases/latest"
#define OTA_ASSET_NAME    "demo.bin"
#define OTA_BUF_MAX       (2500 * 1024)   /* PSRAM 下载缓冲上限 ~2.5MB */
#define OTA_CHUNK         4096            /* 写入/读取分片 */
#define OTA_HTTP_TIMEOUT  15000           /* API 请求超时 ms */
#define OTA_DL_TIMEOUT    30000           /* 下载连接超时 ms (非总时长) */
#define OTA_TASK_PRIO     3
#define OTA_TASK_STACK    16384

/* ---------- 内部状态 ---------- */
typedef enum { CMD_CHECK } ota_cmd_t;

static QueueHandle_t s_cmd_q = NULL;
static volatile bool s_busy = false;
static bool s_inited = false;

/* 事件数据: 静态 (同时只有一次 OTA, 发布后立即被事件循环消费) */
static sys_ota_evt_t s_evt = {0};

static void ota_publish(int stage, int percent)
{
    s_evt.stage   = stage;
    s_evt.percent = percent;
    sys_event_publish(SYS_EVENT_OTA, &s_evt);
}

/* ---------- 工具函数 ---------- */

/** 从 "build-62" 提取 62 */
static int parse_build_num(const char *ver)
{
    if (!ver) return -1;
    const char *dash = strchr(ver, '-');
    if (!dash) return -1;
    return atoi(dash + 1);
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** 将 64 字符 hex 串转 32 字节; 失败返回 false */
static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    if (!hex || strlen(hex) != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* ---------- Stage 1: GitHub API ---------- */

/**
 * 查询 latest release, 提取 tag_name / demo.bin 的 browser_download_url / digest
 */
static esp_err_t ota_api_check(char *dl_url, size_t url_sz,
                               char *sha_hex, size_t sha_sz,
                               char *tag, size_t tag_sz)
{
    esp_http_client_config_t cfg = {
        .url = OTA_API_URL,
        .timeout_ms = OTA_HTTP_TIMEOUT,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "API open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(cli);
        return err;
    }

    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGE(TAG, "API HTTP %d", status);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }

    int clen = esp_http_client_fetch_headers(cli);
    if (clen <= 0) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }

    char *resp = malloc((size_t)clen + 1);
    if (!resp) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }

    int total = 0;
    while (total < clen) {
        int r = esp_http_client_read(cli, resp + total, clen - total);
        if (r <= 0) break;
        total += r;
    }
    resp[total] = '\0';
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);

    /* 解析 JSON */
    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        ESP_LOGE(TAG, "API JSON parse fail");
        return ESP_FAIL;
    }

    /* tag_name */
    cJSON *jt = cJSON_GetObjectItem(root, "tag_name");
    if (!cJSON_IsString(jt)) { cJSON_Delete(root); return ESP_FAIL; }
    snprintf(tag, tag_sz, "%s", jt->valuestring);

    /* assets[] 中找 demo.bin */
    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (!cJSON_IsArray(assets)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "no assets[]");
        return ESP_FAIL;
    }

    bool found = false;
    cJSON *a;
    cJSON_ArrayForEach(a, assets) {
        cJSON *nm = cJSON_GetObjectItem(a, "name");
        if (!cJSON_IsString(nm)) continue;
        if (strcmp(nm->valuestring, OTA_ASSET_NAME) != 0) continue;

        cJSON *u = cJSON_GetObjectItem(a, "browser_download_url");
        if (cJSON_IsString(u)) {
            snprintf(dl_url, url_sz, "%s", u->valuestring);
        }
        /* digest: SHA256 hex (2025.6+ 新增字段, 旧 release 为 null) */
        cJSON *dg = cJSON_GetObjectItem(a, "digest");
        if (cJSON_IsString(dg) && dg->valuestring[0]) {
            snprintf(sha_hex, sha_sz, "%s", dg->valuestring);
        } else {
            sha_hex[0] = '\0';
        }
        found = true;
        break;
    }

    cJSON_Delete(root);
    if (!found) {
        ESP_LOGE(TAG, "%s not in assets", OTA_ASSET_NAME);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---------- Stage 3: 下载到 PSRAM + 流式 SHA256 ---------- */

static esp_err_t ota_download(const char *url, uint8_t **out_buf,
                              size_t *out_len, uint8_t sha_out[32])
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = OTA_DL_TIMEOUT,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = OTA_CHUNK,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dl open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(cli);
        return err;
    }

    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGE(TAG, "dl HTTP %d", status);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }

    esp_http_client_fetch_headers(cli);
    int64_t clen = esp_http_client_get_content_length(cli);
    if (clen <= 0 || clen > (int64_t)OTA_BUF_MAX) {
        ESP_LOGE(TAG, "bad content-length %lld (max %d)",
                 (long long)clen, OTA_BUF_MAX);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }

    uint8_t *buf = heap_caps_malloc((size_t)clen, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM alloc %lld fail", (long long)clen);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }

    /* 流式 SHA256 (PSA multipart; IDF v6.1 mbedtls 4.x 无经典 sha256 接口) */
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&sha, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa sha setup failed");
        heap_caps_free(buf);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }

    ota_publish(3, 0);

    int64_t rd = 0;
    int last_pct = -1;
    while (rd < clen) {
        int want = OTA_CHUNK;
        if (clen - rd < want) want = (int)(clen - rd);
        int r = esp_http_client_read(cli, buf + rd, want);
        if (r <= 0) {
            ESP_LOGE(TAG, "dl read err at %lld/%lld", (long long)rd, (long long)clen);
            psa_hash_abort(&sha);
            heap_caps_free(buf);
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            return ESP_FAIL;
        }
        rd += r;
        if (psa_hash_update(&sha, buf + rd - r, (size_t)r) != PSA_SUCCESS) {
            ESP_LOGE(TAG, "sha update failed");
            psa_hash_abort(&sha);
            heap_caps_free(buf);
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            return ESP_FAIL;
        }

        int pct = (int)(rd * 100 / clen);
        if (pct != last_pct) {
            last_pct = pct;
            ota_publish(3, pct);
        }
    }

    uint8_t result[32];
    size_t hash_len = 0;
    psa_status_t fin = psa_hash_finish(&sha, result, sizeof(result), &hash_len);
    if (fin != PSA_SUCCESS) {
        ESP_LOGE(TAG, "sha finish failed: %d", (int)fin);
        heap_caps_free(buf);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }
    memcpy(sha_out, result, 32);

    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);

    *out_buf = buf;
    *out_len = (size_t)rd;
    return ESP_OK;
}

/* ---------- Stage 4: 写入 OTA 分区 ---------- */

static esp_err_t ota_install(const uint8_t *buf, size_t len)
{
    ota_publish(4, 0);

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        ESP_LOGE(TAG, "no update partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (len > part->size) {
        ESP_LOGE(TAG, "image %u > partition %u", (unsigned)len, (unsigned)part->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t h;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin: %s", esp_err_to_name(err));
        return err;
    }

    size_t off = 0;
    while (off < len) {
        size_t chunk = OTA_CHUNK;
        if (len - off < chunk) chunk = len - off;
        err = esp_ota_write(h, buf + off, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_write @%u: %s", (unsigned)off, esp_err_to_name(err));
            esp_ota_abort(h);
            return err;
        }
        off += chunk;
    }

    err = esp_ota_end(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_end: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/* ---------- 完整 OTA 流程 (ota 任务上下文) ---------- */

static void ota_do_check(void)
{
    s_busy = true;
    ota_publish(1, 0);  /* checking */

    char dl_url[512] = {0};
    char sha_hex[64]  = {0};
    char tag[64]      = {0};

    /* 1. API 查询 */
    esp_err_t err = ota_api_check(dl_url, sizeof(dl_url),
                                  sha_hex, sizeof(sha_hex),
                                  tag, sizeof(tag));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "API check fail: %s", esp_err_to_name(err));
        ota_publish(6, 0);
        s_busy = false;
        return;
    }

    /* 2. 版本门 */
    int local = parse_build_num(CONFIG_APP_PROJECT_VER);
    int remote = parse_build_num(tag);
    ESP_LOGI(TAG, "local=%d remote=%d (tag=%s)", local, remote, tag);
    if (remote <= local) {
        ESP_LOGI(TAG, "up to date");
        ota_publish(5, 0);
        s_busy = false;
        return;
    }

    ota_publish(2, 0);  /* found */

    /* 3. 下载 */
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    uint8_t sha_computed[32];

    err = ota_download(dl_url, &buf, &buf_len, sha_computed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "download fail: %s", esp_err_to_name(err));
        ota_publish(6, 0);
        s_busy = false;
        return;
    }

    /* 4. 校验 */
    bool verified = false;
    if (sha_hex[0] != '\0') {
        /* GitHub 提供了 digest → 严格 SHA256 比对 */
        uint8_t expected[32];
        if (hex_to_bytes(sha_hex, expected, 32)) {
            verified = (memcmp(sha_computed, expected, 32) == 0);
            if (!verified) {
                ESP_LOGE(TAG, "SHA256 MISMATCH - abort");
            }
        } else {
            ESP_LOGW(TAG, "malformed digest, fallback to magic");
            verified = (buf[0] == 0xE9);
        }
    } else {
        /* 无 digest (旧 release) → 魔数检查 */
        verified = (buf[0] == 0xE9);
        if (!verified) {
            ESP_LOGE(TAG, "bad image magic: 0x%02X", buf[0]);
        }
    }
    if (!verified) {
        heap_caps_free(buf);
        ota_publish(6, 0);
        s_busy = false;
        return;
    }

    /* 5. 安装 */
    err = ota_install(buf, buf_len);
    heap_caps_free(buf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "install fail: %s", esp_err_to_name(err));
        ota_publish(6, 0);
        s_busy = false;
        return;
    }

    ota_publish(5, 0);  /* ok */
    ESP_LOGI(TAG, "OTA done, restarting in 2s");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

/* ---------- ota 任务 ---------- */

static void ota_task(void *arg)
{
    (void)arg;
    for (;;) {
        int cmd;
        if (xQueueReceive(s_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd == CMD_CHECK) {
            ota_do_check();
        }
    }
}

/* ---------- 回滚保护: 延迟 mark valid ---------- */

static void ota_mark_valid_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(30000));
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA valid, rollback cancelled");
    } else {
        ESP_LOGW(TAG, "mark valid: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

/* ========================= Public API (P4) ========================= */

esp_err_t sys_ota_init(void)
{
    if (s_inited) return ESP_OK;
    s_cmd_q = xQueueCreate(2, sizeof(int));
    if (!s_cmd_q) return ESP_ERR_NO_MEM;
    if (xTaskCreate(ota_task, "sys_ota", OTA_TASK_STACK, NULL,
                    OTA_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_inited = true;
    ESP_LOGI(TAG, "init (ver: %s)", CONFIG_APP_PROJECT_VER);
    return ESP_OK;
}

esp_err_t sys_ota_check_update(void)
{
    if (!s_inited || s_busy) return ESP_ERR_INVALID_STATE;
    int cmd = CMD_CHECK;
    return xQueueSend(s_cmd_q, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void sys_ota_boot_check(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA) return;

    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) != ESP_OK) return;
    if (st != ESP_OTA_IMG_PENDING_VERIFY) return;

    ESP_LOGI(TAG, "pending verify → mark valid in 30s");
    xTaskCreate(ota_mark_valid_task, "ota_mk", 2048, NULL, 2, NULL);
}

#else  /* S3 / C6: stub */

esp_err_t sys_ota_init(void)
{
    ESP_LOGW(TAG, "not supported (stub)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sys_ota_check_update(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void sys_ota_boot_check(void)
{
    /* no-op */
}

#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

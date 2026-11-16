/**
 * @file sys_settings.c
 * @brief 用户设置实现 —— NVS 命名空间 "sys", 每次修改即写入 (write-through)
 */
#include <stdint.h>
#include "sys_core/sys_settings.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";

#define NVS_NS     "sys"
#define KEY_BRIGHT "bright"
#define KEY_VOLUME "vol"
#define KEY_SFX    "sfx"

static int  s_brightness = 100;
static int  s_volume     = 60;
static bool s_sfx        = true;

esp_err_t sys_settings_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t i;
        uint8_t b;
        if (nvs_get_i32(h, KEY_BRIGHT, &i) == ESP_OK) {
            s_brightness = (int)i;
        }
        if (nvs_get_i32(h, KEY_VOLUME, &i) == ESP_OK) {
            s_volume = (int)i;
        }
        if (nvs_get_u8(h, KEY_SFX, &b) == ESP_OK) {
            s_sfx = (bool)b;
        }
        nvs_close(h);
    }

    /* 范围校验 (防 NVS 数据损坏) */
    if (s_brightness < 0)   s_brightness = 0;
    if (s_brightness > 100) s_brightness = 100;
    if (s_volume < 0)       s_volume = 0;
    if (s_volume > 100)     s_volume = 100;

    ESP_LOGI(TAG, "settings loaded: brightness=%d volume=%d sfx=%s",
             s_brightness, s_volume, s_sfx ? "on" : "off");
    return ESP_OK;
}

static esp_err_t nvs_put(const char *key, int32_t val_is_i32, int32_t i32v, uint8_t u8v)
{
    (void)val_is_i32;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_err_t err = val_is_i32 ? nvs_set_i32(h, key, i32v) : nvs_set_u8(h, key, u8v);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t sys_settings_get_brightness(int *v)
{
    if (!v) {
        return ESP_ERR_INVALID_ARG;
    }
    *v = s_brightness;
    return ESP_OK;
}

esp_err_t sys_settings_set_brightness(int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 100) {
        v = 100;
    }
    s_brightness = v;
    return nvs_put(KEY_BRIGHT, 1, v, 0);
}

esp_err_t sys_settings_get_volume(int *v)
{
    if (!v) {
        return ESP_ERR_INVALID_ARG;
    }
    *v = s_volume;
    return ESP_OK;
}

esp_err_t sys_settings_set_volume(int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 100) {
        v = 100;
    }
    s_volume = v;
    return nvs_put(KEY_VOLUME, 1, v, 0);
}

esp_err_t sys_settings_get_sfx(bool *v)
{
    if (!v) {
        return ESP_ERR_INVALID_ARG;
    }
    *v = s_sfx;
    return ESP_OK;
}

esp_err_t sys_settings_set_sfx(bool v)
{
    s_sfx = v;
    return nvs_put(KEY_SFX, 0, 0, v ? 1 : 0);
}

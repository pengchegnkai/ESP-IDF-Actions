/**
 * @file sys_hal.c
 * @brief 硬件抽象层实现
 *
 * 当前为框架桩(stub): 只建立抽象层结构, 待目标板卡(P4-Function-EV 等)确定后,
 * 在各 init 内填充真实外设驱动 (mipi_dsi/i80, gt911/chsc, es8311, i2c_master 等)。
 * 上层代码依赖本头文件, 因此换板只改本文件, 应用零改动。
 */
#include <string.h>
#include <stdlib.h>
#include "sys_hal/sys_hal.h"
#include "esp_log.h"

static const char *TAG = "sys_hal";

/* ---------- Display ---------- */
struct sys_hal_display {
    sys_hal_display_cfg_t cfg;
    void *panel; /* 具体驱动面板对象 (stub) */
};

esp_err_t sys_hal_display_init(const sys_hal_display_cfg_t *cfg, sys_hal_display_t **out)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    sys_hal_display_t *d = calloc(1, sizeof(*d));
    if (!d) {
        return ESP_ERR_NO_MEM;
    }
    d->cfg = *cfg;
    /* TODO(board): 按目标板初始化 MIPI-DSI / RGB / SPI 显示面板 */
    ESP_LOGI(TAG, "display stub %ux%u", cfg->h_res, cfg->v_res);
    *out = d;
    return ESP_OK;
}

/* ---------- Touch ---------- */
struct sys_hal_touch {
    sys_hal_touch_cfg_t cfg;
};

esp_err_t sys_hal_touch_init(const sys_hal_touch_cfg_t *cfg, sys_hal_touch_t **out)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    sys_hal_touch_t *t = calloc(1, sizeof(*t));
    if (!t) {
        return ESP_ERR_NO_MEM;
    }
    t->cfg = *cfg;
    /* TODO(board): 按目标板初始化电容触摸控制器 */
    ESP_LOGI(TAG, "touch stub %ux%u", cfg->x_max, cfg->y_max);
    *out = t;
    return ESP_OK;
}

esp_err_t sys_hal_touch_read(sys_hal_touch_t *t, uint16_t *x, uint16_t *y, bool *pressed)
{
    if (!t || !x || !y || !pressed) {
        return ESP_ERR_INVALID_ARG;
    }
    *pressed = false; /* stub: 永远无触摸 */
    return ESP_OK;
}

/* ---------- Audio ---------- */
struct sys_hal_audio {
    sys_hal_audio_cfg_t cfg;
};

esp_err_t sys_hal_audio_init(const sys_hal_audio_cfg_t *cfg, sys_hal_audio_t **out)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    sys_hal_audio_t *a = calloc(1, sizeof(*a));
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    a->cfg = *cfg;
    /* TODO(board): 初始化 I2S + codec */
    ESP_LOGI(TAG, "audio stub %luHz x%u ch", (unsigned long)cfg->sample_rate, cfg->channels);
    *out = a;
    return ESP_OK;
}

/* ---------- I2C ---------- */
struct sys_hal_i2c {
    sys_hal_i2c_cfg_t cfg;
    void *bus; /* i2c_master_bus_handle_t (stub) */
};

esp_err_t sys_hal_i2c_init(const sys_hal_i2c_cfg_t *cfg, sys_hal_i2c_t **out)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    sys_hal_i2c_t *b = calloc(1, sizeof(*b));
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    b->cfg = *cfg;
    /* TODO(board): i2c_new_master_bus(...) */
    ESP_LOGI(TAG, "i2c stub port%u sda=%d scl=%d", cfg->port, cfg->sda_gpio, cfg->scl_gpio);
    *out = b;
    return ESP_OK;
}

esp_err_t sys_hal_i2c_write_reg(sys_hal_i2c_t *bus, uint8_t addr,
                                uint8_t reg, const uint8_t *data, size_t len)
{
    if (!bus || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    /* TODO(board): i2c_master_transmit_dev(...) */
    return ESP_OK;
}

esp_err_t sys_hal_i2c_read_reg(sys_hal_i2c_t *bus, uint8_t addr,
                               uint8_t reg, uint8_t *data, size_t len)
{
    if (!bus || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    /* TODO(board): i2c_master_receive_dev(...) */
    memset(data, 0, len);
    return ESP_OK;
}

/**
 * @file sys_hal.c
 * @brief 硬件抽象层实现
 *
 * ESP32-P4 (微雪 ESP32-P4-WIFI6-Touch-LCD-4.3) 上走官方 BSP:
 *   - 显示: MIPI-DSI 2-lane + ST7701 面板 (BSP 内封装 esp_lcd + esp_lv_adapter)
 *   - 触摸: GT911 (BSP 注册为 LVGL 输入设备)
 * 其他芯片目标保持 stub 实现, 保证 CI 三芯片均可编译。
 */
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "sys_hal/sys_hal.h"
#include "esp_log.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#endif

/* 非 P4 目标用普通递归互斥量模拟显示锁 */
#if !CONFIG_IDF_TARGET_ESP32P4
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_disp_mutex;

static SemaphoreHandle_t disp_mutex(void)
{
    if (!s_disp_mutex) {
        s_disp_mutex = xSemaphoreCreateRecursiveMutex();
    }
    return s_disp_mutex;
}
#endif

static const char *TAG = "sys_hal";

/* ---------- Display ---------- */
struct sys_hal_display {
    sys_hal_display_cfg_t cfg;
    void *panel; /* 具体驱动面板对象 (P4: bsp 内部管理) */
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

#if CONFIG_IDF_TARGET_ESP32P4
    /* BSP 自带 LVGL 适配器: 内部完成 DSI/ST7701 初始化、注册显示与 GT911 触摸 */
    bsp_display_cfg_t bsp_cfg = {
        .lv_adapter_cfg   = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation         = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode  = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags      = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    esp_err_t err = bsp_display_start_with_config(&bsp_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed: %s", esp_err_to_name(err));
        free(d);
        return err;
    }
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "display ready (BSP): %ux%u ST7701 MIPI-DSI", (unsigned)cfg->h_res, (unsigned)cfg->v_res);
#else
    /* TODO(board): 非 P4 目标暂为 stub */
    ESP_LOGW(TAG, "display stub %ux%u (no panel driver for this target)",
             (unsigned)cfg->h_res, (unsigned)cfg->v_res);
#endif

    *out = d;
    return ESP_OK;
}

void sys_hal_display_lock(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    bsp_display_lock(-1);
#else
    if (disp_mutex()) {
        xSemaphoreTakeRecursive(disp_mutex(), portMAX_DELAY);
    }
#endif
}

void sys_hal_display_unlock(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    bsp_display_unlock();
#else
    if (s_disp_mutex) {
        xSemaphoreGiveRecursive(s_disp_mutex);
    }
#endif
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
#if CONFIG_IDF_TARGET_ESP32P4
    /* GT911 已由 bsp_display_start_with_config 注册为 LVGL 输入设备 */
    ESP_LOGI(TAG, "touch ready (BSP): GT911 %ux%u", (unsigned)cfg->x_max, (unsigned)cfg->y_max);
#else
    ESP_LOGW(TAG, "touch stub %ux%u", (unsigned)cfg->x_max, (unsigned)cfg->y_max);
#endif
    *out = t;
    return ESP_OK;
}

esp_err_t sys_hal_touch_read(sys_hal_touch_t *t, uint16_t *x, uint16_t *y, bool *pressed)
{
    if (!t || !x || !y || !pressed) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 触摸事件由 LVGL 输入设备异步消费; 此接口仅供需要轮询的场合 */
    *pressed = false;
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
    /* TODO(board): ES8311(输出) + ES7210(双麦) 的 I2S 初始化 */
    ESP_LOGI(TAG, "audio stub %luHz x%u ch", (unsigned long)cfg->sample_rate, (unsigned)cfg->channels);
    *out = a;
    return ESP_OK;
}

/* ---------- I2C ---------- */
struct sys_hal_i2c {
    sys_hal_i2c_cfg_t cfg;
    void *bus; /* i2c_master_bus_handle_t */
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
    /* TODO(board): i2c_new_master_bus(...) — GT911/ES8311/ES7210 均挂在该总线上 */
    ESP_LOGI(TAG, "i2c stub port%u sda=%d scl=%d",
             (unsigned)cfg->port, cfg->sda_gpio, cfg->scl_gpio);
    *out = b;
    return ESP_OK;
}

esp_err_t sys_hal_i2c_write_reg(sys_hal_i2c_t *bus, uint8_t addr,
                                uint8_t reg, const uint8_t *data, size_t len)
{
    if (!bus || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)addr; (void)reg; (void)len;
    return ESP_OK; /* TODO(board): i2c_master_transmit */
}

esp_err_t sys_hal_i2c_read_reg(sys_hal_i2c_t *bus, uint8_t addr,
                               uint8_t reg, uint8_t *data, size_t len)
{
    if (!bus || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)addr; (void)reg;
    memset(data, 0, len); /* TODO(board): i2c_master_transmit_receive */
    return ESP_OK;
}

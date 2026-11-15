/**
 * @file sys_hal.c
 * @brief 硬件抽象层实现 —— 微雪 ESP32-P4-WIFI6-Touch-LCD-4.3
 *
 * P4 目标:
 *   显示/触摸: 官方 BSP (MIPI-DSI + ST7701, GT911, LVGL 适配)
 *   音频:      BSP + esp_codec_dev (ES8311 扬声器 / ES7210 双麦)
 * I2C:         IDF i2c_master 新驱动, 优先复用 BSP 已创建的总线 (GT911/ES8311/ES7210 共用)
 * 其他芯片目标: 显示/触摸/音频降级为 stub, I2C 使用同一驱动 (通用)
 */
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "sys_hal/sys_hal.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_codec_dev.h"
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

#define I2C_XFER_MAX 32
#define I2C_TIMEOUT_MS 100

static const char *TAG = "sys_hal";

/* ========================= Display ========================= */
struct sys_hal_display {
    sys_hal_display_cfg_t cfg;
    void *panel;
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
    /* BSP 内部完成 DSI/ST7701 初始化、注册 LVGL 显示与 GT911 触摸 */
    bsp_display_cfg_t bsp_cfg = {
        .lv_adapter_cfg   = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation         = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode  = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags      = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    lv_display_t *disp = bsp_display_start_with_config(&bsp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        free(d);
        return ESP_FAIL;
    }
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "display ready (BSP): %ux%u ST7701 MIPI-DSI",
             (unsigned)cfg->h_res, (unsigned)cfg->v_res);
#else
    ESP_LOGW(TAG, "display stub %ux%u (no panel driver for this target)",
             (unsigned)cfg->h_res, (unsigned)cfg->v_res);
#endif

    *out = d;
    return ESP_OK;
}

void sys_hal_display_lock(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (!bsp_display_lock(5000)) {
        ESP_LOGE(TAG, "bsp_display_lock timeout 5s!");
    }
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

/* ========================= Touch ========================= */
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
    /* GT911 已由 bsp_display_start_with_config 注册为 LVGL 输入设备,
       LVGL 在显示任务上下文中自动读取触摸事件 */
    ESP_LOGI(TAG, "touch ready (BSP): GT911 %ux%u",
             (unsigned)cfg->x_max, (unsigned)cfg->y_max);
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
    /* 触摸事件由 LVGL 输入设备异步消费; 此接口仅供需要主动轮询的场合 */
    *pressed = false;
    return ESP_OK;
}

/* ========================= Audio ========================= */
struct sys_hal_audio {
    sys_hal_audio_cfg_t cfg;
#if CONFIG_IDF_TARGET_ESP32P4
    esp_codec_dev_handle_t speaker; /* ES8311 */
    esp_codec_dev_handle_t mic;     /* ES7210 */
#endif
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

#if CONFIG_IDF_TARGET_ESP32P4
    esp_codec_dev_sample_info_t info = {
        .bits_per_sample = 16,
        .channel         = cfg->channels ? cfg->channels : 2,
        .channel_mask    = 0x03,
        .sample_rate     = cfg->sample_rate ? cfg->sample_rate : 16000,
        .mclk_multiple   = 256,
    };

    /* 扬声器: ES8311 (官方示例同款 API) */
    a->speaker = bsp_audio_codec_speaker_init();
    if (!a->speaker) {
        ESP_LOGE(TAG, "ES8311 speaker init failed");
        free(a);
        return ESP_FAIL;
    }
    if (esp_codec_dev_open(a->speaker, &info) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES8311 open failed");
        esp_codec_dev_delete(a->speaker);
        free(a);
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(a->speaker, 60);

    /* 麦克风: ES7210 双麦 (失败不致命, 只影响录音能力) */
    a->mic = bsp_audio_codec_microphone_init();
    if (a->mic) {
        if (esp_codec_dev_open(a->mic, &info) == ESP_CODEC_DEV_OK) {
            esp_codec_dev_set_in_gain(a->mic, 30.0f);
        } else {
            esp_codec_dev_delete(a->mic);
            a->mic = NULL;
            ESP_LOGW(TAG, "ES7210 open failed, recording disabled");
        }
    } else {
        ESP_LOGW(TAG, "ES7210 init failed, recording disabled");
    }
    ESP_LOGI(TAG, "audio ready: %luHz x%u ch (ES8311%s)",
             (unsigned long)info.sample_rate, (unsigned)info.channel,
             a->mic ? " + ES7210" : "");
#else
    ESP_LOGW(TAG, "audio stub %luHz x%u ch",
             (unsigned long)cfg->sample_rate, (unsigned)cfg->channels);
#endif

    *out = a;
    return ESP_OK;
}

esp_err_t sys_hal_audio_set_volume(sys_hal_audio_t *a, int volume)
{
    if (!a) {
        return ESP_ERR_INVALID_ARG;
    }
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 100) {
        volume = 100;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    if (!a->speaker) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_vol(a->speaker, volume) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
#else
    (void)volume;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sys_hal_audio_set_mic_gain(sys_hal_audio_t *a, float db)
{
    if (!a) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    if (!a->mic) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_in_gain(a->mic, db) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
#else
    (void)db;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sys_hal_audio_play(sys_hal_audio_t *a, const void *buf, size_t len, uint32_t timeout_ms)
{
    if (!a || !buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    if (!a->speaker) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)timeout_ms; /* esp_codec_dev 内部阻塞写 */
    return esp_codec_dev_write(a->speaker, (void *)buf, (int)len) == ESP_CODEC_DEV_OK
               ? ESP_OK : ESP_FAIL;
#else
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sys_hal_audio_read(sys_hal_audio_t *a, void *buf, size_t len,
                             size_t *bytes_read, uint32_t timeout_ms)
{
    if (!a || !buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    if (!a->mic) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)timeout_ms;
    int ret = esp_codec_dev_read(a->mic, buf, (int)len);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    if (bytes_read) {
        *bytes_read = len;
    }
    return ESP_OK;
#else
    (void)bytes_read;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* ========================= I2C ========================= */
struct sys_hal_i2c {
    sys_hal_i2c_cfg_t cfg;
    i2c_master_bus_handle_t bus;
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

#if CONFIG_IDF_TARGET_ESP32P4
    /* P4: BSP 独占 GPIO7/8 的 I2C 总线 (GT911/ES8311/ES7210),
       绝对不能再在同样引脚上建第二个总线 —— 会把触摸轮询搞挂,
       导致 LVGL 适配任务持锁卡死, 整个 UI 体系瘫痪 */
    b->bus = bsp_i2c_get_handle();
    if (!b->bus) {
        ESP_LOGE(TAG, "BSP i2c bus not ready (display not init?)");
    }
#else
    /* 优先复用 BSP 已经创建的同端口总线 (触摸/Codec 都在上面) */
    if (i2c_master_get_bus_handle((i2c_port_num_t)cfg->port, &b->bus) != ESP_OK) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port          = (i2c_port_num_t)cfg->port,
            .sda_io_num        = (gpio_num_t)cfg->sda_gpio,
            .scl_io_num        = (gpio_num_t)cfg->scl_gpio,
            .clk_source        = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &b->bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2c bus init failed (%s), peripherals unavailable",
                     esp_err_to_name(err));
            b->bus = NULL;
        }
    }
#endif
    ESP_LOGI(TAG, "i2c ready: port%u sda=%d scl=%d%s",
             (unsigned)cfg->port, cfg->sda_gpio, cfg->scl_gpio,
             b->bus ? "" : " (degraded)");
    *out = b;
    return ESP_OK;
}

static esp_err_t i2c_write_raw(sys_hal_i2c_t *bus, uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = bus->cfg.scl_hz ? bus->cfg.scl_hz : 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus->bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(dev, data, len, I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t sys_hal_i2c_write_reg(sys_hal_i2c_t *bus, uint8_t addr,
                                uint8_t reg, const uint8_t *data, size_t len)
{
    if (!bus || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bus->bus) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > I2C_XFER_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t tmp[1 + I2C_XFER_MAX];
    tmp[0] = reg;
    memcpy(&tmp[1], data, len);
    return i2c_write_raw(bus, addr, tmp, len + 1);
}

esp_err_t sys_hal_i2c_read_reg(sys_hal_i2c_t *bus, uint8_t addr,
                               uint8_t reg, uint8_t *data, size_t len)
{
    if (!bus || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bus->bus) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > I2C_XFER_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = bus->cfg.scl_hz ? bus->cfg.scl_hz : 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus->bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit_receive(dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    return err;
}

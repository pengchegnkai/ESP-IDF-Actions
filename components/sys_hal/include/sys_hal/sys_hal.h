/**
 * @file sys_hal.h
 * @brief 硬件抽象层 —— 屏幕/触摸/音频/I2C 的统一入口
 *
 * 每个 driver 一个 init, 成功后返回句柄; 后续 API 全部面向句柄操作,
 * 使上层 (sys_ui / apps) 不感知具体板卡 (P4 原生屏 / C6 协处理器桥接等)。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Display ---------- */
typedef struct sys_hal_display sys_hal_display_t;

typedef struct {
    uint16_t h_res;      /**< 水平像素 */
    uint16_t v_res;      /**< 垂直像素 */
    uint8_t  refresh_hz; /**< 目标刷新率 (0 = 驱动默认) */
} sys_hal_display_cfg_t;

esp_err_t sys_hal_display_init(const sys_hal_display_cfg_t *cfg, sys_hal_display_t **out);

/* ---------- Touch ---------- */
typedef struct sys_hal_touch sys_hal_touch_t;

typedef struct {
    uint16_t x_max;      /**< 触摸坐标最大值 (通常等于屏幕宽) */
    uint16_t y_max;
} sys_hal_touch_cfg_t;

esp_err_t sys_hal_touch_init(const sys_hal_touch_cfg_t *cfg, sys_hal_touch_t **out);

/** 读取一次触摸点 (无触摸返回 ESP_ERR_NOT_FOUND) */
esp_err_t sys_hal_touch_read(sys_hal_touch_t *t, uint16_t *x, uint16_t *y, bool *pressed);

/* ---------- Audio ---------- */
typedef struct sys_hal_audio sys_hal_audio_t;

typedef struct {
    uint32_t sample_rate; /**< 采样率, 0 = 16kHz 默认 */
    uint8_t  channels;    /**< 1 = 单声道 */
} sys_hal_audio_cfg_t;

/**
 * @brief 初始化音频: ES8311(扬声器) + ES7210(双麦), 走 BSP + esp_codec_dev
 * @param cfg 采样参数 (16000Hz / 2ch / 16bit 为板卡默认)
 */
esp_err_t sys_hal_audio_init(const sys_hal_audio_cfg_t *cfg, sys_hal_audio_t **out);

/** 输出音量 0-100 */
esp_err_t sys_hal_audio_set_volume(sys_hal_audio_t *a, int volume);

/** 麦克风增益 (dB, 0-42) */
esp_err_t sys_hal_audio_set_mic_gain(sys_hal_audio_t *a, float db);

/** 播放 PCM (16bit 交错) */
esp_err_t sys_hal_audio_play(sys_hal_audio_t *a, const void *buf, size_t len, uint32_t timeout_ms);

/** 录制 PCM (16bit 交错) */
esp_err_t sys_hal_audio_read(sys_hal_audio_t *a, void *buf, size_t len, size_t *bytes_read, uint32_t timeout_ms);

/* ---------- I2C bus (供外设驱动复用) ---------- */
typedef struct sys_hal_i2c sys_hal_i2c_t;

typedef struct {
    uint8_t  port;       /**< I2C 端口号 */
    uint32_t scl_hz;     /**< SCL 频率 (0 = 100kHz) */
    int8_t   sda_gpio;   /**< GPIO 编号 */
    int8_t   scl_gpio;
} sys_hal_i2c_cfg_t;

esp_err_t sys_hal_i2c_init(const sys_hal_i2c_cfg_t *cfg, sys_hal_i2c_t **out);

/* ---------- Display 互斥 (LVGL 上下文锁, 由 HAL 决定底层实现) ---------- */
bool sys_hal_display_lock(void);   /* false = 未获锁 */
void sys_hal_display_unlock(void);

/** I2C 寄存器读写 (7 位地址) */
esp_err_t sys_hal_i2c_write_reg(sys_hal_i2c_t *bus, uint8_t addr,
                                uint8_t reg, const uint8_t *data, size_t len);
esp_err_t sys_hal_i2c_read_reg(sys_hal_i2c_t *bus, uint8_t addr,
                               uint8_t reg, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

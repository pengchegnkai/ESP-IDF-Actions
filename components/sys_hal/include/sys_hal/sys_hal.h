/**
 * @file sys_hal.h
 * @brief 硬件抽象层 —— 屏幕/触摸/音频/I2C/USB(FIDO) 的统一入口
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

/** 背光亮度 0-100 (P4: BSP LEDC PWM; 其他目标: 不支持) */
esp_err_t sys_hal_display_set_brightness(int percent);

/** 当前亮度 0-100 */
esp_err_t sys_hal_display_get_brightness(int *percent);

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

/** 系统音频句柄 (init 成功后可用; 未初始化返回 NULL) */
sys_hal_audio_t *sys_hal_audio_get(void);

/**
 * @brief 播放短音效 (正弦波 + 起音/释音包络, 非阻塞: 内部任务播放)
 * @param freq_hz 频率 100-8000
 * @param duration_ms 时长 20-500
 */
esp_err_t sys_hal_audio_beep(uint32_t freq_hz, uint32_t duration_ms);

/* ---------- I2C bus (供外设驱动复用) ---------- */
typedef struct sys_hal_i2c sys_hal_i2c_t;

typedef struct {
    uint8_t  port;       /**< I2C 端口号 */
    uint32_t scl_hz;     /**< SCL 频率 (0 = 100kHz) */
    int8_t   sda_gpio;   /**< GPIO 编号 */
    int8_t   scl_gpio;
} sys_hal_i2c_cfg_t;

esp_err_t sys_hal_i2c_init(const sys_hal_i2c_cfg_t *cfg, sys_hal_i2c_t **out);

/* ---------- USB / FIDO (CTAP2 over HID; P4: TinyUSB HS OTG, 其他目标: 不支持) ----------
 * 本板 Type-C "USB OTG" 口 = ESP32-P4 专用 HS OTG 引脚 (USBD_P/USBD_N, 芯片引脚
 * 50/49, 不经 GPIO 矩阵); FIDO HID: usage page 0xF1D0 / usage 0x03, 64B IN/OUT 报告。
 * 手动激活模式: set_active(false) (默认) 时丢弃主机 CTAP 数据, 不应答。 */

/** CTAP2 帧接收回调: 运行在 TinyUSB 任务上下文; buffer 仅回调期间有效, 需自行拷贝 */
typedef void (*sys_hal_usb_fido_rx_cb_t)(const uint8_t *data, int len, void *ctx);

/**
 * @brief 启动 TinyUSB FIDO HID 设备 (协调者接进 app_main; 幂等)
 * @return ESP_OK 成功; 其他芯片目标 ESP_ERR_NOT_SUPPORTED
 */
esp_err_t sys_hal_usb_fido_init(void);

/** 手动激活路由开关 (true=响应 CTAP, false=丢弃数据不应答; 默认 false) */
void sys_hal_usb_fido_set_active(bool active);

/** 注册 CTAP2 帧接收回调 (cb 传 NULL = 注销) */
void sys_hal_usb_fido_register_rx_cb(void (*cb)(const uint8_t *data, int len, void *ctx), void *ctx);

/** 当前 USB 是否已连接主机 (P4: tud_mounted(); 未 init/其他目标: false) */
bool sys_hal_usb_fido_connected(void);

/** 排入一个 CTAP2 应答 IN 报告 (len ≤ 64; 越界/未 init 丢弃) */
void sys_hal_usb_fido_send(const uint8_t *data, int len);

/* ---------- Display 互斥 (LVGL 上下文锁, 由 HAL 决定底层实现) ---------- */
bool sys_hal_display_lock(void);   /* false = 未获锁 */

/**
 * @brief 挂载 SD 卡 (P4: BSP 原生 SDIO + FAT VFS; 其他目标: 不支持)
 * @return ESP_OK 成功; 无卡/未格式化返回错误码 (不致命, 上层降级处理)
 */
esp_err_t sys_hal_sd_mount(void);

/** SD 卡 VFS 挂载点 (P4 默认 "/sdcard", 可经 CONFIG_BSP_SD_MOUNT_POINT 修改) */
const char *sys_hal_sd_mount_point(void);
void sys_hal_display_unlock(void);

/** I2C 寄存器读写 (7 位地址) */
esp_err_t sys_hal_i2c_write_reg(sys_hal_i2c_t *bus, uint8_t addr,
                                uint8_t reg, const uint8_t *data, size_t len);
esp_err_t sys_hal_i2c_read_reg(sys_hal_i2c_t *bus, uint8_t addr,
                               uint8_t reg, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

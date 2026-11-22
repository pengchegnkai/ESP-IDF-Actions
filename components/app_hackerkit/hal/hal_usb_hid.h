/* hal_usb_hid.h — [硬件相关·需真机验证] TinyUSB HID 键盘 (hal 层接口) */
#ifndef HAL_USB_HID_H
#define HAL_USB_HID_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hk_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 安装 TinyUSB 驱动 (HID 键盘). usb_arbiter 状态切换回调里调用. */
esp_err_t hal_usb_hid_init(void);

/* 卸载 TinyUSB 驱动 (USB 让渡给 FIDO2 前调用). */
esp_err_t hal_usb_hid_deinit(void);

/* 发送原始 8 字节键盘报告, 返回 false 表示 USB 未就绪. */
bool hk_tud_send_report(const uint8_t report[HK_REPORT_LEN]);

/* 填充 hk_usb_ops_t 给 core 执行引擎使用. */
void hal_usb_hid_get_ops(hk_usb_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_HID_H */

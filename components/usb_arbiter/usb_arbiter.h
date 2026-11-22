/* usb_arbiter.h — USB 功能互斥状态机 (App_SecurityKey FIDO2 ↔ App_HackerKit BadUSB)
 *
 * [纯逻辑·可仿真] 无 ESP-IDF 依赖. 规格书红线: FIDO2 与 BadUSB 绝不能同时占用
 * USB — 本状态机是唯一仲裁者; 状态切换时通过回调通知 hal 层执行 TinyUSB
 * 配置卸载/重新初始化 (硬件相关·需真机验证, 在 hal 侧实现).
 */
#ifndef USB_ARBITER_H
#define USB_ARBITER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_ARB_OWNER_NONE = 0,
    USB_ARB_OWNER_FIDO2,     /* App_SecurityKey (手动激活后持有) */
    USB_ARB_OWNER_BADUSB,    /* App_HackerKit (payload 执行期间持有) */
} usb_arb_owner_t;

typedef enum {
    USB_ARB_OK = 0,      /* 状态变更成功 (或已是目标状态) */
    USB_ARB_BUSY,        /* 另一 APP 持有 USB */
    USB_ARB_ERR,         /* 非法操作 (如非持有者 release) */
} usb_arb_result_t;

/* 状态切换回调: hal 层在此执行 TinyUSB deinit/reinit (硬件相关).
 * old==new 不会触发. */
typedef void (*usb_arb_transition_cb_t)(usb_arb_owner_t old_owner, usb_arb_owner_t new_owner);

void usb_arb_init(usb_arb_transition_cb_t cb);

/* 请求持有 USB: 当前无主→授予; 已是自己→幂等 OK; 他人持有→BUSY. */
usb_arb_result_t usb_arb_request(usb_arb_owner_t client);

/* 释放 USB: 仅持有者可释放, 释放后状态回 NONE. */
usb_arb_result_t usb_arb_release(usb_arb_owner_t client);

/* 查询当前持有者 (调试/UI 显示用). */
usb_arb_owner_t usb_arb_owner_get(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_ARBITER_H */

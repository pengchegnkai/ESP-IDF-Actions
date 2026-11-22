/* usb_arbiter.c — USB 功能互斥状态机实现
 *
 * [纯逻辑·可仿真] 单状态变量 + 转移表, pc_sim 全覆盖.
 * 线程安全: 真机侧调用方 (FIDO2 前台激活/HackerKit 执行) 都在 APP 任务上下文,
 * 竞争窗口由 hal 层加 FreeRTOS 互斥锁包裹 (usb_arb_* 调用点), 状态机本身保持
 * 无锁纯逻辑以便仿真.
 */
#include <stddef.h>

#include "usb_arbiter.h"

static usb_arb_owner_t s_owner = USB_ARB_OWNER_NONE;
static usb_arb_transition_cb_t s_transition_cb = NULL;

void usb_arb_init(usb_arb_transition_cb_t cb)
{
    s_owner = USB_ARB_OWNER_NONE;
    s_transition_cb = cb;
}

usb_arb_result_t usb_arb_request(usb_arb_owner_t client)
{
    if (client != USB_ARB_OWNER_FIDO2 && client != USB_ARB_OWNER_BADUSB) {
        return USB_ARB_ERR;
    }
    if (s_owner == client) {
        return USB_ARB_OK; /* 幂等 */
    }
    if (s_owner != USB_ARB_OWNER_NONE) {
        return USB_ARB_BUSY; /* 互斥红线 */
    }
    usb_arb_owner_t old = s_owner;
    s_owner = client;
    if (s_transition_cb) {
        s_transition_cb(old, s_owner);
    }
    return USB_ARB_OK;
}

usb_arb_result_t usb_arb_release(usb_arb_owner_t client)
{
    if (s_owner != client) {
        return USB_ARB_ERR; /* 非持有者不可释放 */
    }
    usb_arb_owner_t old = s_owner;
    s_owner = USB_ARB_OWNER_NONE;
    if (s_transition_cb) {
        s_transition_cb(old, s_owner);
    }
    return USB_ARB_OK;
}

usb_arb_owner_t usb_arb_owner_get(void)
{
    return s_owner;
}

/* test_hk_arbiter.c — Arbiter 交互测试: FIDO2 持有时 BadUSB 不执行 */
#include "test_framework.h"
#include "usb_arbiter.h"
#include "hk_executor.h"
#include "hk_backends_host.h"
#include "hid_keycodes.h"

#include <string.h>

TEST(arb_busy_blocks_execution)
{
    hk_usb_ops_t usb;
    hk_fake_usb_get_ops(&usb);
    hk_fake_usb_reset();

    /* 模拟 FIDO2 APP 持有 USB */
    usb_arb_init(NULL);
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_FIDO2), USB_ARB_OK);

    /* BadUSB 请求 → BUSY, 不应执行任何 payload */
    if (usb_arb_request(USB_ARB_OWNER_BADUSB) != USB_ARB_OK) {
        /* UI 提示 FIDO2 占用; 不发任何 HID 报告 */
        CHECK_EQ_INT(hk_fake_usb_get()->count, 0);
    } else {
        CHECK(!"unexpected grant while FIDO2 holds USB");
    }
    CHECK_EQ_INT(usb_arb_owner_get(), USB_ARB_OWNER_FIDO2);
}

TEST(arb_grant_then_execute_release)
{
    hk_usb_ops_t usb;
    hk_fake_usb_get_ops(&usb);
    hk_fake_usb_reset();
    usb_arb_init(NULL);

    /* FIDO2 空闲 → BadUSB 获得 USB */
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_BADUSB), USB_ARB_OK);

    /* 执行一小段 payload */
    hk_event_t ev = {};
    ev.type = HK_EV_STRING;
    strcpy(ev.str, "hi");
    CHECK_EQ_INT(hk_executor_run(&ev, 1, &usb, NULL, NULL), HK_EXEC_OK);
    /* 每字符 按下+松开 = 4 个报告 */
    CHECK_EQ_INT(hk_fake_usb_get()->count, 4);

    /* 结束释放 */
    CHECK_EQ_INT(usb_arb_release(USB_ARB_OWNER_BADUSB), USB_ARB_OK);
    CHECK_EQ_INT(usb_arb_owner_get(), USB_ARB_OWNER_NONE);
}

TEST(arb_release_after_execution_allows_fido2)
{
    usb_arb_init(NULL);
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_BADUSB), USB_ARB_OK);
    CHECK_EQ_INT(usb_arb_release(USB_ARB_OWNER_BADUSB), USB_ARB_OK);
    /* BadUSB 释放后 FIDO2 可持有 */
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_FIDO2), USB_ARB_OK);
    usb_arb_release(USB_ARB_OWNER_FIDO2);
}

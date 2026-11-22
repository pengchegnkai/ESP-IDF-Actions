/* test_usb_arbiter.c — USB 互斥状态机测试 */
#include "test_framework.h"
#include "usb_arbiter.h"

static int cb_old, cb_new, cb_count;

static void trans_cb(usb_arb_owner_t old, usb_arb_owner_t new_owner)
{
    cb_old = (int)old;
    cb_new = (int)new_owner;
    cb_count++;
}

TEST(arb_initial_none)
{
    usb_arb_init(trans_cb);
    CHECK_EQ_INT(usb_arb_owner_get(), USB_ARB_OWNER_NONE);
}

TEST(arb_grant_and_idempotent)
{
    usb_arb_init(trans_cb);
    cb_count = 0;
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_FIDO2), USB_ARB_OK);
    CHECK_EQ_INT(usb_arb_owner_get(), USB_ARB_OWNER_FIDO2);
    CHECK_EQ_INT(cb_count, 1);
    /* 幂等: 已持有再请求 OK, 不触发回调 */
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_FIDO2), USB_ARB_OK);
    CHECK_EQ_INT(cb_count, 1);
}

TEST(arb_mutual_exclusion)
{
    usb_arb_init(NULL);
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_FIDO2), USB_ARB_OK);
    /* 红线: BadUSB 不能抢 */
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_BADUSB), USB_ARB_BUSY);
    CHECK_EQ_INT(usb_arb_owner_get(), USB_ARB_OWNER_FIDO2);
    /* 释放后 BadUSB 可进 */
    CHECK_EQ_INT(usb_arb_release(USB_ARB_OWNER_FIDO2), USB_ARB_OK);
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_BADUSB), USB_ARB_OK);
    CHECK_EQ_INT(usb_arb_owner_get(), USB_ARB_OWNER_BADUSB);
    /* 反向互斥 */
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_FIDO2), USB_ARB_BUSY);
}

TEST(arb_release_rules)
{
    usb_arb_init(NULL);
    /* 非持有者释放 → ERR */
    CHECK_EQ_INT(usb_arb_release(USB_ARB_OWNER_FIDO2), USB_ARB_ERR);
    usb_arb_request(USB_ARB_OWNER_BADUSB);
    CHECK_EQ_INT(usb_arb_release(USB_ARB_OWNER_FIDO2), USB_ARB_ERR);
    CHECK_EQ_INT(usb_arb_release(USB_ARB_OWNER_BADUSB), USB_ARB_OK);
    CHECK_EQ_INT(usb_arb_owner_get(), USB_ARB_OWNER_NONE);
}

TEST(arb_transition_callback)
{
    cb_count = 0;
    usb_arb_init(trans_cb);
    usb_arb_request(USB_ARB_OWNER_FIDO2);
    CHECK_EQ_INT(cb_count, 1);
    CHECK_EQ_INT(cb_old, USB_ARB_OWNER_NONE);
    CHECK_EQ_INT(cb_new, USB_ARB_OWNER_FIDO2);
    usb_arb_release(USB_ARB_OWNER_FIDO2);
    CHECK_EQ_INT(cb_count, 2);
    CHECK_EQ_INT(cb_old, USB_ARB_OWNER_FIDO2);
    CHECK_EQ_INT(cb_new, USB_ARB_OWNER_NONE);
    /* 非法 client 不触发回调 */
    CHECK_EQ_INT(usb_arb_request(USB_ARB_OWNER_NONE), USB_ARB_ERR);
    CHECK_EQ_INT(cb_count, 2);
}

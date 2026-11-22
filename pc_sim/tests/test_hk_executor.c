/* test_hk_executor.c — 执行引擎测试 (fake usb 断言) */
#include "test_framework.h"
#include "hk_executor.h"
#include "hk_backends_host.h"
#include "hid_keycodes.h"

#include <string.h>

static int delay_count;
static uint32_t last_delay_ms;

static void test_delay(uint32_t ms)
{
    delay_count++;
    last_delay_ms = ms;
}

static void reset_backend(void)
{
    hk_fake_usb_reset();
    delay_count = 0;
    last_delay_ms = 0;
}

static bool always_abort(void)
{
    return 1;
}

TEST(executor_string_shift_split)
{
    hk_usb_ops_t usb;
    hk_fake_usb_get_ops(&usb);
    reset_backend();

    hk_event_t ev = {};
    ev.type = HK_EV_STRING;
    strcpy(ev.str, "aA");

    CHECK_EQ_INT(hk_executor_run(&ev, 1, &usb, NULL, NULL), HK_EXEC_OK);

    /* 'a' → 按下 {0, a} + 松开 {0,0}; 'A' → shift 拆分:
     * 按下 shift {LSHIFT,0} + 松开, 按下 {LSHIFT,A} + 松开 */
    CHECK_EQ_INT(hk_fake_usb_get()->count, 6);
    const uint8_t (*r)[HK_REPORT_LEN] = hk_fake_usb_get()->reports;
    CHECK_EQ_INT(r[0][0], HK_MOD_NONE);
    CHECK_EQ_INT(r[0][2], HK_KEY_A); /* 'a' */
    CHECK_EQ_INT(r[1][0], HK_MOD_NONE);
    CHECK_EQ_INT(r[1][2], HK_KEY_NONE); /* 松开 */
    CHECK_EQ_INT(r[2][0], HK_MOD_LSHIFT);
    CHECK_EQ_INT(r[2][2], HK_KEY_NONE); /* shift 按下 */
    CHECK_EQ_INT(r[3][2], HK_KEY_NONE);
    CHECK_EQ_INT(r[4][0], HK_MOD_LSHIFT);
    CHECK_EQ_INT(r[4][2], HK_KEY_A); /* 'A' */
    CHECK_EQ_INT(r[5][0], HK_MOD_NONE);
    CHECK_EQ_INT(r[5][2], HK_KEY_NONE); /* 全松开 */
}

TEST(executor_keycombo)
{
    hk_usb_ops_t usb;
    hk_fake_usb_get_ops(&usb);
    reset_backend();

    hk_event_t ev = {};
    ev.type = HK_EV_KEYCOMBO;
    ev.mods = HK_MOD_LCTRL;
    ev.key = HK_KEY_A + ('c' - 'a');

    CHECK_EQ_INT(hk_executor_run(&ev, 1, &usb, test_delay, NULL), HK_EXEC_OK);
    CHECK_EQ_INT(hk_fake_usb_get()->count, 2); /* 按下 + 松开 */
    const uint8_t (*r)[HK_REPORT_LEN] = hk_fake_usb_get()->reports;
    CHECK_EQ_INT(r[0][0], HK_MOD_LCTRL);
    CHECK_EQ_INT(r[0][2], HK_KEY_A + ('c' - 'a'));
    CHECK_EQ_INT(r[1][0], HK_MOD_NONE);
    CHECK_EQ_INT(r[1][2], HK_KEY_NONE);
}

TEST(executor_delay_counts)
{
    hk_usb_ops_t usb;
    hk_fake_usb_get_ops(&usb);
    reset_backend();

    hk_event_t ev[2] = {};
    ev[0].type = HK_EV_DELAY;
    ev[0].delay_ms = 500;
    ev[1].type = HK_EV_DELAY;
    ev[1].delay_ms = 250;

    CHECK_EQ_INT(hk_executor_run(ev, 2, &usb, test_delay, NULL), HK_EXEC_OK);
    CHECK_EQ_INT(delay_count, 2);
    CHECK_EQ_INT(last_delay_ms, 250);
    CHECK_EQ_INT(hk_fake_usb_get()->count, 0); /* DELAY 不发报告 */
}

TEST(executor_error_cases)
{
    hk_usb_ops_t usb;
    hk_fake_usb_get_ops(&usb);
    reset_backend();

    hk_event_t ev = {};
    CHECK_EQ_INT(hk_executor_run(NULL, 0, &usb, NULL, NULL), HK_EXEC_ERR_PARAM);
    CHECK_EQ_INT(hk_executor_run(&ev, 1, NULL, NULL, NULL), HK_EXEC_ERR_USB);
    /* delay_fn 为 NULL 时不崩, 只是跳过 */
    ev.type = HK_EV_DELAY;
    ev.delay_ms = 100;
    CHECK_EQ_INT(hk_executor_run(&ev, 1, &usb, NULL, NULL), HK_EXEC_OK);
}

TEST(executor_abort_fn)
{
    hk_usb_ops_t usb;
    hk_fake_usb_get_ops(&usb);
    reset_backend();

    hk_event_t ev[2] = {};
    ev[0].type = HK_EV_DELAY;
    ev[1].type = HK_EV_DELAY;

    CHECK_EQ_INT(hk_executor_run(ev, 2, &usb, NULL, always_abort),
                 HK_EXEC_ERR_ABORTED);
}

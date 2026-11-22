/* hk_executor.c — [纯逻辑·可仿真] payload 执行引擎实现
 *
 * 字符串逐字符经 hid_keycodes 转 HID 报告, 自动按 shift 状态
 * 拆分发送/松开 (借鉴 EvilKey-firmware 的按键发送方式).
 * DELAY 经注入的 delay_fn(ms) (host 测试传空实现, 真机传 vTaskDelay 包装).
 */
#include "hk_executor.h"
#include "hid_keycodes.h"

#include <string.h>

/* 发送一个报告并回零 (松开), 遵循 USB 键盘按下→释放节奏 */
static void send_report(const hk_usb_ops_t *usb, uint8_t mod, uint8_t key)
{
    uint8_t report[HK_REPORT_LEN] = {0};
    report[0] = mod;
    report[2] = key; /* keycode[0] = 6 键 rollover 首槽 */
    usb->send_keyboard_report(report);
    memset(report, 0, sizeof(report));
    usb->send_keyboard_report(report); /* 全零 = 全部松开 */
}

hk_exec_result_t hk_executor_run(const hk_event_t *events, int count,
                                 const hk_usb_ops_t *usb,
                                 hk_delay_fn_t delay_fn,
                                 bool (*abort_fn)(void))
{
    if (!events || count <= 0) {
        return HK_EXEC_ERR_PARAM;
    }
    if (!usb || !usb->send_keyboard_report) {
        return HK_EXEC_ERR_USB;
    }

    for (int i = 0; i < count; i++) {
        if (abort_fn && abort_fn()) {
            return HK_EXEC_ERR_ABORTED;
        }
        const hk_event_t *ev = &events[i];
        switch (ev->type) {
        case HK_EV_DELAY:
            if (delay_fn) {
                delay_fn(ev->delay_ms);
            }
            break;

        case HK_EV_KEYCOMBO:
            send_report(usb, ev->mods, ev->key);
            break;

        case HK_EV_STRING: {
            /* 逐字符: 需要 shift 时拆成 "按下shift → shift+键 → 松键 → 松shift" */
            for (const char *p = ev->str; *p; p++) {
                bool need_shift = false;
                uint8_t key = hk_ascii_to_keycode(*p, &need_shift);
                if (!key) {
                    continue; /* 不支持字符: 跳过 */
                }
                if (need_shift) {
                    send_report(usb, HK_MOD_LSHIFT, HK_KEY_NONE); /* 按下 shift */
                    send_report(usb, HK_MOD_LSHIFT, key);         /* shift+键 */
                } else {
                    send_report(usb, HK_MOD_NONE, key);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return HK_EXEC_OK;
}

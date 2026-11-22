/* hk_backend.h — [纯逻辑·可仿真] App_HackerKit 后端接口 (函数指针表)
 *
 * core/ui 通过本接口触达文件系统与 USB, 不直接调 ESP-IDF:
 *   - hal/hal_fs.c + hal/hal_usb_hid.c  在真机实现 ([硬件相关·需真机验证])
 *   - pc_sim/stubs/hk_backends_host.c   在 PC 仿真侧实现 (fake/POSIX)
 */
#ifndef HK_BACKEND_H
#define HK_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HID 键盘报告固定 8 字节: [0]=modifier, [1]=reserved, [2..7]=keycode[6] */
#define HK_REPORT_LEN 8

/* payload 文件名最大长度 (含 '\0') */
#define HK_PAYLOAD_NAME_MAX 64

typedef struct hk_fs_ops {
    /* 列出 dir 下的 payload 文件名到 names (调用方给定容量 max_names),
     * 返回实际数量, 出错返回 -1. */
    int (*list_payloads)(const char *dir, char names[][HK_PAYLOAD_NAME_MAX],
                         int max_names);
    /* 读取 payload 文件到 buf (容量 len, 追加 '\0'), 返回字节数, 出错 -1. */
    int (*read_payload)(const char *path, char *buf, size_t len);
} hk_fs_ops_t;

typedef struct hk_usb_ops {
    /* 发送一个 HID 键盘报告 (真机: TinyUSB tud_hid_keyboard_report). */
    void (*send_keyboard_report)(const uint8_t report[HK_REPORT_LEN]);
} hk_usb_ops_t;

/* DELAY 注入: host 测试传空实现, 真机传 vTaskDelay 包装 */
typedef void (*hk_delay_fn_t)(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HK_BACKEND_H */

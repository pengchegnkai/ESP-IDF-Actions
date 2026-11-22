/* hk_backends_host.h — [纯逻辑·可仿真] PC 仿真侧 HackerKit 后端替身
 *
 * fake usb: 记录每个收到的 HID 报告 + 计数, 供测试断言;
 * POSIX fs: 读取临时目录里的 payload 文件.
 */
#ifndef HK_BACKENDS_HOST_H
#define HK_BACKENDS_HOST_H

#include "hk_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_FAKE_REPORTS_MAX 256

/* fake usb 状态 */
typedef struct {
    uint8_t reports[HK_FAKE_REPORTS_MAX][HK_REPORT_LEN];
    int count;
    int dropped; /* 数组满后丢弃的报告数 */
} hk_fake_usb_t;

/* 初始化 (清空记录) */
void hk_fake_usb_reset(void);

/* 访问 fake usb 状态 (测试断言用) */
hk_fake_usb_t *hk_fake_usb_get(void);

/* 填充 hk_usb_ops_t (fake) */
void hk_fake_usb_get_ops(hk_usb_ops_t *ops);

/* 填充 hk_fs_ops_t (POSIX, dir 参数即本地路径, 测试用临时目录) */
void hk_host_fs_get_ops(hk_fs_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* HK_BACKENDS_HOST_H */

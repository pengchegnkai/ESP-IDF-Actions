/* hk_executor.h — [纯逻辑·可仿真] payload 执行引擎 */
#ifndef HK_EXECUTOR_H
#define HK_EXECUTOR_H

#include "hk_backend.h"
#include "hk_script.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 执行结果 */
typedef enum {
    HK_EXEC_OK = 0,
    HK_EXEC_ERR_PARAM,   /* 参数非法 */
    HK_EXEC_ERR_USB,     /* USB 后端缺失 */
    HK_EXEC_ERR_ABORTED, /* 中止标志被置位 */
} hk_exec_result_t;

/* 执行引擎: 消费事件数组.
 *   usb       — 报告发送后端 (必须提供 send_keyboard_report)
 *   delay_fn  — DELAY 实现 (host 测试可传空实现/计数桩, 真机传 vTaskDelay 包装)
 *   abort_fn  — 可选中止探测 (返回 true 时停止), 可为 NULL
 * 返回 HK_EXEC_OK 或错误码; USB 缺失立即返回. */
hk_exec_result_t hk_executor_run(const hk_event_t *events, int count,
                                 const hk_usb_ops_t *usb,
                                 hk_delay_fn_t delay_fn,
                                 bool (*abort_fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* HK_EXECUTOR_H */

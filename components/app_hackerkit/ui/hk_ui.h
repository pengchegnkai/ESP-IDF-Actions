/* hk_ui.h — [纯逻辑·可仿真] App_HackerKit 纯 LVGL 界面
 *
 * payload 列表 (lv_list, 来自 hk_fs_ops_t) + 执行按钮 + 状态显示.
 * 通过 backend 接口触达文件/USB, 不直接调 ESP-IDF.
 * 执行入口由 APP 注册层 (app_hackerkit.cpp) 注入 hk_ui_set_executor,
 * 使 usb_arbiter 仲裁不进 ui 纯逻辑.
 */
#ifndef HK_UI_H
#define HK_UI_H

#include "lvgl.h"
#include "hk_backend.h"
#include "hk_script.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 执行状态 (UI 显示) */
typedef enum {
    HK_UI_STATUS_IDLE = 0,
    HK_UI_STATUS_RUNNING,
    HK_UI_STATUS_DONE,
    HK_UI_STATUS_FAILED,
    HK_UI_STATUS_BUSY, /* USB 被 FIDO2 占用 */
} hk_ui_status_t;

/* 执行回调: APP 注册层实现 (usb_arb_request → executor → release).
 * 参数: 选中的 payload 文件名. 返回 0 成功, 非 0 失败. */
typedef int (*hk_ui_exec_fn_t)(const char *payload_name);

/* 创建 UI; fs 可为 NULL (无 payload 目录时的降级显示) */
void hk_ui_create(lv_obj_t *parent, const hk_fs_ops_t *fs,
                  const hk_usb_ops_t *usb, hk_delay_fn_t delay_fn);

/* APP 注册层注入执行入口 (必须在 hk_ui_create 前或后调用均可) */
void hk_ui_set_executor(hk_ui_exec_fn_t fn);

/* 状态显示更新 (执行任务完成后由 APP 注册层调用; 真机需在持 LVGL 锁时调用) */
void hk_ui_set_status(hk_ui_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* HK_UI_H */

/**
 * @file sys_ui_secure_dialog.h
 * @brief 安全认证弹窗 —— 顶层 overlay, 任意 APP 在前台都能压住
 *
 * 线程模型: sys_ui_secure_dialog_show() 可从任意 FreeRTOS 任务上下文调用
 *           (如 TinyUSB 任务); 内部自取显示锁, 全部 LVGL 创建在锁内。
 *           on_result 回调运行在 LVGL 渲染上下文 (持显示锁), 弹窗关闭之后
 *           才调用 —— 回调内不要阻塞 (签名等耗时工作留阶段二异步化)。
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 弹窗确认结果回调 (弹窗已关闭后调用); ctx 为 show() 传入的原样指针 */
typedef bool (*sys_ui_secure_dialog_result_cb_t)(bool confirmed, void *ctx);

/**
 * @brief 显示安全认证弹窗 (互斥: 已有顶层弹窗时返回 ESP_ERR_INVALID_STATE)
 *
 * 布局 (Vela 卡片风格): 标题 = SEC_DIALOG_TITLE + op_text, 详情 = detail_text,
 * 按钮 = SEC_CANCEL / SEC_CONFIRM。
 * 点确认: 先显示 SEC_SIGNING (签名中), 短暂渲染后关闭弹窗并回调 on_result(true, ctx);
 * 点取消: 立即关闭弹窗并回调 on_result(false, ctx)。
 *
 * @param op_text     操作名文案 (如 sys_ui_str(SYS_UI_STR_SEC_MAKE_CRED/_SEC_ASSERT))
 * @param detail_text 详情文案 (如 origin 占位); NULL/空 = 不显示详情行
 * @param on_result   结果回调 (必传)
 * @param ctx         透传给 on_result
 */
esp_err_t sys_ui_secure_dialog_show(const char *op_text, const char *detail_text,
                                    sys_ui_secure_dialog_result_cb_t on_result, void *ctx);

#ifdef __cplusplus
}
#endif

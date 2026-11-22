/* sk_popup.h — FIDO2 全局确认弹窗 ([纯逻辑·可仿真])
 *
 * 红线①: 全局弹窗必须用 lv_layer_top() 绘制, 覆盖原厂任何界面 (含状态栏/其他 APP).
 * 纯 LVGL 代码, 只允许 include lvgl.h + 本组件头.
 *
 * 真机由 hal 层/APP 注册层在 esp_brookesia LvLockGuard 内调用
 * (LvLock 不进 ui/ — CONVENTIONS.md 线程安全红线).
 * pc_sim 无 lvgl 时本文件编译为空桩 (ui 逻辑测试在 CMake + LVGL 路径下跑).
 */
#ifndef SK_POPUP_H
#define SK_POPUP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 触摸结果回调: allow = 用户点了"允许"或"拒绝". [纯逻辑·可仿真] */
typedef void (*sk_confirm_cb_t)(bool allow, void *user_data);

/* 显示全局确认弹窗: "「origin」网站请求使用你的安全密钥" + 允许/拒绝按钮.
 * cb 注入触摸结果 (会话层在回调里走 sk_session_confirm).
 * 仅后台弹窗存在时重复调用 → 更新文案. */
void sk_popup_show(const char *origin, sk_confirm_cb_t cb, void *user_data);

/* 隐藏并销毁弹窗 (会话取消/完成/回 Idle 时调用). 幂等. */
void sk_popup_hide(void);

/* 测试辅助: 弹窗当前是否可见 (LVGL 可用时才为 true) */
bool sk_popup_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* SK_POPUP_H */

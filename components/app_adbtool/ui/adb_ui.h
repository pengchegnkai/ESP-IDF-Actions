/* adb_ui.h — ADB 工具命令行界面 ([纯逻辑·可仿真])
 *
 * 纯 LVGL 9.5 代码, 只允许 include lvgl.h + ui_theme.h + 本组件头.
 * 布局 (480x800 深色主题, 参照 ui_theme.h):
 *   - 顶部标题 "ADB 工具" (强调色) + 状态行 (未连接/授权中/已就绪)
 *   - 大输出滚动区 (lv_textarea 只读, 等宽风格)
 *   - 底部单行输入 (lv_textarea 单行) + 发送按钮
 *   - 连接/断开按钮 → app 层注册的回调 (adb_client_start/stop)
 *
 * 线程安全: 所有 adb_ui_* 须在 LVGL 锁内调用 (app 层持锁, LvLock 不进 ui/).
 */
#ifndef ADB_UI_H
#define ADB_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 连接/断开动作回调 (app 层注入, 一般转发到 adb_client_start/stop) */
typedef void (*adb_ui_action_cb_t)(void *user);

/* 创建整个界面 (parent = APP 屏容器); 内部清空此前 widget 指针 */
void adb_ui_create(lv_obj_t *parent);

/* 注入连接/断开回调 (在 adb_ui_create 之前或之后调用均可) */
void adb_ui_set_actions(adb_ui_action_cb_t connect, adb_ui_action_cb_t disconnect, void *user);

/* 状态行: "未连接" / "授权中" / "已就绪" 等 */
void adb_ui_set_status(const char *s);

/* 输出区追加一行 (自动滚动到底, 上限约 8KB 防内存膨胀) */
void adb_ui_append_output(const char *line);

/* 取输入框内容 (内部静态缓冲, 下次 get/清空前有效) */
const char *adb_ui_get_input(void);

/* 清空输入框 (发送成功后调用) */
void adb_ui_clear_input(void);

#ifdef __cplusplus
}
#endif

#endif /* ADB_UI_H */

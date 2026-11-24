/* adb_client.h — USB Host ADB 客户端 ([硬件相关·需真机验证])
 *
 * 接线: 手机 (USB device) ←OTG线→ P4 USB-OTG (host).
 * 首次连接: 手机弹 ADB 授权 → 允许后密钥被手机记住.
 */
#ifndef ADB_CLIENT_H
#define ADB_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 输出回调: shell 输出/状态文本 (UI 层注册, 运行于 USB host 任务) */
typedef void (*adb_line_cb_t)(const char *line, void *user);

/* 初始化 USB host 栈 + RSA 密钥 (NVS 幂等) */
int adb_client_init(void);

/* 状态机启动: 连接手机 → 授权 → 就绪. 非阻塞, 内部建 USB host 任务. */
int adb_client_start(void);
void adb_client_stop(void);

/* 就绪后执行一条 shell 命令 (异步, 输出走 line 回调); 返回 0 = 已提交 */
int adb_client_shell(const char *cmd);

/* 当前是否已连接 (授权完成) */
bool adb_client_ready(void);

#ifdef __cplusplus
}
#endif
#endif /* ADB_CLIENT_H */

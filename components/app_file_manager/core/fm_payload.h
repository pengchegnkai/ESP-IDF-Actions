/* fm_payload.h — Payload 保存 (路径安全校验)
 * [纯逻辑·可仿真] core 层: 只允许 C 标准库 + 本组件头, 无 ESP-IDF 依赖.
 * 借鉴 (参考 espressif/esp-ui 文件管理器的存储策略):
 * payload 统一保存到 /sdcard/payloads/, 仅允许 .txt 扩展名.
 */
#ifndef FM_PAYLOAD_H
#define FM_PAYLOAD_H

#include "fm_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* payload 保存目录 (真机挂载点下) */
#define FM_PAYLOAD_DIR      "/sdcard/payloads"
#define FM_PAYLOAD_DIR_LEN  (sizeof(FM_PAYLOAD_DIR) - 1)
/* 文件名 (不含路径) 最大长度, 含 '\0' 与 ".txt" */
#define FM_PAYLOAD_NAME_MAX 64
/* 单个 payload 内容长度上限 (字节) */
#define FM_PAYLOAD_LEN_MAX  (32 * 1024)

/* 校验 payload 文件名 (不含路径):
 *   非空, 长度 < FM_PAYLOAD_NAME_MAX, 不含 '/' '\\', 无 "../" 穿越,
 *   不以 '.' 开头, 扩展名必须是 ".txt".
 * @return FM_OK 合法; FM_ERR 非法.
 */
int fm_payload_validate_name(const char *name);

/* 写 payload: 先校验 name, 再经 backend 写到 FM_PAYLOAD_DIR/name.
 * [纯逻辑·可仿真] 内容长度限制 FM_PAYLOAD_LEN_MAX.
 * @return FM_OK 成功; FM_ERR 名称非法/写失败; FM_ERR_NOMEM 无目录句柄.
 */
int fm_payload_write(const fm_backend_t *be, const char *name,
                     const char *content, long len);

#ifdef __cplusplus
}
#endif

#endif /* FM_PAYLOAD_H */

/* fm_payload.c — Payload 保存 (路径安全校验)
 * [纯逻辑·可仿真] core 层: 只允许 C 标准库, 无 ESP-IDF 依赖.
 * 借鉴 (参考 espressif/esp-ui 文件管理器的存储策略).
 */
#include "fm_payload.h"

#include <stdio.h>
#include <string.h>

/* [纯逻辑·可仿真] 文件名安全校验: 拒绝穿越/非法扩展/超长名 */
int fm_payload_validate_name(const char *name)
{
    size_t len;

    if (name == NULL) {
        return FM_ERR;
    }
    len = strlen(name);
    if (len == 0 || len >= FM_PAYLOAD_NAME_MAX) {
        return FM_ERR;
    }
    /* 不允许路径分隔符与 ".." 穿越 */
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return FM_ERR;
    }
    if (strstr(name, "..") != NULL) {
        return FM_ERR;
    }
    /* 隐藏文件不允许 */
    if (name[0] == '.') {
        return FM_ERR;
    }
    /* 仅 .txt */
    if (len < 4 || strcmp(name + len - 4, ".txt") != 0) {
        return FM_ERR;
    }
    return FM_OK;
}

/* [纯逻辑·可仿真] 写 payload 到 FM_PAYLOAD_DIR/name (目录需已存在, UI 层可先 mkdir) */
int fm_payload_write(const fm_backend_t *be, const char *name,
                     const char *content, long len)
{
    char path[FM_PAYLOAD_DIR_LEN + FM_PAYLOAD_NAME_MAX + 2];

    if (be == NULL || be->write_file == NULL) {
        return FM_ERR;
    }
    if (fm_payload_validate_name(name) != FM_OK) {
        return FM_ERR;
    }
    if (content == NULL || len < 0 || len > FM_PAYLOAD_LEN_MAX) {
        return FM_ERR;
    }
    if (snprintf(path, sizeof(path), "%s/%s", FM_PAYLOAD_DIR, name)
        >= (int)sizeof(path)) {
        return FM_ERR;
    }
    return be->write_file(be->ctx, path, content, len);
}

/* fm_backend_host.h — fm_backend_t 的 POSIX 主机实现 (pc_sim stub)
 * [纯逻辑·可仿真] 操作 root_dir 下的临时目录, 路径一律映射到 root 内部.
 */
#ifndef FM_BACKEND_HOST_H
#define FM_BACKEND_HOST_H

#include "fm_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 填充 be: 所有路径都会被改写为 <root_dir>/<path>.
 * root_dir 为测试用临时目录 (需已存在). */
void fm_backend_host_init(fm_backend_t *be, const char *root_dir);

#ifdef __cplusplus
}
#endif

#endif /* FM_BACKEND_HOST_H */

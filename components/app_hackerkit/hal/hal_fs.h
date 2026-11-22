/* hal_fs.h — [硬件相关] /sdcard/payloads 文件访问 (hal 层接口) */
#ifndef HAL_FS_H
#define HAL_FS_H

#include "hk_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 填充 hk_fs_ops_t (IDF VFS libc 文件 API 实现) */
void hal_fs_get_ops(hk_fs_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FS_H */

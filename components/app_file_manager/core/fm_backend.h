/* fm_backend.h — 文件系统后端接口 (函数指针表 vtable)
 * [纯逻辑·可仿真] core 层: 只允许 C 标准库 + 本组件头, 无 ESP-IDF 依赖.
 * 借鉴 (参考 espressif/esp-ui 文件管理器的目录遍历思路): core 定义接口,
 * ui 通过接口工作, hal 在真机实现 (hal_sdcard.c), pc_sim/stubs 在主机实现.
 */
#ifndef FM_BACKEND_H
#define FM_BACKEND_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 统一返回码 */
#define FM_OK        0
#define FM_ERR     (-1)
#define FM_ERR_NOMEM (-2)

/* 目录条目名最大长度 (含 '\0') */
#define FM_NAME_MAX 256

typedef struct fm_entry {
    char   name[FM_NAME_MAX];   /* 文件/目录名 (不含路径) */
    bool   is_dir;
    long   size;                /* 字节; 目录为 0 */
} fm_entry_t;

/* 文件系统后端接口 — [纯逻辑·可仿真]
 * 所有函数返回 FM_OK(0) / FM_ERR(-1) / FM_ERR_NOMEM,
 * list_dir 返回实际条目数 (负数=错误), read_file 返回读到的字节数 (负数=错误).
 * ctx 为各实现的自有上下文 (可为 NULL).
 */
typedef struct fm_backend {
    int  (*list_dir)(void *ctx, const char *path, fm_entry_t *out_entries, int max_entries);
    long (*read_file)(void *ctx, const char *path, char *buf, long buf_size);
    int  (*write_file)(void *ctx, const char *path, const char *content, long len);
    int  (*mkdir)(void *ctx, const char *path);
    int  (*remove)(void *ctx, const char *path);
    int  (*rename)(void *ctx, const char *old_path, const char *new_path);
    long (*file_size)(void *ctx, const char *path);
    int  (*file_exists)(void *ctx, const char *path);
    void *ctx;
} fm_backend_t;

#ifdef __cplusplus
}
#endif

#endif /* FM_BACKEND_H */

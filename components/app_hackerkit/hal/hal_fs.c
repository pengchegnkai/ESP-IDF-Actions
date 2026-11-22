/* hal_fs.c — [硬件相关·需真机验证] hk_fs_ops_t 真机实现
 *
 * 经 IDF VFS (libc 文件 API) 读 /sdcard/payloads/ 下的 .txt payload.
 * SD 卡挂载 (sdmmc/SDSPI → VFS "/sdcard") 由工厂基座 main 负责, 本组件不重复挂载.
 */
#include "hal_fs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define HK_PAYLOADS_DIR "/sdcard/payloads"

/* 列出 dir 下 .txt 文件名 — [硬件相关·需真机验证] */
static int hal_list_payloads(const char *dir, char names[][HK_PAYLOAD_NAME_MAX],
                             int max_names)
{
    if (!dir || !names || max_names <= 0) {
        return -1;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max_names) {
        size_t len = strlen(ent->d_name);
        if (len > 4 && strcmp(ent->d_name + len - 4, ".txt") == 0) {
            strncpy(names[count], ent->d_name, HK_PAYLOAD_NAME_MAX - 1);
            names[count][HK_PAYLOAD_NAME_MAX - 1] = '\0';
            count++;
        }
    }
    closedir(d);
    return count;
}

/* 读取整个 payload 文件 — [硬件相关·需真机验证] */
static int hal_read_payload(const char *path, char *buf, size_t len)
{
    if (!path || !buf || len == 0) {
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    size_t n = fread(buf, 1, len - 1, f);
    buf[n] = '\0';
    fclose(f);
    return (int)n;
}

void hal_fs_get_ops(hk_fs_ops_t *ops)
{
    if (ops) {
        ops->list_payloads = hal_list_payloads;
        ops->read_payload = hal_read_payload;
    }
}

/* 未直接使用 HK_PAYLOADS_DIR 常量时的显式引用 */
const char *hk_payloads_dir_default(void)
{
    return HK_PAYLOADS_DIR;
}

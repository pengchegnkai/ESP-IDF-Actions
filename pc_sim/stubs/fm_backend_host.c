/* fm_backend_host.c — fm_backend_t 的 POSIX 主机实现 (pc_sim stub)
 * [纯逻辑·可仿真] 用标准 libc 文件 API (fopen/opendir/stat/...) 操作一个临时
 * 目录: 所有传入路径 strip 掉 "/sdcard" 前缀后拼到 root_dir 下, 与真机
 * hal_sdcard.c 的行为保持一致 (UI 始终以 /sdcard 开头).
 * 借鉴 (参考 espressif/esp-ui 文件管理器的目录遍历与分页思路).
 */
#include "fm_backend_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define HOST_PATH_MAX 1024
/* UI/hal 的挂载点前缀, 映射时剥掉 */
#define MOUNT_PREFIX  "/sdcard"

static char s_root[HOST_PATH_MAX];

/* <root>/<path>: 剥掉 MOUNT_PREFIX, 拒绝 ".." */
static void map_path(char *dst, size_t size, const char *path)
{
    const char *rel = path;

    if (path == NULL) {
        dst[0] = '\0';
        return;
    }
    if (strncmp(rel, MOUNT_PREFIX, strlen(MOUNT_PREFIX)) == 0) {
        rel += strlen(MOUNT_PREFIX);
        if (*rel == '/') {
            rel++;
        }
    } else if (rel[0] == '/') {
        rel++; /* 相对 root 的路径 */
    }
    if (strstr(rel, "..") != NULL) { /* 安全: 主机后端同样拒绝穿越 */
        dst[0] = '\0';
        return;
    }
    snprintf(dst, size, "%s%s%s", s_root, s_root[0] && rel[0] ? "/" : "", rel);
}

static int host_list_dir(void *ctx, const char *path, fm_entry_t *out, int max)
{
    char full[HOST_PATH_MAX];
    DIR *dir;
    struct dirent *de;
    int n = 0;
    (void)ctx;

    map_path(full, sizeof(full), path);
    if (full[0] == '\0' || out == NULL || max <= 0) {
        return FM_ERR;
    }
    dir = opendir(full);
    if (dir == NULL) {
        return FM_ERR;
    }
    while (n < max && (de = readdir(dir)) != NULL) {
        char sub[HOST_PATH_MAX * 2];
        struct stat st;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue; /* 跳过 . 与 .. (真机 hal 层同样过滤) */
        }
        snprintf(sub, sizeof(sub), "%s/%s", full, de->d_name);
        out[n].is_dir = (stat(sub, &st) == 0 && S_ISDIR(st.st_mode));
        out[n].size = out[n].is_dir ? 0 : (long)st.st_size;
        snprintf(out[n].name, sizeof(out[n].name), "%s", de->d_name);
        n++;
    }
    closedir(dir);
    return n;
}

static long host_read_file(void *ctx, const char *path, char *buf, long buf_size)
{
    char full[HOST_PATH_MAX];
    FILE *f;
    long n;
    (void)ctx;

    map_path(full, sizeof(full), path);
    if (full[0] == '\0' || buf == NULL || buf_size <= 0) {
        return FM_ERR;
    }
    f = fopen(full, "rb");
    if (f == NULL) {
        return FM_ERR;
    }
    n = (long)fread(buf, 1, (size_t)buf_size, f);
    fclose(f);
    return n;
}

static int host_write_file(void *ctx, const char *path, const char *content, long len)
{
    char full[HOST_PATH_MAX];
    FILE *f;
    size_t w;
    (void)ctx;

    map_path(full, sizeof(full), path);
    if (full[0] == '\0' || content == NULL || len < 0) {
        return FM_ERR;
    }
    f = fopen(full, "wb");
    if (f == NULL) {
        return FM_ERR;
    }
    w = fwrite(content, 1, (size_t)len, f);
    fclose(f);
    return (long)w == len ? FM_OK : FM_ERR;
}

static int host_mkdir(void *ctx, const char *path)
{
    char full[HOST_PATH_MAX];
    (void)ctx;
    map_path(full, sizeof(full), path);
    if (full[0] == '\0') {
        return FM_ERR;
    }
    return mkdir(full, 0775) == 0 ? FM_OK : FM_ERR;
}

static int host_remove(void *ctx, const char *path)
{
    char full[HOST_PATH_MAX];
    (void)ctx;
    map_path(full, sizeof(full), path);
    if (full[0] == '\0') {
        return FM_ERR;
    }
    return remove(full) == 0 ? FM_OK : FM_ERR;
}

static int host_rename(void *ctx, const char *old_path, const char *new_path)
{
    char ofull[HOST_PATH_MAX], nfull[HOST_PATH_MAX];
    (void)ctx;
    map_path(ofull, sizeof(ofull), old_path);
    map_path(nfull, sizeof(nfull), new_path);
    if (ofull[0] == '\0' || nfull[0] == '\0') {
        return FM_ERR;
    }
    return rename(ofull, nfull) == 0 ? FM_OK : FM_ERR;
}

static long host_file_size(void *ctx, const char *path)
{
    char full[HOST_PATH_MAX];
    struct stat st;
    (void)ctx;
    map_path(full, sizeof(full), path);
    if (full[0] == '\0' || stat(full, &st) != 0) {
        return FM_ERR;
    }
    return (long)st.st_size;
}

static int host_file_exists(void *ctx, const char *path)
{
    char full[HOST_PATH_MAX];
    struct stat st;
    (void)ctx;
    map_path(full, sizeof(full), path);
    return (full[0] != '\0' && stat(full, &st) == 0) ? FM_OK : FM_ERR;
}

void fm_backend_host_init(fm_backend_t *be, const char *root_dir)
{
    if (be == NULL || root_dir == NULL) {
        return;
    }
    snprintf(s_root, sizeof(s_root), "%s", root_dir);
    be->list_dir = host_list_dir;
    be->read_file = host_read_file;
    be->write_file = host_write_file;
    be->mkdir = host_mkdir;
    be->remove = host_remove;
    be->rename = host_rename;
    be->file_size = host_file_size;
    be->file_exists = host_file_exists;
    be->ctx = NULL;
}

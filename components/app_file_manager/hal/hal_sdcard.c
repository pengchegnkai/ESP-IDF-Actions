/* hal_sdcard.c — fm_backend_t 的真机实现 (SDMMC TF 卡 / FATFS)
 * [硬件相关·需真机验证] 依赖 IDF VFS: FATFS 挂载在 /sdcard 后, IDF VFS 把
 * fopen/fread/opendir/readdir 等 POSIX 接口映射到 FATFS — 直接用标准 libc API.
 * 挂载走 board 组件的 board_sdcard_mount() (见 components/board/include/board_storage.h).
 * 借鉴 (参考 espressif/esp-ui 文件管理器的目录遍历与分页思路).
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "board_storage.h"
#include "fm_backend.h"

/* [硬件相关·需真机验证] 挂载 TF 卡. 重复挂载安全 (board_sdcard_mount 幂等). */
static int hal_sdcard_mount(void)
{
    esp_err_t err = board_sdcard_mount();
    return err == 0 ? FM_OK : FM_ERR;
}

/* [硬件相关·需真机验证] 列目录, 返回条目数; 失败返回 FM_ERR */
static int hal_list_dir(void *ctx, const char *path, fm_entry_t *out, int max)
{
    DIR *dir;
    struct dirent *de;
    int n = 0;
    (void)ctx;

    if (path == NULL || out == NULL || max <= 0) {
        return FM_ERR;
    }
    dir = opendir(path);
    if (dir == NULL) {
        return FM_ERR;
    }
    while (n < max && (de = readdir(dir)) != NULL) {
        char full[512];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        out[n].is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        out[n].size = out[n].is_dir ? 0 : (long)st.st_size;
        snprintf(out[n].name, sizeof(out[n].name), "%s", de->d_name);
        n++;
    }
    closedir(dir);
    return n;
}

/* [硬件相关·需真机验证] 读文件, 返回读到的字节数 */
static long hal_read_file(void *ctx, const char *path, char *buf, long buf_size)
{
    FILE *f;
    long n;
    (void)ctx;

    if (path == NULL || buf == NULL || buf_size <= 0) {
        return FM_ERR;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return FM_ERR;
    }
    n = (long)fread(buf, 1, (size_t)buf_size, f);
    fclose(f);
    return n;
}

/* [硬件相关·需真机验证] 写文件 (覆盖) */
static int hal_write_file(void *ctx, const char *path, const char *content, long len)
{
    FILE *f;
    size_t w;
    (void)ctx;

    if (path == NULL || content == NULL || len < 0) {
        return FM_ERR;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return FM_ERR;
    }
    w = fwrite(content, 1, (size_t)len, f);
    fclose(f);
    return (long)w == len ? FM_OK : FM_ERR;
}

static int hal_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    if (path == NULL) {
        return FM_ERR;
    }
    return mkdir(path, 0775) == 0 ? FM_OK : FM_ERR;
}

static int hal_remove(void *ctx, const char *path)
{
    (void)ctx;
    if (path == NULL) {
        return FM_ERR;
    }
    return remove(path) == 0 ? FM_OK : FM_ERR;
}

static int hal_rename(void *ctx, const char *old_path, const char *new_path)
{
    (void)ctx;
    if (old_path == NULL || new_path == NULL) {
        return FM_ERR;
    }
    return rename(old_path, new_path) == 0 ? FM_OK : FM_ERR;
}

static long hal_file_size(void *ctx, const char *path)
{
    struct stat st;
    (void)ctx;
    if (path == NULL || stat(path, &st) != 0) {
        return FM_ERR;
    }
    return (long)st.st_size;
}

static int hal_file_exists(void *ctx, const char *path)
{
    struct stat st;
    (void)ctx;
    return (path != NULL && stat(path, &st) == 0) ? FM_OK : FM_ERR;
}

/* [硬件相关·需真机验证] 初始化真机后端: 挂载 + 填 vtable.
 * 返回 FM_OK 成功; 挂载失败返回 FM_ERR (be 仍被填充, UI 显示 list failed). */
int fm_backend_hal_init(fm_backend_t *be)
{
    if (be == NULL) {
        return FM_ERR;
    }
    be->list_dir = hal_list_dir;
    be->read_file = hal_read_file;
    be->write_file = hal_write_file;
    be->mkdir = hal_mkdir;
    be->remove = hal_remove;
    be->rename = hal_rename;
    be->file_size = hal_file_size;
    be->file_exists = hal_file_exists;
    be->ctx = NULL;
    hal_sdcard_mount();
    return FM_OK;
}

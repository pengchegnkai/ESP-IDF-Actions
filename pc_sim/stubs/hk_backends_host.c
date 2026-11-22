/* hk_backends_host.c — [纯逻辑·可仿真] PC 仿真侧 HackerKit 后端替身实现
 *
 * fake usb + POSIX fs (临时目录/本地路径), 供 test_hk_*.c 断言.
 */
#include "hk_backends_host.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

static hk_fake_usb_t s_fake;

/* ─── fake usb: 记录每个报告 + 计数, 供断言 ─────────────────────────── */

void hk_fake_usb_reset(void)
{
    memset(&s_fake, 0, sizeof(s_fake));
}

hk_fake_usb_t *hk_fake_usb_get(void)
{
    return &s_fake;
}

static void fake_send_keyboard_report(const uint8_t report[HK_REPORT_LEN])
{
    if (s_fake.count < HK_FAKE_REPORTS_MAX) {
        memcpy(s_fake.reports[s_fake.count], report, HK_REPORT_LEN);
        s_fake.count++;
    } else {
        s_fake.dropped++;
    }
}

void hk_fake_usb_get_ops(hk_usb_ops_t *ops)
{
    if (ops) {
        ops->send_keyboard_report = fake_send_keyboard_report;
    }
}

/* ─── POSIX fs (临时目录: dir 参数由测试传, 如 "/tmp/hk_test") ──────── */

static int host_list_payloads(const char *dir, char names[][HK_PAYLOAD_NAME_MAX],
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

static int host_read_payload(const char *path, char *buf, size_t len)
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

void hk_host_fs_get_ops(hk_fs_ops_t *ops)
{
    if (ops) {
        ops->list_payloads = host_list_payloads;
        ops->read_payload = host_read_payload;
    }
}

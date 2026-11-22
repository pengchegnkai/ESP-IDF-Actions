/* test_fm_backend_host.c — POSIX 主机后端全往返测试
 * 覆盖: mkdir-list-write-read-rename-remove 全往返 + file_size/file_exists
 * + 路径映射 (剥离 /sdcard 前缀) + 穿越拒绝.
 */
#include "test_framework.h"
#include "fm_backend_host.h"
#include "fm_payload.h" /* 仅用 FM_PAYLOAD_DIR 前缀常量验证映射 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static char tmp_dir[] = "fm_host_test_XXXXXX";

TEST(host_full_roundtrip)
{
    fm_backend_t be;
    fm_entry_t entries[16];
    char content[] = "line1\nline2\n";
    char rbuf[64];
    char path[512];
    long n;
    int cnt;

    CHECK(mkdtemp(tmp_dir) != NULL);
    fm_backend_host_init(&be, tmp_dir);

    /* mkdir + write */
    CHECK_EQ_INT(be.mkdir(be.ctx, "/sdcard/payloads"), FM_OK);
    CHECK_EQ_INT(be.mkdir(be.ctx, "/sdcard/docs"), FM_OK);
    snprintf(path, sizeof(path), "/sdcard/payloads/one.txt");
    CHECK_EQ_INT(be.write_file(be.ctx, path, content, (long)sizeof(content) - 1), FM_OK);

    /* file_exists / file_size */
    CHECK_EQ_INT(be.file_exists(be.ctx, path), FM_OK);
    CHECK_EQ_INT(be.file_exists(be.ctx, "/sdcard/payloads/missing.txt"), FM_ERR);
    CHECK_EQ_INT(be.file_size(be.ctx, path), (long)sizeof(content) - 1);

    /* read */
    n = be.read_file(be.ctx, path, rbuf, sizeof(rbuf) - 1);
    CHECK_EQ_INT(n, (long)sizeof(content) - 1);
    rbuf[n] = '\0';
    CHECK_STREQ(rbuf, content);

    /* list: payloads 下应有 1 个文件 */
    cnt = be.list_dir(be.ctx, "/sdcard/payloads", entries, 16);
    CHECK_EQ_INT(cnt, 1);
    CHECK_STREQ(entries[0].name, "one.txt");
    CHECK(entries[0].is_dir == false);
    CHECK_EQ_INT(entries[0].size, (long)sizeof(content) - 1);

    /* list 根: 应有 2 个目录 */
    cnt = be.list_dir(be.ctx, "/sdcard", entries, 16);
    CHECK_EQ_INT(cnt, 2);

    /* rename */
    CHECK_EQ_INT(be.rename(be.ctx, "/sdcard/payloads/one.txt",
                           "/sdcard/payloads/two.txt"), FM_OK);
    CHECK_EQ_INT(be.file_exists(be.ctx, "/sdcard/payloads/two.txt"), FM_OK);
    CHECK_EQ_INT(be.file_exists(be.ctx, "/sdcard/payloads/one.txt"), FM_ERR);

    /* remove */
    CHECK_EQ_INT(be.remove(be.ctx, "/sdcard/payloads/two.txt"), FM_OK);
    CHECK_EQ_INT(be.file_exists(be.ctx, "/sdcard/payloads/two.txt"), FM_ERR);

    /* 错误分支 */
    CHECK_EQ_INT(be.list_dir(be.ctx, "/sdcard/no_such_dir", entries, 16), FM_ERR);
    CHECK_EQ_INT(be.read_file(be.ctx, "/sdcard/no_such_file", rbuf, 10), FM_ERR);
}

TEST(host_rejects_traversal)
{
    fm_backend_t be;
    char buf[16];
    char tmp2[] = "fm_host_trav_XXXXXX";

    CHECK(mkdtemp(tmp2) != NULL);
    fm_backend_host_init(&be, tmp2);
    /* ".." 穿越被主机后端拒绝 (返回错误而非写穿 root) */
    CHECK_EQ_INT(be.write_file(be.ctx, "/sdcard/../evil.txt", "x", 1), FM_ERR);
    CHECK_EQ_INT(be.read_file(be.ctx, "/sdcard/../etc/passwd", buf, sizeof(buf)), FM_ERR);
    /* 绝对系统路径被剥离后映射进 root, 不会命中真实 /etc/passwd */
    CHECK_EQ_INT(be.read_file(be.ctx, "/etc/passwd", buf, sizeof(buf)), FM_ERR);
}

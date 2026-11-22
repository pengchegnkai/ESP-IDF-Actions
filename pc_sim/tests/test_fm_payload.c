/* test_fm_payload.c — payload 路径安全校验测试
 * 覆盖: 路径穿越拒绝 / 扩展名校验 / 超长名 / 空名 / 写入往返.
 */
#include "test_framework.h"
#include "fm_payload.h"
#include "fm_backend_host.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* --- 名字校验 --- */

TEST(payload_name_reject_traversal)
{
    CHECK_EQ_INT(fm_payload_validate_name("../evil.txt"), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name("a/../b.txt"), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name(".."), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name("dir/secret.txt"), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name("back\\slash.txt"), FM_ERR);
}

TEST(payload_name_reject_bad_ext)
{
    CHECK_EQ_INT(fm_payload_validate_name("script.sh"), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name("noext"), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name(".txt"), FM_ERR);      /* 隐藏文件 */
    CHECK_EQ_INT(fm_payload_validate_name(".hidden.txt"), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name(""), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name(NULL), FM_ERR);
    CHECK_EQ_INT(fm_payload_validate_name("ok"), FM_ERR);        /* < 4 字符 */
}

TEST(payload_name_reject_too_long)
{
    /* 恰好 63 字符 + '\0' = 合法上限内 */
    char name[FM_PAYLOAD_NAME_MAX + 8];
    memset(name, 'a', 59);
    strcpy(name + 59, ".txt");
    CHECK_EQ_INT(fm_payload_validate_name(name), FM_OK);
    /* 64+ 字符名 → 拒绝 */
    memset(name, 'a', 60);
    strcpy(name + 60, ".txt");
    CHECK_EQ_INT(fm_payload_validate_name(name), FM_ERR);
}

TEST(payload_name_accept_normal)
{
    CHECK_EQ_INT(fm_payload_validate_name("hello.txt"), FM_OK);
    CHECK_EQ_INT(fm_payload_validate_name("my_script-v2.txt"), FM_OK);
}

/* --- 写入 (走 host 后端) --- */

static char tmp_dir[] = "fm_payload_test_XXXXXX";

TEST(payload_write_roundtrip_and_guard)
{
    fm_backend_t be;
    char content[] = "print(\"hi\")\n";
    char path[256];
    char rbuf[128];
    long n;

    CHECK(mkdtemp(tmp_dir) != NULL);
    fm_backend_host_init(&be, tmp_dir);

    /* 目录未建 → 写失败 (backend fopen "d/<name>" 失败) */
    CHECK_EQ_INT(fm_payload_write(&be, "a.txt", content, (long)sizeof(content) - 1), FM_ERR);
    CHECK_EQ_INT(be.mkdir(be.ctx, FM_PAYLOAD_DIR), FM_OK);

    /* 合法写入 */
    CHECK_EQ_INT(fm_payload_write(&be, "a.txt", content, (long)sizeof(content) - 1), FM_OK);
    snprintf(path, sizeof(path), "/sdcard/payloads/a.txt");
    n = be.read_file(be.ctx, path, rbuf, sizeof(rbuf) - 1);
    CHECK_EQ_INT(n, (long)sizeof(content) - 1);
    rbuf[n] = '\0';
    CHECK_STREQ(rbuf, content);

    /* 非法名/超长内容/空 backend → 拒绝 */
    CHECK_EQ_INT(fm_payload_write(&be, "../evil.txt", content, 5), FM_ERR);
    CHECK_EQ_INT(fm_payload_write(&be, "b.bin", content, 5), FM_ERR);
    CHECK_EQ_INT(fm_payload_write(&be, "c.txt", content, FM_PAYLOAD_LEN_MAX + 1), FM_ERR);
    CHECK_EQ_INT(fm_payload_write(NULL, "d.txt", content, 5), FM_ERR);
}

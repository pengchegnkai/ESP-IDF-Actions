/* test_fm_text.c — 文本分页测试
 * 覆盖: 空文件 / 单行超长 / 末页边界 / 行不截断 / 总页数.
 */
#include "test_framework.h"
#include "fm_text.h"

#include <string.h>

#define FM_TEST_OUT_MAX 4096

/* 测试用读取回调: 从静态缓冲顺序取块 */
typedef struct {
    const char *text;
    long pos;
} src_t;

static long src_read(void *user, char *buf, long buf_size)
{
    src_t *s = (src_t *)user;
    long remain = (long)strlen(s->text) - s->pos;

    if (remain <= 0) {
        return 0;
    }
    if (buf_size > remain) {
        buf_size = remain;
    }
    memcpy(buf, s->text + s->pos, (size_t)buf_size);
    s->pos += buf_size;
    return buf_size;
}

/* 把 text 分页抽完, 校验拼接还原, 总页数写入 *out_total */
static void extract_and_verify(long *out_total, const char *text, long page_chars, const char *tag)
{
    src_t s = { text, 0 };
    long p, total = 0;
    char out[FM_TEST_OUT_MAX];

    total = fm_text_extract(src_read, &s, page_chars, -1, out, sizeof(out));
    (void)tag;
    /* 逐页取出并拼接 */
    src_t s2 = { text, 0 };
    long got = 0;
    for (p = 0; p < total; p++) {
        s2.pos = 0;
        long n = fm_text_extract(src_read, &s2, page_chars, p, out, sizeof(out));
        CHECK_EQ_INT(n, total);
        CHECK(out[0] != '\0' || total == 0);
        got += (long)strlen(out);
    }
    /* 还原长度 = 原文长度 (页拼接不丢字符) */
    CHECK_EQ_INT(got, (long)strlen(text));
    *out_total = total;
}

TEST(text_empty_file)
{
    src_t s = { "", 0 };
    char out[64];

    CHECK_EQ_INT(fm_text_page_count(src_read, &s, 10), 0);
    CHECK_EQ_INT(fm_text_extract(src_read, &s, 10, 0, out, sizeof(out)), 0);
}

TEST(text_single_short_line)
{
    src_t s = { "hello\n", 0 };
    /* 一行 6 字符, 每页 100 → 1 页 */
    CHECK_EQ_INT(fm_text_page_count(src_read, &s, 100), 1);
}

TEST(text_line_not_split)
{
    src_t s = { "aaa\nbbb\nccc\n", 0 };
    char out[64];
    /* 每页 7 字符: "aaa\n"(4)+"bbb\n"(4)=8>7 → 每页一行, 3 页 */
    CHECK_EQ_INT(fm_text_page_count(src_read, &s, 7), 3);
    s.pos = 0;
    CHECK_EQ_INT(fm_text_extract(src_read, &s, 7, 0, out, sizeof(out)), 3);
    CHECK_STREQ(out, "aaa\n");
    s.pos = 0;
    CHECK_EQ_INT(fm_text_extract(src_read, &s, 7, 2, out, sizeof(out)), 3);
    CHECK_STREQ(out, "ccc\n");
    /* 越界页 → 0 (契约: page 越界返回 0) */
    s.pos = 0;
    CHECK_EQ_INT(fm_text_extract(src_read, &s, 7, 3, out, sizeof(out)), 0);
}

TEST(text_single_overlong_line_own_page)
{
    /* 单行 100 字符 (无换行), 每页 10 → 独占 1 页, 不截断 */
    char text[128];
    memset(text, 'x', 100);
    text[100] = '\0';
    {
        src_t s = { text, 0 };
        CHECK_EQ_INT(fm_text_page_count(src_read, &s, 10), 1);
    }
}

TEST(text_last_page_boundary)
{
    /* 4 行各 5 字符 "aaaaa\n", 每页 10 字符 (换行计入) → 每页 1 行, 共 4 页 */
    src_t s = { "aaaaa\nbbbbb\nccccc\nddddd\n", 0 };
    char out[64];

    CHECK_EQ_INT(fm_text_page_count(src_read, &s, 10), 4);
    s.pos = 0;
    CHECK_EQ_INT(fm_text_extract(src_read, &s, 10, 1, out, sizeof(out)), 4);
    CHECK_STREQ(out, "bbbbb\n");
    /* 末页之后越界 → 0 */
    s.pos = 0;
    CHECK_EQ_INT(fm_text_extract(src_read, &s, 10, 4, out, sizeof(out)), 0);
}

TEST(text_pages_reassemble)
{
    char text[500];
    long i, pages;
    for (i = 0; i < 499; i++) {
        /* 每 64 字符插入换行 → 多页 (单行超长独占一页的契约下, 无换行只有 1 页) */
        text[i] = (i % 64 == 63) ? '\n' : (char)('a' + (i % 26));
    }
    text[499] = '\0';
    extract_and_verify(&pages, text, 64, "reassemble");
    CHECK(pages > 1);
}

TEST(text_bad_args)
{
    src_t s = { "abc", 0 };
    CHECK_EQ_INT(fm_text_page_count(NULL, &s, 10), 0);
    CHECK_EQ_INT(fm_text_page_count(src_read, &s, 0), 0);
    CHECK_EQ_INT(fm_text_page_count(src_read, &s, -5), 0);
}

/* fm_text.c — 文本分页预览
 * [纯逻辑·可仿真] core 层: 只允许 C 标准库, 无 ESP-IDF 依赖.
 * 借鉴 (参考 espressif/esp-ui 文件管理器的分页预览思路).
 *
 * 分页规则: 按行 (以 '\n' 结尾) 累积, 每页字符数不超过 page_chars;
 * 不截断行中间 — 放不下的行换到下一页; 单行超过 page_chars 时独占一页.
 */
#include "fm_text.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* 通过回调把整个文本读入内存 */
static char *load_all(fm_text_read_cb cb, void *user, long *out_len)
{
    char *buf = NULL;
    long len = 0, cap = 0, n;
    char tmp[512];

    if (cb == NULL) {
        return NULL;
    }
    while ((n = cb(user, tmp, (long)sizeof(tmp))) > 0) {
        if (len + n + 1 > cap) {
            long new_cap = cap == 0 ? 4096 : cap * 2;
            char *nb;
            while (new_cap < len + n + 1) {
                new_cap *= 2;
            }
            nb = (char *)realloc(buf, (size_t)new_cap);
            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap = new_cap;
        }
        memcpy(buf + len, tmp, (size_t)n);
        len += n;
    }
    if (n < 0) {
        free(buf);
        return NULL;
    }
    if (buf != NULL) {
        buf[len] = '\0';
    }
    *out_len = len;
    return buf;
}

/* 遍历文本, 返回总页数; page >= 0 时把第 page 页内容拷给 out (可为 NULL) */
static long paginate(const char *text, long len, long page_chars,
                     long page, char *out, long out_size)
{
    long pos = 0, cur_page = 0;
    long page_start = 0, page_len = 0;

    if (page_chars <= 0 || text == NULL) {
        return 0;
    }
    if (out != NULL && out_size > 0) {
        out[0] = '\0';
    }
    while (pos <= len) {
        /* 找一行 (到 '\n' 或文末) */
        long line_end = pos;
        while (line_end < len && text[line_end] != '\n') {
            line_end++;
        }
        if (line_end < len) {
            line_end++; /* 含 '\n' */
        }
        {
            long line_len = line_end - pos;
            bool line_is_last = (line_end >= len && line_len == 0);
            if (line_is_last) {
                /* 文件以 '\n' 结尾时的尾部空 "行", 结束 */
                break;
            }
            if (page_len > 0 && page_len + line_len > page_chars) {
                /* 当前行放不下: 结束当前页 */
                if (cur_page == page && out != NULL) {
                    long copy = page_len < out_size - 1 ? page_len : out_size - 1;
                    memcpy(out, text + page_start, (size_t)copy);
                    out[copy] = '\0';
                }
                cur_page++;
                page_start = pos;
                page_len = 0;
            }
            page_len += line_len;
            /* 单行超长: 独占一页, 立即翻页 */
            if (page_len >= page_chars) {
                if (cur_page == page && out != NULL) {
                    long copy = page_len < out_size - 1 ? page_len : out_size - 1;
                    memcpy(out, text + page_start, (size_t)copy);
                    out[copy] = '\0';
                }
                cur_page++;
                page_start = pos + line_len;
                page_len = 0;
            }
            pos = line_end;
        }
    }
    /* 最后一页 (尚有累积内容) */
    if (page_len > 0) {
        if (cur_page == page && out != NULL) {
            long copy = page_len < out_size - 1 ? page_len : out_size - 1;
            memcpy(out, text + page_start, (size_t)copy);
            out[copy] = '\0';
        }
        cur_page++;
    }
    return cur_page;
}

long fm_text_page_count(fm_text_read_cb cb, void *user, long page_chars)
{
    return fm_text_extract(cb, user, page_chars, -1, NULL, 0);
}

long fm_text_extract(fm_text_read_cb cb, void *user, long page_chars,
                     long page, char *out, long out_size)
{
    long len = 0;
    char *text = load_all(cb, user, &len);
    long pages;

    if (text == NULL) {
        return len == 0 && cb != NULL ? 0 : 0;
    }
    pages = paginate(text, len, page_chars, page, out, out_size);
    free(text);
    if (page >= 0 && page >= pages) {
        return 0; /* 越界页: 契约为返回 0 */
    }
    return pages;
}

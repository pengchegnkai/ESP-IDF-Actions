/* fm_text.h — 文本分页预览
 * [纯逻辑·可仿真] core 层: 只允许 C 标准库 + 本组件头, 无 ESP-IDF 依赖.
 * 借鉴 (参考 espressif/esp-ui 文件管理器的分页预览思路):
 * 给定读取回调 + 每页字符数, 抽取第 page 页; 换行不截断行中间;
 * 单行超长时该行独占一页.
 */
#ifndef FM_TEXT_H
#define FM_TEXT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 读取回调: 把文本内容填充进 buf, 返回字节数, 文件尾返回 0, 错误返回负数.
 * user 为调用方自有上下文. 多次调用按序拼接为完整文本. */
typedef long (*fm_text_read_cb)(void *user, char *buf, long buf_size);

/* 计算总页数.
 * @param page_chars 每页最大字符数 (>0)
 * @return 总页数 (>0); 参数非法返回 0.
 */
long fm_text_page_count(fm_text_read_cb cb, void *user, long page_chars);

/* 抽取第 page 页 (0-based) 到 out.
 * @param out 输出缓冲 (以 '\0' 结尾); 传 NULL 时只统计页数.
 * @param out_size out 容量.
 * @return 总页数; 参数非法返回 0; page 越界时返回 0.
 */
long fm_text_extract(fm_text_read_cb cb, void *user, long page_chars,
                     long page, char *out, long out_size);

#ifdef __cplusplus
}
#endif

#endif /* FM_TEXT_H */

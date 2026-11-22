/* hk_script.h — [纯逻辑·可仿真] payload 脚本解析状态机
 *
 * 脚本格式 (借鉴 EvilKey-firmware 的 microSD 脚本加载执行,
 * 指令集思路参考 USB Rubber Ducky):
 *   '#' 开头为注释, 空行忽略
 *   STRING "hello world"   — 发送字符串 (支持 \" \' \\ 转义)
 *   DELAY 500              — 等待毫秒
 *   KEYCOMBO ctrl c        — 组合键 (修饰键在前, 普通键最后)
 * 每行一条指令; 未知指令 → 解析错误 (返回负行号).
 */
#ifndef HK_SCRIPT_H
#define HK_SCRIPT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HK_STRING_MAX 64
#define HK_NAME_MAX   64
#define HK_TOKEN_MAX  32

typedef enum {
    HK_EV_STRING = 0,
    HK_EV_DELAY,
    HK_EV_KEYCOMBO,
} hk_event_type_t;

typedef struct {
    hk_event_type_t type;
    char str[HK_STRING_MAX];   /* HK_EV_STRING: 内容 (已去引号/解转义) */
    uint32_t delay_ms;         /* HK_EV_DELAY */
    uint8_t mods;              /* HK_EV_KEYCOMBO: 修饰键掩码 */
    uint8_t key;               /* HK_EV_KEYCOMBO: 普通键 usage ID */
} hk_event_t;

/* 解析整段脚本文本, 事件写入调用方给定数组 (容量 max_events, 防溢出).
 * 返回事件数; 解析失败返回 -(错误行号) (行号从 1 起); 参数非法返回 -1. */
int hk_script_parse(const char *text, hk_event_t *events, int max_events);

/* 逐行喂入式解析状态机: 每次喂一行, 解析出的事件追加到 *events/
 * (*count 为当前事件数, 上限 max_events). 返回 0 成功;
 * 返回 -(该行内的错误行号) 表示本行解析失败; 溢出返回 -100000. */
int hk_script_parse_line(const char *line, hk_event_t *events, int max_events, int *count);

#ifdef __cplusplus
}
#endif

#endif /* HK_SCRIPT_H */

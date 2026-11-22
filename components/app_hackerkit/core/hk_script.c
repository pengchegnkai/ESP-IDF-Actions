/* hk_script.c — [纯逻辑·可仿真] 脚本解析状态机实现
 *
 * 借鉴 EvilKey-firmware (microSD 脚本加载执行) 与 USB Rubber Ducky 指令集.
 */
#include "hk_script.h"
#include "hid_keycodes.h"

#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* 跳过行首空白 */
static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/* 取下一个 token (空白分隔), 返回写入长度, 无 token 返回 0 */
static int next_token(const char **s, char *out, size_t cap)
{
    size_t n = 0;
    const char *p = skip_ws(*s);
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (n + 1 < cap) {
            out[n++] = *p;
        }
        p++;
    }
    *s = p;
    out[n] = '\0';
    return (int)n;
}

/* 解析 STRING 的带引号字符串 (处理 \" \' \\ 转义), 返回 0/-1 */
static int parse_string_arg(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    s = skip_ws(s);
    if (*s != '"') {
        return -1;
    }
    s++;
    while (*s && *s != '\n' && *s != '\r') {
        char ch = *s;
        if (ch == '\\' && (s[1] == '"' || s[1] == '\'' || s[1] == '\\')) {
            s++;
            ch = *s;
        } else if (ch == '"') {
            return 0; /* 收尾引号 */
        }
        if (n + 1 < cap) {
            out[n++] = ch;
        } else {
            return -1; /* 超长视为错误 */
        }
        s++;
    }
    return -1; /* 未闭合 */
}

/* 解析 KEYCOMBO 修饰键/普通键序列 */
static int parse_keycombo(const char *s, uint8_t *mods, uint8_t *key)
{
    char tok[HK_TOKEN_MAX];
    uint8_t m = HK_MOD_NONE;
    uint8_t k = HK_KEY_NONE;
    int ntok = 0;

    while (next_token(&s, tok, sizeof(tok)) > 0) {
        uint8_t km = hk_mod_from_name(tok);
        if (km) {
            m |= km;
        } else {
            uint8_t sk = hk_special_key_from_name(tok);
            if (!sk) {
                /* 普通单字符键: 保留原字符 — 大写经 ascii_to_keycode 自动加 LSHIFT */
                if (tok[1] == '\0') {
                    bool sh = false;
                    sk = hk_ascii_to_keycode((unsigned char)tok[0], &sh);
                    if (sh) {
                        m |= HK_MOD_LSHIFT;
                    }
                }
                if (!sk) {
                    return -1;
                }
            }
            k = sk;
            ntok++;
        }
    }
    if (ntok != 1) {
        return -1; /* 必须恰好一个普通键 */
    }
    *mods = m;
    *key = k;
    return 0;
}

int hk_script_parse_line(const char *line, hk_event_t *events, int max_events, int *count)
{
    char cmd[HK_TOKEN_MAX];
    const char *p = line;

    p = skip_ws(p);
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') {
        return 0; /* 空行/注释: 忽略 */
    }
    if (next_token(&p, cmd, sizeof(cmd)) <= 0) {
        return 0;
    }

    if (*count >= max_events) {
        return -100000; /* 事件数组溢出 */
    }
    hk_event_t *ev = &events[*count];
    memset(ev, 0, sizeof(*ev));

    if (strcmp(cmd, "STRING") == 0) {
        if (parse_string_arg(p, ev->str, HK_STRING_MAX) != 0) {
            return -1;
        }
        ev->type = HK_EV_STRING;
    } else if (strcmp(cmd, "DELAY") == 0) {
        char arg[HK_TOKEN_MAX];
        if (next_token(&p, arg, sizeof(arg)) <= 0) {
            return -1;
        }
        /* 仅接受非负十进制数 */
        for (const char *d = arg; *d; d++) {
            if (!isdigit((unsigned char)*d)) {
                return -1;
            }
        }
        ev->type = HK_EV_DELAY;
        ev->delay_ms = (uint32_t)strtoul(arg, NULL, 10);
    } else if (strcmp(cmd, "KEYCOMBO") == 0) {
        if (parse_keycombo(p, &ev->mods, &ev->key) != 0) {
            return -1;
        }
        ev->type = HK_EV_KEYCOMBO;
    } else {
        return -1; /* 未知指令 */
    }
    (*count)++;
    return 0;
}

int hk_script_parse(const char *text, hk_event_t *events, int max_events)
{
    if (!text || !events || max_events <= 0) {
        return -1;
    }
    int count = 0;
    int line_no = 0;
    const char *p = text;
    char line[512];

    while (*p) {
        size_t n = 0;
        while (*p && *p != '\n' && n + 1 < sizeof(line)) {
            line[n++] = *p++;
        }
        line[n] = '\0';
        if (*p == '\n') {
            p++;
        }
        line_no++;
        if (hk_script_parse_line(line, events, max_events, &count) != 0) {
            return -line_no;
        }
    }
    return count;
}

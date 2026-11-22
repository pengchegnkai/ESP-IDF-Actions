/* test_hk_script.c — 脚本解析状态机测试 */
#include "test_framework.h"
#include "hk_script.h"
#include "hid_keycodes.h"

TEST(script_mixed_and_comments)
{
    const char *script =
        "# comment line\n"
        "\n"
        "STRING \"hello world\"\n"
        "DELAY 500\n"
        "KEYCOMBO ctrl c\n";
    hk_event_t ev[8];
    int n = hk_script_parse(script, ev, 8);
    CHECK_EQ_INT(n, 3);
    CHECK_EQ_INT(ev[0].type, HK_EV_STRING);
    CHECK_STREQ(ev[0].str, "hello world");
    CHECK_EQ_INT(ev[1].type, HK_EV_DELAY);
    CHECK_EQ_INT(ev[1].delay_ms, 500);
    CHECK_EQ_INT(ev[2].type, HK_EV_KEYCOMBO);
    CHECK_EQ_INT(ev[2].mods, HK_MOD_LCTRL);
    CHECK_EQ_INT(ev[2].key, HK_KEY_A + ('c' - 'a'));
}

TEST(script_unknown_command_line_no)
{
    hk_event_t ev[8];
    /* 第 2 行是未知指令 → 返回 -2 */
    CHECK_EQ_INT(hk_script_parse("DELAY 10\nFOO bar\n", ev, 8), -2);
    /* 第 3 行未知 → -3 */
    CHECK_EQ_INT(hk_script_parse("DELAY 1\nDELAY 2\nREBOOT\n", ev, 8), -3);
}

TEST(script_escape_in_string)
{
    hk_event_t ev[4];
    CHECK_EQ_INT(hk_script_parse("STRING \"a\\\"b\\\\c\"", ev, 4), 1);
    CHECK_STREQ(ev[0].str, "a\"b\\c");
    /* 单引号转义 */
    CHECK_EQ_INT(hk_script_parse("STRING \"it\\'s\"", ev, 4), 1);
    CHECK_STREQ(ev[0].str, "it's");
}

TEST(script_event_overflow)
{
    hk_event_t ev[2];
    /* 3 条指令但容量只有 2 → 溢出错误 */
    int rc = hk_script_parse("DELAY 1\nDELAY 2\nDELAY 3\n", ev, 2);
    CHECK(rc < 0);
}

TEST(script_bad_args_and_unclosed_string)
{
    hk_event_t ev[4];
    CHECK_EQ_INT(hk_script_parse(NULL, ev, 4), -1);
    CHECK_EQ_INT(hk_script_parse("STRING \"abc\n", ev, 4), -1); /* 未闭合 */
    CHECK_EQ_INT(hk_script_parse("DELAY abc\n", ev, 4), -1);    /* 非数字 */
}

TEST(script_keycombo_variants)
{
    hk_event_t ev[4];
    CHECK_EQ_INT(hk_script_parse("KEYCOMBO ctrl alt delete\n", ev, 4), -1); /* 多个普通键 */
    CHECK_EQ_INT(hk_script_parse("KEYCOMBO enter\n", ev, 4), 1);
    CHECK_EQ_INT(ev[0].key, HK_KEY_ENTER);
    CHECK_EQ_INT(ev[0].mods, HK_MOD_NONE);
    /* 大写 'C' → shift+c */
    CHECK_EQ_INT(hk_script_parse("KEYCOMBO ctrl C\n", ev, 4), 1);
    CHECK_EQ_INT(ev[0].mods, HK_MOD_LCTRL | HK_MOD_LSHIFT);
    CHECK_EQ_INT(ev[0].key, HK_KEY_A + ('c' - 'a'));
}

TEST(script_ascii_to_keycode_shift)
{
    bool sh = false;
    CHECK_EQ_INT(hk_ascii_to_keycode('a', &sh), HK_KEY_A);
    CHECK(!sh);
    CHECK_EQ_INT(hk_ascii_to_keycode('A', &sh), HK_KEY_A);
    CHECK(sh);
    /* HID usage: '1'..'0' = 0x1E..0x27, '5' = 0x22; '%' = Shift+5 */
    CHECK_EQ_INT(hk_ascii_to_keycode('5', &sh), 0x22);
    CHECK(!sh);
    CHECK_EQ_INT(hk_ascii_to_keycode('%', &sh), 0x22);
    CHECK(sh);
    CHECK_EQ_INT(hk_ascii_to_keycode(' ', &sh), HK_KEY_SPACE);
    CHECK_EQ_INT(hk_ascii_to_keycode('\n', &sh), HK_KEY_ENTER);
    /* 不支持字符返回 0 */
    CHECK_EQ_INT(hk_ascii_to_keycode(0x7F, &sh), 0);
}

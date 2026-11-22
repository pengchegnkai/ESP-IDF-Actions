/* hid_keycodes.h — [纯逻辑·可仿真] HID usage table 常用 keycode 映射
 *
 * 借鉴 EvilKey-firmware 与 USB Rubber Ducky 指令集思路: 可打印 ASCII
 * (含大小写 shift 标记) + 修饰键 + 常用特殊键.
 * 无 ESP-IDF 依赖, pc_sim 覆盖.
 */
#ifndef HK_HID_KEYCODES_H
#define HK_HID_KEYCODES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 修饰键位掩码 (HID report modifier byte) */
#define HK_MOD_NONE   0x00
#define HK_MOD_LCTRL  0x01
#define HK_MOD_LSHIFT 0x02
#define HK_MOD_LALT   0x04
#define HK_MOD_LGUI   0x08
#define HK_MOD_RCTRL  0x10
#define HK_MOD_RSHIFT 0x20
#define HK_MOD_RALT   0x40
#define HK_MOD_RGUI   0x80

/* 常用特殊键 usage IDs (USB HID Usage Tables, Keyboard/Keypad page) */
#define HK_KEY_NONE       0x00
#define HK_KEY_A          0x04  /* 'a'..'z' = 0x04..0x1D */
#define HK_KEY_1          0x1E  /* '1'..'9' = 0x1E..0x26, '0' = 0x27 */
#define HK_KEY_ENTER      0x28
#define HK_KEY_ESC        0x29
#define HK_KEY_BACKSPACE  0x2A
#define HK_KEY_TAB        0x2B
#define HK_KEY_SPACE      0x2C
#define HK_KEY_MINUS      0x2D
#define HK_KEY_EQUAL      0x2E
#define HK_KEY_LEFTBRACE  0x2F
#define HK_KEY_RIGHTBRACE 0x30
#define HK_KEY_BACKSLASH  0x31
#define HK_KEY_SEMICOLON  0x33
#define HK_KEY_QUOTE      0x34
#define HK_KEY_GRAVE      0x35
#define HK_KEY_COMMA      0x36
#define HK_KEY_DOT        0x37
#define HK_KEY_SLASH      0x38
#define HK_KEY_CAPSLOCK   0x39
#define HK_KEY_F1         0x3A  /* F1..F12 = 0x3A..0x45 */
#define HK_KEY_F12        0x45
#define HK_KEY_RIGHT      0x4F
#define HK_KEY_LEFT       0x50
#define HK_KEY_DOWN       0x51
#define HK_KEY_UP         0x52

/* ASCII -> HID keycode.
 * 返回 usage ID; 不支持字符返回 0. needShift (可空) 置 true 表示该字符
 * 需按住 shift (大写字母/符号). */
uint8_t hk_ascii_to_keycode(char c, bool *need_shift);

/* 名字 -> 修饰键位掩码, 用于 KEYCOMBO 解析: "ctrl"/"lctrl" -> HK_MOD_LCTRL
 * 等; "shift"/"alt"/"gui"(+"r"前缀变体) 同理. 未知名返回 0. */
uint8_t hk_mod_from_name(const char *name);

/* 特殊键名 -> usage ID: enter/tab/esc/backspace/space/up/down/left/right,
 * f1..f12. 未知名返回 0. */
uint8_t hk_special_key_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* HK_HID_KEYCODES_H */

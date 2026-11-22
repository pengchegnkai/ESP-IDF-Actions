/* hid_keycodes.c — [纯逻辑·可仿真] ASCII → HID usage 映射实现 */
#include "hid_keycodes.h"

uint8_t hk_ascii_to_keycode(char c, bool *need_shift)
{
    if (need_shift) {
        *need_shift = false;
    }
    if (c >= 'a' && c <= 'z') {
        return (uint8_t)(HK_KEY_A + (c - 'a'));
    }
    if (c >= 'A' && c <= 'Z') {
        if (need_shift) {
            *need_shift = true;
        }
        return (uint8_t)(HK_KEY_A + (c - 'A'));
    }
    switch (c) {
    case '1': return 0x1E;
    case '2': return 0x1F;
    case '3': return 0x20;
    case '4': return 0x21;
    case '5': return 0x22;
    case '6': return 0x23;
    case '7': return 0x24;
    case '8': return 0x25;
    case '9': return 0x26;
    case '0': return 0x27;
    /* need shift 的数字/符号行 */
    case '!': if (need_shift) { *need_shift = true; } return 0x1E;
    case '@': if (need_shift) { *need_shift = true; } return 0x1F;
    case '#': if (need_shift) { *need_shift = true; } return 0x20;
    case '$': if (need_shift) { *need_shift = true; } return 0x21;
    case '%': if (need_shift) { *need_shift = true; } return 0x22;
    case '^': if (need_shift) { *need_shift = true; } return 0x23;
    case '&': if (need_shift) { *need_shift = true; } return 0x24;
    case '*': if (need_shift) { *need_shift = true; } return 0x25;
    case '(': if (need_shift) { *need_shift = true; } return 0x26;
    case ')': if (need_shift) { *need_shift = true; } return 0x27;
    case '\n': return HK_KEY_ENTER;
    case '\t': return HK_KEY_TAB;
    case ' ':  return HK_KEY_SPACE;
    case '-':  return HK_KEY_MINUS;
    case '_':  if (need_shift) { *need_shift = true; } return HK_KEY_MINUS;
    case '=':  return HK_KEY_EQUAL;
    case '+':  if (need_shift) { *need_shift = true; } return HK_KEY_EQUAL;
    case '[':  return HK_KEY_LEFTBRACE;
    case '{':  if (need_shift) { *need_shift = true; } return HK_KEY_LEFTBRACE;
    case ']':  return HK_KEY_RIGHTBRACE;
    case '}':  if (need_shift) { *need_shift = true; } return HK_KEY_RIGHTBRACE;
    case '\\': return HK_KEY_BACKSLASH;
    case '|':  if (need_shift) { *need_shift = true; } return HK_KEY_BACKSLASH;
    case ';':  return HK_KEY_SEMICOLON;
    case ':':  if (need_shift) { *need_shift = true; } return HK_KEY_SEMICOLON;
    case '\'': return HK_KEY_QUOTE;
    case '"':  if (need_shift) { *need_shift = true; } return HK_KEY_QUOTE;
    case '`':  return HK_KEY_GRAVE;
    case '~':  if (need_shift) { *need_shift = true; } return HK_KEY_GRAVE;
    case ',':  return HK_KEY_COMMA;
    case '<':  if (need_shift) { *need_shift = true; } return HK_KEY_COMMA;
    case '.':  return HK_KEY_DOT;
    case '>':  if (need_shift) { *need_shift = true; } return HK_KEY_DOT;
    case '/':  return HK_KEY_SLASH;
    case '?':  if (need_shift) { *need_shift = true; } return HK_KEY_SLASH;
    default:   return HK_KEY_NONE;
    }
}

uint8_t hk_mod_from_name(const char *name)
{
    if (!name) {
        return 0;
    }
    if (name[0] == 'c') { /* ctrl / lctrl / rctrl */
        if (name[1] == 't') {                    /* ctrl */
            return HK_MOD_LCTRL;
        }
        if (name[1] == 'l') { return HK_MOD_LCTRL; }
        if (name[1] == 'r') { return HK_MOD_RCTRL; }
    }
    if (name[0] == 's') { /* shift / lshift / rshift */
        if (name[1] == 'h') { return HK_MOD_LSHIFT; }
        if (name[1] == 'l') { return HK_MOD_LSHIFT; }
        if (name[1] == 'r') { return HK_MOD_RSHIFT; }
    }
    if (name[0] == 'a') { /* alt / lalt / ralt */
        if (name[1] == 'l') { return HK_MOD_LALT; }
        if (name[1] == 't') { return HK_MOD_LALT; }
        if (name[1] == 'r') { return HK_MOD_RALT; }
    }
    if (name[0] == 'g') { /* gui / lgui / rgui */
        if (name[1] == 'u') { return HK_MOD_LGUI; }
        if (name[1] == 'l') { return HK_MOD_LGUI; }
        if (name[1] == 'r') { return HK_MOD_RGUI; }
    }
    return 0;
}

uint8_t hk_special_key_from_name(const char *name)
{
    if (!name) {
        return HK_KEY_NONE;
    }
    if (name[0] == 'e') {
        if (name[1] == 'n' && name[2] == 't' && name[3] == 'e' && name[4] == 'r') {
            return HK_KEY_ENTER; /* "enter" */
        }
        if (name[1] == 's' && name[2] == 'c') {
            return HK_KEY_ESC;   /* "esc" */
        }
    }
    if (name[0] == 't' && name[1] == 'a' && name[2] == 'b') {
        return HK_KEY_TAB;
    }
    if (name[0] == 'b' && name[1] == 'a' && name[2] == 'c' && name[3] == 'k') {
        return HK_KEY_BACKSPACE; /* "backspace" */
    }
    if (name[0] == 's' && name[1] == 'p' && name[2] == 'a') {
        return HK_KEY_SPACE;     /* "space" */
    }
    if (name[0] == 'u' && name[1] == 'p') { return HK_KEY_UP; }
    if (name[0] == 'd' && name[1] == 'o') { return HK_KEY_DOWN; }
    if (name[0] == 'l' && name[1] == 'e' && name[2] == 'f') { return HK_KEY_LEFT; }
    if (name[0] == 'r' && name[1] == 'i' && name[2] == 'g') { return HK_KEY_RIGHT; }
    if (name[0] == 'f' && name[1] >= '1' && name[1] <= '9' && name[2] == '\0') {
        return (uint8_t)(HK_KEY_F1 + (name[1] - '1'));
    }
    if (name[0] == 'f' && name[1] == '1' && name[2] >= '0' && name[2] <= '2' && name[3] == '\0') {
        return (uint8_t)(HK_KEY_F1 + 10 + (name[2] - '0')); /* f10..f12 */
    }
    return HK_KEY_NONE;
}

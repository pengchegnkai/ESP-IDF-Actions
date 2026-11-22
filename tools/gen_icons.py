#!/usr/bin/env python3
# gen_icons.py — 程序化生成三个 APP 启动器图标 (64x64 ARGB8888, LVGL C 数组)
# 用法: python3 gen_icons.py <输出目录>
import math
import os
import sys

SIZE = 64
SS = 4  # 超采样倍率

def lerp(a, b, t):
    return a + (b - a) * t

def in_shield(x, y):  # 盾牌
    # 上边横梁 y=12..16, 主体曲线收窄到底部尖
    if not (10 <= y <= 54):
        return False
    top = 16
    half_top = 20
    t = (y - top) / (54 - top)
    half = half_top * (1 - 0.72 * t * t)
    return abs(x - 32) <= half

def in_key(x, y):  # 钥匙 (SecurityKey): 圆环 + 柄
    dx, dy = x - 24, y - 24
    if 6 <= dx * dx + dy * dy <= 12 and not (dx * dx + dy * dy <= 8):
        return True
    if 34 <= x <= 58 and 29 <= y <= 35:
        return True
    if 48 <= x <= 58 and 35 <= y <= 43:
        return True
    return False

def in_folder(x, y):  # 文件夹
    if 8 <= x <= 56 and 18 <= y <= 50:
        return not (8 <= x <= 22 and 12 <= y <= 22 and x - 8 < (22 - y) * 0.5)
    if 10 <= x <= 26 and 12 <= y <= 20:
        return True
    return False

def in_usb(x, y):  # USB 键盘 (HackerKit): 键盘底座 + 连接线
    if 10 <= x <= 54 and 26 <= y <= 48:  # 键盘体
        return True
    if 28 <= x <= 36 and 8 <= y <= 26:  # 线缆
        return True
    return False

def shade(base, hi, t):
    return tuple(int(lerp(base[i], hi[i], t)) for i in range(3))

def render(shape_fn, base, hi):
    buf = bytearray(SIZE * SIZE * 4)
    for py in range(SIZE):
        for px in range(SIZE):
            cov = 0
            for sy in range(SS):
                for sx in range(SS):
                    x = px + (sx + 0.5) / SS
                    y = py + (sy + 0.5) / SS
                    if shape_fn(x, y):
                        cov += 1
            if cov == 0:
                continue
            a = cov * 255 // (SS * SS)
            t = min(1.0, (py - 10) / 44.0)
            r, g, b = shade(base, hi, t)
            i = (py * SIZE + px) * 4
            # LVGL ARGB8888: B G R A (小端)
            buf[i] = b
            buf[i + 1] = g
            buf[i + 2] = r
            buf[i + 3] = a
    return buf

def edge_ok(fn, x, y):  # 描边检测: 形状边界 2px 内
    return fn(x, y) and not all(fn(x + dx, y + dy) for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)))

def emit(name, buf, outdir):
    lines = []
    lines.append(f"/* {name} — 64x64 ARGB8888 程序化生成 (gen_icons.py), 蓝紫渐变 */")
    lines.append("#ifdef __has_include")
    lines.append('    #if __has_include("lvgl.h")')
    lines.append("        #ifndef LV_LVGL_H_INCLUDE_SIMPLE")
    lines.append("            #define LV_LVGL_H_INCLUDE_SIMPLE")
    lines.append("        #endif")
    lines.append("    #endif")
    lines.append("#endif")
    lines.append("")
    lines.append("#if defined(LV_LVGL_H_INCLUDE_SIMPLE)")
    lines.append('    #include "lvgl.h"')
    lines.append("#else")
    lines.append('    #include "lvgl/lvgl.h"')
    lines.append("#endif")
    lines.append("")
    lines.append(f"const uint8_t {name}_map[{SIZE * SIZE * 4}] = {{")
    for row in range(SIZE):
        chunk = buf[row * SIZE * 4:(row + 1) * SIZE * 4]
        hexes = ",".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hexes},")
    lines.append("};")
    lines.append("")
    lines.append(f"const lv_image_dsc_t {name} = {{")
    lines.append("    .header.cf = LV_COLOR_FORMAT_ARGB8888,")
    lines.append("    .header.magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append(f"    .header.w = {SIZE},")
    lines.append(f"    .header.h = {SIZE},")
    lines.append(f"    .data_size = {SIZE * SIZE * 4},")
    lines.append(f"    .data = {name}_map,")
    lines.append("};")
    with open(os.path.join(outdir, f"img_{name}.c"), "w") as f:
        f.write("\n".join(lines) + "\n")

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(outdir, exist_ok=True)
    icons = [
        ("img_app_security_key", in_key, (40, 60, 220), (120, 180, 255)),
        ("img_app_file_manager", in_folder, (30, 130, 200), (90, 200, 255)),
        ("img_app_hackerkit", in_usb, (150, 60, 200), (230, 140, 255)),
    ]
    for name, fn, base, hi in icons:
        emit(name, render(fn, base, hi), outdir)
        print(f"generated {name} -> {outdir}/img_{name}.c")

if __name__ == "__main__":
    main()

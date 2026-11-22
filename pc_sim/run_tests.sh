#!/data/data/com.termux/files/usr/bin/bash
# pc_sim — 主机侧仿真测试: 编译 + 运行 (Termux / Linux / CI 通用)
# 编译 APP 的 core/+ui/ 纯逻辑源码, 用 stubs/ 替换硬件依赖.
# LVGL UI 渲染测试: 需先 git clone --depth 1 --branch release/v9.5
#   https://github.com/lvgl/lvgl pc_sim/lvgl  (之后走 CMake 构建, 见 CMakeLists.txt)
set -e
cd "$(dirname "$0")"
ROOT=..
mkdir -p build

SRCS="test_framework_stub.c"
[ -d stubs ] && SRCS="$SRCS $(ls stubs/*.c 2>/dev/null || true)"
SRCS="$SRCS $(ls tests/*.c 2>/dev/null || true)"

# APP 纯逻辑源码 (core/; ui/ 依赖 LVGL, 走 CMake 路径的 UI 测试)
for app in app_file_manager app_hackerkit app_security_key; do
  d="$ROOT/components/$app"
  if [ -d "$d" ]; then
    SRCS="$SRCS $(find "$d/core" -name '*.c' 2>/dev/null || true)"
  fi
done
[ -f "$ROOT/components/usb_arbiter/usb_arbiter.c" ] && SRCS="$SRCS $ROOT/components/usb_arbiter/usb_arbiter.c"

gcc -std=gnu11 -Wall -Wextra -g -O1 \
  -I. -Istubs \
  -I"$ROOT/components/app_file_manager/core" -I"$ROOT/components/app_file_manager/ui" \
  -I"$ROOT/components/app_hackerkit/core" -I"$ROOT/components/app_hackerkit/ui" \
  -I"$ROOT/components/app_security_key/core" -I"$ROOT/components/app_security_key/ui" \
  -I"$ROOT/components/usb_arbiter" \
  $SRCS -o build/pc_sim_tests

./build/pc_sim_tests

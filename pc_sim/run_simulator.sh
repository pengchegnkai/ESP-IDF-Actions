#!/usr/bin/env bash
# run_simulator.sh — 交互式模拟器 (用户亲手测试入口)
# SDL2 窗口 480x800, 鼠标/手指 = 触摸; FileManager 后端指向 sim_fs/ (真实 POSIX 文件)
set -e
cd "$(dirname "$0")"
mkdir -p sim_fs/payloads
echo "sim_fs/payloads/ 就绪 — 往里放 .txt 即可在 FileManager 里看到"

cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build -j4 --target pc_sim_app
echo "=== 启动模拟器 (HTTP 模式) — 手机浏览器打开 http://127.0.0.1:8080 ==="
./build/pc_sim_app --http

# pc_sim — PC 仿真测试 harness (模拟先行, 分层开发)
#
# 在主机 (Termux / Linux / CI ubuntu) 上编译 APP 的 core/ 纯逻辑层 + UI 层,
# 用 stubs/ 替换 ESP-IDF 硬件依赖, 跑 test_*.c 单元测试.
# 目标: 代码在 PC 模拟器和 P4 真机之间无缝切换 (同一份 core/ui 源码).
#
# 用法:
#   cd pc_sim && ./run_tests.sh          # 构建 + 运行全部测试
#
# 注意: 本目录在 components/ 之外, 不会进入固件构建 (main/CMakeLists.txt GLOB 只扫 components/).

cmake_minimum_required(VERSION 3.16)
project(pc_sim C)

set(CMAKE_C_STANDARD 11)

# 仓库根目录
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_compile_options(-Wall -Wextra -g -O1)

# APP 源码目录 (core/ + ui/ 是纯逻辑, 硬件无关)
set(APP_ROOTS
    ${REPO_ROOT}/components/app_file_manager
    ${REPO_ROOT}/components/app_hackerkit
    ${REPO_ROOT}/components/app_security_key
    ${REPO_ROOT}/components/usb_arbiter
)

set(SIM_SRCS test_framework_stub.c)

# stubs: 主机侧硬件替身
file(GLOB SIM_STUB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.c)
list(FILTER SIM_STUB_SRCS EXCLUDE REGEX "stub_disabled")

# tests: test_*.c
file(GLOB SIM_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c)

add_executable(pc_sim_tests ${SIM_SRCS} ${SIM_STUB_SRCS} ${SIM_TEST_SRCS})

target_include_directories(pc_sim_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${APP_ROOTS}
    # 每个 APP 的 core/ui 头文件目录
    ${REPO_ROOT}/components/app_file_manager/core
    ${REPO_ROOT}/components/app_file_manager/ui
    ${REPO_ROOT}/components/app_hackerkit/core
    ${REPO_ROOT}/components/app_hackerkit/ui
    ${REPO_ROOT}/components/app_security_key/core
    ${REPO_ROOT}/components/app_security_key/ui
    ${REPO_ROOT}/components/usb_arbiter
)

# 固件源码中纯逻辑 .c 文件 (不含 hal/ — hal 只进固件构建)
foreach(app ${APP_ROOTS})
    if(EXISTS ${app})
        file(GLOB_RECURSE APP_CORE_SRCS ${app}/core/*.c ${app}/ui/*.c)
        # usb_arbiter 是纯 C 组件, core 即根目录
        if(EXISTS ${app}/usb_arbiter.c)
            list(APPEND APP_CORE_SRCS ${app}/usb_arbiter.c)
        endif()
        target_sources(pc_sim_tests PRIVATE ${APP_CORE_SRCS})
    endif()
endforeach()

# LVGL 主机构建 (UI 层测试 + 交互式模拟器): 本地 v9.5.0 源码 (与工厂基座同版本)
#   git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl pc_sim/lvgl
# 渲染: SDL2 窗口 (LV_USE_SDL=1, 见 lv_conf.h) — 交互模拟器必需; 无 SDL2 时跳过 UI 部分
find_package(SDL2 QUIET)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lvgl AND SDL2_FOUND)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lvgl lvgl_build)
    target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
    # 显式指定 conf (绕开 __has_include 装载链 — 见 lv_conf_internal.h 内 GCC bug 注释)
    # LV_USE_SDL=1 双保险: conf 装载失败时内部 #ifndef 默认 0 会吞掉 SDL 驱动, 命令行定义兜底
    set(PC_SIM_LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")
    target_compile_definitions(lvgl PUBLIC
        LV_CONF_PATH="${PC_SIM_LV_CONF_PATH}"
        LV_USE_SDL=1
        LV_CONF_SUPPRESS_DEFINE_CHECK=1)
    target_link_libraries(pc_sim_tests PRIVATE lvgl SDL2::SDL2)
    target_compile_definitions(pc_sim_tests PRIVATE PC_SIM_HAS_LVGL=1)
    message(STATUS "pc_sim: LVGL host build ENABLED (SDL2, headless UI tests active)")

    # 交互式模拟器 (用户亲手测试入口): run_simulator.sh
    file(GLOB SIM_APP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/simulator/*.c)
    add_executable(pc_sim_app ${SIM_APP_SRCS} ${SIM_STUB_SRCS})
    target_include_directories(pc_sim_app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs
        ${REPO_ROOT}/components/app_file_manager/core
        ${REPO_ROOT}/components/app_file_manager/ui
        ${REPO_ROOT}/components/app_hackerkit/core
        ${REPO_ROOT}/components/app_hackerkit/ui
        ${REPO_ROOT}/components/app_security_key/core
        ${REPO_ROOT}/components/app_security_key/ui
        ${REPO_ROOT}/components/usb_arbiter
        ${SDL2_INCLUDE_DIRS})
    # APP 纯逻辑源码与测试同源 (core/ + ui/)
    foreach(app ${APP_ROOTS})
        if(EXISTS ${app})
            file(GLOB_RECURSE APP_UI_SRCS ${app}/core/*.c ${app}/ui/*.c)
            target_sources(pc_sim_app PRIVATE ${APP_UI_SRCS})
        endif()
    endforeach()
    target_link_libraries(pc_sim_app PRIVATE lvgl SDL2::SDL2)
else()
    message(STATUS "pc_sim: LVGL/SDL2 not available — UI tests & interactive simulator skipped (logic tests still run)")
endif()

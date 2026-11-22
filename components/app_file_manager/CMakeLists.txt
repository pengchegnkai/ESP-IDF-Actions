# app_file_manager — 文件与 Payload 管理 APP (固件构建)
#
# core/  纯逻辑 (可仿真): fm_backend 接口 + payload 校验 + 文本分页
# ui/    纯 LVGL: fm_ui (目录浏览器/分页预览/编辑对话框)
# hal/   [硬件相关·需真机验证]: hal_sdcard (board_sdcard_mount + IDF VFS)
# 根目录: Brookesia App 子类注册
#
# pc_sim 构建见 pc_sim/CMakeLists.txt (只编 core + ui).
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(GLOB_OPTIONS CONFIGURE_DEPENDS)
endif()
file(GLOB_RECURSE SRCS_CORE ${GLOB_OPTIONS} "${CMAKE_CURRENT_LIST_DIR}/core/*.c")
file(GLOB_RECURSE SRCS_UI ${GLOB_OPTIONS} "${CMAKE_CURRENT_LIST_DIR}/ui/*.c")
file(GLOB_RECURSE SRCS_HAL ${GLOB_OPTIONS} "${CMAKE_CURRENT_LIST_DIR}/hal/*.c")
file(GLOB_RECURSE SRCS_ASSETS ${GLOB_OPTIONS} "${CMAKE_CURRENT_LIST_DIR}/assets/*.c")

# WHOLE_ARCHIVE keeps the static app registrar that main installs from the registry.
idf_component_register(
    SRCS
        ${SRCS_CORE}
        ${SRCS_UI}
        ${SRCS_HAL}
        ${SRCS_ASSETS}
        "app_file_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        board
        brookesia_core
    WHOLE_ARCHIVE
)

set_source_files_properties("app_file_manager.cpp" PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers")

# App_HackerKit (精简版 BadUSB) — Brookesia APP 组件
#
# 固件构建: core + ui + hal + 根 .cpp; pc_sim 只编 core+ui (见 pc_sim/CMakeLists.txt).
# CONFIGURE_DEPENDS 使新增源文件免手动 reconfigure (early expansion 时不允许).
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(GLOB_OPTIONS CONFIGURE_DEPENDS)
endif()
file(GLOB_RECURSE SRCS_C ${GLOB_OPTIONS} "${CMAKE_CURRENT_LIST_DIR}/*.c")
file(GLOB_RECURSE SRCS_CPP ${GLOB_OPTIONS} "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

# WHOLE_ARCHIVE 保留静态 app registrar (main 从 registry 安装)
idf_component_register(
    SRCS ${SRCS_C} ${SRCS_CPP}
    INCLUDE_DIRS "." "core" "ui" "hal"
    REQUIRES
        usb_arbiter
        brookesia_core
        esp_tinyusb
        app_shared
    WHOLE_ARCHIVE
)

set_source_files_properties(${SRCS_CPP} PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers")

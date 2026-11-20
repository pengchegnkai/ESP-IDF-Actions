idf_component_register(
    SRCS "src/sys_ui.c" "src/sys_ui_font.c" "src/sys_ui_mem.c"
         "src/sys_ui_gesture.c" "src/sys_ui_quickpanel.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_hal sys_net lvgl heap
    # sys_ui_mem.c 提供 LVGL 自定义分配器 _core 符号, 由 lvgl 库反向引用;
    # 静态库按需提取不会自动拉入, 必须整库入链
    WHOLE_ARCHIVE
)

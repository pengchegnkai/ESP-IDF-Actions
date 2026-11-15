idf_component_register(
    SRCS "src/sys_ui.c" "src/sys_ui_font.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_hal lvgl heap
)

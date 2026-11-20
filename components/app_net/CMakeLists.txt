idf_component_register(
    SRCS "app_net.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_ui sys_net sys_hal lvgl
)

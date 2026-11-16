idf_component_register(
    SRCS "app_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_ui lvgl sys_hal heap
)

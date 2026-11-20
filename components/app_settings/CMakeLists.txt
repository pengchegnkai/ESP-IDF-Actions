idf_component_register(
    SRCS "app_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_ui sys_hal app_watch lvgl heap
)

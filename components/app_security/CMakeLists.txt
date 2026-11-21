idf_component_register(
    SRCS "app_security.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_ui sys_hal secure_link app_watch lvgl
)

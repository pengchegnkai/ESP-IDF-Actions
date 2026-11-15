idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash sys_core sys_hal sys_ui app_launcher
)

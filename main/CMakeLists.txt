idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash sys_core sys_hal sys_ui sys_net
             app_splash app_launcher app_settings app_watch app_net
)

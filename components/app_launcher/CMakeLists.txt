idf_component_register(
    SRCS "launcher_app.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_ui app_settings app_splash app_net app_watch lvgl
)

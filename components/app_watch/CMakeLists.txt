idf_component_register(
    SRCS "watch_app.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_core sys_ui sys_net app_launcher lvgl
)

idf_component_register(
    SRCS "src/sys_event_bus.c" "src/sys_app_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event
)

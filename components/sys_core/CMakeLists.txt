idf_component_register(
    SRCS "src/sys_event_bus.c" "src/sys_app_manager.c" "src/sys_time_service.c"
        "src/sys_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_timer nvs_flash freertos
)

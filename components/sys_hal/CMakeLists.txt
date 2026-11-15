idf_component_register(
    SRCS "src/sys_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos driver
)

# P4 目标额外依赖微雪官方 BSP 与 codec 驱动 (其他芯片目标不带这些依赖)
if(CONFIG_IDF_TARGET_ESP32P4)
    idf_component_optional_requires(PRIVATE
        waveshare__esp32_p4_wifi6_touch_lcd_4_3
        espressif__esp_codec_dev)
endif()

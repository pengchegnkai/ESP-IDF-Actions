# sys_net —— 网络子系统 (WiFi / ESP-NOW / NTP)
#
# P4 目标: WiFi 由板载 ESP32-C6 协处理器经 ESP-Hosted (SDIO) 提供,
#          P4 端直接用标准 esp_wifi API (esp_wifi_remote 组件),
#          与微雪官方 04_wifistation 示例同款依赖 (见该示例 idf_component.yml)。
# S3/C6 独立目标: 无协处理器, 全部接口返回 ESP_ERR_NOT_SUPPORTED (stub),
#                 保证 CI 三芯片均可编译。
set(sys_net_reqs sys_core)
# P4 才有协处理器链路; S3/C6 独立目标走 stub, 不拉 WiFi 组件
if(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND sys_net_reqs
        esp_wifi esp_netif nvs_flash lwip esp_wifi_remote esp_hosted driver)
endif()

idf_component_register(
    SRCS "src/sys_net.c" "src/sys_net_espnow.c" "src/sys_net_time.c"
    INCLUDE_DIRS "include"
    REQUIRES ${sys_net_reqs}
)

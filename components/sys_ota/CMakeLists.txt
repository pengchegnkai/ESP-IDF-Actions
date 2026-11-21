# sys_ota —— 网络 OTA (GitHub Release → 下载 → 校验 → 安装 → 重启)
#
# P4: 完整实现 (WiFi 由 sys_net / esp_wifi_remote 提供, 本组件只做 HTTP + OTA 写入)
# S3/C6: stub (返回 NOT_SUPPORTED), 保证 CI 三芯片均可编译
set(sys_ota_reqs
    sys_core
    esp_http_client
    espressif__cjson
    app_update
    esp_app_format
    esp_partition
    # IDF v6.1: esp_crt_bundle 已并入 mbedtls 组件 (esp_crt_bundle.h /
    # esp_crt_bundle_attach 均由 mbedtls 提供, 独立组件名不再存在)
    mbedtls
    lwip
)

idf_component_register(
    SRCS "src/sys_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES ${sys_ota_reqs}
)

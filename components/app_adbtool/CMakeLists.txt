# app_adbtool — USB Host ADB 客户端组件 ([硬件相关·需真机验证])
# 手机 (device) ←OTG→ P4 (host): CNXN/AUTH(RSA)/shell, 密钥存 NVS.
idf_component_register(
    SRCS
        "core/adb_client.c"
        "core/adb_proto.c"
        "core/adb_rsa.c"
        "ui/adb_ui.c"
        "assets/img_app_adbtool.c"
        "app_adbtool.cpp"
    INCLUDE_DIRS "core" "ui"
    REQUIRES mbedtls nvs_flash usb freertos brookesia_core app_shared
    WHOLE_ARCHIVE
)

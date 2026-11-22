# components/app_security_key — FIDO2 安全密钥 APP (固件构建)
# pc_sim 只编 core/+ui/ (见 pc_sim/run_tests.sh), hal/ 只进本构建.
#
# 红线②: 纯软件安全层, 无 eFuse 相关源 (全仓库不出现任何 efuse 烧写调用).
idf_component_register(
    SRCS
        "core/ctap2.c"
        "core/sk_session.c"
        "ui/sk_popup.c"
        "hal/hal_c6_crypto.c"
        "hal/hal_usb_ctap.c"
        "assets/img_app_security_key.c"
        "app_security_key.cpp"
    INCLUDE_DIRS "." "core" "ui" "hal"
    REQUIRES
        usb_arbiter        # 红线③: USB 互斥
        board              # C6 协处理器信息 (board_wifi)
        brookesia_core     # Brookesia APP 注册 / LvLockGuard
        mbedtls            # AES-GCM (P4↔C6 通道加密; 密钥运算本体在 C6)
        esp_tinyusb        # FIDO2 CTAP-HID (attach/detach 驱动装卸)
        app_shared         # 深色主题 (ui_theme.h)
    WHOLE_ARCHIVE          # 静态 app registrar (main 从 registry 安装)
)

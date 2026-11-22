idf_component_register(
    SRCS "c6_rf_test.c"
    INCLUDE_DIRS "include"
    # [硬件相关·需真机验证] 依赖 esp-hosted 代理的 esp_wifi 与 (可选) BT host 栈。
    # esp_coex 头文件随 esp_wifi 传入; bt 组件在 P4 上以 controller-disabled 档位参与编译。
    REQUIRES esp_wifi nvs_flash bt
)

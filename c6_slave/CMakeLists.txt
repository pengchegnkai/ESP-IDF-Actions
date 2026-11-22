# C6 从机侧签名服务 — 独立工程
idf_component_register(SRCS "c6_slave_main.c" INCLUDE_DIRS "." REQUIRES esp_hosted nvs_flash mbedtls)

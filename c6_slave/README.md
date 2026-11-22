# C6 从机签名服务 (esp_hosted slave)

P4 侧 `components/app_security_key/hal/hal_c6_crypto.c` 的对偶端。
私钥只在 C6 侧生成并加密存 NVS，绝不出 C6（红线②）。

## 协议

- 传输：esp_hosted peer_data（SDIO，与 WiFi 共总线，msg_id 逻辑隔离）
- 配对：`SK_MSG_ID_HELLO (0x534B0003)` 明文交换 MAC → 双方各自
  `HKDF-SHA256(mac_p4 || mac_c6, "SK-C6-PSK-v1")` 派生 PSK
- 请求：`SK_MSG_ID_REQ (0x534B0001)`，帧 = `[magic 'SK'][seq 4B][nonce 12B][GCM(密文)+tag 16B]`
- 应答：`SK_MSG_ID_RSP (0x534B0002)`，同 seq、同长度签名（当前 HMAC-SHA256，ECDSA 联调后切换）
- msg_id 命名空间：`0x534B0000` 段归安全通道，`0xFFFFFFFF` 保留给 esp_hosted

## 编译烧录（C6 从机镜像）

```bash
# 1. 环境: ESP-IDF v5.5.x + esp_hosted slave SDK（从机端组件树）
# 2. target 选 ESP32-C6
idf.py set-target esp32c6
idf.py -D SK_C6_LINK_HOSTED2=1 build
idf.py -D SK_C6_LINK_HOSTED2=1 flash monitor
```

注意：C6 从机烧录需通过 P4↔C6 的 SDIO烧录窗口（基座 Kconfig 开启
`CONFIG_ESP_HOSTED_{...}` 从机烧录辅助）或直接接 C6 的 USB 口。
**P4 侧升级 esp_hosted 1.4.7 → 2.x 时需要同步重烧 C6 从机镜像**
（两端协议版本必须一致）。

## P4 侧启用

升级 esp_hosted 2.x 后，在 app_security_key 的 CMakeLists 或全局编译参数加：

```
-D SK_C6_LINK_HOSTED2=1
```

未启用该开关时（当前基座 1.4.7），P4 侧编译为"链路未就绪"，
sign 返回失败——逻辑不变，CI 门禁保持绿色。

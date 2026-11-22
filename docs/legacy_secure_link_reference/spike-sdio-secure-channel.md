# 阶段二 Spike 报告：P4↔C6 SDIO 安全通道可行性

日期: 2026-11-21 · 调研人: 协调者 (scout agent 因供应商错误中止后接管) · 结论: **可行**

## 目标
在 ESP32-P4 ↔ ESP32-C6 间建立私有安全通道 (AES-GCM/PSK)，承载 FIDO2 签名运算：
私钥存 C6 NVS，P4 经该通道下发签名请求、取回签名。本板 P4/C6 均无 BT 射频，
WiFi 走 ESP-Hosted/SDIO，故候选载体只有一个：**SDIO 上的 ESP-Hosted 用户数据通道**。

## 发现 (按编号, 附出处)

### 1. 物理链路 = 仅 SDIO (无独立 P4↔C6 SPI)
- 微雪出厂固件 (brookesia) 配置自述: "ESP32-P4 uses the on-board ESP32-C6 through
  ESP-Hosted over SDIO. Transport pins and reset defaults come from the matching
  esp_hosted 1.4.x P4 profile"，`CONFIG_SLAVE_IDF_TARGET_ESP32C6=y`
  (出处: ~/wifi_ex/brookesia_sdkconfig L104-106)
- P4 侧 SDIO 默认引脚 (esp_hosted P4 默认 profile, 不选 board preset 时生效):
  **CMD=19, CLK=18, D0-D3=14-17, reset=54**
  (出处: esp-hosted unified_release examples/peer_data_transfer/mcu_host/sdkconfig.defaults.esp32p4 注释)
- C6 侧 SDIO GPIO 芯片级固定 (不可配): CMD=18 CLK=19 D0=20 D1=21 D2=22 D3=23
  (出处: coprocessor/eh_cp_transport/Kconfig.cp.sdio)
- 含义: 安全通道与 WiFi 链路**共用同一物理总线** (逻辑通道隔离), 无第二条物理线可用。

### 2. esp_hosted 2.x 源码位置 (IDF 6.x 体系)
- GitHub espressif/esp-hosted 的 main/master 分支只剩文档 (32 条目);
  **2.x 源码在 `unified_release` 分支** (2848 条目), 新布局:
  `host/` + `coprocessor/` + `common/` + `examples/` (slave 更名 coprocessor)
- esp-idf v6.0/v6.1 仓库**不含** esp_hosted 组件 (components/esp_hosted → 404);
  组件经注册表分发 (本项目用 espressif/esp_hosted >=2.12,<3.0)
- 兼容层保留: host/compat/include/esp_hosted.h 仍提供 esp_hosted_init /
  esp_hosted_connect_to_slave / esp_hosted_tx 等旧 API (Kconfig: EH_USE_LEGACY_API),
  原生新 API 为 `eh_host_*` 系列
- 注意: 构建自定义 C6 镜像需本地源码 (示例用 `override_path` 指向 esp_hosted 仓库),
  注册表 2.x 具体 tag 与 unified_release 分支 commit 的对应关系需在实际构建时锁定

### 3. 用户自定义数据通道: peer_data 特性 (即所需机制, 官方一等公民)
通用 msg_id 数据收发, 非 WiFi/BT 专属:
- **P4 (host) 侧 API** (host/features/eh_host_feat_peer_data/include/eh_host_feat_peer_data.h):
  - `esp_err_t eh_host_peer_data_send(uint32_t msg_id, const uint8_t *data, size_t size);`
  - `esp_err_t eh_host_peer_data_register(uint32_t msg_id, void (*cb)(uint32_t msg_id, const uint8_t *data, size_t len, void *user), void *user);`
  - msg_id: 任意 uint32_t (0xFFFFFFFF 除外); **单包载荷上限 ~8166 字节** (官方示例实测值)
  - Kconfig: `CONFIG_ESP_HOSTED_HOST_FEAT_PEER_DATA=y` (依赖 RPC_EXT_V2);
    `ESP_HOSTED_HOST_FEAT_PEER_DATA_AUTO_INIT` 默认 y (启动自动 init);
    `ESP_HOSTED_HOST_FEAT_PEER_DATA_MAX_CUSTOM_MSG_HANDLERS` 默认 3 (range 1-32, 按需调大)
- **C6 (coprocessor) 侧 API** (coprocessor/features/eh_cp_feat_peer_data/include/eh_cp_feat_peer_data.h):
  - `esp_err_t eh_cp_feat_peer_data_init(void);` (register/send 前必须调用一次)
  - `esp_err_t eh_cp_feat_peer_data_register_callback(uint32_t msg_id_exp, void (*cb)(uint32_t, const uint8_t *, size_t, void *), void *local_context);`
  - `esp_err_t eh_cp_feat_peer_data_send(uint32_t msg_id, const uint8_t *data, size_t len);`
  - 入站消息由 RPC 适配层 `eh_cp_feat_peer_data_dispatch()` 派发 (RPC RX 上下文, 回调须快)
  - Kconfig: `CONFIG_ESP_HOSTED_CP_FEAT_PEER_DATA=y`
- **P4 host 侧配套配置** (peer_data 示例 sdkconfig.defaults.esp32p4):
  `CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6=y`, `CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_SDIO=y`,
  `CONFIG_ESP_HOSTED_SDIO_SLOT_1=y`, `CONFIG_ESP_HOSTED_HOST_SDIO_BUS_WIDTH_4=y`,
  `CONFIG_ESP_HOSTED_HOST_SDIO_RX_STREAMING_MODE=y`
- 官方完整示例: examples/peer_data_transfer (host + cp 双工程, 含 P4/C6 配置)

### 4. C6 镜像必须重编 (出厂 stock 镜像无我们的 handler)
- 模板: examples/peer_data_transfer/cp/ — 标准 `idf.py` 工程, target=esp32c6,
  esp_hosted 经 idf_component.yml `override_path` 引用本地源码,
  公共配置 (cp/sdkconfig.defaults): `CONFIG_ESP_HOSTED=y`, `CONFIG_ESP_HOSTED_CP=y`,
  `CONFIG_ESP_HOSTED_CP_FOR_MCU=y`, `CONFIG_ESP_HOSTED_CP_RPC_V2=y`,
  分区表 `partitions_eh_cp_ota_4m.csv` (含 CP 侧 OTA 分区)
- **C6 侧 NVS 可用**: hci_demo 的 CP 工程 app_main 直接 `nvs_flash_init()` 且正常
  → FIDO2 私钥可存 C6 NVS (红线: 物理 dump 仍可读, 定位"离线防盗令牌"不变)
- 安全通道 v1 的 C6 镜像 = hosted CP 核心 (WiFi 继续托管) + peer_data handler
  + AES-GCM/PSK 加解密 + NVS 密钥存储

### 5. C6 镜像的烧录与后续升级
- **首次**需物理烧录路径: 板上有无可达的 C6 UART/测试点需查微雪文档/原理图
  (CH343P 桥的是 P4 的 UART, C6 无独立 USB) → **本 spike 唯一未闭环项, 阶段二第一门禁**
- **后续升级免硬件**: examples/ota/coprocessor_ota 提供 host 经 SDIO 更新 C6 固件的
  三种方式 (HTTPS / LittleFS / Partition), C6 分区表自带 ota 分区 →
  首次烧入后, C6 镜像可随 P4 固件一起 OTA 演进
- 版本兼容: host/cp 版本主.次需匹配 (coprocessor_ota 的 VERSION_CHECK 选项),
  我方自管两端版本, 可控

### 6. BT 自定义路径 (旁证, 不用于本方案)
2.x 另提供 raw HCI 字节管道 (`esp_hosted_bt_host_stack_setup()` +
`ESP_HOSTED_BT_HOST_STACK_CONFIG_CUSTOM(rx, ctx)`), 证明 "custom" 扩展机制成熟;
与 FIDO2 无关 (本板无 BT 射频), 仅记录。

## 结论
**可行, 且是官方一等公民机制** — 无需 hack:
1. P4 侧: secure_link 组件 = peer_data 封装 (msg_id 规划) + AES-GCM/PSK 加密层
   + 请求/响应队列; Kconfig 开 HOST_FEAT_PEER_DATA
2. C6 侧: 重编镜像 = hosted CP 核心 + peer_data handler + 加密 + NVS 密钥;
   模板直接用官方 peer_data 示例 cp 工程
3. 载荷余量: FIDO2 CTAP2 帧经 HID 64B 报告拆分, 单包远小于 8166B 上限
4. 总线共用 WiFi: 逻辑通道隔离 (独立 msg_id 命名空间), 带宽上签名请求频率极低, 无影响

## 工作量估计 (v1)
| 项 | 估计 |
|---|---|
| C6 镜像 (模板 + 加密 + NVS + 构建流程) | 1-2 天 |
| P4 侧 secure_link (peer_data 封装 + AES-GCM + 队列) | 1 天 |
| 本地联调 (esp_hosted 源码本地化 + 双端构建 + 回环验证) | 0.5-1 天 |
| 首次 C6 物理烧录路径打通 | 视板卡测试点, 0.5-2 天 (风险项) |

## 风险清单
| # | 风险 | 缓解 |
|---|---|---|
| R1 | C6 首次物理烧录路径未知 (无独立 USB/UART 引出?) | 阶段二第一门禁: 查微雪原理图/文档; 兜底 = 自制 JTAG/UART 小夹子 |
| R2 | esp_hosted 源码版本与注册表 2.x tag 对应关系 | 构建 C6 镜像时锁定同一 commit 双端使用; override_path 保证一致 |
| R3 | PSK 预置方式未定 (写死固件 = 可提取) | 阶段二设计: PSK 派生自出厂唯一 ID (eFuse MAC, 只读不熔断) + 固定盐; 定位仍是防盗令牌 |
| R4 | SDIO 物理旁路 (测试点监听) | 通道本身无加密 (WiFi 帧明文), 我方 peer_data 载荷整体 AES-GCM; PSK 不出两端 |
| R5 | peer_data 默认最多 3 个 handler | Kconfig 调 MAX_CUSTOM_MSG_HANDLERS (1-32), 我方 ≤3 个 msg_id 够用 |

## 下一步 (阶段二启动门禁, 按序)
1. 查微雪板卡文档/原理图确认 C6 物理烧录路径 (R1, 阻塞项)
2. 本地拉 esp_hosted unified_release 源码, 构建 peer_data 示例 cp 镜像 (C6) +
   改 P4 host 配置开 FEAT_PEER_DATA, 双端回环收发验证 (spike 原型)
3. 原型通过后: 定 msg_id 规划与帧格式 (magic + 序号 + AES-GCM(nonce,ct,tag)),
   开写 secure_link 真实现 (替换 stub) 与 C6 镜像

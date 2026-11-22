# C6 全功能共存配置指南 — Wi-Fi 6 / BLE 5 / 802.15.4 (零 eFuse · 零烧录)

> 适用板卡: 微雪 ESP32-P4-WIFI6-Touch-LCD-4.3B (P4 主控 + C6 协处理器, SDIO 互联)
> 基座: Brookesia 工厂固件 (esp_hosted ^1.4.7)
> 红线: 开发阶段零烧录、绝对禁止熔断 eFuse — 全部安全能力为纯软件层。
> 依据: esp-hosted 官方文档/示例 (components.espressif.com, esp-hosted-mcu),
>       IDF RF 共存编程指南 (esp32c6), Espressif 官方回复 IDFGH-18546 (2026-11)。

---

## 0. 结论速览与可行性矩阵

C6 单颗 2.4 GHz 射频 (SOC_PHY_COMBO_MODULE=1: Wi-Fi/BLE/802.15.4 共用一套 RF),
硬件层带 PTI 共存仲裁 (SOC_COEX_HW_PTI=1)。三协议在本板上的可行性:

| 能力 | 可行性 | 前提 | 说明 |
|---|---|---|---|
| Wi-Fi (含 Wi-Fi 6 / 11ax) | ✅ 出厂即用 | 无 (现状) | esp_hosted 1.4.7 SDIO; 11ax 由 C6 SoC 能力 (SOC_WIFI_HE_SUPPORT) 默认包含, 无需专门开关 |
| BLE 5 (控制器在 C6, host 栈在 P4) | ✅ 可行 | esp_hosted 升 2.x+/3.x + **C6 从机固件重建** (烧录窗口执行) | BT 控制器跑在 C6, 经 VHCI 走 SDIO; host 栈 (NimBLE/Bluedroid) 跑在 P4 |
| 802.15.4 (官方 RCP 模式) | ❌ 本板不可行 | 需 P4↔C6 **专用 UART** (spinel 数据面) | over-hosted-transport 选项存在但官方未实现; 本板 P4↔C6 仅 SDIO, 无 UART 布线 |
| 802.15.4 (自定义 CP 服务) | ⚠️ 自研路线 | C6 从机固件加入 ieee802154 驱动 + esp-hosted 自定义帧代理 | 官方无现成路径, 框架已在 §4 预留 (TODO) |

**三协议同时收发的官方结论** (Espressif, IDFGH-18546, 2026-11):
> "Wi-Fi, 15.4, BLE 三栈同时收发目前不被支持; 同时启用三栈共存可能引入未知风险。
> 已验证的典型用法是: 三栈都在固件里, 但同一时间只启用两个 — 例如 BLE 配网后关闭 BLE,
> Wi-Fi + 15.4 共存正常工作。"

因此本指南的验证目标设定为: **三栈共存于固件 + 两两组合轮换验证** (官方已验证模式),
测试框架见 §4。Wi-Fi↔BLE 共存矩阵 (扫描/连接/广播各组合) 官方全绿;
Wi-Fi↔15.4 矩阵中 SOFTAP+Router、Sniffer+Router 为不支持 (X), 其余 Y/C1。

**版本代际注意**: 基座锁定 esp_hosted ^1.4.7 (Wi-Fi-only 代际, Kconfig 为
`ESP_HOST_*` 前缀)。§1/§2 的 BLE/15.4 Kconfig 名称来自 esp_hosted 2.x/3.x
(`ESP_HOSTED_CP_FEAT_BT` 等) — **升级 esp_hosted 是启用 BLE 的前置工程**,
host 与 CP 两侧配置必须同代际匹配 (传输类型/GPIO 极性不匹配会直接失联)。

---

## 1. C6 从机端 (coprocessor) menuconfig 清单

### 1.1 现状 (esp_hosted 1.4.7, 出厂 C6 固件) — 无需改动

出厂 C6 固件 = esp_hosted 1.4.x 从机镜像: SDIO 传输 + Wi-Fi 功能已配好。
开发阶段**不动 C6 flash** (零烧录红线)。Wi-Fi 6 能力由 C6 SoC 默认提供。

### 1.2 升级后从机重建 (esp_hosted 2.x/3.x, 在烧录窗口执行)

从机工程 = esp_hosted 组件自带 slave example (`idf.py set-target esp32c6`),
menuconfig 关键项:

```
# ── 传输 (必须与 P4 host 侧同代际匹配) ──
CONFIG_ESP_HOSTED_CP_TRANSPORT_SDIO=y        # CP transport → SDIO (2.x 菜单名)
                                             # SDIO clock/GPIO/checksum 用板级默认 (与 Waveshare 布线一致)

# ── Wi-Fi (含 Wi-Fi 6) ──
CONFIG_ESP_HOSTED_CP_WIFI=y                  # 默认 y
CONFIG_ESP_WIFI_ENABLED=y                    # esp_wifi 组件默认
# Wi-Fi 6: 无专门 Kconfig — C6 的 SOC_WIFI_HE_SUPPORT 使 2.4G 默认协议含 11AX。
# 运行时限定/确认协议位图 (host 侧 P4 调):
#   esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|
#                                       WIFI_PROTOCOL_11N|WIFI_PROTOCOL_11AX);
# 可选漫游特性: CONFIG_ESP_WIFI_11KV_SUPPORT / CONFIG_ESP_WIFI_11R_SUPPORT

# ── BLE 5 (BT 控制器在 C6, host 栈在 P4) ──
CONFIG_ESP_HOSTED_CP_FEAT_BT=y               # ESP-Hosted BT controller 功能
CONFIG_ESP_HOSTED_CP_BT_ENABLED=y            # 上项的依赖 (Kconfig 缺它会告警)
CONFIG_ESP_HOSTED_CP_FEAT_BT_HCI_VHCI=y      # BT HCI 走 host 总线 (VHCI over SDIO)
CONFIG_BT_ENABLED=y                          # C6 侧 BT 控制器栈
CONFIG_BT_CONTROLLER_ONLY=y                  # controller-only 档位 (host 栈在 P4)
# BLE 5 特性 (BLE 5.0 广播扩展/2M PHY/长包) 属于 host 栈配置, 见 §2

# ── 802.15.4 (官方 RCP 模式 — 本板无 UART 布线, 不适用, 列出备查) ──
CONFIG_ESP_HOSTED_CP_FEAT_OPENTHREAD=y       # RCP 生命周期/控制走 hosted RPC
CONFIG_ESP_HOSTED_OT_TRANSPORT_UART=y        # spinel 数据面 = 专用 UART (官方唯一支持路径)
CONFIG_ESP_HOSTED_OT_UART_PORT=1             # UART 端口
CONFIG_ESP_HOSTED_OT_UART_PIN_TX=21          # TX GPIO
CONFIG_ESP_HOSTED_OT_UART_PIN_RX=20          # RX GPIO
CONFIG_ESP_HOSTED_OT_UART_BAUDRATE=460800
CONFIG_OPENTHREAD_ENABLED=y                  # IDF OpenThread
CONFIG_OPENTHREAD_RADIO=y                    # radio-only 设备类型 (RCP)

# ── 802.15.4 (自定义 CP 服务路线 — 本板推荐) ──
CONFIG_IEEE802154_ENABLED=y                  # ieee802154 组件, C6 默认 y (SOC_IEEE802154_SUPPORTED)
CONFIG_IEEE802154_RX_BUFFER_SIZE=20          # 收包缓冲, 默认 20 (范围 2..100)
CONFIG_IEEE802154_CCA_MODE=1                 # CCA: 0=carrier 1=ED(默认,-75dBm) 2=carrier|ED 3=carrier&ED
CONFIG_IEEE802154_CCA_THRESHOLD=-75          # dBm
# 帧经 esp-hosted 自定义服务代理到 P4 (自研, §4 框架预留 TODO)

# ── RF 共存 ──
CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y          # 软件共存总开关 (不开则共存机制整体无效)
CONFIG_BT_LE_COEX_PHY_CODED_TX_RX_TLIM=y     # BLE Coded PHY 收发时限, 防长包挤压 Wi-Fi
CONFIG_BT_LE_COEX_PHY_CODED_TX_RX_TLIM_EN=y
```

### 1.3 eFuse 红线清单 (绝对禁止开启)

| 配置 | menuconfig 路径 | 触发后果 |
|---|---|---|
| `CONFIG_SECURE_BOOT_V2_ENABLED` | Security → Secure Boot V2 | 烧写 secure boot eFuse (一次性、不可逆) |
| `CONFIG_SECURE_FLASH_ENCRYPTION_ENABLED` + **Release Mode** 子项 | Security → Flash Encryption → Release Mode | 烧写 flash encryption eFuse |
| 以上两者的全部子选项 (`SECURE_BOOT_V2_PUBKEY_*`、`SECURE_BOOT_V2_RSA_*` 等) | 同上 | 同上 |
| 运行期 `esp_efuse_write*()` 调用 (含自定义 MAC 写入) | — | 写 eFuse 位 |

补充说明:
- Flash Encryption **Development 模式**不熔 eFuse (密钥走软件), 但按"纯软件加密、
  零 eFuse"红线, Security 菜单整体保持关闭最稳。
- **读** eFuse (出厂 MAC、chip revision) 不违反红线 — 红线禁止的是"写"。
- 本指南全程不执行任何烧录: C6 从机固件的重建/升级统一推迟到用户许可的烧录窗口。

---

## 2. P4 主机端 menuconfig (esp_hosted 2.x+, 烧录窗口内与 C6 从机同步升级)

```
# ── ESP-Hosted host 侧 ──
CONFIG_ESP_HOSTED_HOST_FEAT_BT=y             # host BT 功能 (控制器在 C6)
CONFIG_ESP_HOSTED_HOST_FEAT_WIFI=y           # 默认 y

# ── BT host 栈 (二选一; 推荐 NimBLE — 体积小, BLE-only 够用) ──
CONFIG_BT_ENABLED=y
CONFIG_BT_CONTROLLER_DISABLED=y              # P4 无本地 BT 控制器 — 由 C6 经 VHCI 提供
CONFIG_BT_NIMBLE_ENABLED=y                   # 方案 A: NimBLE (推荐)
# 或 CONFIG_BT_BLUEDROID_ENABLED=y           # 方案 B: Bluedroid (经典 BT API 风格)

# ── BLE 5 特性 (host 栈能力) ──
CONFIG_BT_BLE_50_FEATURES_SUPPORTED=y        # BLE 5.0: 扩展广播 / 2M PHY / DTM
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3           # 按需
```

纯 Wi-Fi 6 路线 (现状 1.4.7) 无需任何上述配置 — 出厂已可用。

---

## 3. 不依赖 eFuse 的安全通信 (P4 ↔ C6)

### 3.1 密钥协商 — 双层设计 (纯软件, 零 eFuse)

**第 1 层: 出厂 PSK (bootstrap, 立即可用)**
- 双侧镜像烧入同一把 32 字节静态 PSK (编译期常量/secure 配置头文件);
- legacy 分支 `secure_link` 组件即此方案 (见 `docs/legacy_secure_link_reference/`,
  470 行 `app_security.c` = SDIO peer_data + AES-GCM/PSK 完整实现, 直接移植)。

**第 2 层 (可选增强): ECDH 一次性配对 → 每次启动派生会话密钥**
1. C6 首次启动用 mbedTLS 生成 ECC P-256 密钥对 (纯软件计算, 不碰 eFuse);
2. 公钥经配对信道分发到 P4: USB 串口 (出厂配对) 或 屏幕二维码 (用户确认式);
3. 双方 ECDH → 共享秘密 → KDF → 128/256 位 AES-GCM 主密钥;
4. 主密钥各自存入**加密 NVS 分区** (mbedTLS 软件密钥, 无 eFuse);
5. 每次启动: 双方从加密 NVS 取主密钥 → 随机数握手 → 会话密钥 (仅存 RAM, 掉电即失)
   → 前向保密。

### 3.2 密钥存储 (无 eFuse 下的诚实边界)

| 资产 | 位置 | 保护 |
|---|---|---|
| PSK / ECDH 主密钥 | 双侧加密 NVS 分区 | mbedTLS AES 软件加密 — 防分区文件窃取 |
| FIDO2 签名私钥 | **C6** 加密 NVS | P4 无法经 SDIO 总线直读 C6 flash — 私钥不出 C6 |
| 会话密钥 | 双侧 RAM | 掉电即失 |

已知边界: 无 eFuse 硬件根密钥时, "拆芯片直接读 flash" 的防护弱于硬件方案 —
这是零 eFuse 红线下的产品级权衡 (规格书已确认接受: 私钥加密存储于 C6)。

### 3.3 信道与帧格式 (AES-GCM over SDIO)

- 传输: esp-hosted SDIO peer_data (自定义帧通道, 1.4.7 已具备);
- 加密: mbedTLS AES-256-GCM (P4 与 C6 均有 mbedTLS; C6 硬件 AES 外设同样不依赖 eFuse);
- 帧: `{ cmd(1B) | len(2B) | nonce(12B: 4B 计数器 + 8B 随机) | ciphertext | tag(16B) }`;
- nonce 计数器单调递增, 随机段防重启碰撞 — 同一 key 下 nonce 绝不复用。

---

## 4. P4 测试代码框架 (components/c6_rf_test/)

已落地组件 (编译门禁, 默认关闭零影响):

```
components/c6_rf_test/
├── include/c6_rf_test.h    # API + 统计结构 + 两两组合枚举
├── c6_rf_test.c            # 实现: CONFIG_C6_RF_TEST_ENABLED=n 时退化为桩
├── Kconfig                 # C6_RF_TEST_ENABLED / C6_RF_TEST_ENABLE_BLE (均默认 n)
└── CMakeLists.txt
```

用例框架 (真机验证时按序执行):

```c
#include "c6_rf_test.h"

void run_c6_rf_verification(void)
{
    c6_rf_test_init();

    /* ① Wi-Fi 扫描 — 经 esp-hosted 在 C6 上执行 (现状即可用) */
    c6_rf_test_wifi_scan(5000);

    /* ② BLE 扫描 — BT 控制器在 C6 (VHCI), 需烧录窗口内的 esp_hosted 2.x 升级 */
    c6_rf_test_ble_scan_start(10000);

    /* ③ 两两共存轮换 (官方已验证模式): 每组合观察 10s */
    c6_rf_test_coex_pairwise(C6_RF_PAIR_WIFI_BLE,  10000);  /* 现状: 1.4.7 无 BLE → 桩 */
    c6_rf_test_coex_pairwise(C6_RF_PAIR_WIFI_154,  10000);  /* 需自定义 CP 服务 → NOT_SUPPORTED */
    c6_rf_test_coex_pairwise(C6_RF_PAIR_BLE_154,   10000);  /* 同上 */

    /* ④ 统计: wifi_scan_done / ble_scan_reports / frames_154 / active_pair */
    const c6_rf_test_stats_t *s = c6_rf_test_stats_get();
    ESP_LOGI(TAG, "final: wifi=%u ble=%u", s->wifi_scan_done, s->ble_scan_reports);
}
```

802.15.4 自定义 CP 服务 (第 3 条路, 自研) 在 `c6_rf_test.c` 中以 `TODO(自定义 CP 服务)`
标注: C6 从机侧 ieee802154 驱动收发 + esp-hosted 自定义帧双向代理, 落地后
`C6_RF_PAIR_WIFI_154` / `C6_RF_PAIR_BLE_154` 两个用例自动激活。

---

## 5. 阶段路线图 (对齐"模拟先行 + 零烧录"总策略)

| 阶段 | 内容 | 前提 | 烧录? |
|---|---|---|---|
| A (现在) | Wi-Fi 全能力 (含 11ax 协议位图确认) — 出厂 esp_hosted 1.4.7 | 无 | 否 |
| B (烧录窗口) | esp_hosted 升 2.x/3.x (host+CP 双侧同代际) + C6 从机重建 → BLE 5 全开 + BT 扫描用例激活 | 用户许可烧录 | 是 (C6 + P4) |
| C (自研) | 802.15.4 自定义 CP 服务 (ieee802154 on C6 + SDIO 帧代理) → PAIR_WIFI_154/PAIR_BLE_154 用例激活 | B | 是 |
| D (安全底座) | FIDO2 C6 签名通道: legacy secure_link (AES-GCM/PSK) 移植 + 可选 ECDH 配对 — 与 B 的 C6 固件同窗口烧录 | B | 是 |

每阶段统一走: pc_sim 纯逻辑仿真 → CI 编译门禁 → 用户确认 → 烧录 → 真机验证。

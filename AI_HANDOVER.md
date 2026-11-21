# AI 交接文档（AI Handover）

> 最后更新：2026-11-21
> 已验证固件：**build-59** —— 串口日志全流程通过（无崩溃、中文 UI、splash→表盘切换正常）。
> 待上机验证：**build-62**（字库内嵌 fonts 分区，CI 成功、资产已下载校验）+ **build-63**（Vela 表盘重做 + 天气/OTA/FIDO2 安全密钥 + 双 OTA 分区，CI 进行中）—— 等用户板卡 USB 重连后烧录。
>
> 本文档写给下一个 AI/开发者：包含项目目标、架构、已验证结论、踩坑根因链、
> 手机端工具链的确切用法、以及当前进行到一半的工作。不要依赖本文档之外的"记忆"。

---

## 1. 项目概览

- **目标**：在微雪 ESP32-P4-WIFI6-LCD-4.3 板卡上构建**通用系统基座**（APP 生命周期 + 事件总线 + 全局 UI + 硬件抽象层），后续往里填真实 APP。
- **仓库**：`https://github.com/pengchegnkai/ESP-IDF-Actions`（branch `main`），本地路径 `~/esp-idf-gh-actions-demo/`
- **编译**：GitHub Actions 云端编译（ESP-IDF v6.1），push 触发固定编 `esp32p4`，手动 dispatch 可选 `esp32p4`/`esp32c6`/`esp32s3`。每次成功构建自动发 Release，命名 `build-NN` 递增。
- **烧录**：手机（Android Termux）经 USB OTG 直连板卡烧录 + 抓串口。工具链在 `~/`（见 §6、§9）。
- **当前里程碑**：Vela 交互系统跑通（build-59 串口验证）：深空表盘 + 应用网格 + 快捷控制中心 + WiFi/ESP-NOW + 设置；字库已可内嵌固件（fonts 分区，不依赖 SD 卡）。产品路线为"个人离线智能安全终端"三阶段：① 系统基座+Vela 交互+中文字库 ✅（build-62 待上机）② P4↔C6 SDIO 安全通道（spike 完成：esp_hosted peer_data 可行，报告见 `docs/`）③ FIDO2 双芯安全（v1 骨架已实现，真签名待阶段二通道）。

## 2. 板卡规格（微雪 ESP32-P4-WIFI6-Touch-LCD-4.3B）

| 项目 | 值 |
|---|---|
| 主控 | ESP32-P4NRW32（**32MB PSRAM**，hex 200MHz；不开 PSRAM 会 `ESP_ERR_NO_MEM`） |
| 协处理器 | ESP32-C6-MINI（WiFi6） |
| 屏幕 | 4.3" 480×800 竖屏 MIPI-DSI 2-lane，驱动 ST7701 |
| 触摸 | GT911 电容触摸（I2C，走 BSP 总线） |
| 音频 | ES8311 扬声器 + ES7210 双麦（I2S/I2C） |
| 板载 I2C | SDA=GPIO7 SCL=GPIO8，**已被 BSP 占用**（GT911/ES8311/ES7210 共用） |
| SD 卡 | uSD 卡槽，原生 SDIO 4-bit，BSP 提供 `bsp_sdcard_mount()`，挂载点 `/sdcard`（`CONFIG_BSP_SD_MOUNT_POINT`） |
| USB | CH343P CDC-ACM（VID=1a86 PID=55d3），双接口 |

**官方参考仓库**（标准用法以此为准，用户要求"严格按官方文档写"）：
- 板级示例：`waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3`（`examples/esp-idf/08_lvgl_demo_v9/` 是 LVGL 标准参照；`05_sdmmc` 是 SD 卡示例）
- BSP 组件：`waveshareteam/Waveshare-ESP32-components`（本项目的 managed component `waveshare/esp32_p4_wifi6_touch_lcd_4_3 ^1.0.1`）
- 官方 LVGL 用法核心：`bsp_display_start_with_config(&cfg)` → `bsp_display_backlight_on()` → 建 UI 时 `bsp_display_lock(-1)`（**永久等待**，返回 `esp_err_t`，`ESP_OK`=成功）→ 用完 `bsp_display_unlock()`。渲染由 BSP 内置适配任务跑 `lv_timer_handler`。

## 3. 系统架构

```
esp-idf-gh-actions-demo/
├── components/
│   ├── sys_core/          # APP 生命周期 / 事件总线 (SYS_EVENT_*: USB_IN/OUT, WEATHER, OTA, SEC_ACTIVE, ...) / 时间服务
│   ├── sys_hal/           # P4=官方 BSP 直通 (display/touch/audio/i2c/sd) + usb (TinyUSB FIDO HID, 仅 P4); 其他芯片 stub
│   ├── sys_ui/            # LVGL v9 封装: 屏幕切换/overlay/手势框架/主题 (sys_ui_theme)/快捷面板 (sys_ui_quickpanel)/字库+i18n/安全认证弹窗 (sys_ui_secure_dialog)
│   ├── sys_net/           # WiFi (C6 协处理器) + 自动连接退避 + 天气服务 (sys_net_weather, wttr.in, 30min 周期)
│   ├── sys_ota/           # 网络 OTA: GitHub Release 下载 (SHA256) + 双 OTA 分区 + 30s 回滚保护 (仅 P4)
│   ├── secure_link/       # P4↔C6 安全通道接口 (v1 stub; 阶段二 = SDIO AES-GCM/PSK, 接口不变)
│   ├── app_splash/ app_launcher/ app_settings/ app_watch/ (Vela 深空大数字表盘) app_net/ (WiFi/ESP-NOW/天气/OTA 卡) app_security/ (FIDO2 手动激活)
├── main/app_main.c        # 启动 (顺序见下)
├── partitions_p4.csv      # 双 OTA: ota_0 2MB@0x10000 / fonts 18MB@0x210000 / ota_1 2MB@0x1410000 / ota_data@0x1610000 (仅 P4; S3/C6 用 partitions.csv)
├── .github/workflows/build.yml   # CI: push→esp32p4; 资产: merged-binary.bin + font.bin (TTF 来自仓库) + demo.bin (OTA 镜像)
├── docs/spike-sdio-secure-channel.md  # 阶段二 spike 报告 (esp_hosted peer_data over SDIO)
├── sdkconfig.defaults(.esp32p4)
└── idf_component.yml (组件级)
```

**启动顺序（app_main.c，有依赖不可乱）**：
1. NVS+settings → 2. 事件总线 + APP 调度器 → 3. 显示（`sys_ui_init`，先点亮屏幕盖白屏）→ 4. 时间服务 + 注册 6 APP（splash/launcher/settings/watch/net/security）→ 5. 进 splash → 6. touch/i2c + USB FIDO（P4，失败不致命）→ **6.5 SD 卡挂载（失败不致命）** → 7. 字库（SD→fonts 分区→英文 三级回退）→ 8. 音频/网络后台任务（net 任务内：WiFi→`sys_net_weather_start`→`sys_ota_init`→`secure_link_init`）→ 9. `splash_set_ready()` + `sys_ota_boot_check()`（双 OTA 回滚保护）。

**UI 线程模型（铁律）**：
- 所有 LVGL 对象操作必须在 `sys_ui_lock()/sys_ui_unlock()` 之间（P4 上即 BSP 锁，与适配任务的 `lv_timer_handler` 互斥）。
- `sys_ui_lock()` 返回 `bool`：**false = 没拿到锁，禁止执行任何 LVGL 操作**（调用方必须检查）。
- `sys_ui_set_root(screen)` 会把中文字体设到屏幕根对象（子控件继承；新屏幕不继承旧屏幕样式，所以每次 set_root 重设）。顶层弹窗挂 `lv_layer_top()`，不在屏幕树下，字体必须显式设置。

**APP 生命周期铁律**：
- `on_init` 只做内存分配，**不碰 LVGL**（注册阶段拿显示锁会与适配任务死锁）。
- UI 构建延迟到 `on_start` 首次进前台（`p->ui_built` 守卫）。

## 4. 已完成且已验证（截至 commit `a502b9e`）

- ✅ 完整项目骨架 + CI（push 编 esp32p4，dispatch 可选三芯片，自动 Release）
- ✅ 系统基座：APP 生命周期调度、事件总线、时间服务、全局 UI（屏幕切换 + 顶层弹窗 overlay）、sys_hal（BSP 直通）
- ✅ APP：launcher（桌面）、settings（真实功能）、splash（启动画面）
- ✅ **中文界面正常**（用户确认；NotoSansSC.ttf 在 SD 卡 fonts/ 目录）
- ✅ 设置页真实功能：亮度/音量滑块 + 按键音效开关 + 系统信息；NVS 持久化（write-through，namespace "sys"，key: bright/vol/sfx）
- ✅ 启动画面掩盖上电白屏：上电 ~1.9s 出 splash（此前 ~6.5s 白屏），字库加载期间持续渲染，加载完自动升中文，`splash_set_ready()` 后切桌面
-串口日志全流程验证通过（2026-11-19）：splash 1.86s 前台 → 字库 6.5s 加载完 → 7.1s 切 launcher，无 abort/assert/WDT
- ✅ **Vela 交互系统**（build-59，2026-11-20 串口验证）：深空表盘 + 应用网格 + 快捷控制中心（顶部边缘下拉）+ 手势框架，无崩溃、中文 UI 正常
- ✅ **字库内嵌固件**（build-62，代码+CI 完成，待上机）：`partitions_p4.csv` 新增 fonts 18MB 分区@0x210000，TTF 入仓库，CI 发 font.bin 资产，flasher 双镜像 + md5 缓存，`sys_ui_font` 三级回退 SD→分区→英文
- ✅ **网络/OTA/安全**（build-63，CI 进行中）：WiFi 自动连接退避（5 次）+ wttr.in 天气（30min + `SYS_EVENT_WEATHER`）+ OTA（GitHub Release，SHA256，双 OTA + 30s 回滚保护）+ FIDO2 安全密钥 v1（TinyUSB FIDO HID 0xF1D0/0x03、手动激活默认 OFF、CTAP2 GetInfo 自动应答 + MakeCredential/GetAssertion 认证弹窗）

## 5. 调试记录：黑屏 → 白屏 → 正常（根因链，按时间顺序）

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | 黑屏 | PSRAM 未开启，DPI 面板创建 `ESP_ERR_NO_MEM` | `CONFIG_SPIRAM=y` + `SPIRAM_MODE_HEX` + `SPIRAM_SPEED_200M` |
| 2 | 白屏 | 音频初始化阻塞 UI 启动 | 音频移到后台任务 `audio_boot_task` |
| 3 | 白屏+死锁 | APP 注册期 `on_init` 调 `sys_ui_lock()` 与 LVGL 适配任务死锁 | UI 构建延迟到 `on_start` 首次进前台 |
| 4 | 白屏 | 自建第二 I2C 总线（GPIO7/8）与 BSP 总线冲突 → 触摸轮询卡死 → 适配任务持锁不释放 | P4 上直接复用 `bsp_i2c_get_handle()`，不再自建 |
| 5 | 白屏+看门狗 | **`on_start` 在 `build_ui` 之前操作 `p->grid`（NULL）**。P4 上地址 0 可读且返回 0（不触发缺页），空指针在 LVGL 链表里**静默空转到 task_wdt**，无 panic 无 backtrace | `on_start` 先 `build_ui` 再操作 grid；`ui_built` 守卫 |
| 附 | 诊断误导 | `bsp_display_lock` 返回 `esp_err_t`（成功=ESP_OK=0），旧代码 `if (!bsp_display_lock(5000))` 判断反了——"timeout 5s" 日志其实是**拿锁成功** | 改 `== ESP_OK` 判断，并按官方用 `bsp_display_lock(-1)` 永久等待 |

**经验**：P4 上"无 panic 的卡死/空转"先怀疑空指针（地址 0 可读）；LVGL 链表空转是典型症状。

## 6. 工具链与环境（手机端 Termux，重要！）

### 6.1 烧录与串口（USB OTG 直连）

板卡 USB 是 **CH343P CDC-ACM**，内核 `cdc_acm` 驱动会抢接口，必须用户态 libusb 接管（`acm_bridge.c` 已处理 `detach_kernel_driver`）。**桥接不转发 line coding**，esptool stub 切 460800 会让芯片失联——烧录用 `--no-stub` 全程 115200。

工作流（`flasher.py` 已封装全部步骤，**直接跑它**）：
```bash
cd ~/ && python3 flasher.py            # 双镜像: ~/merged-binary.bin + ~/font.bin@0x210000 (md5 缓存: 未变则跳过; 清场→探测→起桥→下载模式→烧录)
```

抓串口日志（复位进应用模式 + 持续捕获 25s）：
```bash
cd ~/ && printf 'BAUD=115200\nSEQ=1,0;100;0,1;100;0,0;100\n' > bridge.conf
# 清残留进程(参考 flasher.py 的清场逻辑)后:
DEV=$(termux-usb -l | python3 -c "import sys,json;d=json.load(sys.stdin);print(d[0] if d else '')")
termux-usb -r -e ~/acm_bridge "$DEV" &   # 桥接, 从 bridge.log 里取 /dev/pts/N
python3 cap.py /dev/pts/N 25              # 捕获到 serial_cap.txt
tr -d '\r' < serial_cap.txt | grep -aE "关键词" | tail -20
```

关键文件：`flasher.py`（编排器）、`acm_bridge.c`（CDC-ACM→pty 桥，支持 SEQ/BAUD/DUMP/NOPTY/NORESET 配置）、`bridge.conf`（配置，**不能用环境变量**——termux-usb 走 Android binder 不传 env）、`cap.py`（pty 捕获）、`calibrate.sh`（复位序列标定）。

**复位序列（已标定，勿改）**：
- 进下载模式：`0,1;100;1,0;50;1,0;100`
- 启动应用：`1,0;100;0,1;100;0,0;100`

**USB 掉线**（`termux-usb -l` 为空）：让用户物理重插 USB 线。

### 6.2 GitHub 操作（PAT 已配在 `~/.git-credentials`）

```bash
T=$(printf "protocol=https\nhost=github.com\n\n" | git credential fill 2>/dev/null | sed -n 's/^password=//p')
```

- **固件下载必须用资产端点**（Release 级端点返回 415；github.com 直连在 Termux 不稳）。build-62+ 有三个资产：`merged-binary.bin` + `font.bin`（17MB 中文字库）+ `demo.bin`（OTA 镜像）：
```bash
curl -sfL -C - -H "Authorization: Bearer $T" -H "Accept: application/octet-stream" \
  "https://api.github.com/repos/pengchegnkai/ESP-IDF-Actions/releases/assets/<asset_id>" -o <name>
```
  asset_id 从 `GET /releases?per_page=1` 取（按 `assets[].name` 查）。
- **网络可达性（本手机实测，2026-11-21 更新）**：`api.github.com` ✅（可靠，首选）；`raw.githubusercontent.com` ❌（TCP reset，拉源码改用 api `contents` 端点 + base64 解码）；资产下载**带 auth 头会 415/TCP 掐断，无认证 + `-C -`（206 断点续传）分段下载正常且快**（仓库公开，无需认证）。font.bin 可直接拷贝本地 `~/sdcard_fonts/NotoSansSC.ttf`（内容一致，md5 已核对）。
- CI 状态：`GET /repos/pengchegnkai/ESP-IDF-Actions/actions/runs?per_page=1`；CI 日志：`GET /actions/runs/<id>/logs`（返回 ZIP）；资产 SHA256 可从 release API `assets[].digest` 取。

## 7. 未完成项（哪些还没搞完 —— 接手重点）

### 7.1 【已解决】此前的中文支持/崩溃问题（历史记录见 §8 铁律）

中文支持（build-31 时代）已由用户确认可用；后续一系列启动崩溃的根因与修复见 §8 铁律 8-10，
关键 commit：`3eea3b1`（事件总线分组）、`d6f5ac8`（禁 ICG）、`a502b9e`（LVGL 内存后端切 PSRAM + WHOLE_ARCHIVE）。

### 7.2 【当前卡点】build-62/63 烧录 + 上机验证（等用户板卡 USB 重连）

- build-62 资产已下载校验：`~/merged-binary.bin`（1,666,288 B）+ `~/font.bin`（17,772,300 B，由本地 `~/sdcard_fonts/NotoSansSC.ttf` 拷贝，免下载）
- 烧录：`cd ~/ && python3 flasher.py`（约 10 分钟，双镜像 + md5 缓存）
- build-62 验证点：SD 卡在位 → 字库仍走 SD（行为不变）；拔卡 → 串口出 `embedded CJK font loaded from 'fonts' partition` + 中文 UI 正常
- build-63（CI 成功后）验证点：新表盘观感 / 天气卡（需连 WiFi）/ OTA 卡 / 安全 APP（FIDO 激活开关、USB 插拔状态）/ 开机无崩溃

### 7.3 【已知小瑕疵】

- S3/C6 目标的 sys_hal 仍是 stub（显示/触摸/音频降级），SD 挂载返回 `ESP_ERR_NOT_SUPPORTED`（预期行为，中文功能仅 P4 可用）。
- `sys_ui_mem.c` 的 monitor 统计是整堆口径（PSRAM 全局），非 LVGL 独占用量。

### 7.4 【路线图 TODO（三阶段安全终端）】

- **阶段二**：P4↔C6 SDIO 安全通道（spike 已通，报告 `docs/spike-sdio-secure-channel.md`）：esp_hosted **peer_data** 特性（host `eh_host_peer_data_send`，单包上限 8166B）承载整体 AES-GCM/PSK 加密；PSK 由出厂唯一 ID（eFuse MAC 只读）+ 固定盐派生，**禁熔断 eFuse**；C6 镜像需重编（官方 peer_data 示例 CP 工程为模板，NVS 已验证）；**最大风险 R1：C6 首次物理烧录路径未知**（板卡 USB 口仅 P4，C6 无独立烧录口，只能经 SDIO 通道或板厂协助）
- **阶段三**：FIDO2 真签名：阶段二通道 + C6 侧 ECDSA-P256（私钥存 C6 NVS）+ CTAP2 完整 CBOR 应答（MakeCredential/GetAssertion、PIN/UvAuth）+ 多报告消息拼装（CTAP2 消息可 >64B）
- 其余：音频播放/录音演示、电量管理、APP 间通信、英文界面 LVGL 符号字体检查

## 8. 代码铁律（违反必炸，血泪总结）

1. **P4 上地址 0 可读返回 0**：空指针不 panic 只静默空转。所有 LVGL 指针操作前判空。
2. `bsp_display_lock` 返回 **`esp_err_t`**，成功 = `ESP_OK`(0)。官方用法 `bsp_display_lock(-1)` 永久等待；**超时后不得继续执行 LVGL 操作**。
3. 所有 LVGL 操作必须在 `sys_ui_lock()` 成功返回 true 之后；`on_init` 不碰 LVGL。
4. **P4 上不新建 I2C 总线**：GPIO7/8 已被 BSP 占用，用 `bsp_i2c_get_handle()`。
5. 换板/换屏参数只改 `main/app_main.c` 顶部的 CFG 结构。
6. 新 APP：写 `sys_app_t`（生命周期函数）+ `xxx_app_get()`，在 `app_main.c` 注册；文案进 `sys_ui_font.c` 的 i18n 表（中/英各一条）。
7. 编译目标：push 固定 esp32p4；改公共代码后 S3/C6 目标也需在 dispatch 里验证（sys_ui/sys_hal 有非 P4 分支）。
8. **P4 内部 DRAM 极其紧张（动态池仅 ~60KB）**：任何大静态数组（.bss）都可能挤爆启动期分配（32K DMA 池保留 / sleep clock ICG / REGDMA）。LVGL 内存必须走 `LV_USE_CUSTOM_MALLOC` + `sys_ui_mem.c`（全 PSRAM），**不要改回内置分配器**；`CONFIG_PM_SLEEP_CLK_ICG_ENABLE=n`（绕过 IDFGH-17859）保留。
9. **LVGL 9.3 API 坑（与 8.x/网上资料不同）**：字体宏是 `const lv_font_t` 对象（取地址 `&lv_font_montserrat_48`，且要赋给 `const lv_font_t *`）；没有 `lv_switch_get/set_state`（用 `lv_obj_has_state/add_state` + `LV_STATE_CHECKED`）；没有 `lv_obj_set_text`（用 `lv_label_set_text`）；Kconfig 字体符号**无 USE_ 前缀**（`CONFIG_LV_FONT_MONTSERRAT_48`，库选项才带，如 `LV_USE_TINY_TTF`）。
10. **组件反向提供符号给 lvgl（如自定义分配器 _core）必须 `WHOLE_ARCHIVE`**：静态库按需提取不会为后到的引用拉入对象，会链接期 undefined reference。
11. **flash 32MB + 自定义分区表**：P4 用 `partitions_p4.csv` 双 OTA（ota_0 2MB@0x10000，fonts 18MB@0x210000，ota_1 2MB@0x1410000，ota_data 0x2000@0x1610000）；偏移算术：ota_0 末尾恰好接 fonts，**改分区时先算偏移**。S3/C6 用 `partitions.csv`（factory）。
12. **事件总线回调跑在 "sys_evt" 任务，不持显示锁**：回调内操作 LVGL 必须显式 `sys_ui_lock()`（检查返回值）；回调 payload 指针生命周期 = 发布期间，要留存数据须自拷贝。
13. **LVGL 9.3 补充坑**（9.5/8.x 网上资料不适用）：无 `bg_blur_size`（9.5 才有 blur_radius，发光用 shadow）；无 `LV_OPA_15`（用字面量 38）；arc 无 start/end 角度样式属性（整圆环用 `lv_arc_set_bg_angles(arc,0,360)` + `lv_arc_set_rotation(arc,270)` 从 12 点起）；`lv_obj_set_style_bg_grad` 只存 dsc 指针 → 渐变 dsc 必须长生命周期（放 priv 结构）。
14. **IDF v6.1**：内置 `json` 组件已移除，cJSON 走托管组件 `espressif/cjson`（CMake 名 `espressif__cjson`）；hosted 不导出 `esp_wifi_is_started`；C 条件编译用 `IDF_TARGET` 变量。
15. **P4 USB OTG（TinyUSB FIDO）**：本板 Type-C OTG 口 = P4 专用 HS OTG 引脚（USBD_P/N，芯片脚 50/49），不经 GPIO 矩阵；esp_tinyusb 2.x，FIDO HID usage page 0xF1D0/usage 0x03，64B IN/OUT；CTAP2 帧 = [channel, ccid, params]，错误帧 = [channel, 命令 CCID, err]。
16. **锁序**：调度器锁 → 显示锁（不可反）；TinyUSB 回调（CTAP rx）跑在 USB 任务（非 LVGL 非 ISR），弹窗 show 内部自取显示锁；回调内不碰共享 UI 状态之外的东西。

## 9. 关键文件地图

**仓库**（`~/esp-idf-gh-actions-demo/`）：见 §3。另有 `AI_HANDOVER.md`（本文档）。

**手机家目录**（`~/`，烧录工具链）：`flasher.py`、`acm_bridge.c`、`bridge.conf`、`cap.py`、`calibrate.sh`、`merged-binary.bin`（待烧固件）。
**参考资料缓存**（`~/`，已下载）：`bsp_43.c`（4.3 BSP 全源码）、`official_lvgl_main.c`（官方 LVGL 示例）、`lv_tiny_ttf.h/.c`、`lv_fs_stdio.c`、`lvgl_kconfig`、`lvgl_tree.json`、`tree.json`（BSP 仓库树）、`blob_*.json`。

## 10. 下一步行动清单（按顺序）

1. ✅ build-62：字库内嵌分区（CI 成功，资产已下载校验）。
2. **烧录 build-62**（等用户板卡 USB 重连）：`cd ~/ && python3 flasher.py` → 抓串口验证字库三级回退（SD 在位/拔卡两个场景）。
3. **build-63**（CI 进行中）：Vela 表盘重做 + 天气/OTA/FIDO2 安全密钥 + 双 OTA 分区。CI 成功 → 下载资产 → 覆烧 → 验证各卡片 + 安全 APP + 开机无崩溃。
4. **阶段二**：SDIO 安全通道（spike 完成）：先解决 C6 首次烧录路径（R1），再启用 esp_hosted peer_data host/CP 两侧 + PSK 派生 + AES-GCM。
5. **阶段三**：FIDO2 真签名（C6 ECDSA + CTAP2 完整应答 + PIN/UvAuth）。


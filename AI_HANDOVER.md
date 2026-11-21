# AI 交接 — ESP32-P4-WIFI6-Touch-LCD-4.3 (Brookesia 工厂基座)

> 最后更新: 2026-11-21 (切换到工厂基座)

## 现状

- **main 分支 = Brookesia 工厂固件基座**（导入自 `waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3`
  的 `firmware/brookesia/`，与乐鑫开源 `espressif/esp-brookesia` 同源）
- **legacy 分支** = 之前的自研 Vela 工程（build-72 已上机验证；显示 bug 根因已定位：
  P4 硅片版本档错配——自研 sdkconfig 用 rev1.x 档 (SPIRAM 200MHz)，而板子是 rev3.x
  必须 `CONFIG_ESP32P4_REV_MIN_300` + `CONFIG_SPIRAM_SPEED_250M`）
- 切换原因（用户决策）：工厂基座显示/WiFi(C6 配对固件)/音频全部验证过；
  APP 扩展点干净（components/ 加目录 + 插件注册，参考 `brookesia_app_calculator`）

## 用户已确认的产品决策（2026-11-21）

1. 主屏 = 工厂 launcher 图标网格（表盘降级为普通 APP，后续移植）
2. 出厂 APP 全部保留（含小智 AIChats 在线 AI）
3. 中文字体 = 工厂霞鹜 30px（SPIFFS 内 `xiaozhi/font.bin`）
4. 本阶段范围 = **先最小基座**（跑起来 + 烧录验证），功能移植分批做

## 关键事实

- 板子: ESP32-P4 rev3.x (32MB PSRAM/flash) + ESP32-C6-MINI (SDIO WiFi6, 协处理器固件与出厂镜像配对)
- 显示: 4.3" 480×800 MIPI-DSI ST7701；触摸 GT911；**串口控制台 2000000 波特率**（不是 115200！）
- 构建: IDF **v5.5.5**（`main/idf_component.yml` 要求 `>=5.5,<6.0`）；
  **必须** `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.rev3_x"`
  （rev3_x = REV_MIN_300 + SPIRAM_SPEED_250M；跨版本档抄时钟设置 → 面板时序错）
- **依赖版本（勿动, 10 轮 CI 排障后固化, 详见「CI 依赖排障记录」）**:
  `components/board/idf_component.yml`:
  - `espressif/esp_board_manager: >=0.7.5` → 解析 0.7.6（有 devices/ 设备抽象层；
    0.7.5 没有, board 代码的 dev_*/periph_* 类型会 unknown）
  - `espressif/esp_codec_dev: ^2.0.0-beta4` → 解析 2.0.0-beta5（board 代码与
    esp_board_manager 0.7.6 都按 2.x API 编写；1.x 的 audio_codec_new 已改名）
- **sdkconfig.defaults.rev3_x 含 11 个 ESP_BOARD_*_SUPPORT 最小集**（伞头
  esp_board_manager_includes.h 按 Kconfig 门控包含设备头, 全默认 n；
  **勿全量启用**——P4 无 DAC 硬件, 0.7.6 periph_dac.c 是 IDF6-only API）
- CI: `.github/workflows/build.yml` — docker `espressif/idf:v5.5.5`，
  流程: `pip install esp-bmgr-assist==0.8.3 && idf.py reconfigure &&
  idf.py bmgr -b esp32_p4_wifi6_touch_lcd_4_3 && idf.py build && idf.py merge-bin`
  （bmgr 从 waveshare_boards 0.2.0 板卡定义生成 components/gen_bmgr_codes/ 设备表,
  README 工厂流程; g_esp_board_* 符号是生成代码）；
  release 资产 `build/merged-binary.bin`（tag `build-N`）
- 分区 (32MB): nvs/nvsfactory + otadata + phy_init + model(ESP-SR, 960K) +
  **factory app 8M @0x200000** + storage SPIFFS 6M；**单 factory 分区，无双 OTA**
- 出厂 APP: DrawPanel / Squareline / Calculator / SpecAnalyzer / MusicPlayer /
  VideoPlayer / Settings / AIChats(小智)
  （Camera 已移除: 用户板卡未装摄像头；含 XiaozhiApp 的 explain_image 视觉 MCP 工具
  与 setXiaozhiDeveloperToolsChat 接口，commit 2a978b2/9a87aee）

## APP 模型（移植参考）

- C++ 类继承 `esp_brookesia::systems::phone::App`
  （生命周期 `init/run/back/close/deinit`；构造带 name + launcher 图标 +
  状态栏/导航条配置，`ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT`）
- `ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR` 注册（WHOLE_ARCHIVE 保 registrar）；
  `components/` 下新建目录即自动构建（main/CMakeLists.txt GLOB）
- 开机安装顺序 + 主屏图标顺序: `main/main.cpp` 的 `BOOT_APP_GROUPS`
- 板级 API: `board_display_start` / `board_wifi_service_init` (adf_event) /
  `board_codec_init` / `board_spiffs_mount`

## CI 依赖排障记录（2026-11-21, 10 轮, 已全部固化）

导入工厂快照后 CI 连续失败, 每轮定位一层（run id 为 GitHub Actions）:

| # | run | 症状 | 根因 | 修复 |
|---|-----|------|------|------|
| 1 | 44891983365 | `dev_camera_handle_t` / `esp_codec_dev.h` 缺失 | `^0.7.5` 浮动解析, optional 依赖未入图 | 钉 `==0.7.5` (1955fa9) |
| 2 | 44904312871 | 同上 + "Skipping optional dependency" | 无锁文件 fresh 解析跳过 board_manager 的 optional 依赖 | 显式声明 esp_codec_dev (后续轮次) |
| 3 | 44907636631 | 诊断步骤失败 | runner 宿主机 DNS 解析不了注册表域名（docker 容器内可以） | 诊断改打包 managed_components 上传 artifact |
| 4 | 44909794843 | 仍 dev_camera_handle_t | 0.7.5 确实缺类型 | — (转向移除摄像头) |
| 5 | 44910757511 | CMake: Failed to resolve component 'Camera' | 移除 Camera 后 XiaozhiApp CMakeLists PRIV_REQUIRES 漏改 | 移除 Camera 依赖 (9a87aee) |
| 6 | 44911371483 | `dev_audio_codec_handles_t` 等 unknown | **0.7.5 无 devices/ 设备抽象层**（源码在 espressif/esp-board-manager 仓库） | 版本放宽 `>=0.7.5` → 0.7.6 (7f90d59) |
| 7 | 44913800275 | 同上 | 0.7.6 有 devices 层, 但伞头 `esp_board_manager_includes.h` 按 Kconfig 门控, 58 个 `*_SUPPORT` 全默认 n | 先全量启用 58 项 (d4b0fc2) |
| 8 | 44915985902 | `periph_dac.c: dac_oneshot_*` unknown | **P4 无 DAC 硬件**; 全量启用踩中 IDF6-only 驱动 | 改 11 项最小集 (7031c2c) |
| 9 | 44917352932 | `audio_codec_new` implicit declaration | esp_codec_dev 钉 1.3.x, 但 board 代码与 0.7.6 都是 **2.x API** | 改 `^2.0.0-beta4` (fbd9fd0) |
| 10 | 44918326316 | 链接: `g_esp_board_*` 4 个符号 undefined | 设备表是**生成代码**: 缺 `idf.py bmgr -b <board>` 生成步骤 | CI 补 esp-bmgr-assist + bmgr (3cd5718) |

**最小 Kconfig 集（sdkconfig.defaults.rev3_x, 11 项）**:
- 设备: `DEV_DISPLAY_LCD`(+`SUB_DSI`) / `DEV_AUDIO_CODEC` / `DEV_LEDC_CTRL` / `DEV_LCD_TOUCH`(+`SUB_I2C`)
- 外设: `PERIPH_ADC` / `PERIPH_I2C` / `PERIPH_I2S` / `PERIPH_LEDC` / `PERIPH_SPI`
- 推导方法: 全树 grep `dev_*/periph_*` 实际引用 + 0.7.6 各设备驱动的传递依赖闭包

**bmgr 生成机制**: `g_esp_board_devices` / `g_esp_board_peripherals` /
`g_esp_board_device_handles` / `g_esp_board_periph_handles` 四个设备表符号由
`gen_bmgr_config_codes.py` 从板卡定义（waveshare_boards 0.2.0 组件）生成到
`components/gen_bmgr_codes/`。esp_board_manager 0.7.6 的 `idf_ext.py`（提供
`idf.py bmgr` 命令）只在 IDF 6.0+ 自动加载, IDF 5.5 需 `pip install
esp-bmgr-assist==0.8.3`（README 工厂流程）。

**排查工具链**: CI 诊断步骤把 `managed_components/espressif__esp_board_manager`
+ `dependencies.lock` + `sdkconfig` + `components/gen_bmgr_codes` 打包上传
`diag-dump` artifact, 本地可解压直接读源码/清单定位问题（runner 宿主机连不了
注册表, 但组件管理器已在容器内下载完整组件）。

## 待办（分批移植）

1. [ ] Vela 表盘 → 新 brookesia APP（legacy 分支 `components/app_watch/watch_app.c` UI 可参考移植）
2. [ ] 中文字体增强（现仅 30px 霞鹜；多字号需分区重排或子集化，32MB 布局见上）
3. [ ] OTA 双分区（分区表需重设计: factory → ota_0/ota_1）
4. [ ] FIDO2 安全终端（USB HS 口空闲；TinyUSB HID + 新 APP；禁熔断 eFuse 红线）
5. [ ] 天气/网络卡片（走 board_wifi_service）

## 本地工具（Termux 环境）

- `~/flasher.py`: 分块断点续烧（烧录波特率 115200；**监控波特率 2000000**）
- `~/diag.sh` / `~/cap.py`: 串口一键抓取
- 烧录前必须先问用户确认（用户红线）

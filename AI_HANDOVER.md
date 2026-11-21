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
- CI: `.github/workflows/build.yml` — docker `espressif/idf:v5.5.5`，
  `idf.py build && idf.py merge-bin`，release 资产 `build/merged-binary.bin`（tag `build-N`）
- 分区 (32MB): nvs/nvsfactory + otadata + phy_init + model(ESP-SR, 960K) +
  **factory app 8M @0x200000** + storage SPIFFS 6M；**单 factory 分区，无双 OTA**
- 出厂 APP: DrawPanel / Squareline / Calculator / SpecAnalyzer / MusicPlayer /
  Camera / VideoPlayer / Settings / AIChats(小智)

## APP 模型（移植参考）

- C++ 类继承 `esp_brookesia::systems::phone::App`
  （生命周期 `init/run/back/close/deinit`；构造带 name + launcher 图标 +
  状态栏/导航条配置，`ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT`）
- `ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR` 注册（WHOLE_ARCHIVE 保 registrar）；
  `components/` 下新建目录即自动构建（main/CMakeLists.txt GLOB）
- 开机安装顺序 + 主屏图标顺序: `main/main.cpp` 的 `BOOT_APP_GROUPS`
- 板级 API: `board_display_start` / `board_wifi_service_init` (adf_event) /
  `board_codec_init` / `board_spiffs_mount`

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

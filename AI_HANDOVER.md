# AI 交接文档（AI Handover）

> 最后更新：2026-11-15
> 已验证固件：**build-29**（commit `4446174`）—— 用户已确认 Launcher 界面正常显示、触摸导航正常
> ⚠️ **工作区有未提交的中文支持代码**（12 个文件，见 §7），**尚未编译、未验证**。接手后第一件事是读 §7 和 §10。
>
> 本文档写给下一个 AI/开发者：包含项目目标、架构、已验证结论、踩坑根因链、
> 手机端工具链的确切用法、以及当前进行到一半的工作。不要依赖本文档之外的"记忆"。

---

## 1. 项目概览

- **目标**：在微雪 ESP32-P4-WIFI6-LCD-4.3 板卡上构建**通用系统基座**（APP 生命周期 + 事件总线 + 全局 UI + 硬件抽象层），后续往里填真实 APP。
- **仓库**：`https://github.com/pengchegnkai/ESP-IDF-Actions`（branch `main`），本地路径 `~/esp-idf-gh-actions-demo/`
- **编译**：GitHub Actions 云端编译（ESP-IDF v6.1），push 触发固定编 `esp32p4`，手动 dispatch 可选 `esp32p4`/`esp32c6`/`esp32s3`。每次成功构建自动发 Release，命名 `build-NN` 递增。
- **烧录**：手机（Android Termux）经 USB OTG 直连板卡烧录 + 抓串口。工具链在 `~/`（见 §6、§9）。
- **当前里程碑**：系统基座 UI 已跑通（build-29，用户确认屏幕显示正常）。下一步功能：**SD 卡字库的中文支持**（进行中，代码已写未验证）。

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
│   ├── sys_core/          # 系统核心
│   │   ├── sys_app.h / sys_app_manager.*   # APP 生命周期: on_init/on_start/on_suspend/on_stop
│   │   ├── sys_event_bus.*                 # 全局事件总线 (基于 esp_event 适配器)
│   │   └── sys_time_service.*              # 1Hz TIME_TICK
│   ├── sys_hal/           # 硬件抽象层 (display/touch/audio/i2c/sd)
│   │   └── P4: 直通官方 BSP; 其他芯片: stub
│   ├── sys_ui/            # LVGL v9 封装: 初始化/屏幕切换/顶层弹窗/字库+i18n
│   ├── app_launcher/      # Launcher APP (桌面: APP 网格 + 状态栏 + 弹窗演示)
│   └── app_settings/      # Settings APP (演示: 开关/USB 状态/返回)
├── main/app_main.c        # 启动: NVS→事件总线→HAL(+SD)→UI→字库→注册APP→启动Launcher
├── .github/workflows/build.yml
├── sdkconfig.defaults(.esp32p4)
└── idf_component.yml (组件级)
```

**启动顺序（app_main.c，有依赖不可乱）**：
1. NVS → 2. 事件总线 → 3. HAL（display/touch/i2c）→ **3.5 SD 卡挂载（失败不致命）** → 4. `sys_ui_init()` → **4.5 `sys_ui_font_init()`（SD 字库+语言判定，必须在任何 APP 建 UI 前）** → 5. 注册 APP → 6. `sys_app_launch(LAUNCHER_APP_ID)` → 音频后台任务。

**UI 线程模型（铁律）**：
- 所有 LVGL 对象操作必须在 `sys_ui_lock()/sys_ui_unlock()` 之间（P4 上即 BSP 锁，与适配任务的 `lv_timer_handler` 互斥）。
- `sys_ui_lock()` 返回 `bool`：**false = 没拿到锁，禁止执行任何 LVGL 操作**（调用方必须检查）。
- `sys_ui_set_root(screen)` 会把中文字体设到屏幕根对象（子控件继承；新屏幕不继承旧屏幕样式，所以每次 set_root 重设）。顶层弹窗挂 `lv_layer_top()`，不在屏幕树下，字体必须显式设置。

**APP 生命周期铁律**：
- `on_init` 只做内存分配，**不碰 LVGL**（注册阶段拿显示锁会与适配任务死锁）。
- UI 构建延迟到 `on_start` 首次进前台（`p->ui_built` 守卫）。

## 4. 已完成且已验证（截至 build-29 / `4446174`）

- ✅ 完整项目骨架 + CI（push 编 esp32p4，dispatch 可选三芯片，自动 Release）
- ✅ 系统基座：APP 生命周期调度、事件总线、时间服务、全局 UI（屏幕切换 + 顶层弹窗 overlay）、sys_hal（BSP 直通）
- ✅ 双 APP：launcher（桌面）、settings（设置演示，含返回栈 `sys_app_back`）
- ✅ **屏幕显示正常、触摸导航正常**（用户确认：launcher ↔ settings 来回点击切换均正常，弹窗演示可用）
- ✅ PSRAM 开启（32MB hex 200MHz）、I2C 复用 BSP 总线、音频后台初始化

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
cd ~/ && python3 flasher.py            # 烧录 ~/merged-binary.bin（清场→探测→起桥→下载模式→烧录）
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

- **固件下载必须用资产端点**（Release 级端点返回 415；github.com 直连在 Termux 不稳）：
```bash
curl -sfL -H "Authorization: Bearer $T" -H "Accept: application/octet-stream" \
  "https://api.github.com/repos/pengchegnkai/ESP-IDF-Actions/releases/assets/<asset_id>" -o merged-binary.bin
```
  asset_id 从 `GET /releases?per_page=1` 取（取 `assets[].id`，找 `merged-binary.bin`）。
- **网络可达性（本手机实测）**：`api.github.com` ✅（可靠，首选）、`raw.githubusercontent.com` ⚠️（时好时坏）、CDN（jsdelivr/github releases 下载）❌ 基本不可达。大文件走 **`api.github.com/git/blobs/<sha>`**（base64，限 100MB）最稳。
- CI 状态：`GET /repos/pengchegnkai/ESP-IDF-Actions/actions/runs?per_page=1`。

## 7. 未完成项（哪些还没搞完 —— 接手重点）

### 7.1 【进行中】SD 卡字库中文支持 —— 代码已全部写完，**未编译、未烧录、未验证**

设计：启动时扫描 `/sdcard/fonts/*.ttf`（第一个）→ 读入 PSRAM → LVGL 9.3 `lv_tiny_ttf_create_data()` 按需渲染（stb_truetype 流式，不吃整文件内存）→ 中文 UI；无 SD 卡/无字库 → 英文 UI（内置 Montserrat）。语言由字库可用性自动决定，无需手动切换。

已写文件（12 个，git 状态 M/??）：
| 文件 | 改动 |
|---|---|
| `components/sys_hal/src/sys_hal.c` + 头 | 新增 `sys_hal_sd_mount()`（P4 直通 `bsp_sdcard_mount()`）、`sys_hal_sd_mount_point()` |
| `components/sys_ui/src/sys_ui_font.c` | **新文件**：字库扫描/加载/PSRAM 缓存、按字号字体缓存（8 槽）、i18n 字符串表（中/英 12 条）、`sys_ui_font_init/lang/str/cjk_font` |
| `components/sys_ui/src/sys_ui.c` + 头 | `sys_ui_set_root` 自动给屏幕设中文字体；头文件新增语言/文案/字体 API |
| `components/sys_ui/CMakeLists.txt` | 加 `sys_ui_font.c` + `REQUIRES heap` |
| `components/app_launcher/launcher_app.c` + CMakeLists | 全部文案走 `sys_ui_str()`；弹窗显式设中文字体；`REQUIRES app_settings`（用 SETTINGS_APP_ID） |
| `components/app_settings/app_settings.c` | 全部文案走 `sys_ui_str()` |
| `main/app_main.c` | HAL 后挂 SD（失败不致命）、`sys_ui_init` 后调 `sys_ui_font_init`（APP 建 UI 前） |
| `sdkconfig.defaults` | `CONFIG_LV_USE_TINY_TTF=y` |
| `sdkconfig.defaults.esp32p4` | **新文件**：`CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL=y`（空卡自动 FAT 格式化） |

**未验证风险点（CI 大概率先炸这里）**：
1. `lv_tiny_ttf.h` 的 include 路径——用 `__has_include` 级联兼容三种布局（`libs/tiny_ttf/...` / `src/libs/tiny_ttf/...` / 裸名），都不中会 `#error`。可查 lvgl 仓库 `CMakeLists.txt` 的 `INCLUDE_DIRS` 确认。
2. `lv_tiny_ttf_create_data` API 签名按 LVGL 9.3 官方头文件核实过（data/size/size 三参），但没实际编译过。
3. 中文渲染效果、字体大小观感（默认 24px）未上屏确认。

### 7.2 【已下载，待用户拷卡】字库文件

Noto Sans SC（variable font，17,772,300 字节，TTF 魔数已验证）已下载到手机 **`~/sdcard_fonts/NotoSansSC.ttf`**（走 GitHub API blob 通道，CDN 在本机不可达）。
用户把它拷到 **SD 卡的 `fonts/` 目录**（卡需 FAT32；空卡固件会自动格式化），即 `/sdcard/fonts/NotoSansSC.ttf`。
注意：字库 17MB 常驻 PSRAM（板载 32MB），可行但占比高——若内存吃紧可换更小的 CJK TTF（如霞鹜文楷 ~8MB）。

### 7.3 【未做】中文支持的两条路径验证

CI 编译 → 下载固件 → 烧录 → 抓串口，验证：
- 无字库路径：日志 `no TTF found in /sdcard/fonts, English UI` + 屏幕英文界面正常；
- 有字库路径：日志 `CJK font loaded: ... -> Chinese UI` + 屏幕中文渲染正常（重点看：中文字形是否出方块、字号观感、触摸导航仍正常）。
串口关键词：`ui_font:`（新模块 TAG）、`UI language:`、`CJK font`。

### 7.4 【已知小瑕疵】

- `app_main.c` 中 `DISPLAY_CFG`/`TOUCH_CFG` 的分辨率仅用于日志（BSP 内部用真实 480×800），当前值已是 480×800，无问题。
- S3/C6 目标的 sys_hal 仍是 stub（显示/触摸/音频降级），SD 挂载返回 `ESP_ERR_NOT_SUPPORTED`（预期行为，中文功能仅 P4 可用）。

### 7.5 【远期 TODO（未开始）】

- WiFi 联网（esp32_c6 协处理器路径未接）
- 音频播放/录音 API 演示（目前只有 init + volume）
- 电量/电池管理、USB 事件源（事件 ID 已预留，无真实触发源）
- 更多 APP、APP 间通信、持久化设置（NVS 仅 init）
- 英文界面时 LVGL 符号图标（LV_SYMBOL_*）依赖内置字体含符号子集，若换默认字体需检查

## 8. 代码铁律（违反必炸，血泪总结）

1. **P4 上地址 0 可读返回 0**：空指针不 panic 只静默空转。所有 LVGL 指针操作前判空。
2. `bsp_display_lock` 返回 **`esp_err_t`**，成功 = `ESP_OK`(0)。官方用法 `bsp_display_lock(-1)` 永久等待；**超时后不得继续执行 LVGL 操作**。
3. 所有 LVGL 操作必须在 `sys_ui_lock()` 成功返回 true 之后；`on_init` 不碰 LVGL。
4. **P4 上不新建 I2C 总线**：GPIO7/8 已被 BSP 占用，用 `bsp_i2c_get_handle()`。
5. 换板/换屏参数只改 `main/app_main.c` 顶部的 CFG 结构。
6. 新 APP：写 `sys_app_t`（生命周期函数）+ `xxx_app_get()`，在 `app_main.c` 注册；文案进 `sys_ui_font.c` 的 i18n 表（中/英各一条）。
7. 编译目标：push 固定 esp32p4；改公共代码后 S3/C6 目标也需在 dispatch 里验证（sys_ui/sys_hal 有非 P4 分支）。

## 9. 关键文件地图

**仓库**（`~/esp-idf-gh-actions-demo/`）：见 §3。另有 `AI_HANDOVER.md`（本文档）。

**手机家目录**（`~/`，烧录工具链）：`flasher.py`、`acm_bridge.c`、`bridge.conf`、`cap.py`、`calibrate.sh`、`merged-binary.bin`（待烧固件）。
**参考资料缓存**（`~/`，已下载）：`bsp_43.c`（4.3 BSP 全源码）、`official_lvgl_main.c`（官方 LVGL 示例）、`lv_tiny_ttf.h/.c`、`lv_fs_stdio.c`、`lvgl_kconfig`、`lvgl_tree.json`、`tree.json`（BSP 仓库树）、`blob_*.json`。

## 10. 下一步行动清单（按顺序）

1. **CI 编译中文支持代码**：push 后轮询 `actions/runs`。若挂：优先查 `lv_tiny_ttf.h` include（§7.1 风险 1），查 lvgl 仓库 `CMakeLists.txt` 的 `INCLUDE_DIRS`；其次查 API 签名。修完重推。
2. **下载固件**（§6.2 资产端点）→ `python3 flasher.py` 烧录。
3. **无字库路径验证**：§7.3。
4. **交付字库**（§7.2）→ 用户拷卡 → **有字库路径验证**：§7.3。
5. 中文上屏 OK 后：更新本文档 §4/§7 状态，按 §7.5 排下一个功能。


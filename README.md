# ESP32-P4 System Base

基于 **微雪 ESP32-P4-WIFI6-Touch-LCD-4.3** 开发板（ESP32-P4 主控 + ESP32-C6 协处理器）的通用系统基座。
纯底层框架：硬件抽象、APP 生命周期调度、全局事件总线、LVGL UI 与顶层弹窗，不含具体业务。

## 板卡

| 项 | 规格 |
|---|---|
| 主控 | ESP32-P4NRW32（双核 RISC-V @360MHz，封装内 32MB PSRAM，板载 32MB NOR Flash） |
| 协处理器 | ESP32-C6-MINI-1（SDIO，Wi-Fi 6 / BLE 5） |
| 屏幕 | 4.3" IPS 480×800，MIPI-DSI 2-lane，ST7701 |
| 触摸 | GT911 电容 5 点（I2C，SDA=GPIO7 / SCL=GPIO8） |
| 音频 | ES8311 codec + ES7210 双麦前级 |

P4 目标通过官方 BSP `waveshare/esp32_p4_wifi6_touch_lcd_4_3` 完成屏幕/触摸初始化与 LVGL 适配；
S3 / C6 目标保持 stub 实现，保证 CI 三芯片均可编译。

## 目录结构

```
main/app_main.c            启动: NVS -> 事件总线 -> HAL -> UI -> 时间服务 -> 注册 APP -> Launcher
components/
├── sys_core/              系统内核
│   ├── sys_app.h          APP 接口规范 (init/start/suspend/stop)
│   ├── sys_app_manager.*  调度器: launch / back(返回栈) / stop / 枚举
│   ├── sys_event_bus.*    事件总线: 订阅/发布 + USB/网络/电量事件源
│   └── sys_time_service.* 1Hz 心跳 (SYS_EVENT_TIME_TICK)
├── sys_hal/               硬件抽象层: 显示(含互斥锁)/触摸/音频/I2C
├── sys_ui/                UI 框架: 屏幕切换、lv_layer_top 顶层弹窗
├── app_launcher/          桌面 APP: 状态栏、APP 网格、弹窗演示
└── app_settings/          设置 APP: 演示多 APP 调度与返回栈
```

## 核心 API

| API | 说明 |
|---|---|
| `sys_app_register(app)` | 注册 APP（触发 `on_init`） |
| `sys_app_launch("settings")` | 启动/切换 APP，当前前台自动 `on_suspend` 并入返回栈 |
| `sys_app_back()` | 返回上一个 APP（栈空返回 `ESP_ERR_NOT_FOUND`） |
| `sys_app_stop("settings")` | 停止 APP（`on_stop` 释放资源） |
| `sys_app_count()` / `sys_app_get(i)` | 枚举已注册 APP（Launcher 网格用） |
| `sys_event_subscribe(id, cb, ctx, &h)` | 订阅系统事件 |
| `sys_event_publish(id, data)` | 发布事件 |
| `sys_event_report_usb/net/battery` | 系统事件源上报（HAL/驱动层调用） |
| `sys_ui_switch_to(app_id)` | 屏幕切换（等价 `sys_app_launch`） |
| `sys_ui_overlay_show/dismiss` | 系统级顶层弹窗，永远压在所有 APP 之上 |
| `sys_ui_lock/unlock` | LVGL 上下文锁（P4 上即 BSP 锁） |
| `sys_hal_*` | 显示/触摸/音频/I2C 抽象，换板只改 `sys_hal.c` |

## 系统事件

`SYS_EVENT_TIME_TICK`、`USB_IN`、`USB_OUT`、`NET_UP`、`NET_DOWN`、`BATTERY_LOW`、`APP_STATE`。

## 新增 APP

1. `components/` 下新建组件，静态定义一个 `sys_app_t`，填 4 个生命周期回调；
2. `app_main.c` 中 `sys_app_register()` 注册；
3. `on_init` 创建根屏幕，`on_start` 调用 `sys_ui_set_root()` 挂载；
4. 需要事件则在 `on_init` 里 `sys_event_subscribe()`，`on_stop` 里取消订阅。

## CI（GitHub Actions）

- `push` / PR：只编译主控芯片 `esp32p4`
- 手动触发（Actions → Run workflow）：下拉选择 `esp32p4` / `esp32c6` / `esp32s3`，只编译选中的一款
- 产物：`firmware-<target>`（.bin/.elf/.map），失败时错误行写入 check annotations，构建日志另存 `build-log-<target>`

```bash
git push origin main      # 触发 P4 编译
```

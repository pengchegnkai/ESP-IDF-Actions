# ESP32-P4 System Base

基于 **ESP32-P4（主控）+ ESP32-C6（协处理器）** 开发板的通用系统基座，纯底层框架，不含具体业务。

## 架构

```
main/app_main.c            启动流程: NVS -> 事件总线 -> HAL -> UI -> 注册 APP -> 启动 Launcher
components/
├── sys_core/              系统内核
│   ├── sys_app.h          APP 接口规范 (生命周期: init/start/suspend/stop)
│   ├── sys_app_manager.*  APP 调度器 (注册/启动/前台切换)
│   └── sys_event_bus.*    全局事件总线 (esp_event 封装, 发布/订阅)
├── sys_hal/               硬件抽象层 (屏幕/触摸/音频/I2C, 当前为 stub, 换板只改这里)
├── sys_ui/                UI 框架 (LVGL v9 心跳任务/屏幕切换/顶层弹窗 Overlay)
└── app_launcher/          Launcher 桌面 APP (新 APP 的参考模板)
```

## 关键 API

| API | 说明 |
|---|---|
| `sys_app_register(app)` | 注册 APP, 回调 `on_init` |
| `sys_app_launch("launcher")` | 启动/切换 APP: 自动挂起当前前台 APP |
| `sys_event_subscribe / publish` | 订阅/发布系统事件 (USB、网络、电量、心跳等) |
| `sys_ui_switch_to(app_id)` | 屏幕切换 (转发到调度器) |
| `sys_ui_overlay_show / dismiss` | 系统级顶层弹窗, 永远压在所有 APP 之上 |

## 新增一个 APP 的步骤

1. `components/` 下新建组件, 静态定义一个 `sys_app_t`, 填写 4 个生命周期回调;
2. 在 `app_main.c` 中 `sys_app_register()` 注册;
3. UI 在 `on_init` 创建根屏幕, 在 `on_start` 调 `sys_ui_set_root()` 挂载。

## CI

GitHub Actions (`.github/workflows/build.yml`) 对 `esp32p4` / `esp32c6` / `esp32s3` 三个目标自动编译,
产物在运行页 Artifacts 下载。LVGL 通过 managed component 自动拉取。

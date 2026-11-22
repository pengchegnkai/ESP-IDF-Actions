# pc_sim 仿真规范 (模拟先行, 分层开发)

所有新 APP 代码必须遵循本规范, 保证同一份 core/ui 源码能在
**PC 模拟器 (本 harness / SDL2)** 和 **P4 真机 (固件构建)** 之间无缝切换.

## 目录分层 (每个 APP 组件内)

```
components/app_xxx/
  core/    # 纯逻辑 (可仿真): 状态机/解析器/协议栈/后端接口定义.
           #   禁止 #include 任何 esp-*.h / freertos / driver 头. 只允许 C 标准库 + 本组件头.
  ui/      # 纯逻辑 (可仿真): 纯 LVGL C 代码.
           #   只允许 #include "lvgl.h" + core/ 头 + 本组件头.
           #   通过 backend 接口 (函数指针表) 触达文件/USB/加密, 不直接调 ESP-IDF.
  hal/     # 硬件相关 (需真机验证): ESP-IDF 实现 (SDMMC/TinyUSB/C6 通道/LvLock 包裹).
           #   只进固件构建 (CMakeLists 的 SRCS_HAL), 不进 pc_sim.
  CMakeLists.txt   # 固件: core+ui+hal; pc_sim: 只编 core+ui (run_tests.sh)
```

`components/usb_arbiter/` 例外: 整组件纯逻辑 (USB 互斥状态机), core 即根目录.

## 注释标注 (规格书要求)

每个源文件头部 + 每个关键函数上方, 二选一批注:

```c
/* [纯逻辑·可仿真] 状态机解析, 无 ESP-IDF 依赖, pc_sim 覆盖. */
/* [硬件相关·需真机验证] SDMMC 挂载, 依赖 IDF sdmmc 驱动. */
```

## 后端接口模式 (硬件无关化的关键)

core 定义 backend 接口 (函数指针表), ui 通过它工作, hal 在真机实现,
stubs/ 在 PC 实现:

```c
/* core/xx_backend.h — [纯逻辑·可仿真] */
typedef struct { int (*op_a)(...); int (*op_b)(...); } xx_backend_t;

/* hal/hal_xxx.c — [硬件相关·需真机验证] */
void xx_backend_target_init(xx_backend_t *be);   // 填 ESP-IDF 实现

/* pc_sim/stubs/xx_backend_host.c — [纯逻辑·可仿真] */
void xx_backend_host_init(xx_backend_t *be);     // 填 POSIX/fake 实现
```

UI 创建函数签名固定形式: `xxx_ui_create(lv_obj_t *parent, const xx_backend_t *be)`
— parent 由调用方给 (真机: 工厂 Phone 系统的 APP 屏幕; 仿真: harness 建的虚拟屏),
UI 代码内部不创建 display/input 驱动 (那由底层 lv_disp_drv/lv_indev_drv 负责).

## LVGL 线程安全 (真机红线)

真机上所有 UI 创建/回调必须持 `esp_brookesia::gui::LvLockGuard` (C 代码用
`LvLock::getInstance().lock()/unlock()` 的 C 包装 `brookesia_lv_lock()/unlock()`).
**锁的获取放在 hal 层/APP 注册层**, 不进 ui/ 纯逻辑 (仿真侧无锁).

## 运行测试

```
cd pc_sim && ./run_tests.sh     # Termux/Linux/CI: 编 core+ui+stubs+tests 并运行
```

测试文件 `pc_sim/tests/test_<area>_*.c`, 用 `test_framework.h` 的 `TEST()` 宏
(constructor 自注册, 无需 main). 每个 APP 的成员只在自己 APP 目录 + 自己的
测试文件内写代码, 不碰共享文件.

## 开源参考 (代码注释中注明借鉴思路)

- 组件与架构: espressif/esp-brookesia + espressif/esp-ui (事件驱动, APP 隔离)
- Payload 执行: EvilKey-firmware (microSD 脚本加载执行)
- 安全底座: pico-fido (CTAP2 协议栈)

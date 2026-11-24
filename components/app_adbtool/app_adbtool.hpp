/* SPDX-License-Identifier: Apache-2.0 */
/* app_adbtool.hpp — App_AdbTool (ADB 工具 APP)
 * 注册模式完全对齐 components/app_security_key/esp_brookesia_app_security_key.hpp
 * ([硬件相关·需真机验证] 的 USB host ADB; ui/ 纯逻辑可仿真). */
#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps
{

class AdbTool : public systems::phone::App
{
public:
    static AdbTool &requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~AdbTool();

protected:
    AdbTool(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool deinit(void) override;

private:
    static AdbTool *_instance;

    /* adb core 输出行回调 (adb host 任务上下文, TODO: 真机加 LvLockGuard) */
    static void on_adb_line(const char *line, void *user);

    /* UI 连接/断开按钮 → adb_client_start/stop */
    static void on_connect(void *user);
    static void on_disconnect(void *user);

    /* 周期轮询 (100ms): adb_client_ready → 状态行 + 输入框非空 → adb_client_shell */
    static void status_poll(lv_timer_t *t);

    lv_timer_t *_timer = nullptr;
};

} // namespace esp_brookesia::apps

/* app_hackerkit.hpp — Brookesia App 子类 (App_HackerKit, 精简版 BadUSB)
 *
 * 参考 brookesia_app_calculator 注册模式.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "core/hk_backend.h"
#include "core/hk_script.h"

namespace esp_brookesia::apps
{

class HackerKit : public systems::phone::App
{
public:
    static HackerKit *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~HackerKit() override;

protected:
    HackerKit(bool use_status_bar, bool use_navigation_bar);

    /* Core app interface */
    bool init(void) override;
    bool deinit(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    /* 执行入口: usb_arb_request(BADUSB) → 后台任务跑 HK_executor → release.
     * 由 hk_ui_set_executor 注入到 UI. */
    static int exec_payload(const char *payload_name);

    /* 后台执行任务 (FreeRTOS): 持 arb 锁期间跑解析+执行, 结束 release */
    static void exec_task(void *arg);

    static HackerKit *_instance;
};

} // namespace esp_brookesia::apps

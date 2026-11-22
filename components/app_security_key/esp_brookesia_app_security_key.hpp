/* SPDX-License-Identifier: Apache-2.0 */
/* esp_brookesia_app_security_key.hpp — App_SecurityKey (FIDO2 安全密钥 APP)
 * 注册模式参考 components/brookesia_app_calculator. */
#pragma once

#include <cstddef>
#include <cstdint>

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "sk_session.h"

namespace esp_brookesia::apps
{

class SecurityKey : public systems::phone::App
{
public:
    static SecurityKey &requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~SecurityKey();

protected:
    SecurityKey(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool deinit(void) override;

private:
    static SecurityKey *_instance;

    /* 手动激活开关 → arbiter → session (app_security_key.cpp) */
    static void switch_event_cb(lv_event_t *e);
    void apply_switch(bool on);

    /* CTAP 请求 (hal 回调, TinyUSB 上下文) → sk_popup_show → confirm */
    static void on_ctap_msg(uint32_t cid, const uint8_t *msg, size_t len, void *user);
    static void popup_confirm_cb(bool allow, void *user_data);
    static void origin_from_msg(char *buf, size_t cap, const uint8_t *msg, size_t len);

    lv_obj_t *_switch = nullptr;
    lv_obj_t *_status = nullptr;
    sk_session_t _session {};
    uint32_t _pending_cid = 0;
    char _origin[96] = { 0 };
};

} // namespace esp_brookesia::apps

/* app_security_key.cpp — Brookesia App 子类 ([硬件相关·需真机验证] 的注册/arbiter/锁;
 * core/ui 状态机与弹窗本身是 [纯逻辑·可仿真])
 *
 * 注册模式参考 components/brookesia_app_calculator (esp_brookesia phone App).
 * 功能:
 *   - APP 屏: "手动激活" lv_switch + 状态显示;
 *   - 开关 ON: 前台检查 + usb_arb_request(FIDO2) (红线③ USB 互斥) → 成功
 *     sk_session_activate + hal_usb_ctap_attach; 失败回弹 OFF;
 *   - BadUSB 抢占 (usb_arbiter 状态回调, 本 APP 失去 USB) → sk_session_preempt
 *     (挂起回 Idle) + hal_usb_ctap_detach;
 *   - CTAP 请求 (hal 回调, TinyUSB 任务上下文) → LvLockGuard 内 sk_popup_show
 *     (红线①: lv_layer_top 全局弹窗) → 触摸结果回 sk_session_confirm.
 *
 * 红线②: 本 APP 全链路纯软件, 无任何 eFuse 操作.
 */
#include "esp_brookesia_app_security_key.hpp"

/* assets/img_app_security_key.c — launcher 图标占位 */
LV_IMG_DECLARE(img_app_security_key);

#include <atomic>
#include <cinttypes>

#include "esp_lib_utils.h"
#include "usb_arbiter.h"

#include "sk_session.h"
#include "sk_popup.h"
#include "hal_sk.h"

namespace esp_brookesia::apps
{

/* ---------------- 单例 (calculator 模式) ---------------- */
SecurityKey *SecurityKey::_instance = nullptr;

SecurityKey &SecurityKey::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (!_instance) {
        _instance = new SecurityKey(use_status_bar, use_navigation_bar);
    }
    return *_instance;
}

SecurityKey::SecurityKey(bool use_status_bar, bool use_navigation_bar)
    : systems::phone::App("SecurityKey", &img_app_security_key, true, use_status_bar, use_navigation_bar) {}

SecurityKey::~SecurityKey()
{
    _instance = nullptr;
}

bool SecurityKey::init(void)
{
    _session.state = SK_ST_IDLE;
    _session.cfg.backend = hal_c6_backend();
    sk_session_init(&_session, &_session.cfg);

    hal_c6_crypto_init();              /* C6 通道 ([硬件相关·需真机验证]) */
    hal_usb_ctap_set_msg_cb(on_ctap_msg, this);

    /* BadUSB 抢占监控: 真机由 usb_arbiter 状态切换回调驱动 (hal 层),
     * 此处订阅式检查 (APP 前台轮询 + 事件回调均可, lead 集成时选一). */
    usb_arb_init(nullptr);
    return true;
}

bool SecurityKey::deinit(void)
{
    sk_popup_hide();
    hal_usb_ctap_detach();
    usb_arb_release(USB_ARB_OWNER_FIDO2);
    return true;
}

bool SecurityKey::run(void)   /* 每次进入 APP 屏构建 UI */
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "安全密钥 (FIDO2)");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    /* 手动激活开关 */
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, lv_pct(100), 64);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "手动激活 (独占 USB)");
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(sw, switch_event_cb, LV_EVENT_VALUE_CHANGED, this);
    _switch = sw;

    _status = lv_label_create(scr);
    lv_obj_align(_status, LV_ALIGN_TOP_MID, 0, 160);
    lv_label_set_text(_status, "USB: 未激活");
    return true;
}

bool SecurityKey::back(void)
{
    return false; /* 默认行为: 关屏回桌面 */
}

bool SecurityKey::close(void)
{
    /* 离开前台: 挂起语义 — 主动关闭激活 (释放 USB) */
    if (usb_arb_owner_get() == USB_ARB_OWNER_FIDO2) {
        usb_arb_release(USB_ARB_OWNER_FIDO2);
        hal_usb_ctap_detach();
        sk_session_deactivate(&_session);
        sk_popup_hide();
    }
    return true;
}

/* ---------------- 开关 → arbiter → session ---------------- */
void SecurityKey::switch_event_cb(lv_event_t *e)
{
    SecurityKey *self = static_cast<SecurityKey *>(lv_event_get_user_data(e));
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    self->apply_switch(on);
}

void SecurityKey::apply_switch(bool on)
{
    if (on) {
        /* 手动激活门禁: APP 必须前台 (本回调只可能在 APP 屏触发) +
         * arbiter 授予 USB (BadUSB 持有则 BUSY) */
        usb_arb_result_t r = usb_arb_request(USB_ARB_OWNER_FIDO2);
        if (r != USB_ARB_OK) {
            if (_switch) lv_obj_remove_state(_switch, LV_STATE_CHECKED);
            if (_status) lv_label_set_text(_status, "USB 被占用 (BadUSB?)");
            return;
        }
        hal_usb_ctap_attach();                    /* [硬件相关] TinyUSB 挂载 */
        sk_session_activate(&_session);
        if (_status) lv_label_set_text(_status, "USB: 已激活 (等待电脑请求)");
    } else {
        hal_usb_ctap_detach();
        sk_session_deactivate(&_session);
        sk_popup_hide();
        usb_arb_release(USB_ARB_OWNER_FIDO2);
        if (_status) lv_label_set_text(_status, "USB: 未激活");
    }
}

/* ---------------- CTAP 请求 → 弹窗 → session ---------------- */
/* hal 回调: TinyUSB 任务上下文, 不持 LVGL 锁 — UI 调用前包 LvLockGuard
 * (锁在注册层, 不进 ui/). */
void SecurityKey::on_ctap_msg(uint32_t cid, const uint8_t *msg, size_t len, void *user)
{
    SecurityKey *self = static_cast<SecurityKey *>(user);
    if (!self || !msg || len == 0) return;

    self->_pending_cid = cid;
    uint8_t st = sk_session_on_request(&self->_session, cid, msg, len);
    if (st != Ctap2_Sw_UserActionPending) {
        /* 未激活/参数错等: 直接回错误状态帧 */
        uint8_t resp = st;
        hal_usb_ctap_send(cid, &resp, 1);
        return;
    }

    /* 红线①: 全局弹窗 (lv_layer_top). TODO(lead#7): 真机在此处加
     * esp_brookesia::gui::LvLockGuard 锁包装 (hal/APP 注册层持锁,
     * core 请求入口为 TinyUSB 任务). */
    origin_from_msg(self->_origin, sizeof(self->_origin), msg, len);
    sk_popup_show(self->_origin, popup_confirm_cb, self);
}

void SecurityKey::popup_confirm_cb(bool allow, void *user_data)
{
    SecurityKey *self = static_cast<SecurityKey *>(user_data);
    uint8_t st = sk_session_confirm(&self->_session, allow);
    /* 应答状态码经 CTAP-HID 回电脑 (真实 CBOR 应答组装 TODO(lead#8)) */
    uint8_t resp = st;
    hal_usb_ctap_send(self->_pending_cid, &resp, 1);
}

/* 从 MakeCredential/GetAssertion CBOR 里抽 rpId 做弹窗文案 (失败给通用文案) */
void SecurityKey::origin_from_msg(char *buf, size_t cap, const uint8_t *msg, size_t len)
{
    ctap2_getassert_req_t ga;
    if (msg[0] == CTAP2_CMD_GET_ASSERTION &&
        ctap2_getassert_req_decode(msg + 1, len - 1, &ga) == 0 && ga.rp_id) {
        snprintf(buf, cap, "%s", ga.rp_id);
        return;
    }
    ctap2_makecred_req_t mc;
    if (msg[0] == CTAP2_CMD_MAKE_CREDENTIAL &&
        ctap2_makecred_req_decode(msg + 1, len - 1, &mc) == 0 && mc.rp_id) {
        snprintf(buf, cap, "%s", mc.rp_id);
        return;
    }
    snprintf(buf, cap, "网站");
}

// The app is a singleton owned by itself; the registry must not delete it.
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, SecurityKey, "SecurityKey", []()
{
    return std::shared_ptr<SecurityKey>(&SecurityKey::requestInstance(), [](SecurityKey *) {});
})

} // namespace esp_brookesia::apps

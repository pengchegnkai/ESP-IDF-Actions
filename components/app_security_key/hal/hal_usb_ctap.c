/* hal_usb_ctap.c — TinyUSB CTAP over HID 传输 ([硬件相关·需真机验证])
 *
 * CTAP-HID (FIDO HID spec, bcdHID 0x0120) 设备描述符 + CTAP2 字节流 ↔
 * core/ctap2 的胶水. 借鉴 pico-fido 的 tinyusb fido HID 传输层.
 *
 * ===================== Kconfig 项 (lead 统一加到 sdkconfig/Kconfig) =====================
 * CONFIG_TINYUSB_FIDO_ENABLED=y          # 工程若有 tinyusb Kconfig 开关则打开
 * CONFIG_TINYUSB_FIDO_RHPORT=0           # 高速端口 0 (P4 内置 OTG HS)
 * # TinyUSB 已由板级/usb 设备栈引入时, 本组件 REQUIRES 不重复声明, lead 合并
 * ================================================================================
 *
 * TODO(lead#5) TinyUSB 设备栈集成: 工厂 Brookesia 基座里 TinyUSB 的初始化
 *   归属 (usb_arbiter hal 侧统一 deinit/reinit) 需要与 BadUSB 的 usb 栈
 *   互斥切换 — usb_arbiter 状态切换回调里只允许一方 attach.
 * TODO(lead#6) tusb_config.h: 需加 CFG_TUD_FIDO (自定义 class) 或复用
 *   HID generic in/out endpoint, report 64B.
 * ================================================================================
 */
#include "hal_sk.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "ctap2.h"
#include "usb_arbiter.h"

/* TODO(lead#5/6): 真机启用 tusb.h; 编译依赖见 Kconfig 注释
#include "tusb.h" */

static const char *TAG = "sk_usb";

/* ---------------- CTAP-HID 描述符 (bcd 0x0120, report 64B) ----------------
 * Usage Page = FIDO Alliance (0xFF1C), Usage = 0x01; Input/Output report
 * 均 65B (report id 1 + 64B data). */
static const uint8_t s_fido_report_desc[] __attribute__((unused)) = {
    0x06, 0xD0, 0xF1, /* Usage Page (FIDO Alliance) */
    0x09, 0x01,       /* Usage (FIDO CTAP HID) */
    0xA1, 0x01,       /* Collection (Application) */
    0x09, 0x20,       /*   Usage (Input Report Data) */
    0x15, 0x00,       /*   Logical Min 0 */
    0x26, 0xFF, 0x00, /*   Logical Max 255 */
    0x75, 0x08,       /*   Report Size 8 */
    0x95, 0x40,       /*   Report Count 64 */
    0x81, 0x02,       /*   Input (Data,Var,Abs) */
    0x09, 0x21,       /*   Usage (Output Report Data) */
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40,
    0x91, 0x02,       /*   Output (Data,Var,Abs) */
    0xC0,             /* End Collection */
};

/* TODO(lead#5): 挂到 TinyUSB descriptor 组 (tud_descriptor_... 回调体系),
 * bcdHID = 0x0120 (CTAP-HID 规范要求), 接口 class = HID. */

/* ---------------- 状态与胶水 ---------------- */
typedef struct {
    bool attached;
    hal_usb_ctap_msg_cb_t cb;
    void *user;
    ctap2_demux_t demux; /* CTAP-HID 分包器 ([纯逻辑] core 复用) */
} sk_usb_t;

static sk_usb_t s_usb;

/* TinyUSB 收到 64B report 的回调 (TODO(lead#5): 接到 tud_mount/回调体系):
 * feed 进 demux → 完整 CTAP2 消息 → 通知 app 层. */
static void usb_ctap_report_in(const uint8_t report[CTAPHID_REPORT_SZ])
{
    ctap2_msg_t msg;
    int rc = ctap2_demux_push(&s_usb.demux, report, CTAPHID_REPORT_SZ, &msg);
    if (rc == 1 && s_usb.cb) {
        s_usb.cb(msg.cid, msg.payload, msg.len, s_usb.user);
    } else if (rc < 0) {
        ESP_LOGW(TAG, "CTAP-HID frame error, resynced");
        ctap2_demux_init(&s_usb.demux);
    }
}

int hal_usb_ctap_attach(void)
{
    /* 红线③: 只有 arbiter 判定 FIDO2 持有 USB 时才允许挂载 */
    if (usb_arb_owner_get() != USB_ARB_OWNER_FIDO2) {
        ESP_LOGE(TAG, "attach rejected: arbiter not FIDO2");
        return -1;
    }
    if (s_usb.attached) return 0;
    /* TODO(lead#5): tusb 初始化 (FIDO interface) + 启动收发任务 */
    ctap2_demux_init(&s_usb.demux);
    s_usb.attached = true;
    return 0;
}

void hal_usb_ctap_detach(void)
{
    if (!s_usb.attached) return;
    /* TODO(lead#5): TinyUSB deinit / suspend endpoint, 让位 BadUSB */
    s_usb.attached = false;
}

int hal_usb_ctap_set_msg_cb(hal_usb_ctap_msg_cb_t cb, void *user)
{
    s_usb.cb = cb;
    s_usb.user = user;
    return 0;
}

int hal_usb_ctap_send(uint32_t cid, const uint8_t *msg, size_t len)
{
    if (!s_usb.attached) return -1;
    uint8_t framed[CTAPHID_MSG_MAX + CTAPHID_REPORT_SZ];
    int n = ctap2_frame_msg(cid, msg[0], msg, len, framed, sizeof(framed));
    if (n <= 0) return -1;
    /* TODO(lead#5): 按 64B report 逐个 tud_hid_n_report / endpoint 写出 */
    (void)framed;
    return 0;
}

/* 让测试可见的内部钩子 (仅编译验证用; 真机由 TinyUSB 回调驱动) */
__attribute__((unused)) static void usb_ctap_test_hook(const uint8_t *r)
{
    usb_ctap_report_in(r);
}

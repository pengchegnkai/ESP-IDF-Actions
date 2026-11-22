/* app_hackerkit.cpp — Brookesia App 子类实现 (App_HackerKit, 精简版 BadUSB)
 *
 * 执行流程:
 *   UI Run 按钮 → exec_payload():
 *     1. usb_arb_request(USB_ARB_OWNER_BADUSB) — 失败(BUSY)则 UI 提示 FIDO2 占用
 *     2. 成功 → 后台 FreeRTOS 任务: 读 payload → hk_script_parse →
 *        hk_executor_run (持锁期间) → usb_arb_release
 *
 * 线程安全说明:
 *   - usb_arb_request/release 内部为纯状态机; 真机上 APP 层 (此处) 用
 *     FreeRTOS 互斥锁 (或仅在 UI 任务发起请求) 保证 request→release
 *     的序列化, hal 侧 TinyUSB 收发由 esp_tinyusb 内部任务串行化.
 *   - 真机 UI 更新必须持 brookesia LvLock (hal 集成点), pc_sim 侧无锁.
 *   - hal_usb_hid_init/deinit 由 usb_arbiter 状态切换回调触发 (hal 集成点).
 */
#include "app_hackerkit.hpp"

#include <atomic>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lib_utils.h"
#include "usb_arbiter.h"

#include "hk_executor.h"
#include "hk_ui.h"
#include "hal_fs.h"
#include "hal_usb_hid.h"

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(img_app_hackerkit);

#define APP_NAME "HackerKit"
#define HK_PAYLOADS_DIR "/sdcard/payloads"
#define HK_PAYLOAD_BUF_LEN 4096
#define HK_EXEC_TASK_STACK 8192
#define HK_EXEC_TASK_PRIO  5

namespace esp_brookesia::apps
{

HackerKit *HackerKit::_instance = nullptr;

HackerKit *HackerKit::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new HackerKit(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

HackerKit::HackerKit(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_hackerkit, true, use_status_bar, use_navigation_bar)
{
}

HackerKit::~HackerKit()
{
    ESP_UTILS_LOGD("Destroy(@0x%p)", this);
}

bool HackerKit::init(void)
{
    ESP_UTILS_LOGD("Init(@0x%p)", this);
    /* 真机: hal 侧填充 backend (pc_sim 由 stubs 填充) */
    return true;
}

bool HackerKit::deinit(void)
{
    ESP_UTILS_LOGD("Deinit(@0x%p)", this);
    return true;
}

/* 后台执行任务: 持 arb 锁期间解析+执行 payload, 结束 release */
void HackerKit::exec_task(void *arg)
{
    char *payload_name = static_cast<char *>(arg);
    char *buf = static_cast<char *>(malloc(HK_PAYLOAD_BUF_LEN));
    hk_fs_ops_t fs = {};
    hk_usb_ops_t usb = {};

    if (buf == nullptr) {
        goto out;
    }
    hal_fs_get_ops(&fs);
    hal_usb_hid_get_ops(&usb);

    do {
        char path[HK_PAYLOAD_NAME_MAX + sizeof(HK_PAYLOADS_DIR) + 2];
        snprintf(path, sizeof(path), HK_PAYLOADS_DIR "/%s", payload_name);

        /* 线程安全: request 已在 exec_payload 中成功持有, 此处仅使用 */
        if (fs.read_payload && fs.read_payload(path, buf, HK_PAYLOAD_BUF_LEN) < 0) {
            ESP_UTILS_LOGE("read payload %s failed", path);
            break;
        }

        hk_event_t events[64];
        int count = hk_script_parse(buf, events, sizeof(events) / sizeof(events[0]));
        if (count < 0) {
            ESP_UTILS_LOGE("payload %s parse error at line %d", payload_name, -count);
            break;
        }

        /* delay_fn: 真机传 vTaskDelay 包装 */
        hk_exec_result_t rc = hk_executor_run(
            events, count, &usb,
            [](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); },
            nullptr);
        ESP_UTILS_LOGI("payload %s exec rc=%d", payload_name, (int)rc);
    } while (0);

    free(buf);
out:
    free(payload_name);
    /* 线程安全: 仅本任务持有 BADUSB, release 幂等且串行 */
    usb_arb_release(USB_ARB_OWNER_BADUSB);
    vTaskDelete(nullptr);
}

/* 执行入口 (UI Run 按钮回调, LVGL 任务上下文) */
int HackerKit::exec_payload(const char *payload_name)
{
    /* 线程安全: UI 任务发起 request, 后台任务 release, 状态机自身原子;
     * 若需严格序列化可在此包 FreeRTOS 互斥锁 (hal 集成点). */
    if (usb_arb_request(USB_ARB_OWNER_BADUSB) != USB_ARB_OK) {
        ESP_UTILS_LOGW("USB busy: FIDO2 holds the arbiter");
        hk_ui_set_status(HK_UI_STATUS_BUSY); /* UI 提示 FIDO2 占用 */
        return -1;
    }

    char *name_dup = strdup(payload_name);
    if (name_dup == nullptr) {
        usb_arb_release(USB_ARB_OWNER_BADUSB);
        return -1;
    }
    if (xTaskCreate(exec_task, "hk_exec", HK_EXEC_TASK_STACK, name_dup,
                    HK_EXEC_TASK_PRIO, nullptr) != pdPASS) {
        free(name_dup);
        usb_arb_release(USB_ARB_OWNER_BADUSB);
        return -1;
    }
    return 0;
}

bool HackerKit::run(void)
{
    ESP_UTILS_LOGD("Run(@0x%p)", this);
    /* hal backend 已由 init/仲裁回调就绪; UI 拿接口表 */
    hk_fs_ops_t fs = {};
    hk_usb_ops_t usb = {};
    hal_fs_get_ops(&fs);
    hal_usb_hid_get_ops(&usb);

    hk_ui_set_executor(exec_payload);

    /* 容器铺满 APP 可视区 (参考 calculator 模式), UI 创建进容器 */
    auto visual_area = this->getVisualArea();
    lv_obj_t *container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(container, lv_area_get_width(&visual_area),
                    lv_area_get_height(&visual_area));
    lv_obj_center(container);
    hk_ui_create(container, &fs, &usb, nullptr);
    return true;
}

bool HackerKit::back(void)
{
    return true;
}

bool HackerKit::close(void)
{
    /* 兜底: APP 关闭时若仍持有 USB 则释放 */
    usb_arb_release(USB_ARB_OWNER_BADUSB);
    return true;
}

/* 单例自持有; 注册表不得删除 */
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, HackerKit, APP_NAME, []()
{
    return std::shared_ptr<HackerKit>(HackerKit::requestInstance(), [](HackerKit *) {});
})

} // namespace esp_brookesia::apps

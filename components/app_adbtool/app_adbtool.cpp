/* app_adbtool.cpp — Brookesia App 子类 ([硬件相关·需真机验证]: USB host ADB;
 * ui/ 状态机与命令行界面本身是 [纯逻辑·可仿真])
 *
 * 注册模式参考 components/app_security_key/app_security_key.cpp (esp_brookesia phone App).
 * 流程:
 *   - init(): adb_client_init (USB host 栈 + NVS RSA 密钥) → adb_client_start
 *     (连接手机 → 授权 → 就绪, [硬件相关]) → adb_client_set_line_cb(输出送 UI);
 *   - run(): adb_ui_create 构建命令行界面; 周期 timer 轮询 adb_client_ready
 *     刷新状态行 (未连接/授权中/已就绪);
 *   - 发送: timer 轮询输入框 → adb_client_shell(输入) → 输出行经 line cb 回 UI;
 *   - close(): 停 timer; deinit(): adb_client_stop.
 *
 * 线程注意: line cb 来自 adb host 任务, 简单起见直接调 adb_ui_append_output
 * (TODO(lead): 真机在注册层包 esp_brookesia::gui::LvLockGuard, LvLock 不进 ui/).
 */
#include "app_adbtool.hpp"

/* assets/img_app_adbtool.c — launcher 图标 (1x1 占位) */
LV_IMG_DECLARE(img_app_adbtool);

#include "esp_lib_utils.h"

#include "adb_client.h"
#include "adb_ui.h"

namespace esp_brookesia::apps
{

/* ---------------- 单例 (calculator/security_key 模式) ---------------- */
AdbTool *AdbTool::_instance = nullptr;

AdbTool &AdbTool::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (!_instance) {
        _instance = new AdbTool(use_status_bar, use_navigation_bar);
    }
    return *_instance;
}

AdbTool::AdbTool(bool use_status_bar, bool use_navigation_bar)
    : App("ADB 工具", &img_app_adbtool, true, use_status_bar, use_navigation_bar) {}

AdbTool::~AdbTool()
{
    _instance = nullptr;
}

bool AdbTool::init(void)
{
    /* [硬件相关·需真机验证] USB host 栈 + RSA 密钥 NVS */
    adb_client_init();
    adb_client_set_line_cb(on_adb_line, this);
    adb_client_start();
    return true;
}

bool AdbTool::deinit(void)
{
    adb_client_stop();
    return true;
}

bool AdbTool::run(void)   /* 每次进入 APP 屏构建 UI */
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    adb_ui_set_actions(on_connect, on_disconnect, this);
    adb_ui_create(scr);
    adb_ui_set_status(adb_client_ready() ? "已就绪" : "未连接");

    /* 周期轮询: 状态行 + 发送输入 (100ms) */
    if (!_timer) _timer = lv_timer_create(status_poll, 100, this);
    else _timer->user_data = this;
    return true;
}

bool AdbTool::back(void)
{
    return false; /* 默认行为: 关屏回桌面 */
}

bool AdbTool::close(void)
{
    if (_timer) {
        lv_timer_del(_timer);
        _timer = nullptr;
    }
    return true;
}

/* ---------------- adb core 输出 → UI ---------------- */
void AdbTool::on_adb_line(const char *line, void *user)
{
    (void)user;
    /* adb host 任务上下文: 直接调用 (TODO: 真机在注册层包 LvLockGuard) */
    adb_ui_append_output(line);
}

/* ---------------- UI 按钮 → adb core ---------------- */
void AdbTool::on_connect(void *user)
{
    (void)user;
    adb_ui_set_status("授权中");
    if (adb_client_start() != 0) {
        adb_ui_set_status("启动失败 (USB 被占用?)");
        adb_ui_append_output("[!] adb_client_start 失败");
    }
}

void AdbTool::on_disconnect(void *user)
{
    (void)user;
    adb_client_stop();
    adb_ui_set_status("未连接");
    adb_ui_append_output("[i] 已断开");
}

/* ---------------- 周期轮询 ---------------- */
void AdbTool::status_poll(lv_timer_t *t)
{
    AdbTool *self = static_cast<AdbTool *>(t->user_data);
    (void)self;

    /* 状态行: ready 轮询 */
    adb_ui_set_status(adb_client_ready() ? "已就绪" : "未连接");

    /* 发送: 输入框非空 → shell; 就绪前提示 */
    const char *in = adb_ui_get_input();
    if (in && in[0] != '\0') {
        if (adb_client_ready()) {
            adb_client_shell(in);
        } else {
            adb_ui_append_output("[!] 未连接, 无法执行命令");
        }
        adb_ui_clear_input();
    }
}

// The app is a singleton owned by itself; the registry must not delete it.
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AdbTool, "ADB 工具", []()
{
    return std::shared_ptr<AdbTool>(&AdbTool::requestInstance(), [](AdbTool *) {});
})

} // namespace esp_brookesia::apps

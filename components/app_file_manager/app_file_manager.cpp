/* app_file_manager.cpp — Brookesia APP 注册层 (文件与 Payload 管理)
 * [硬件相关·需真机验证] 依赖 brookesia_core (App 基类 / LvLockGuard) 与
 * hal 层的 SDMMC 挂载, 仅进固件构建 (pc_sim 不编译本文件).
 *
 * 模式参考 components/brookesia_app_calculator (ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR,
 * 生命周期 init/run/back/close/deinit). UI 创建被 LvLockGuard 包裹 (LVGL 线程安全红线).
 * 借鉴 (参考 espressif/esp-ui 文件管理器的目录遍历与分页思路).
 */
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "esp_lib_utils.h"

#include "fm_backend.h"
#include "fm_ui.h"

/* hal_sdcard.c 中实现 (真机后端: board_sdcard_mount + POSIX VFS) */
extern "C" int fm_backend_hal_init(fm_backend_t *be);

/* assets/img_app_file_manager.c — launcher 图标占位 */
LV_IMG_DECLARE(img_app_file_manager);

namespace esp_brookesia::apps {

class FileManager : public systems::phone::App
{
public:
    static FileManager *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~FileManager();

protected:
    FileManager(bool use_status_bar, bool use_navigation_bar);

    bool init(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool deinit(void) override;

private:
    static FileManager *_instance;
    fm_backend_t _backend;
    bool _backend_ok;
};

FileManager *FileManager::_instance = nullptr;

FileManager *FileManager::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new FileManager(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

FileManager::FileManager(bool use_status_bar, bool use_navigation_bar) :
    // 等价于 App(name, ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(nullptr, use_status_bar,
    // use_navigation_bar)) 的简化构造形式 (与 calculator 相同的 5 参基础构造)
    App("FileManager", &img_app_file_manager, true, use_status_bar, use_navigation_bar),
    _backend_ok(false)
{
}

FileManager::~FileManager()
{
    ESP_UTILS_LOGD("Destroy(@0x%p)", this);
}

bool FileManager::init(void)
{
    ESP_UTILS_LOGD("Init(@0x%p)", this);
    _backend_ok = (fm_backend_hal_init(&_backend) == 0);
    if (!_backend_ok) {
        ESP_UTILS_LOGE("SD card backend init failed (mount?)");
    }
    return true;
}

bool FileManager::run(void)
{
    ESP_UTILS_LOGD("Run(@0x%p)", this);

    lv_obj_t *screen = lv_screen_active();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");

    // LVGL 线程安全红线: UI 创建/销毁必须持锁 (CONVENTIONS.md)
    esp_brookesia::gui::LvLockGuard gui_guard;
    lv_obj_clean(screen);
    fm_ui_create(screen, _backend_ok ? &_backend : nullptr);
    return true;
}

bool FileManager::back(void)
{
    ESP_UTILS_LOGD("Back(@0x%p)", this);
    return true;
}

bool FileManager::close(void)
{
    ESP_UTILS_LOGD("Close(@0x%p)", this);
    return true;
}

bool FileManager::deinit(void)
{
    ESP_UTILS_LOGD("Deinit(@0x%p)", this);
    return true;
}

// The app is a singleton owned by itself; the registry must not delete it.
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, FileManager, "FileManager", []()
{
    return std::shared_ptr<FileManager>(FileManager::requestInstance(), [](FileManager *) {});
})

} // namespace esp_brookesia::apps

/**
 * @file sys_ui_font.c
 * @brief 中文字库加载与 i18n 字符串表
 *
 * 规则 (系统约定):
 *   - 启动时扫描 SD 卡 <挂载点>/fonts/ 目录下的第一个 .ttf 文件
 *   - 找到  → 读入 PSRAM, 用 LVGL tiny_ttf 按需渲染, 界面语言 = 中文
 *   - 找不到 → 回退 LVGL 内置字体 (Montserrat), 界面语言 = 英文
 *
 * 字库文件要求: 标准 TrueType (glyf 轮廓), 含 CJK 字形,
 * 如 Noto Sans SC / 霞鹜文楷 等。目录约定: SD 卡根下 fonts 目录中的第一个 ttf 文件
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "sys_ui/sys_ui.h"
#include "sys_hal/sys_hal.h"

/* tiny_ttf 是 LVGL 可选库, 主 lvgl.h 不自动包含;
   组件 include 根布局可能是 "src" 或仓库根, 级联兼容 */
#if __has_include("libs/tiny_ttf/lv_tiny_ttf.h")
#include "libs/tiny_ttf/lv_tiny_ttf.h"
#elif __has_include("src/libs/tiny_ttf/lv_tiny_ttf.h")
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#elif __has_include("lv_tiny_ttf.h")
#include "lv_tiny_ttf.h"
#else
#error "lv_tiny_ttf.h not found in lvgl include path"
#endif

static const char *TAG = "ui_font";

/* 字库原始数据常驻 PSRAM (tiny_ttf 通过它按需读字形) */
static bool     s_font_ready;
static uint8_t *s_ttf_buf;
static size_t   s_ttf_size;

/* 按字号缓存字体对象 (不同控件用不同字号, 共用同一数据) */
#define FONT_CACHE_SLOTS 8
static lv_font_t *s_fonts[FONT_CACHE_SLOTS];
static uint32_t   s_font_px[FONT_CACHE_SLOTS];

#define TTF_MAX_SIZE (32U * 1024U * 1024U)

/* ========================= i18n 字符串表 ========================= */
/* 中文表用纯文字 (TTF 不含 LVGL 符号字形); 英文表带 LVGL 符号 (内置字体支持) */
static const char *const s_str_zh[SYS_UI_STR_COUNT] = {
    [SYS_UI_STR_APP_LAUNCHER]      = "桌面",
    [SYS_UI_STR_APP_SETTINGS]      = "设置",
    [SYS_UI_STR_SYSTEM_BASE_TITLE] = "系统基座",
    [SYS_UI_STR_LAST_EVENT]        = "最近事件",
    [SYS_UI_STR_BTN_DEMO]          = "弹窗演示",
    [SYS_UI_STR_OVERLAY_TITLE]     = "安全警告",
    [SYS_UI_STR_OVERLAY_BODY]      = "这是一个系统级弹窗\n永远显示在所有 APP 之上",
    [SYS_UI_STR_OVERLAY_OK]        = "知道了",
    [SYS_UI_STR_BTN_BACK]          = "返回",
    [SYS_UI_STR_USB_IN]            = "USB: 已插入",
    [SYS_UI_STR_USB_OUT]           = "USB: 已拔出",
    [SYS_UI_STR_SWITCH_LABEL]      = "演示开关",
    [SYS_UI_STR_SPLASH_SUBTITLE]   = "系统基座",
    [SYS_UI_STR_SPLASH_STARTING]   = "正在启动...",
    [SYS_UI_STR_SET_BRIGHTNESS]    = "亮度",
    [SYS_UI_STR_SET_VOLUME]        = "音量",
    [SYS_UI_STR_SET_SFX]           = "按键音效",
    [SYS_UI_STR_SET_INFO]          = "系统信息",
    [SYS_UI_STR_SET_PSRAM]         = "内存",
    [SYS_UI_STR_SET_UPTIME]        = "运行时间",
    [SYS_UI_STR_SET_LANG]          = "语言",
    [SYS_UI_STR_SET_FONT]         = "字库",
    [SYS_UI_STR_APP_NET]          = "网络",
    [SYS_UI_STR_NET_WIFI]         = "WiFi",
    [SYS_UI_STR_NET_WIFI_OFF]     = "未连接",
    [SYS_UI_STR_NET_WIFI_CONNECTING] = "连接中...",
    [SYS_UI_STR_NET_WIFI_CONNECTED]  = "已连接: %s",
    [SYS_UI_STR_NET_WIFI_SCAN]    = "扫描",
    [SYS_UI_STR_NET_WIFI_SCANNING]   = "正在扫描...",
    [SYS_UI_STR_NET_WIFI_EMPTY]   = "未扫到热点",
    [SYS_UI_STR_NET_WIFI_OPEN]    = "开放",
    [SYS_UI_STR_NET_WIFI_LOCKED]  = "加密",
    [SYS_UI_STR_NET_WIFI_PASSWORD] = "输入 WiFi 密码",
    [SYS_UI_STR_BTN_CONNECT]      = "连接",
    [SYS_UI_STR_BTN_CANCEL]       = "取消",
    [SYS_UI_STR_BTN_SYNC]         = "立即同步",
    [SYS_UI_STR_NET_ESPNOW]       = "ESP-NOW",
    [SYS_UI_STR_NET_ESPNOW_HINT]  = "2.4G 直连广播, 发现附近的同固件设备",
    [SYS_UI_STR_NET_ESPNOW_PEERS] = "邻居: %d",
    [SYS_UI_STR_NET_BT]           = "蓝牙",
    [SYS_UI_STR_NET_BT_UNSUPPORTED] = "本板无线芯片 (ESP32-C6) 不支持蓝牙",
    [SYS_UI_STR_NET_TIME]         = "时间同步",
    [SYS_UI_STR_NET_TIME_OK]      = "已同步",
    [SYS_UI_STR_NET_TIME_NOSYNC]  = "未同步",
    [SYS_UI_STR_LAUNCHER_NOSYNC]  = "未校时 · 显示运行时间",
    [SYS_UI_STR_QC_TITLE]         = "控制中心",
    [SYS_UI_STR_WD_MON]           = "周一",
    [SYS_UI_STR_WD_TUE]           = "周二",
    [SYS_UI_STR_WD_WED]           = "周三",
    [SYS_UI_STR_WD_THU]           = "周四",
    [SYS_UI_STR_WD_FRI]           = "周五",
    [SYS_UI_STR_WD_SAT]           = "周六",
    [SYS_UI_STR_WD_SUN]           = "周日",
};

static const char *const s_str_en[SYS_UI_STR_COUNT] = {
    [SYS_UI_STR_APP_LAUNCHER]      = "Launcher",
    [SYS_UI_STR_APP_SETTINGS]      = LV_SYMBOL_SETTINGS "  Settings",
    [SYS_UI_STR_SYSTEM_BASE_TITLE] = "System Base",
    [SYS_UI_STR_LAST_EVENT]        = "last event",
    [SYS_UI_STR_BTN_DEMO]          = LV_SYMBOL_WARNING " Demo Popup",
    [SYS_UI_STR_OVERLAY_TITLE]     = "Warning",
    [SYS_UI_STR_OVERLAY_BODY]      = "This is a system-level popup\nalways on top of all apps",
    [SYS_UI_STR_OVERLAY_OK]        = "OK",
    [SYS_UI_STR_BTN_BACK]          = LV_SYMBOL_LEFT "  Back",
    [SYS_UI_STR_USB_IN]            = "USB: connected",
    [SYS_UI_STR_USB_OUT]           = "USB: disconnected",
    [SYS_UI_STR_SWITCH_LABEL]      = "Demo switch",
    [SYS_UI_STR_SPLASH_SUBTITLE]   = "System Base",
    [SYS_UI_STR_SPLASH_STARTING]   = "Starting up...",
    [SYS_UI_STR_SET_BRIGHTNESS]    = "Brightness",
    [SYS_UI_STR_SET_VOLUME]        = "Volume",
    [SYS_UI_STR_SET_SFX]           = "Key sound",
    [SYS_UI_STR_SET_INFO]          = "System info",
    [SYS_UI_STR_SET_PSRAM]         = "Memory",
    [SYS_UI_STR_SET_UPTIME]        = "Uptime",
    [SYS_UI_STR_SET_LANG]          = "Language",
    [SYS_UI_STR_SET_FONT]         = "Font",
    [SYS_UI_STR_APP_NET]          = "Network",
    [SYS_UI_STR_NET_WIFI]         = "WiFi",
    [SYS_UI_STR_NET_WIFI_OFF]     = "Not connected",
    [SYS_UI_STR_NET_WIFI_CONNECTING] = "Connecting...",
    [SYS_UI_STR_NET_WIFI_CONNECTED]  = "Connected: %s",
    [SYS_UI_STR_NET_WIFI_SCAN]    = "Scan",
    [SYS_UI_STR_NET_WIFI_SCANNING]   = "Scanning...",
    [SYS_UI_STR_NET_WIFI_EMPTY]   = "No networks found",
    [SYS_UI_STR_NET_WIFI_OPEN]    = "Open",
    [SYS_UI_STR_NET_WIFI_LOCKED]  = "Encrypted",
    [SYS_UI_STR_NET_WIFI_PASSWORD] = "Enter WiFi password",
    [SYS_UI_STR_BTN_CONNECT]      = "Connect",
    [SYS_UI_STR_BTN_CANCEL]       = "Cancel",
    [SYS_UI_STR_BTN_SYNC]         = "Sync now",
    [SYS_UI_STR_NET_ESPNOW]       = "ESP-NOW",
    [SYS_UI_STR_NET_ESPNOW_HINT]  = "2.4 GHz direct link, finds nearby peers",
    [SYS_UI_STR_NET_ESPNOW_PEERS] = "Peers: %d",
    [SYS_UI_STR_NET_BT]           = "Bluetooth",
    [SYS_UI_STR_NET_BT_UNSUPPORTED] = "This board's radio (ESP32-C6) has no Bluetooth",
    [SYS_UI_STR_NET_TIME]         = "Time sync",
    [SYS_UI_STR_NET_TIME_OK]      = "Synced",
    [SYS_UI_STR_NET_TIME_NOSYNC]  = "Not synced",
    [SYS_UI_STR_LAUNCHER_NOSYNC]  = "Not synced, showing uptime",
    [SYS_UI_STR_QC_TITLE]         = "Control Center",
    [SYS_UI_STR_WD_MON]           = "Mon",
    [SYS_UI_STR_WD_TUE]           = "Tue",
    [SYS_UI_STR_WD_WED]           = "Wed",
    [SYS_UI_STR_WD_THU]           = "Thu",
    [SYS_UI_STR_WD_FRI]           = "Fri",
    [SYS_UI_STR_WD_SAT]           = "Sat",
    [SYS_UI_STR_WD_SUN]           = "Sun",
};

sys_ui_lang_t sys_ui_lang(void)
{
    return s_font_ready ? SYS_UI_LANG_ZH : SYS_UI_LANG_EN;
}

const char *sys_ui_str(sys_ui_str_id_t id)
{
    if (id >= SYS_UI_STR_COUNT) {
        return "?";
    }
    return s_font_ready ? s_str_zh[id] : s_str_en[id];
}

lv_font_t *sys_ui_cjk_font(uint32_t px)
{
    if (!s_font_ready) {
        return NULL;
    }
    for (size_t i = 0; i < FONT_CACHE_SLOTS; i++) {
        if (s_font_px[i] == px) {
            return s_fonts[i];
        }
        if (!s_fonts[i] && s_font_px[i] == 0) {
            s_fonts[i] = lv_tiny_ttf_create_data(s_ttf_buf, s_ttf_size, (int32_t)px);
            s_font_px[i] = s_fonts[i] ? px : UINT32_MAX; /* 占位防重试 */
            if (s_fonts[i]) {
                ESP_LOGI(TAG, "CJK font created: %u px", (unsigned)px);
            } else {
                ESP_LOGE(TAG, "lv_tiny_ttf_create_data failed at %u px", (unsigned)px);
            }
            return s_fonts[i];
        }
    }
    return NULL; /* 缓存已满 */
}

/* ========================= 字库加载 ========================= */
static bool name_is_ttf(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".ttf") == 0;
}

esp_err_t sys_ui_font_init(void)
{
    /* 1. 扫描 <挂载点>/fonts/ 下第一个 .ttf */
    char dir[96];
    snprintf(dir, sizeof(dir), "%s/fonts", sys_hal_sd_mount_point());

    char path[384] = {0};  /* dir(96) + '/' + d_name(<=255) + NUL = 353 上限 */
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.' || !name_is_ttf(e->d_name)) {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            break;
        }
        closedir(d);
    }
    if (path[0] == '\0') {
        ESP_LOGI(TAG, "no TTF found in %s, English UI", dir);
        return ESP_ERR_NOT_FOUND;
    }

    /* 2. 读入 PSRAM (SD 卡首读偶发瞬时错误, 重试最多 3 次) */
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "fopen failed: %s", path);
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long sz = ftell(f);
    fclose(f);
    if (sz <= 0 || (size_t)sz > TTF_MAX_SIZE) {
        ESP_LOGE(TAG, "bad font size %ld (limit %u MB)", sz, TTF_MAX_SIZE / (1024 * 1024));
        return ESP_ERR_INVALID_SIZE;
    }
    s_ttf_buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!s_ttf_buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %zu KB", sz / 1024);
        return ESP_ERR_NO_MEM;
    }
    size_t rd = 0;
    for (int try = 0; try < 3; try++) {
        f = fopen(path, "rb");
        if (!f) {
            ESP_LOGW(TAG, "fopen retry failed: %s", path);
            break;
        }
        fseek(f, 0, SEEK_SET);
        rd = fread(s_ttf_buf, 1, (size_t)sz, f);
        fclose(f);
        if (rd == (size_t)sz) {
            break;
        }
        ESP_LOGW(TAG, "fread short: %zu/%ld (try %d/3)", rd, sz, try + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (rd != (size_t)sz) {
        ESP_LOGE(TAG, "fread short after retries: %zu/%ld", rd, sz);
        heap_caps_free(s_ttf_buf);
        s_ttf_buf = NULL;
        return ESP_FAIL;
    }
    s_ttf_size = (size_t)sz;

    /* 3. 预建默认字号 (兼作 TTF 合法性验证), 结果存入缓存槽 0
     *    LVGL 字体对象创建走 lv_malloc, 必须持显示锁与 LVGL 渲染任务互斥
     *    (新启动顺序下渲染任务此时已在跑, 启动画面正在刷新) */
    if (!sys_ui_lock()) {
        heap_caps_free(s_ttf_buf);
        s_ttf_buf = NULL;
        return ESP_FAIL;
    }
    s_fonts[0] = lv_tiny_ttf_create_data(s_ttf_buf, s_ttf_size, SYS_UI_FONT_DEFAULT_PX);
    sys_ui_unlock();
    if (!s_fonts[0]) {
        ESP_LOGE(TAG, "invalid TTF: %s", path);
        heap_caps_free(s_ttf_buf);
        s_ttf_buf = NULL;
        return ESP_FAIL;
    }
    s_font_px[0] = SYS_UI_FONT_DEFAULT_PX;

    s_font_ready = true;
    ESP_LOGI(TAG, "CJK font loaded: %s (%u KB) -> Chinese UI", path, (unsigned)(sz / 1024));
    return ESP_OK;
}

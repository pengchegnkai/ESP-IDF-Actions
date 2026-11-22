/* fm_ui.h — 文件管理 APP 纯 LVGL UI
 * [纯逻辑·可仿真] ui 层: 只允许 lvgl.h + core/ 头 + 本组件头, 不触碰 ESP-IDF.
 * 借鉴 (参考 espressif/esp-ui 文件管理器的目录浏览/分页预览/编辑对话框思路).
 */
#ifndef FM_UI_H
#define FM_UI_H

#include "lvgl.h"
#include "fm_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 创建文件管理 UI 到 parent 上.
 * parent: 真机为 Brookesia APP 屏幕; 仿真为 harness 虚拟屏.
 * be:     后端接口 (真机 hal_sdcard / 仿真 fm_backend_host).
 * 起始目录: /sdcard (真机 TF 卡根).
 */
void fm_ui_create(lv_obj_t *parent, const fm_backend_t *be);

#ifdef __cplusplus
}
#endif

#endif /* FM_UI_H */

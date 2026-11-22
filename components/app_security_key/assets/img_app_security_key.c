/* [硬件相关·需真机验证] 启动器图标占位 (1x1 ARGB8888 纯色).
 * 仅作 App 注册占位, 后续替换为正式资源 (参考 assets/img_app_calculator.c 的生成方式). */
#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

const uint8_t img_app_security_key_map[4] = {
    0xFF, 0x40, 0x00, 0xFF /* ARGB8888 占位色 */
};

const lv_image_dsc_t img_app_security_key = {
    .header.cf = LV_COLOR_FORMAT_ARGB8888,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = 1,
    .header.h = 1,
    .data_size = 4,
    .data = img_app_security_key_map,
};

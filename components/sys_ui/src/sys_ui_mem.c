/**
 * @file sys_ui_mem.c
 * @brief LVGL 自定义内存后端 (LV_USE_CUSTOM_MALLOC) —— 全部 LVGL 内存走 PSRAM
 *
 * 背景 (两层事故, 同一根源):
 *   1. LVGL 内置分配器是静态 .bss 内存池 (LV_MEM_SIZE_KILOBYTES), 在
 *      ESP32-P4 上落在内部 DRAM: 512KB 的 .bss 直接吃掉内部 RAM
 *      (heap_init 全机只有 ~60KB 内部动态内存), 启动时 32K DMA 池
 *      保留失败 → abort 重启循环 (2026-11 实测: .dram0.bss 72KB→518KB)。
 *   2. 池开回 64KB 又不够中文字形缓存 (128 字形×~1.3KB), 渲染时
 *      stb 的 context malloc 失败 → stbtt__new_active assert。
 *
 * 方案: 实现 LVGL 的 _core 分配器接口, 全部 LVGL 分配走 IDF 堆:
 *   - 有 PSRAM 时 (P4: 32MB, 字库 17MB 后仍充足) 落 PSRAM;
 *   - 无 PSRAM 目标 (C6/S3) 自动退回内部堆。
 *   IDF 堆本身线程安全, LVGL 任务与 app_main 任务可并发分配。
 *
 * 接口清单来自 LVGL 9.3.0 src/stdlib/builtin/lv_mem_core_builtin.c
 * (该文件在 CUSTOM 模式下编译为空, 符号必须由本文件提供)。
 */
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "esp_psram.h"

/* 分配域: lv_mem_init 时探测 PSRAM, 之后所有分配统一用同一 caps
 * (混合域会让跨域 realloc 失败) */
static int s_caps = MALLOC_CAP_DEFAULT;

void lv_mem_init(void)
{
    s_caps = esp_psram_is_initialized() ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                        : MALLOC_CAP_DEFAULT;
}

void lv_mem_deinit(void)
{
}

void * lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, s_caps);
}

void * lv_realloc_core(void * p, size_t new_size)
{
    if (p == NULL) {
        return heap_caps_malloc(new_size, s_caps);
    }
    if (new_size == 0) {
        return NULL; /* lv_realloc 对 new_size==0 自行处理释放 */
    }
    return heap_caps_realloc(p, new_size, s_caps);
}

void lv_free_core(void * p)
{
    heap_caps_free(p);
}

/* 池管理: 单堆模式下没有独立的 "池" 概念, 占位实现 */
lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    (void)mem;
    (void)bytes;
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    (void)pool;
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    lv_memzero(mon_p, sizeof(*mon_p));
    mon_p->total_size        = heap_caps_get_total_size(s_caps);
    mon_p->free_size         = heap_caps_get_free_size(s_caps);
    mon_p->free_biggest_size = heap_caps_get_largest_free_block(s_caps);
    mon_p->used_pct = mon_p->total_size
                       ? 100 - mon_p->free_size * 100 / mon_p->total_size : 0;
    mon_p->frag_pct = mon_p->free_size
                       ? 100 - mon_p->free_biggest_size * 100 / mon_p->free_size : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

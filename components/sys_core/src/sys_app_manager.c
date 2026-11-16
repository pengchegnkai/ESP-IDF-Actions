/**
 * @file sys_app_manager.c
 * @brief APP 调度器实现 —— 注册表 + 前台/后台切换 + 返回栈
 *
 * 并发模型: 互斥锁保护全局状态。launch/back/stop 可能来自不同任务
 * (启动主任务 / LVGL 适配器任务的按钮回调 / LVGL timer 回调),
 * 整个切换过程持锁完成。on_start/on_suspend 回调在持锁期间执行,
 * 其内可再取显示锁 (锁序: 本锁 -> 显示锁, 反向不会出现)。
 */
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sys_core/sys_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "esp_log.h"

static const char *TAG = "app_mgr";

/* 静态注册表: 系统 APP 数量少且编译期已知, 不需要动态内存 */
static sys_app_t *s_apps[SYS_APP_MAX];
static size_t     s_app_count;
static sys_app_t *s_foreground;

/* 返回栈 */
static sys_app_t *s_stack[SYS_APP_MAX];
static size_t     s_depth;

static SemaphoreHandle_t s_lock = NULL;

esp_err_t sys_app_manager_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool lock(void)
{
    return s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t app_do_init(sys_app_t *app)
{
    if (app->state != SYS_APP_STATE_STOPPED) {
        return ESP_OK;
    }
    if (app->on_init && app->on_init(app) != ESP_OK) {
        ESP_LOGE(TAG, "app '%s' on_init failed", app->id);
        return ESP_FAIL;
    }
    app->state = SYS_APP_STATE_READY;
    ESP_LOGI(TAG, "app '%s' initialized", app->id);
    return ESP_OK;
}

static void broadcast_state(const sys_app_t *app, sys_app_state_t st)
{
    sys_app_state_evt_t evt = { .app_id = app->id, .new_state = st };
    sys_event_publish(SYS_EVENT_APP_STATE, &evt);
}

static void suspend_foreground(void)
{
    if (!s_foreground) {
        return;
    }
    if (s_foreground->on_suspend) {
        s_foreground->on_suspend(s_foreground);
    }
    s_foreground->state = SYS_APP_STATE_SUSPENDED;
    ESP_LOGI(TAG, "app '%s' suspended", s_foreground->id);
    broadcast_state(s_foreground, SYS_APP_STATE_SUSPENDED);
    s_foreground = NULL;
}

static esp_err_t start_foreground(sys_app_t *app)
{
    esp_err_t err = app_do_init(app);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "calling on_start of '%s'", app->id);
    err = app->on_start ? app->on_start(app) : ESP_OK;
    ESP_LOGI(TAG, "on_start of '%s' returned %d", app->id, (int)err);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app '%s' on_start failed", app->id);
        return err;
    }
    s_foreground = app;
    app->state = SYS_APP_STATE_ACTIVE;
    ESP_LOGI(TAG, "app '%s' now foreground", app->id);
    broadcast_state(app, SYS_APP_STATE_ACTIVE);
    return ESP_OK;
}

/* 压栈 (去重: 同一个 APP 不重复入栈) */
static void stack_push(sys_app_t *app)
{
    if (s_depth > 0 && s_stack[s_depth - 1] == app) {
        return;
    }
    if (s_depth >= SYS_APP_MAX) {
        ESP_LOGW(TAG, "back stack full, drop oldest");
        memmove(&s_stack[0], &s_stack[1], sizeof(s_stack[0]) * (SYS_APP_MAX - 1));
        s_depth = SYS_APP_MAX - 1;
    }
    s_stack[s_depth++] = app;
}

esp_err_t sys_app_register(sys_app_t *app)
{
    if (!app || !app->id || !app->on_start) {
        return ESP_ERR_INVALID_ARG; /* on_start 是最低要求 */
    }
    if (!lock()) {
        return ESP_FAIL;
    }
    esp_err_t err = ESP_OK;
    if (s_app_count >= SYS_APP_MAX) {
        ESP_LOGE(TAG, "registry full (max %d)", SYS_APP_MAX);
        err = ESP_ERR_NO_MEM;
    } else if (sys_app_find(app->id)) {
        ESP_LOGE(TAG, "app id '%s' duplicated", app->id);
        err = ESP_ERR_INVALID_STATE;
    } else if ((err = app_do_init(app)) == ESP_OK) {
        s_apps[s_app_count++] = app;
        ESP_LOGI(TAG, "app '%s' registered (%u/%u)", app->id,
                 (unsigned)s_app_count, (unsigned)SYS_APP_MAX);
    }
    unlock();
    return err;
}

sys_app_t *sys_app_find(const char *app_id)
{
    if (!app_id) {
        return NULL;
    }
    for (size_t i = 0; i < s_app_count; i++) {
        if (strcmp(s_apps[i]->id, app_id) == 0) {
            return s_apps[i];
        }
    }
    return NULL;
}

sys_app_t *sys_app_current(void)
{
    return s_foreground;
}

size_t sys_app_count(void)
{
    return s_app_count;
}

sys_app_t *sys_app_get(size_t index)
{
    return index < s_app_count ? s_apps[index] : NULL;
}

esp_err_t sys_app_launch(const char *app_id)
{
    if (!lock()) {
        return ESP_FAIL;
    }
    esp_err_t err;
    sys_app_t *target = sys_app_find(app_id);
    if (!target) {
        ESP_LOGE(TAG, "launch: unknown app '%s'", app_id);
        err = ESP_ERR_NOT_FOUND;
    } else if (target == s_foreground) {
        err = ESP_OK; /* 已在前台 */
    } else {
        /* 1. 目标 APP 尚未初始化则先 init */
        err = app_do_init(target);
        /* 2. 当前前台 APP 挂起并压入返回栈 */
        if (err == ESP_OK && s_foreground) {
            sys_app_t *prev = s_foreground;
            suspend_foreground();
            stack_push(prev);
        }
        /* 3. 目标 APP 进入前台 */
        if (err == ESP_OK) {
            err = start_foreground(target);
        }
    }
    unlock();
    return err;
}

esp_err_t sys_app_back(void)
{
    if (!lock()) {
        return ESP_FAIL;
    }
    esp_err_t err;
    if (s_depth == 0) {
        err = ESP_ERR_NOT_FOUND; /* 已在栈底, 由调用方决定是否退出 */
    } else {
        sys_app_t *prev = s_stack[--s_depth];
        suspend_foreground();
        err = start_foreground(prev);
    }
    unlock();
    return err;
}

esp_err_t sys_app_stop(const char *app_id)
{
    if (!lock()) {
        return ESP_FAIL;
    }
    esp_err_t err;
    sys_app_t *app = sys_app_find(app_id);
    if (!app) {
        err = ESP_ERR_NOT_FOUND;
    } else {
        if (app == s_foreground) {
            suspend_foreground();
        }
        /* 从返回栈中移除该 APP 的所有记录 */
        size_t w = 0;
        for (size_t i = 0; i < s_depth; i++) {
            if (s_stack[i] != app) {
                s_stack[w++] = s_stack[i];
            }
        }
        s_depth = w;

        if (app->on_stop) {
            app->on_stop(app);
        }
        app->state = SYS_APP_STATE_STOPPED;
        broadcast_state(app, SYS_APP_STATE_STOPPED);
        ESP_LOGI(TAG, "app '%s' stopped", app->id);
        err = ESP_OK;
    }
    unlock();
    return err;
}

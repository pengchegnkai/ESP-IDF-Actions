/**
 * @file sys_app_manager.c
 * @brief APP 调度器实现 —— 注册表 + 前台/后台切换
 */
#include <string.h>
#include <stdbool.h>
#include "sys_core/sys_app.h"
#include "sys_core/sys_app_manager.h"
#include "sys_core/sys_event_bus.h"
#include "esp_log.h"

static const char *TAG = "app_mgr";

/* 静态注册表: 系统 APP 数量少且编译期已知, 不需要动态内存 */
static sys_app_t *s_apps[SYS_APP_MAX];
static size_t     s_app_count;
static sys_app_t *s_foreground;

static esp_err_t app_do_init(sys_app_t *app)
{
    if (app->state != SYS_APP_STATE_STOPPED) {
        return ESP_OK; /* 已 init */
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

esp_err_t sys_app_register(sys_app_t *app)
{
    if (!app || !app->id || !app->on_start) {
        return ESP_ERR_INVALID_ARG; /* on_start 是最低要求 */
    }
    if (s_app_count >= SYS_APP_MAX) {
        ESP_LOGE(TAG, "registry full (max %d)", SYS_APP_MAX);
        return ESP_ERR_NO_MEM;
    }
    if (sys_app_find(app->id)) {
        ESP_LOGE(TAG, "app id '%s' duplicated", app->id);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = app_do_init(app);
    if (err != ESP_OK) {
        return err;
    }
    s_apps[s_app_count++] = app;
    ESP_LOGI(TAG, "app '%s' registered (%u/%u)", app->id,
             (unsigned)s_app_count, (unsigned)SYS_APP_MAX);
    return ESP_OK;
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

esp_err_t sys_app_launch(const char *app_id)
{
    sys_app_t *target = sys_app_find(app_id);
    if (!target) {
        ESP_LOGE(TAG, "launch: unknown app '%s'", app_id);
        return ESP_ERR_NOT_FOUND;
    }
    if (target == s_foreground) {
        return ESP_OK; /* 已在前台 */
    }

    /* 1. 目标 APP 尚未初始化则先 init */
    esp_err_t err = app_do_init(target);
    if (err != ESP_OK) {
        return err;
    }

    /* 2. 挂起当前前台 APP */
    if (s_foreground && s_foreground->on_suspend) {
        s_foreground->on_suspend(s_foreground);
        s_foreground->state = SYS_APP_STATE_SUSPENDED;
        ESP_LOGI(TAG, "app '%s' suspended", s_foreground->id);
        broadcast_state(s_foreground, SYS_APP_STATE_SUSPENDED);
    }

    /* 3. 目标 APP 进入前台 */
    err = target->on_start(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app '%s' on_start failed", target->id);
        return err;
    }
    s_foreground = target;
    target->state = SYS_APP_STATE_ACTIVE;
    ESP_LOGI(TAG, "app '%s' now foreground", target->id);
    broadcast_state(target, SYS_APP_STATE_ACTIVE);
    return ESP_OK;
}

/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_app_core.h"

static const char *TAG = "BT_APP_CORE";

/*********************************
 * STATIC FUNCTION DECLARATIONS
 ********************************/

/* application task handler */
static void bt_app_task_handler(void *arg);

/*********************************
 * STATIC VARIABLE DEFINITIONS
 ********************************/
static QueueHandle_t bt_app_queue_handle = NULL;
static TaskHandle_t bt_app_task_handle = NULL;

/*********************************
 * STATIC FUNCTION DEFINITIONS
 ********************************/

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback)
{
    ESP_LOGD(TAG, "bt_app_work_dispatch sig 0x%x, cb %p, param %p", event, p_cback, p_params);

    bt_app_work_item_t item = {
        .cb = p_cback,
        .event = event,
        .param = p_params,
        .param_len = param_len,
        .copy_cb = p_copy_cback
    };

    BaseType_t ret = xQueueSend(bt_app_queue_handle, &item, 10 / portTICK_PERIOD_MS);
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "bt_app_work_dispatch failed to dispatch event 0x%x", event);
        return false;
    }
    return true;
}

static void bt_app_task_handler(void *arg)
{
    bt_app_work_item_t item;
    for (;;) {
        if (xQueueReceive(bt_app_queue_handle, &item, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "bt_app_task_handler sig 0x%x, cb %p, param %p", item.event, item.cb, item.param);
            if (item.cb) {
                item.cb(item.event, item.param);
            }
            if (item.param) {
                if (item.copy_cb) {
                    item.copy_cb(item.param);
                } else {
                    free(item.param);
                }
            }
        }
    }
}

/*********************************
 * EXTERN FUNCTION DEFINITIONS
 ********************************/

void bt_app_task_start_up(void)
{
    bt_app_queue_handle = xQueueCreate(BT_APP_CORE_TASK_QUEUE_SIZE, sizeof(bt_app_work_item_t));
    if (bt_app_queue_handle == NULL) {
        ESP_LOGE(TAG, "Create Bluetooth application queue failed!");
        return;
    }

    BaseType_t ret = xTaskCreate(bt_app_task_handler, BT_APP_CORE_TASK_NAME, BT_APP_CORE_TASK_STACK_SIZE, NULL, BT_APP_CORE_TASK_PRIO, &bt_app_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Create Bluetooth application task failed!");
        vQueueDelete(bt_app_queue_handle);
        bt_app_queue_handle = NULL;
        return;
    }
}

void bt_app_task_shut_down(void)
{
    if (bt_app_task_handle) {
        vTaskDelete(bt_app_task_handle);
        bt_app_task_handle = NULL;
    }
    if (bt_app_queue_handle) {
        vQueueDelete(bt_app_queue_handle);
        bt_app_queue_handle = NULL;
    }
}

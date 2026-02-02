/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "bt_app_core.h"
#include "osi/allocator.h"
#include "mem_util.h"
#include <stdarg.h>

static const char *TAG = "BT_APP_CORE";

/* Ring buffer for app event queue */
typedef struct {
    bt_app_msg_t msg;
    void *param;
} bt_app_evt_msg_t;

/* Event queue handle */
static QueueHandle_t s_bt_app_queue = NULL;
/* Task handle */
static TaskHandle_t s_bt_app_task_handle = NULL;

static void bt_app_task_handler(void *arg);
static bool bt_app_send_msg(bt_app_msg_t msg, void *param);

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback)
{
    ESP_LOGD(TAG, "%s event: 0x%x, param len: %d", __func__, event, param_len);
    
    bt_app_msg_t msg;
    safe_memset(&msg, sizeof(msg), 0, sizeof(msg));

    msg.sig = BT_APP_SIG_WORK_DISPATCH;
    msg.event = event;
    msg.cb = p_cback;

    /* If caller provided parameters to dispatch, ensure they are copied into
     * the message. If the caller did not provide a custom copy callback,
     * use the default deep-copy helper `bt_app_work_copy_cb` so the task
     * receives a valid `param` pointer. This prevents NULL dereferences in
     * handlers that assume param is present for non-zero param_len. */
    if (param_len && p_params) {
        /* Copy semantics / ownership contract:
         * - Callers may pass a pointer to parameters with non-zero param_len.
         * - If they provide a custom copy callback (p_copy_cback), it will
         *   be used to copy/attach the data into the message.
         * - If no copy callback is provided, the dispatcher will perform a
         *   default deep copy into heap memory via bt_app_work_copy_cb.
         * - The queued event will carry ownership of the copied buffer and
         *   will free it using msg.param_free_cb when processed.
         */
        bt_app_copy_cb_t copy_cb = p_copy_cback ? p_copy_cback : bt_app_work_copy_cb;
        if (copy_cb(&msg, p_params, param_len) != BT_APP_WORK_OK) {
            return false;
        }
    }

    return bt_app_send_msg(msg, NULL);
}

void bt_app_task_start_up(void)
{
    ESP_LOGI(TAG, "%s", __func__);
    
    if (s_bt_app_queue == NULL) {
        s_bt_app_queue = xQueueCreate(20, sizeof(bt_app_evt_msg_t)); // Increased from 10 to 20
        if (!s_bt_app_queue) {
            ESP_LOGE(TAG, "Failed to create app work queue");
            return;
        }
    }
    
    if (s_bt_app_task_handle == NULL) {
        xTaskCreate(bt_app_task_handler, "BtAppTask", 8192, NULL, 10, &s_bt_app_task_handle); // Increased from 4096 to 8192
        if (!s_bt_app_task_handle) {
            ESP_LOGE(TAG, "Failed to create app task");
            return;
        }
    }
}

void bt_app_task_shut_down(void)
{
    ESP_LOGI(TAG, "%s", __func__);
    
    if (s_bt_app_task_handle) {
        vTaskDelete(s_bt_app_task_handle);
        s_bt_app_task_handle = NULL;
    }
    
    if (s_bt_app_queue) {
        vQueueDelete(s_bt_app_queue);
        s_bt_app_queue = NULL;
    }
}

static bool bt_app_send_msg(bt_app_msg_t msg, void *param)
{
    if (s_bt_app_queue == NULL) {
        ESP_LOGE(TAG, "%s: bt_app_queue is not initialized", __func__);
        return false;
    }
    
    bt_app_evt_msg_t evt_msg;
    evt_msg.msg = msg;
    /* Prefer the parameter stored inside the message (msg.param) which
     * holds a deep-copied buffer when bt_app_work_dispatch copied it.
     * The caller may pass `param` as NULL (the prior bug), so using
     * msg.param ensures handlers receive the actual data pointer. */
    evt_msg.param = msg.param;
    
    if (xQueueSend(s_bt_app_queue, &evt_msg, 10 / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGE(TAG, "%s: xQueue send failed", __func__);
        return false;
    }
    return true;
}

static void bt_app_task_handler(void *arg)
{
    bt_app_evt_msg_t evt_msg;
    
    ESP_LOGI(TAG, "%s: starting", __func__);
    
    while (1) {
        /* Wait for event */
        if (pdTRUE == xQueueReceive(s_bt_app_queue, &evt_msg, (TickType_t)portMAX_DELAY)) {
            ESP_LOGD(TAG, "%s: received signal 0x%x", __func__, evt_msg.msg.sig);
            
            switch (evt_msg.msg.sig) {
            case BT_APP_SIG_WORK_DISPATCH:
                ESP_LOGD(TAG, "%s: dispatching event 0x%x", __func__, evt_msg.msg.event);
                if (evt_msg.msg.cb) {
                    evt_msg.msg.cb(evt_msg.msg.event, evt_msg.param);
                }
                break;
            default:
                ESP_LOGW(TAG, "%s: unhandled signal 0x%x", __func__, evt_msg.msg.sig);
                break;
            }
            
            /* Free parameter buffer if needed */
            if (evt_msg.param && evt_msg.msg.param_free_cb) {
                evt_msg.msg.param_free_cb(evt_msg.param);
            }
        } else {
            // If queue receive times out, sleep a bit to prevent tight loop
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

int bt_app_work_copy_cb(bt_app_msg_t *msg, void *p_dest, int len)
{
    if (msg == NULL || p_dest == NULL || len < 0) {
        return BT_APP_WORK_FAIL;
    }
    
    if (msg->param) {
        ESP_LOGE(TAG, "%s: already has param", __func__);
        return BT_APP_WORK_FAIL;
    }
    
    msg->param = osi_malloc(len);
    if (msg->param) {
        safe_memcpy(msg->param, (size_t)len, p_dest, (size_t)len);
        msg->param_free_cb = bt_app_param_free_cb;
        return BT_APP_WORK_OK;
    }
    ESP_LOGE(TAG, "%s: malloc failed", __func__);
    return BT_APP_WORK_FAIL;
}

void bt_app_param_free_cb(void *param)
{
    if (param) {
        osi_free(param);
    }
}

#if UNIT_TEST
/*
 * Test-only helpers to drive the queue without the FreeRTOS task loop.
 * These are no-ops in production builds.
 */
size_t bt_app_core_queue_depth(void)
{
    return s_bt_app_queue ? uxQueueMessagesWaiting(s_bt_app_queue) : 0;
}

bool bt_app_core_process_once(void)
{
    if (s_bt_app_queue == NULL) {
        return false;
    }

    bt_app_evt_msg_t evt_msg;
    if (xQueueReceive(s_bt_app_queue, &evt_msg, 0) != pdTRUE) {
        return false;
    }

    switch (evt_msg.msg.sig) {
    case BT_APP_SIG_WORK_DISPATCH:
        if (evt_msg.msg.cb) {
            evt_msg.msg.cb(evt_msg.msg.event, evt_msg.param);
        }
        break;
    default:
        break;
    }

    if (evt_msg.param && evt_msg.msg.param_free_cb) {
        evt_msg.msg.param_free_cb(evt_msg.param);
    }

    return true;
}

size_t bt_app_core_drain(size_t max_iterations)
{
    size_t drained = 0;
    while (drained < max_iterations && bt_app_core_process_once()) {
        drained++;
    }
    return drained;
}
#endif

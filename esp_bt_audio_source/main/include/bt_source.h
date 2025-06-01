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
#include "esp_err.h"

/*********************************
 * STATIC FUNCTION DECLARATIONS
 ********************************/

/* application task handler */
static void bt_app_task_handler(void *arg);
/* message sender for Work queue */
static bool bt_app_send_msg(bt_app_msg_t *msg);
/* handler for dispatched message */
static void bt_app_work_dispatched(bt_app_msg_t *msg);

/*********************************
 * STATIC VARIABLE DEFINITIONS
 ********************************/
static QueueHandle_t s_bt_app_task_queue = NULL;
static TaskHandle_t s_bt_app_task_handle = NULL;

/*********************************
 * STATIC FUNCTION DEFINITIONS
 ********************************/

static bool bt_app_send_msg(bt_app_msg_t *msg)
{
    if (msg == NULL) {
        return false;
    }

    if (pdTRUE != xQueueSend(s_bt_app_task_queue, msg, 10 / portTICK_PERIOD_MS)) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s xQueue send failed", __func__);
        return false;
    }

    return true;
}

static void bt_app_work_dispatched(bt_app_msg_t *msg)
{
    if (msg->cb) {
        msg->cb(msg->event, msg->param);
    }
}

static void bt_app_task_handler(void *arg)
{
    bt_app_msg_t msg;

    for (;;) {
        /* receive message from work queue and handle it */
        if (pdTRUE == xQueueReceive(s_bt_app_task_queue, &msg, (TickType_t)portMAX_DELAY)) {
            ESP_LOGD(BT_APP_CORE_TAG, "%s, signal: 0x%x, event: 0x%x", __func__, msg.sig, msg.event);

            switch (msg.sig) {
            case BT_APP_SIG_WORK_DISPATCH:
                bt_app_work_dispatched(&msg);
                break;
            default:
                ESP_LOGW(BT_APP_CORE_TAG, "%s, unhandled signal: %d", __func__, msg.sig);
                break;
            }

            if (msg.param) {
                free(msg.param);
            }
        }
    }
}

/*********************************
 * EXTERN FUNCTION DEFINITIONS
 ********************************/

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback)
{
    ESP_LOGD(BT_APP_CORE_TAG, "%s event: 0x%x, param len: %d", __func__, event, param_len);

    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(bt_app_msg_t));

    msg.sig = BT_APP_SIG_WORK_DISPATCH;
    msg.event = event;
    msg.cb = p_cback;

    if (param_len == 0) {
        return bt_app_send_msg(&msg);
    } else if (p_params && param_len > 0) {
        if ((msg.param = malloc(param_len)) != NULL) {
            memcpy(msg.param, p_params, param_len);
            /* check if caller has provided a copy callback to do the deep copy */
            if (p_copy_cback) {
                p_copy_cback(msg.param, p_params, param_len);
            }
            return bt_app_send_msg(&msg);
        }
    }

    return false;
}

// Updated to use larger stack size for better WiFi coexistence 
void bt_app_task_start_up(void)
{
    // Create the queue first before creating the task
    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));
    if (s_bt_app_task_queue == NULL) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s queue create failed", __func__);
        return;
    }
    
    BaseType_t res = xTaskCreate(bt_app_task_handler, "BtAppTask", 8192, NULL, 10, &s_bt_app_task_handle);
    if (res != pdPASS) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s failed: %d", __func__, res);
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
        return;
    }
}

void bt_app_task_shut_down(void)
{
    if (s_bt_app_task_handle) {
        vTaskDelete(s_bt_app_task_handle);
        s_bt_app_task_handle = NULL;
    }
    if (s_bt_app_task_queue) {
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
    }
}

/**
 * @brief Simulate a connection drop for testing auto-reconnect
 * 
 * This function is used to test the auto-reconnection functionality.
 * It simulates a scenario where the Bluetooth connection is unexpectedly dropped.
 * For test purposes only.
 * 
 * @return ESP_OK if successful, ESP_FAIL otherwise
 */
esp_err_t bt_simulate_connection_drop(void)
{
    // Implementation for simulating a connection drop
    // This is typically used for testing the reconnection logic
    // For now, we just log the event and return success

    ESP_LOGI(BT_APP_CORE_TAG, "Simulating connection drop");

    // Here you would add the code to actually drop the connection
    // For example, by calling the appropriate Bluetooth API

    return ESP_OK;
}

/**
 * @brief Connection states for the BT connection
 */
typedef enum {
    BT_STATE_UNKNOWN = 0,
    BT_STATE_DISCONNECTED,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_DISCONNECTING,
    BT_STATE_ERROR
} bt_connection_state_t;

/**
 * @brief Streaming states for audio
 */
typedef enum {
    BT_STREAM_STATE_UNKNOWN = 0,
    BT_STREAM_STATE_STOPPED,
    BT_STREAM_STATE_STREAMING,
    BT_STREAM_STATE_PAUSED,
    BT_STREAM_STATE_ERROR
} bt_streaming_state_t;

/**
 * @brief Event types for Bluetooth events
 */
typedef enum {
    BT_EVENT_UNKNOWN = 0,
    BT_EVENT_CONNECTION_STATE_CHANGED,
    BT_EVENT_AUDIO_STATE_CHANGED,
    BT_EVENT_SCAN_RESULT,
    BT_EVENT_SCAN_COMPLETE,
    BT_EVENT_ERROR
} bt_event_t;

/**
 * @brief Event callback function type
 */
typedef void (*bt_event_callback_t)(bt_event_t event, void* param);

/**
 * @brief Register for streaming events
 * 
 * @param callback Event callback function
 * @return ESP_OK on success
 */
esp_err_t bt_register_streaming_callback(bt_event_callback_t callback);

/**
 * @brief Get current connection state
 *
 * @return Current connection state
 */
bt_connection_state_t bt_get_connection_state(void);

/**
 * @brief Get current streaming state
 *
 * @return Current streaming state
 */
bt_streaming_state_t bt_get_streaming_state(void);

/**
 * @brief Start streaming audio to connected Bluetooth device
 *
 * @return ESP_OK on success, ESP_FAIL if not connected or error
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop streaming audio to connected Bluetooth device
 *
 * @return ESP_OK on success
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Check if audio is currently streaming
 *
 * @return true if streaming, false otherwise
 */
bool bt_is_streaming(void);

/**
 * @brief Pause audio streaming
 *
 * @return ESP_OK on success, ESP_FAIL if not streaming or error
 */
esp_err_t bt_pause_streaming(void);

/**
 * @brief Resume previously paused audio streaming
 *
 * @return ESP_OK on success, ESP_FAIL if not paused or error
 */
esp_err_t bt_resume_streaming(void);

/**
 * @brief Register for connection state change events
 *
 * @param callback Event callback function
 * @return ESP_OK on success
 */
esp_err_t bt_register_connection_callback(bt_event_callback_t callback);
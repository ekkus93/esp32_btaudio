/* bt_source_stubs_a2dp.c — A2DP streaming stubs.
 * Split out of bt_source_stubs.c; shares bt_source_stubs_internal.h. */
#include "bt_source_stubs_internal.h"

static const char *TAG = "BT_SOURCE_STUB";


/**
 * @brief Start A2DP streaming
 */
BT_WEAK_FN esp_err_t bt_a2dp_start_streaming(void)
{
    if (!s_is_connected) {
        ESP_LOGE(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_stub_streaming_state == BT_STREAMING_STATE_STREAMING) {
        ESP_LOGW(TAG, "Already streaming");
        return ESP_OK;
    }
    
    /* Update state */
    s_stub_streaming_state = BT_STREAMING_STATE_STARTING;
    vTaskDelay(pdMS_TO_TICKS(100)); // Simulate start delay
    s_stub_streaming_state = BT_STREAMING_STATE_STREAMING;
    
    ESP_LOGI(TAG, "Started A2DP streaming (stub)");
    return ESP_OK;
}

/**
 * @brief Stop A2DP streaming
 */
BT_WEAK_FN esp_err_t bt_a2dp_stop_streaming(void)
{
    if (!s_is_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_OK;
    }
    
    if (s_stub_streaming_state != BT_STREAMING_STATE_STREAMING &&
        s_stub_streaming_state != BT_STREAMING_STATE_PAUSED) {
        ESP_LOGW(TAG, "Not streaming");
        return ESP_OK;
    }
    
    /* Update state */
    s_stub_streaming_state = BT_STREAMING_STATE_STOPPING;
    vTaskDelay(pdMS_TO_TICKS(100)); // Simulate stop delay
    s_stub_streaming_state = BT_STREAMING_STATE_STOPPED;
    
    ESP_LOGI(TAG, "Stopped A2DP streaming (stub)");
    return ESP_OK;
}

/**
 * @brief Pause A2DP streaming
 */
BT_WEAK_FN esp_err_t bt_a2dp_pause_streaming(void)
{
    if (!s_is_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_stub_streaming_state != BT_STREAMING_STATE_STREAMING) {
        ESP_LOGW(TAG, "Not streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Update state */
    s_stub_streaming_state = BT_STREAMING_STATE_PAUSED;
    
    ESP_LOGI(TAG, "Paused A2DP streaming (stub)");
    return ESP_OK;
}

/**
 * @brief Resume A2DP streaming
 */
BT_WEAK_FN esp_err_t bt_a2dp_resume_streaming(void)
{
    if (!s_is_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_stub_streaming_state != BT_STREAMING_STATE_PAUSED) {
        ESP_LOGW(TAG, "Not paused");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Update state */
    s_stub_streaming_state = BT_STREAMING_STATE_STREAMING;
    
    ESP_LOGI(TAG, "Resumed A2DP streaming (stub)");
    return ESP_OK;
}

/**
 * @brief Check if streaming is active
 */
BT_WEAK_FN bool bt_a2dp_is_streaming(void)
{
    return (s_stub_streaming_state == BT_STREAMING_STATE_STREAMING);
}

/**
 * @brief Check if A2DP is connected
 */
BT_WEAK_FN bool bt_a2dp_is_connected(void)
{
    return s_is_connected;
}

/**
 * @brief Get streaming state
 */
BT_WEAK_FN bt_streaming_state_t bt_get_streaming_state(void)
{
    return s_stub_streaming_state;
}

/**
 * @brief Get streaming info
 */
BT_WEAK_FN esp_err_t bt_get_streaming_info(bt_streaming_info_t* info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    info->state = s_stub_streaming_state;
    info->paused = (s_stub_streaming_state == BT_STREAMING_STATE_PAUSED);
    info->bytes_sent = 0;
    info->packets_sent = 0;
    info->packet_errors = 0;
    info->stream_duration = 0;
    
    return ESP_OK;
}

/**
 * @brief Check if streaming is paused
 */
BT_WEAK_FN bool bt_is_paused(void)
{
    return (s_stub_streaming_state == BT_STREAMING_STATE_PAUSED);
}

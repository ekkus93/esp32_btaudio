#include <stdbool.h>
#include "bt_streaming.h"
#include "esp_log.h"

static const char *TAG = "BT_STREAMING_MOCK";

// Use these flags to track state independently
static bool mock_streaming = false;
static bool mock_connected = false;

/**
 * For test purposes - simulate a connection
 */
void bt_streaming_mock_set_connected(bool connected) {
    mock_connected = connected;
    ESP_LOGI(TAG, "Mock connection state set to: %s", connected ? "connected" : "disconnected");
}

/**
 * Check if streaming is active
 */
bool bt_is_streaming(void) {
    return mock_streaming;
}

/**
 * Start streaming audio
 */
esp_err_t bt_start_streaming(void) {
    ESP_LOGI(TAG, "Stub: Starting audio streaming");
    
    if (!mock_connected) {
        ESP_LOGW(TAG, "Cannot start streaming - not connected");
        return ESP_FAIL;
    }
    
    mock_streaming = true;
    return ESP_OK;
}

/**
 * Stop streaming audio
 */
esp_err_t bt_stop_streaming(void) {
    ESP_LOGI(TAG, "Stub: Stopping audio streaming");
    mock_streaming = false;
    return ESP_OK;
}

/**
 * Pause audio streaming
 */
esp_err_t bt_pause_streaming(void) {
    ESP_LOGI(TAG, "Stub: Pausing audio streaming");
    
    if (!mock_streaming) {
        ESP_LOGW(TAG, "Cannot pause - not streaming");
        return ESP_FAIL;
    }
    
    // In our simple mock, we keep streaming state as true
    // Just log the pause action
    ESP_LOGI(TAG, "Audio stream paused (but still marked as streaming)");
    
    return ESP_OK;
}

/**
 * Resume audio streaming
 */
esp_err_t bt_resume_streaming(void) {
    ESP_LOGI(TAG, "Stub: Resuming audio streaming");
    
    if (!mock_connected) {
        ESP_LOGW(TAG, "Cannot resume - not connected");
        return ESP_FAIL;
    }
    
    if (!mock_streaming) {
        ESP_LOGW(TAG, "Not streaming, starting new stream");
        return bt_start_streaming();
    }
    
    ESP_LOGI(TAG, "Audio stream resumed");
    
    return ESP_OK;
}

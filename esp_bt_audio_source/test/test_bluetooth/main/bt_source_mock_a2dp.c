/* bt_source_mock_a2dp.c — A2DP source + streaming mocks.
 * Split out of bt_source_mock.c; shares bt_source_mock_internal.h. */
#include "bt_source_mock_internal.h"

static const char *TAG = "BT_SOURCE_MOCK";


void bt_manager_test_invoke_a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    /* the a2dp tests drive streaming-state transitions through this hook */
    if (event != ESP_A2D_AUDIO_STATE_EVT || param == NULL) {
        return;
    }
    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        s_streaming = true;
        s_streaming_paused = false;
        s_streaming_state = BT_STREAMING_STATE_STREAMING;
    } else {
        /* STOPPED / REMOTE_SUSPEND: remote pause semantics */
        s_streaming = false;
        s_streaming_paused = true;
        s_streaming_state = BT_STREAMING_STATE_PAUSED;
    }
}

/* Start streaming */
esp_err_t bt_start_streaming(void)
{
    ESP_LOGI(TAG, "Mock: Starting audio streaming");
    
    // Check if not connected - meaningful behavior
    if (!s_connected) {
        return ESP_FAIL;
    }
    
    // Check if already streaming - meaningful behavior
    if (s_streaming) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update streaming state
    s_streaming = true;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STREAMING;
    s_active_profile = BT_PROFILE_A2DP_SINK;
    
    return ESP_OK;
}

/* Stop streaming */
esp_err_t bt_stop_streaming(void)
{
    ESP_LOGI(TAG, "Mock: Stopping audio streaming");
    
    // Check if not streaming - meaningful behavior
    if (!s_streaming && !s_streaming_paused) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update streaming state
    s_streaming = false;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    
    return ESP_OK;
}

/* Pause streaming */
esp_err_t bt_pause_streaming(void)
{
    ESP_LOGI(TAG, "Mock: Pausing audio streaming");
    
    // Can only pause if actually streaming
    if (!s_streaming) {
        return ESP_FAIL;
    }
    
    // Update streaming state
    s_streaming = false;
    s_streaming_paused = true;
    s_streaming_state = BT_STREAMING_STATE_PAUSED;
    
    return ESP_OK;
}

/* Resume streaming */
esp_err_t bt_resume_streaming(void)
{
    ESP_LOGI(TAG, "Mock: Resuming audio streaming");
    
    // Can only resume if paused
    if (!s_streaming_paused) {
        return ESP_FAIL;
    }
    
    // Update streaming state
    s_streaming = true;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STREAMING;
    
    return ESP_OK;
}

/* Check if streaming */
bool bt_is_streaming(void)
{
    return s_streaming;
}

/* Check if streaming is paused */
bool bt_is_paused(void)
{
    return s_streaming_paused;
}

/**
 * Get current streaming state
 */
bt_streaming_state_t bt_get_streaming_state(void)
{
    return s_streaming_state;
}

/* Keep streaming state in sync with injected A2DP audio state events. */
void bt_source_mock_handle_audio_state(esp_a2d_audio_state_t state)
{
    if (state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        s_streaming = false;
        s_streaming_paused = true;
        s_streaming_state = BT_STREAMING_STATE_PAUSED;
    } else if (state == ESP_A2D_AUDIO_STATE_STARTED) {
        s_streaming = true;
        s_streaming_paused = false;
        s_streaming_state = BT_STREAMING_STATE_STREAMING;
    } else if (state == ESP_A2D_AUDIO_STATE_STOPPED) {
        s_streaming = false;
        s_streaming_paused = false;
        s_streaming_state = BT_STREAMING_STATE_STOPPED;
    }
}

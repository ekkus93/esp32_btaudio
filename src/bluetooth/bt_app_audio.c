#include "bluetooth/bt_app_audio.h"
#include "bluetooth/bt_app_global.h"

#include "custom_log.h"

#define TAG "BT_APP_AUDIO"

static int16_t sine_table[TABLE_SIZE];

// New helper function to initialize the sine_table once
static void init_sine_table(void) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        float angle = 2.0f * M_PI * i / TABLE_SIZE;
        sine_table[i] = (int16_t)(32767.5f * sinf(angle));
    }
    sine_table_initialized = true;
}

// New helper function to trigger a beep.
void trigger_beep(void) {
    s_beep_in_progress = true;
    s_beep_duration = 0;
    s_beep_index = 0;
    if (!sine_table_initialized) {
        init_sine_table();
    }
    SAFE_ESP_LOGI(TAG, "Beep triggered (trigger_beep)");
}

// Helper function to check if enough time has passed since last operation
bool is_operation_time_ok(void) {
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
    if (current_time - s_last_operation_time < BT_OPERATION_DELAY_MS) {
        SAFE_ESP_LOGW(TAG, "Operation attempted too soon after previous one");
        return false;
    }
    s_last_operation_time = current_time;
    return true;
}

esp_err_t bluetooth_write_audio(const uint8_t* data, size_t* written) {  // Update this line to match the declaration
    if (!data || !written || !*written) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_media_state != APP_AV_MEDIA_STATE_STARTED) {
        SAFE_ESP_LOGW(TAG, "A2DP media not started");
        return ESP_ERR_INVALID_STATE;
    }

    // Add logging to monitor buffer usage
    //esp_a2d_source_get_buffer_status(&available, &total);
    //SAFE_ESP_LOGD(TAG, "A2DP Buffer: Available=%d, Total=%d", available, total);

    return esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
}

// Make sure this function is present in the source file with proper implementation
esp_err_t bluetooth_send_beep(void) {
    SAFE_ESP_LOGI(TAG, "Sending beep");
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGW(TAG, "Not connected: cannot send beep");
        return ESP_ERR_INVALID_STATE;
    }
    trigger_beep();
    return ESP_OK;
}

#include "bt_events_a2dp.h"

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
#include "esp_log.h"
#include "bt_manager_internal.h"
#include "bt_manager.h"
#include "audio_processor.h"
#include <string.h>

#define TAG "BT_EVT_A2DP"

/* Forward declarations for connection manager callbacks and internal functions */
#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
extern void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
extern void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);
extern bt_err_t bt_start_audio(void);
#endif

void bt_events_handle_a2dp_connection(const esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        char bda_str[18];
        safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                      param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                      param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                      param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);

        ESP_LOGI(TAG, "Connected to device: %s", bda_str);  // NOLINT(bugprone-branch-clone)

        bt_ctx.connected = true;
        safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), bda_str);

        if (bt_ctx.connected_callback != NULL) {
            bt_ctx.connected_callback(bda_str, bt_ctx.connected_name);
        }

        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->conn_stat.remote_bda, sizeof(tmp_addr));
        bt_connection_state_cb(param->conn_stat.state, tmp_addr);

        if (s_autostart_enabled) {
    #if defined(UNIT_TEST)
            s_autostart_attempts++;
    #endif
            bt_err_t start_ret = bt_start_audio();
            ESP_LOGI(TAG, "Auto-start after connect -> %s", start_ret == ESP_OK ? "OK" : esp_err_to_name(start_ret));  // NOLINT(bugprone-branch-clone)
        }
    } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected from device: %s", bt_ctx.connected_mac);  // NOLINT(bugprone-branch-clone)

        if (bt_ctx.disconnected_callback != NULL) {
            bt_ctx.disconnected_callback(bt_ctx.connected_mac);
        }

        bt_ctx.connected = false;
        bt_ctx.audio_playing = false;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->conn_stat.remote_bda, sizeof(tmp_addr));
        bt_connection_state_cb(param->conn_stat.state, tmp_addr);
    }
}

void bt_events_handle_a2dp_audio(const esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        ESP_LOGI(TAG, "Audio streaming started");  // NOLINT(bugprone-branch-clone)
        bt_ctx.audio_playing = true;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
        ESP_LOGI(TAG, "Audio streaming stopped");  // NOLINT(bugprone-branch-clone)
        bt_ctx.audio_playing = false;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        ESP_LOGI(TAG, "Audio streaming suspended");  // NOLINT(bugprone-branch-clone)
        bt_ctx.audio_playing = false;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    }
}

// A2DP callback for audio events
void bt_events_a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            bt_events_handle_a2dp_connection(param);
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            bt_events_handle_a2dp_audio(param);
            break;
        default:
            break;
    }
}

#ifdef ESP_PLATFORM
// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written
int32_t bt_events_a2dp_data_callback(uint8_t *buf, int32_t len)
{
    if (len <= 0 || buf == NULL) {
        return 0;
    }

    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, (size_t)len, &bytes_read);
    if (ret != ESP_OK) {
        return 0;
    }

    return (int32_t)bytes_read;
}
#endif // ESP_PLATFORM

#endif // ESP_PLATFORM || UNIT_TEST

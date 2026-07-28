#include "bt_events_a2dp.h"

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
#include "esp_log.h"
#include "bt_manager_internal.h"
#include "bt_manager.h"
#include "bt_pairing_store.h"
#include "bt_duplex_policy.h"
#include "audio_processor.h"
#include <string.h>

#define TAG "BT_EVT_A2DP"

/* Forward declarations for connection manager callbacks and internal functions */
#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
extern void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
extern void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);
extern bt_err_t bt_start_audio(void);
#endif

static void bda_to_string(const uint8_t bda[6], char out[18])
{
    safe_snprintf(out, 18U, "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static void report_policy_result(const char *event_name, esp_err_t err)
{
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) return;
    ESP_LOGE(TAG, "%s policy update failed: %s", event_name,
             esp_err_to_name(err));
}

static void apply_connection_policy(const esp_a2d_cb_param_t *param)
{
    bt_a2dp_profile_state_t mapped;
    switch (param->conn_stat.state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        mapped = BT_A2DP_PROFILE_DISCONNECTED;
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
        mapped = BT_A2DP_PROFILE_CONNECTING;
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
        mapped = BT_A2DP_PROFILE_CONNECTED;
        break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
        mapped = BT_A2DP_PROFILE_DISCONNECTING;
        break;
    default:
        report_policy_result("A2DP connection", ESP_ERR_INVALID_ARG);
        return;
    }
    char peer[18];
    bda_to_string(param->conn_stat.remote_bda, peer);
    report_policy_result("A2DP connection",
                         bt_manager_hfp_handle_a2dp_profile_event(peer, mapped));
}

static void apply_audio_policy(const esp_a2d_cb_param_t *param)
{
    bt_a2dp_audio_state_t mapped;
    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        mapped = BT_A2DP_AUDIO_STARTED;
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        mapped = BT_A2DP_AUDIO_REMOTE_SUSPENDED;
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
        mapped = BT_A2DP_AUDIO_STOPPED;
    } else {
        report_policy_result("A2DP audio", ESP_ERR_INVALID_ARG);
        return;
    }
    char peer[18];
    bda_to_string(param->audio_stat.remote_bda, peer);
    report_policy_result("A2DP audio",
                         bt_manager_hfp_handle_a2dp_audio_event(peer, mapped));
}

void bt_events_handle_a2dp_connection(const esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        char bda_str[18];
        bda_to_string(param->conn_stat.remote_bda, bda_str);

        ESP_LOGI(TAG, "Connected to device: %s", bda_str);  // NOLINT(bugprone-branch-clone)

        /* Lock bt_ctx, update fields, save callback, unlock, then invoke */
        bt_connected_cb cb = NULL;
        char mac[18] = {0};
        char name[32] = {0};

        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            bt_ctx.connected = true;
            bt_ctx.connecting = false;
            safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), bda_str);
            cb = bt_ctx.connected_callback;
            safe_copy_str(mac, sizeof(mac), bt_ctx.connected_mac);
            safe_copy_str(name, sizeof(name), bt_ctx.connected_name);
            bt_ctx_unlock();
        }

        if (cb) {
            cb(mac, name);
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
        /* Lock bt_ctx, save callback, update fields, unlock, then invoke */
        bt_disconnected_cb cb = NULL;
        char mac[18] = {0};

        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            cb = bt_ctx.disconnected_callback;
            safe_copy_str(mac, sizeof(mac), bt_ctx.connected_mac);
            bt_ctx.connected = false;
            bt_ctx.connecting = false;
            bt_ctx.audio_playing = false;
            bt_ctx_unlock();
        }

        ESP_LOGI(TAG, "Disconnected from device: %s", mac);  // NOLINT(bugprone-branch-clone)

        if (cb) {
            cb(mac);
        }

        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->conn_stat.remote_bda, sizeof(tmp_addr));
        bt_connection_state_cb(param->conn_stat.state, tmp_addr);
        /* Emit PAIR FAILED if a pairing initiation never reached AUTH_CMPL
         * (e.g. page timeout, HCI connection refused). */
        bt_pairing_handle_connection_failed(tmp_addr);
    }

    apply_connection_policy(param);
}

void bt_events_handle_a2dp_audio(const esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        ESP_LOGI(TAG, "Audio streaming started");  // NOLINT(bugprone-branch-clone)
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            bt_ctx.audio_playing = true;
            bt_ctx_unlock();
        }
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
        ESP_LOGI(TAG, "Audio streaming stopped");  // NOLINT(bugprone-branch-clone)
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            bt_ctx.audio_playing = false;
            bt_ctx_unlock();
        }
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        ESP_LOGI(TAG, "Audio streaming suspended");  // NOLINT(bugprone-branch-clone)
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            bt_ctx.audio_playing = false;
            bt_ctx_unlock();
        }
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    }

    apply_audio_policy(param);
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
    /* Input validation (CODE_REVIEW 2602101453, P1.1.4)
     *
     * WHY: Negative or zero length indicates a bug in the Bluetooth stack.
     *      Null buffer pointer is invalid and would cause crashes.
     *      Same defensive programming pattern as bt_audio_data_callback().
     *
     * SAFETY: Guard against upstream bugs by rejecting invalid parameters immediately.
     */
    if (buf == NULL) {
        ESP_LOGE(TAG, "bt_events_a2dp_data_callback: NULL buffer pointer");
        return 0;
    }

    if (len < 0) {
        ESP_LOGE(TAG, "bt_events_a2dp_data_callback: INVALID negative length=%d (BT stack bug!)", len);
        return 0;
    }

    if (len == 0) {
        ESP_LOGW(TAG, "bt_events_a2dp_data_callback: zero-length request (should never happen)");
        return 0;
    }

    /* Cast to size_t once after validation (CODE_REVIEW 2602101453, P1.1.4)
     * WHY: len is now known to be positive, so safe to cast to size_t.
     *      Using size_t for audio_processor_read() eliminates signed/unsigned issues.
     */
    size_t req = (size_t)len;

    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, req, &bytes_read);
    if (ret != ESP_OK) {
        return 0;
    }

    return (int32_t)bytes_read;
}
#endif // ESP_PLATFORM

#endif // ESP_PLATFORM || UNIT_TEST

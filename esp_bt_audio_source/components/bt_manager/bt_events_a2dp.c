#include "bt_events_a2dp_internal.h"

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
#include "esp_log.h"
#include "bt_manager_internal.h"
#include "bt_manager.h"
#include "bt_pairing_store.h"
#include "bt_hfp_manager.h"
#include <string.h>

#define TAG "BT_EVT_A2DP"

/* Forward declarations for connection manager callbacks and internal functions */
extern void bt_connection_state_cb(esp_a2d_connection_state_t state,
                                   esp_bd_addr_t bd_addr);
extern void bt_audio_state_cb(esp_a2d_audio_state_t state,
                              esp_bd_addr_t bd_addr);
extern bt_err_t bt_start_audio(void);

void bda_to_string(const uint8_t bda[6], char out[18])
{
    safe_snprintf(out, 18U, "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

void increment_u64_saturating(uint64_t *value)
{
    if (*value != UINT64_MAX) (*value)++;
}

void report_policy_result(const char *event_name, esp_err_t err)
{
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) return;
    ESP_LOGE(TAG, "%s policy update failed: %s", event_name,
             esp_err_to_name(err));
}

static void apply_connection_policy(a2dp_bound_profile_event_t *bound)
{
    esp_err_t err = refresh_bound_generation(
        bound->peer_mac, bound->conn_handle, bound->lifecycle_serial,
        true, bound->generation, &bound->generation);
    if (err == ESP_OK) {
        err = bt_manager_hfp_handle_a2dp_profile_event(
            bound->generation, bound->peer_mac, bound->state);
    }
    if (err == ESP_OK && bound->state != BT_A2DP_PROFILE_DISCONNECTED) {
        err = refresh_bound_generation(
            bound->peer_mac, bound->conn_handle, bound->lifecycle_serial,
            false, bound->generation, &bound->generation);
    } else if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        record_rejected_bound_event(
            bound->generation, bound->peer_mac, "PROFILE_POLICY_REJECTED");
    }

    if (bound->state == BT_A2DP_PROFILE_DISCONNECTED) {
        esp_err_t clear_err = clear_binding_if_identity(
            bound->lifecycle_serial, bound->conn_handle);
#ifdef UNIT_TEST
        s_test_last_binding_clear_error = clear_err;
#endif
        if (clear_err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG,
                     "A2DP disconnect binding clear skipped: lifecycle=%u handle=%u error=%s reason=IDENTITY_CHANGED",
                     (unsigned)bound->lifecycle_serial,
                     (unsigned)bound->conn_handle,
                     esp_err_to_name(clear_err));
        } else if (clear_err != ESP_OK) {
            ESP_LOGE(TAG,
                     "A2DP disconnect binding clear failed: lifecycle=%u handle=%u primary=%s clear=%s",
                     (unsigned)bound->lifecycle_serial,
                     (unsigned)bound->conn_handle,
                     esp_err_to_name(err),
                     esp_err_to_name(clear_err));
        }
        /* ESP_ERR_NOT_FOUND here is the idempotent "no duplex session"
         * policy result, not a hard primary failure. A real binding-clear
         * failure is more actionable and must remain visible. Hard policy
         * errors remain authoritative. */
        if ((err == ESP_OK || err == ESP_ERR_NOT_FOUND) &&
            clear_err != ESP_OK) {
            err = clear_err;
        }
    }
#ifdef UNIT_TEST
    s_test_last_connection_policy_error = err;
#endif
    report_policy_result("A2DP connection", err);
}

static void apply_audio_policy(a2dp_bound_audio_event_t *bound)
{
    esp_err_t err = refresh_bound_generation(
        bound->peer_mac, bound->conn_handle, bound->lifecycle_serial,
        true, bound->generation, &bound->generation);
    if (err == ESP_OK && bound->generation == 0U) {
        err = ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK) {
        err = bt_manager_hfp_handle_a2dp_audio_event(
            bound->generation, bound->peer_mac, bound->state);
    }
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        record_rejected_bound_event(
            bound->generation, bound->peer_mac, "AUDIO_POLICY_REJECTED");
    }
    report_policy_result("A2DP audio", err);
}

void bt_events_handle_a2dp_connection(const esp_a2d_cb_param_t *param) {
    a2dp_bound_profile_event_t bound;
    esp_err_t identity_err = prepare_connection_event(param, &bound);
    if (identity_err != ESP_OK) {
        report_policy_result("A2DP connection identity", identity_err);
        return;
    }

    if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Connected to device: %s", bound.peer_mac);  // NOLINT(bugprone-branch-clone)

        if (bound.connected_callback) {
            bound.connected_callback(bound.callback_mac, bound.callback_name);
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
        ESP_LOGI(TAG, "Disconnected from device: %s", bound.callback_mac);  // NOLINT(bugprone-branch-clone)

        if (bound.disconnected_callback) {
            bound.disconnected_callback(bound.callback_mac);
        }

        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->conn_stat.remote_bda, sizeof(tmp_addr));
        bt_connection_state_cb(param->conn_stat.state, tmp_addr);
        bt_pairing_handle_connection_failed(tmp_addr);
    }

    apply_connection_policy(&bound);
}

void bt_events_handle_a2dp_audio(const esp_a2d_cb_param_t *param) {
    a2dp_bound_audio_event_t bound;
    esp_err_t identity_err = prepare_audio_event(param, &bound);
    if (identity_err != ESP_OK) {
        report_policy_result("A2DP audio identity", identity_err);
        return;
    }

    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        ESP_LOGI(TAG, "Audio streaming started");  // NOLINT(bugprone-branch-clone)
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
        ESP_LOGI(TAG, "Audio streaming stopped");  // NOLINT(bugprone-branch-clone)
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        ESP_LOGI(TAG, "Audio streaming suspended");  // NOLINT(bugprone-branch-clone)
    }

    esp_bd_addr_t tmp_addr = {0};
    safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
    bt_audio_state_cb(param->audio_stat.state, tmp_addr);

    apply_audio_policy(&bound);
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

esp_err_t bt_events_a2dp_reset_binding(void)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return err;
    }

    memset(&s_policy_binding, 0, sizeof(s_policy_binding));
    bt_ctx_unlock();
    return ESP_OK;
}

#endif // ESP_PLATFORM || UNIT_TEST

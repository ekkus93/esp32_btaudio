#include "bt_events_a2dp.h"

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
#include "esp_log.h"
#include "bt_manager_internal.h"
#include "bt_manager.h"
#include "bt_pairing_store.h"
#include "bt_duplex_policy.h"
#include "bt_duplex_state_events.h"
#include "bt_hfp_manager.h"
#include "audio_processor.h"
#include <string.h>
#include <strings.h>

#define TAG "BT_EVT_A2DP"
#define A2DP_BINDING_MAC_STR_LEN 18U

typedef struct {
    bool valid;
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    uint32_t lifecycle_serial;
    uint32_t last_duplex_generation;
    uint64_t missing_binding_rejections;
    uint64_t wrong_peer_rejections;
    uint64_t generation_sync_failures;
} a2dp_policy_binding_t;

/* This binding is guarded by bt_ctx.lock. It represents one A2DP connection
 * lifecycle, not one raw duplex generation: the duplex generation legitimately
 * rotates when an HFP audio session starts while the same A2DP link remains up.
 * A policy event may refresh the generation only after its peer and lifecycle
 * binding have been validated. This removes unbound self-stamping while keeping
 * valid A2DP events alive across HFP audio-generation rotations.
 *
 * ESP-IDF's callback does not expose a lower-layer connection token. Therefore
 * a delayed event from an old connection that arrives after a new connection to
 * the same peer has already established a new lifecycle binding is inherently
 * indistinguishable. That residual limitation is documented and tested as a
 * platform-boundary constraint rather than hidden by current-state fallback. */
static a2dp_policy_binding_t s_policy_binding;

/* Forward declarations for connection manager callbacks and internal functions */
extern void bt_connection_state_cb(esp_a2d_connection_state_t state,
                                   esp_bd_addr_t bd_addr);
extern void bt_audio_state_cb(esp_a2d_audio_state_t state,
                              esp_bd_addr_t bd_addr);
extern bt_err_t bt_start_audio(void);

static void bda_to_string(const uint8_t bda[6], char out[18])
{
    safe_snprintf(out, 18U, "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static void increment_u64_saturating(uint64_t *value)
{
    if (*value != UINT64_MAX) (*value)++;
}

static uint32_t next_lifecycle_serial(uint32_t current)
{
    current++;
    return current == 0U ? 1U : current;
}

static void report_policy_result(const char *event_name, esp_err_t err)
{
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) return;
    ESP_LOGE(TAG, "%s policy update failed: %s", event_name,
             esp_err_to_name(err));
}

static void record_rejected_bound_event(uint32_t generation,
                                        const char *event_peer)
{
    if (generation != 0U && event_peer != NULL) {
        /* Validation updates the existing visible wrong-peer or stale-generation
         * counter. If the identity still matches, the helper explicitly records
         * this caller-classified event as stale. */
        (void)bt_duplex_record_stale_operation_event(generation, event_peer);
    }
}

static void record_rejected_unbound_event(const char *event_peer)
{
    bt_hfp_manager_status_t status;
    if (event_peer != NULL &&
        bt_manager_hfp_get_status(&status) == ESP_OK &&
        status.duplex.peer_valid &&
        status.duplex.session_generation != 0U) {
        (void)bt_duplex_record_stale_operation_event(
            status.duplex.session_generation, event_peer);
    }
}

static esp_err_t create_or_capture_profile_binding(
    const char *peer,
    bt_a2dp_profile_state_t state,
    uint32_t *serial_out,
    uint32_t *last_generation_out)
{
    if (peer == NULL || serial_out == NULL || last_generation_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;

    if (s_policy_binding.valid) {
        if (strcasecmp(peer, s_policy_binding.peer_mac) != 0) {
            const uint32_t generation =
                s_policy_binding.last_duplex_generation;
            increment_u64_saturating(
                &s_policy_binding.wrong_peer_rejections);
            bt_ctx_unlock();
            record_rejected_bound_event(generation, peer);
            return ESP_ERR_INVALID_STATE;
        }
        *serial_out = s_policy_binding.lifecycle_serial;
        *last_generation_out = s_policy_binding.last_duplex_generation;
        bt_ctx_unlock();
        return ESP_OK;
    }

    if (state == BT_A2DP_PROFILE_DISCONNECTED ||
        state == BT_A2DP_PROFILE_DISCONNECTING) {
        increment_u64_saturating(
            &s_policy_binding.missing_binding_rejections);
        bt_ctx_unlock();
        record_rejected_unbound_event(peer);
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t serial =
        next_lifecycle_serial(s_policy_binding.lifecycle_serial);
    memset(s_policy_binding.peer_mac, 0,
           sizeof(s_policy_binding.peer_mac));
    safe_copy_str(s_policy_binding.peer_mac,
                  sizeof(s_policy_binding.peer_mac), peer);
    s_policy_binding.valid = true;
    s_policy_binding.lifecycle_serial = serial;
    s_policy_binding.last_duplex_generation = 0U;
    *serial_out = serial;
    *last_generation_out = 0U;
    bt_ctx_unlock();
    return ESP_OK;
}

static esp_err_t refresh_bound_generation(const char *peer,
                                          uint32_t lifecycle_serial,
                                          bool allow_no_session,
                                          uint32_t fallback_generation,
                                          uint32_t *generation_out)
{
    if (peer == NULL || generation_out == NULL || lifecycle_serial == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    bt_hfp_manager_status_t status;
    esp_err_t err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) {
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            if (s_policy_binding.valid &&
                s_policy_binding.lifecycle_serial == lifecycle_serial) {
                increment_u64_saturating(
                    &s_policy_binding.generation_sync_failures);
            }
            bt_ctx_unlock();
        }
        return err;
    }

    uint32_t generation = fallback_generation;
    if (status.duplex.peer_valid) {
        if (strcasecmp(peer, status.duplex.peer_mac) != 0 ||
            status.duplex.session_generation == 0U) {
            if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
                if (s_policy_binding.valid &&
                    s_policy_binding.lifecycle_serial == lifecycle_serial) {
                    increment_u64_saturating(
                        &s_policy_binding.generation_sync_failures);
                }
                bt_ctx_unlock();
            }
            record_rejected_bound_event(fallback_generation, peer);
            return ESP_ERR_INVALID_STATE;
        }
        generation = status.duplex.session_generation;
    } else if (!allow_no_session) {
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            if (s_policy_binding.valid &&
                s_policy_binding.lifecycle_serial == lifecycle_serial) {
                increment_u64_saturating(
                    &s_policy_binding.generation_sync_failures);
            }
            bt_ctx_unlock();
        }
        record_rejected_unbound_event(peer);
        return ESP_ERR_INVALID_STATE;
    }

    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!s_policy_binding.valid ||
        s_policy_binding.lifecycle_serial != lifecycle_serial ||
        strcasecmp(peer, s_policy_binding.peer_mac) != 0) {
        bt_ctx_unlock();
        record_rejected_bound_event(generation, peer);
        return ESP_ERR_INVALID_STATE;
    }
    s_policy_binding.last_duplex_generation = generation;
    *generation_out = generation;
    bt_ctx_unlock();
    return ESP_OK;
}

static void clear_binding_if_serial(uint32_t lifecycle_serial)
{
    if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) return;
    if (s_policy_binding.valid &&
        s_policy_binding.lifecycle_serial == lifecycle_serial) {
        const uint64_t missing =
            s_policy_binding.missing_binding_rejections;
        const uint64_t wrong = s_policy_binding.wrong_peer_rejections;
        const uint64_t sync = s_policy_binding.generation_sync_failures;
        const uint32_t serial = s_policy_binding.lifecycle_serial;
        memset(&s_policy_binding, 0, sizeof(s_policy_binding));
        /* Retain process-lifetime diagnostics and a monotonic serial across
         * connection teardown. */
        s_policy_binding.lifecycle_serial = serial;
        s_policy_binding.missing_binding_rejections = missing;
        s_policy_binding.wrong_peer_rejections = wrong;
        s_policy_binding.generation_sync_failures = sync;
    }
    bt_ctx_unlock();
}

static esp_err_t capture_audio_binding(const char *peer,
                                       uint32_t *serial_out,
                                       uint32_t *generation_out)
{
    if (peer == NULL || serial_out == NULL || generation_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!s_policy_binding.valid) {
        increment_u64_saturating(
            &s_policy_binding.missing_binding_rejections);
        bt_ctx_unlock();
        record_rejected_unbound_event(peer);
        return ESP_ERR_INVALID_STATE;
    }
    if (strcasecmp(peer, s_policy_binding.peer_mac) != 0) {
        const uint32_t generation = s_policy_binding.last_duplex_generation;
        increment_u64_saturating(&s_policy_binding.wrong_peer_rejections);
        bt_ctx_unlock();
        record_rejected_bound_event(generation, peer);
        return ESP_ERR_INVALID_STATE;
    }
    *serial_out = s_policy_binding.lifecycle_serial;
    *generation_out = s_policy_binding.last_duplex_generation;
    bt_ctx_unlock();
    return ESP_OK;
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
    uint32_t lifecycle_serial = 0U;
    uint32_t generation = 0U;
    esp_err_t err = create_or_capture_profile_binding(
        peer, mapped, &lifecycle_serial, &generation);
    if (err == ESP_OK) {
        err = refresh_bound_generation(
            peer, lifecycle_serial, true, generation, &generation);
    }
    if (err == ESP_OK) {
        err = bt_manager_hfp_handle_a2dp_profile_event(
            generation, peer, mapped);
    }
    if (err == ESP_OK && mapped != BT_A2DP_PROFILE_DISCONNECTED) {
        /* A generation can be created by the profile handler or rotated by a
         * concurrent HFP transition. Store the authoritative post-event value,
         * but only while the same lifecycle serial is still current. */
        err = refresh_bound_generation(
            peer, lifecycle_serial, false, generation, &generation);
    } else if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        record_rejected_bound_event(generation, peer);
    }

    if (mapped == BT_A2DP_PROFILE_DISCONNECTED ||
        (generation == 0U && err != ESP_OK)) {
        clear_binding_if_serial(lifecycle_serial);
    }
    report_policy_result("A2DP connection", err);
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
    uint32_t lifecycle_serial = 0U;
    uint32_t generation = 0U;
    esp_err_t err = capture_audio_binding(
        peer, &lifecycle_serial, &generation);
    if (err == ESP_OK) {
        err = refresh_bound_generation(
            peer, lifecycle_serial, false, generation, &generation);
    }
    if (err == ESP_OK) {
        err = bt_manager_hfp_handle_a2dp_audio_event(
            generation, peer, mapped);
    }
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        record_rejected_bound_event(generation, peer);
    }
    report_policy_result("A2DP audio", err);
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

#ifdef UNIT_TEST
esp_err_t bt_events_a2dp_test_get_binding(
    bt_events_a2dp_binding_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    memset(out, 0, sizeof(*out));
    out->valid = s_policy_binding.valid;
    safe_copy_str(out->peer_mac, sizeof(out->peer_mac),
                  s_policy_binding.peer_mac);
    out->lifecycle_serial = s_policy_binding.lifecycle_serial;
    out->last_duplex_generation =
        s_policy_binding.last_duplex_generation;
    out->missing_binding_rejections =
        s_policy_binding.missing_binding_rejections;
    out->wrong_peer_rejections = s_policy_binding.wrong_peer_rejections;
    out->generation_sync_failures =
        s_policy_binding.generation_sync_failures;
    bt_ctx_unlock();
    return ESP_OK;
}

void bt_events_a2dp_test_reset_binding(void)
{
    if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
        memset(&s_policy_binding, 0, sizeof(s_policy_binding));
        bt_ctx_unlock();
    } else {
        memset(&s_policy_binding, 0, sizeof(s_policy_binding));
    }
}
#endif

#endif // ESP_PLATFORM || UNIT_TEST

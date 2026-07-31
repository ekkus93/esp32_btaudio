#include "bt_events_a2dp_internal.h"

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
#include "esp_log.h"
#include "bt_manager_internal.h"
#include "bt_manager.h"
#include "bt_duplex_policy.h"
#include "bt_duplex_state_events.h"
#include "bt_hfp_manager.h"
#include <string.h>
#include <strings.h>

#define TAG "BT_EVT_A2DP"

a2dp_policy_binding_t s_policy_binding;

#ifdef UNIT_TEST
esp_err_t s_test_last_generation_diag_update_error = ESP_OK;
esp_err_t s_test_last_binding_clear_error = ESP_OK;
esp_err_t s_test_last_stale_record_error = ESP_OK;
esp_err_t s_test_last_unbound_status_error = ESP_OK;
esp_err_t s_test_last_connection_policy_error = ESP_OK;
#endif

static uint32_t next_lifecycle_serial(uint32_t current)
{
    current++;
    return current == 0U ? 1U : current;
}

esp_err_t record_rejected_bound_event(uint32_t generation,
                                        const char *event_peer,
                                        const char *reason)
{
    if (generation == 0U || event_peer == NULL) {
#ifdef UNIT_TEST
        s_test_last_stale_record_error = ESP_ERR_NOT_FOUND;
#endif
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = bt_duplex_record_stale_operation_event(
        generation, event_peer);
#ifdef UNIT_TEST
    s_test_last_stale_record_error = err;
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "A2DP stale-operation telemetry failed: peer=%s generation=%u reason=%s error=%s",
                 event_peer,
                 (unsigned)generation,
                 reason != NULL ? reason : "UNKNOWN",
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t record_rejected_unbound_event(const char *event_peer,
                                        const char *reason)
{
    if (event_peer == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    bt_hfp_manager_status_t status;
    esp_err_t status_err = bt_manager_hfp_get_status(&status);
#ifdef UNIT_TEST
    s_test_last_unbound_status_error = status_err;
#endif
    if (status_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "A2DP unbound stale-operation status lookup failed: peer=%s reason=%s error=%s",
                 event_peer,
                 reason != NULL ? reason : "UNKNOWN",
                 esp_err_to_name(status_err));
        return status_err;
    }
    if (!status.duplex.peer_valid ||
        status.duplex.session_generation == 0U) {
        return ESP_ERR_NOT_FOUND;
    }

    return record_rejected_bound_event(
        status.duplex.session_generation, event_peer, reason);
}

static void apply_base_profile_state_locked(a2dp_bound_profile_event_t *bound)
{
    if (bound->state == BT_A2DP_PROFILE_CONNECTED) {
        bt_ctx.connected = true;
        bt_ctx.connecting = false;
        safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac),
                      bound->peer_mac);
        bound->connected_callback = bt_ctx.connected_callback;
        safe_copy_str(bound->callback_mac, sizeof(bound->callback_mac),
                      bt_ctx.connected_mac);
        safe_copy_str(bound->callback_name, sizeof(bound->callback_name),
                      bt_ctx.connected_name);
    } else if (bound->state == BT_A2DP_PROFILE_DISCONNECTED) {
        bound->disconnected_callback = bt_ctx.disconnected_callback;
        safe_copy_str(bound->callback_mac, sizeof(bound->callback_mac),
                      bt_ctx.connected_mac);
        bt_ctx.connected = false;
        bt_ctx.connecting = false;
        bt_ctx.audio_playing = false;
    }
}

static esp_err_t create_or_capture_profile_binding(
    a2dp_bound_profile_event_t *bound)
{
    if (bound == NULL || bound->peer_mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;

    if (s_policy_binding.valid) {
        const uint32_t generation = s_policy_binding.last_duplex_generation;
        if (strcasecmp(bound->peer_mac, s_policy_binding.peer_mac) != 0) {
            increment_u64_saturating(
                &s_policy_binding.wrong_peer_rejections);
            bt_ctx_unlock();
            record_rejected_bound_event(
                generation, bound->peer_mac, "PROFILE_WRONG_PEER");
            return ESP_ERR_INVALID_STATE;
        }
        if (bound->conn_handle != s_policy_binding.conn_handle) {
            increment_u64_saturating(
                &s_policy_binding.stale_handle_rejections);
            bt_ctx_unlock();
            record_rejected_bound_event(
                generation, bound->peer_mac, "PROFILE_STALE_HANDLE");
            return ESP_ERR_INVALID_STATE;
        }
        bound->lifecycle_serial = s_policy_binding.lifecycle_serial;
        bound->generation = generation;
        apply_base_profile_state_locked(bound);
        bt_ctx_unlock();
        return ESP_OK;
    }

    if (bound->state == BT_A2DP_PROFILE_DISCONNECTED ||
        bound->state == BT_A2DP_PROFILE_DISCONNECTING) {
        increment_u64_saturating(
            &s_policy_binding.missing_binding_rejections);
        bt_ctx_unlock();
        record_rejected_unbound_event(
            bound->peer_mac, "PROFILE_NO_ACTIVE_BINDING");
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t serial =
        next_lifecycle_serial(s_policy_binding.lifecycle_serial);
    memset(s_policy_binding.peer_mac, 0,
           sizeof(s_policy_binding.peer_mac));
    safe_copy_str(s_policy_binding.peer_mac,
                  sizeof(s_policy_binding.peer_mac), bound->peer_mac);
    s_policy_binding.valid = true;
    s_policy_binding.conn_handle = bound->conn_handle;
    s_policy_binding.lifecycle_serial = serial;
    s_policy_binding.last_duplex_generation = 0U;
    bound->lifecycle_serial = serial;
    bound->generation = 0U;
    apply_base_profile_state_locked(bound);
    bt_ctx_unlock();
    return ESP_OK;
}

static esp_err_t increment_generation_sync_failure(
    uint32_t lifecycle_serial,
    esp_a2d_conn_hdl_t conn_handle)
{
    esp_err_t lock_err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (lock_err != ESP_OK) return lock_err;
    if (s_policy_binding.valid &&
        s_policy_binding.lifecycle_serial == lifecycle_serial &&
        s_policy_binding.conn_handle == conn_handle) {
        increment_u64_saturating(
            &s_policy_binding.generation_sync_failures);
    }
    bt_ctx_unlock();
    return ESP_OK;
}

static esp_err_t preserve_primary_generation_error(
    esp_err_t primary_error,
    uint32_t lifecycle_serial,
    esp_a2d_conn_hdl_t conn_handle)
{
    esp_err_t diag_err = increment_generation_sync_failure(
        lifecycle_serial, conn_handle);
#ifdef UNIT_TEST
    s_test_last_generation_diag_update_error = diag_err;
#endif
    if (diag_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "A2DP generation failure diagnostic update failed: lifecycle=%u handle=%u primary=%s diagnostic=%s",
                 (unsigned)lifecycle_serial,
                 (unsigned)conn_handle,
                 esp_err_to_name(primary_error),
                 esp_err_to_name(diag_err));
    }
    return primary_error;
}

esp_err_t refresh_bound_generation(const char *peer,
                                    esp_a2d_conn_hdl_t conn_handle,
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
        return preserve_primary_generation_error(
            err, lifecycle_serial, conn_handle);
    }

    uint32_t generation = fallback_generation;
    if (status.duplex.peer_valid) {
        if (strcasecmp(peer, status.duplex.peer_mac) != 0 ||
            status.duplex.session_generation == 0U) {
            err = preserve_primary_generation_error(
                ESP_ERR_INVALID_STATE, lifecycle_serial, conn_handle);
            record_rejected_bound_event(
                fallback_generation, peer, "GENERATION_PEER_MISMATCH");
            return err;
        }
        generation = status.duplex.session_generation;
    } else if (!allow_no_session) {
        err = preserve_primary_generation_error(
            ESP_ERR_INVALID_STATE, lifecycle_serial, conn_handle);
        record_rejected_unbound_event(
            peer, "GENERATION_NO_ACTIVE_SESSION");
        return err;
    }

    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!s_policy_binding.valid ||
        s_policy_binding.lifecycle_serial != lifecycle_serial ||
        s_policy_binding.conn_handle != conn_handle ||
        strcasecmp(peer, s_policy_binding.peer_mac) != 0) {
        bt_ctx_unlock();
        record_rejected_bound_event(
            generation, peer, "GENERATION_BINDING_CHANGED");
        return ESP_ERR_INVALID_STATE;
    }
    s_policy_binding.last_duplex_generation = generation;
    *generation_out = generation;
    bt_ctx_unlock();
    return ESP_OK;
}

/* Return ESP_ERR_NOT_FOUND when the expected identity no longer owns the
 * binding. That is an idempotent stale-session outcome, not a successful clear.
 * Lock acquisition failures are returned exactly and never trigger an unlocked
 * fallback. */
esp_err_t clear_binding_if_identity(uint32_t lifecycle_serial,
                                    esp_a2d_conn_hdl_t conn_handle)
{
    esp_err_t lock_err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (lock_err != ESP_OK) return lock_err;
    if (!s_policy_binding.valid ||
        s_policy_binding.lifecycle_serial != lifecycle_serial ||
        s_policy_binding.conn_handle != conn_handle) {
        bt_ctx_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    const uint64_t missing =
        s_policy_binding.missing_binding_rejections;
    const uint64_t wrong = s_policy_binding.wrong_peer_rejections;
    const uint64_t stale = s_policy_binding.stale_handle_rejections;
    const uint64_t sync = s_policy_binding.generation_sync_failures;
    const uint64_t late_terminal =
        s_policy_binding.late_terminal_events_ignored;
    const uint32_t serial = s_policy_binding.lifecycle_serial;
    memset(&s_policy_binding, 0, sizeof(s_policy_binding));
    s_policy_binding.lifecycle_serial = serial;
    s_policy_binding.missing_binding_rejections = missing;
    s_policy_binding.wrong_peer_rejections = wrong;
    s_policy_binding.stale_handle_rejections = stale;
    s_policy_binding.generation_sync_failures = sync;
    s_policy_binding.late_terminal_events_ignored = late_terminal;
    bt_ctx_unlock();
    return ESP_OK;
}

esp_err_t capture_audio_binding(a2dp_bound_audio_event_t *bound)
{
    if (bound == NULL || bound->peer_mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!s_policy_binding.valid) {
        const bool terminal =
            bound->state == BT_A2DP_AUDIO_STOPPED ||
            bound->state == BT_A2DP_AUDIO_REMOTE_SUSPENDED;
        if (terminal) {
            /* DISCONNECTED is authoritative and already cleared playback.
             * A later terminal event has no trusted identity and is therefore
             * observable only as an idempotent no-op. */
            increment_u64_saturating(
                &s_policy_binding.late_terminal_events_ignored);
            bt_ctx_unlock();
            ESP_LOGD(TAG,
                     "Ignoring unbound terminal A2DP audio event: state=%d peer=%s handle=%u reason=NO_ACTIVE_BINDING",
                     (int)bound->state, bound->peer_mac,
                     (unsigned)bound->conn_handle);
            return ESP_ERR_NOT_FOUND;
        }
        increment_u64_saturating(
            &s_policy_binding.missing_binding_rejections);
        bt_ctx_unlock();
        record_rejected_unbound_event(
            bound->peer_mac, "AUDIO_NO_ACTIVE_BINDING");
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t generation = s_policy_binding.last_duplex_generation;
    if (strcasecmp(bound->peer_mac, s_policy_binding.peer_mac) != 0) {
        increment_u64_saturating(&s_policy_binding.wrong_peer_rejections);
        bt_ctx_unlock();
        record_rejected_bound_event(
                generation, bound->peer_mac, "AUDIO_WRONG_PEER");
        return ESP_ERR_INVALID_STATE;
    }
    if (bound->conn_handle != s_policy_binding.conn_handle) {
        increment_u64_saturating(&s_policy_binding.stale_handle_rejections);
        bt_ctx_unlock();
        record_rejected_bound_event(
                generation, bound->peer_mac, "AUDIO_STALE_HANDLE");
        return ESP_ERR_INVALID_STATE;
    }
    bound->lifecycle_serial = s_policy_binding.lifecycle_serial;
    bound->generation = generation;
    bt_ctx.audio_playing = bound->state == BT_A2DP_AUDIO_STARTED;
    bt_ctx_unlock();
    return ESP_OK;
}

esp_err_t prepare_connection_event(
    const esp_a2d_cb_param_t *param,
    a2dp_bound_profile_event_t *bound)
{
    if (param == NULL || bound == NULL) return ESP_ERR_INVALID_ARG;
    memset(bound, 0, sizeof(*bound));
    switch (param->conn_stat.state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        bound->state = BT_A2DP_PROFILE_DISCONNECTED;
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
        bound->state = BT_A2DP_PROFILE_CONNECTING;
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
        bound->state = BT_A2DP_PROFILE_CONNECTED;
        break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
        bound->state = BT_A2DP_PROFILE_DISCONNECTING;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    bda_to_string(param->conn_stat.remote_bda, bound->peer_mac);
    bound->conn_handle = param->conn_stat.conn_hdl;
    return create_or_capture_profile_binding(bound);
}

esp_err_t prepare_audio_event(const esp_a2d_cb_param_t *param,
                              a2dp_bound_audio_event_t *bound)
{
    if (param == NULL || bound == NULL) return ESP_ERR_INVALID_ARG;
    memset(bound, 0, sizeof(*bound));
    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        bound->state = BT_A2DP_AUDIO_STARTED;
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        bound->state = BT_A2DP_AUDIO_REMOTE_SUSPENDED;
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
        bound->state = BT_A2DP_AUDIO_STOPPED;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    bda_to_string(param->audio_stat.remote_bda, bound->peer_mac);
    bound->conn_handle = param->audio_stat.conn_hdl;
    return capture_audio_binding(bound);
}

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
    out->conn_handle = s_policy_binding.conn_handle;
    out->lifecycle_serial = s_policy_binding.lifecycle_serial;
    out->last_duplex_generation =
        s_policy_binding.last_duplex_generation;
    out->missing_binding_rejections =
        s_policy_binding.missing_binding_rejections;
    out->wrong_peer_rejections = s_policy_binding.wrong_peer_rejections;
    out->stale_handle_rejections =
        s_policy_binding.stale_handle_rejections;
    out->generation_sync_failures =
        s_policy_binding.generation_sync_failures;
    out->late_terminal_events_ignored =
        s_policy_binding.late_terminal_events_ignored;
    bt_ctx_unlock();
    return ESP_OK;
}

esp_err_t bt_events_a2dp_test_reset_binding(void)
{
    return bt_events_a2dp_reset_binding();
}

void bt_events_a2dp_test_reset_secondary_errors(void)
{
    s_test_last_generation_diag_update_error = ESP_OK;
    s_test_last_binding_clear_error = ESP_OK;
    s_test_last_connection_policy_error = ESP_OK;
}

void bt_events_a2dp_test_reset_telemetry_errors(void)
{
    s_test_last_stale_record_error = ESP_OK;
    s_test_last_unbound_status_error = ESP_OK;
}

esp_err_t bt_events_a2dp_test_get_last_generation_diag_update_error(void)
{
    return s_test_last_generation_diag_update_error;
}

esp_err_t bt_events_a2dp_test_get_last_binding_clear_error(void)
{
    return s_test_last_binding_clear_error;
}

esp_err_t bt_events_a2dp_test_get_last_stale_record_error(void)
{
    return s_test_last_stale_record_error;
}

esp_err_t bt_events_a2dp_test_get_last_unbound_status_error(void)
{
    return s_test_last_unbound_status_error;
}

esp_err_t bt_events_a2dp_test_get_last_connection_policy_error(void)
{
    return s_test_last_connection_policy_error;
}

esp_err_t bt_events_a2dp_test_prepare_audio_event(
    const esp_a2d_cb_param_t *param)
{
    a2dp_bound_audio_event_t bound;
    return prepare_audio_event(param, &bound);
}

esp_err_t bt_events_a2dp_test_refresh_bound_generation(
    const char *peer,
    esp_a2d_conn_hdl_t conn_handle,
    uint32_t lifecycle_serial,
    bool allow_no_session,
    uint32_t fallback_generation,
    uint32_t *generation_out)
{
    return refresh_bound_generation(
        peer,
        conn_handle,
        lifecycle_serial,
        allow_no_session,
        fallback_generation,
        generation_out);
}

esp_err_t bt_events_a2dp_test_clear_binding_if_identity(
    uint32_t lifecycle_serial,
    esp_a2d_conn_hdl_t conn_handle)
{
    return clear_binding_if_identity(lifecycle_serial, conn_handle);
}
#endif

#endif // ESP_PLATFORM || UNIT_TEST

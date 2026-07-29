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
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t last_duplex_generation;
    uint64_t missing_binding_rejections;
    uint64_t wrong_peer_rejections;
    uint64_t stale_handle_rejections;
    uint64_t generation_sync_failures;
    uint64_t late_terminal_events_ignored;
} a2dp_policy_binding_t;

typedef struct {
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t generation;
    bt_a2dp_profile_state_t state;
    bt_connected_cb connected_callback;
    bt_disconnected_cb disconnected_callback;
    char callback_mac[A2DP_BINDING_MAC_STR_LEN];
    char callback_name[32];
} a2dp_bound_profile_event_t;

typedef struct {
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t generation;
    bt_a2dp_audio_state_t state;
} a2dp_bound_audio_event_t;

/* Guarded by bt_ctx.lock. The ESP-IDF connection handle is the event-owned
 * identity token. The duplex generation may legitimately rotate while the
 * same A2DP connection remains active, so generation refresh is allowed only
 * after peer, lifecycle serial, and connection handle have all matched. */
static a2dp_policy_binding_t s_policy_binding;

#ifdef UNIT_TEST
/* Test-only observations of secondary failures that are otherwise surfaced by
 * production logs. They let host tests assert the exact error without changing
 * the public ESP-IDF callback signatures. */
static esp_err_t s_test_last_generation_diag_update_error = ESP_OK;
static esp_err_t s_test_last_binding_clear_error = ESP_OK;
#endif

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
            record_rejected_bound_event(generation, bound->peer_mac);
            return ESP_ERR_INVALID_STATE;
        }
        if (bound->conn_handle != s_policy_binding.conn_handle) {
            increment_u64_saturating(
                &s_policy_binding.stale_handle_rejections);
            bt_ctx_unlock();
            record_rejected_bound_event(generation, bound->peer_mac);
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
        record_rejected_unbound_event(bound->peer_mac);
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

static esp_err_t refresh_bound_generation(const char *peer,
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
            record_rejected_bound_event(fallback_generation, peer);
            return err;
        }
        generation = status.duplex.session_generation;
    } else if (!allow_no_session) {
        err = preserve_primary_generation_error(
            ESP_ERR_INVALID_STATE, lifecycle_serial, conn_handle);
        record_rejected_unbound_event(peer);
        return err;
    }

    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!s_policy_binding.valid ||
        s_policy_binding.lifecycle_serial != lifecycle_serial ||
        s_policy_binding.conn_handle != conn_handle ||
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

/* Return ESP_ERR_NOT_FOUND when the expected identity no longer owns the
 * binding. That is an idempotent stale-session outcome, not a successful clear.
 * Lock acquisition failures are returned exactly and never trigger an unlocked
 * fallback. */
static esp_err_t clear_binding_if_identity(uint32_t lifecycle_serial,
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

static esp_err_t capture_audio_binding(a2dp_bound_audio_event_t *bound)
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
        record_rejected_unbound_event(bound->peer_mac);
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t generation = s_policy_binding.last_duplex_generation;
    if (strcasecmp(bound->peer_mac, s_policy_binding.peer_mac) != 0) {
        increment_u64_saturating(&s_policy_binding.wrong_peer_rejections);
        bt_ctx_unlock();
        record_rejected_bound_event(generation, bound->peer_mac);
        return ESP_ERR_INVALID_STATE;
    }
    if (bound->conn_handle != s_policy_binding.conn_handle) {
        increment_u64_saturating(&s_policy_binding.stale_handle_rejections);
        bt_ctx_unlock();
        record_rejected_bound_event(generation, bound->peer_mac);
        return ESP_ERR_INVALID_STATE;
    }
    bound->lifecycle_serial = s_policy_binding.lifecycle_serial;
    bound->generation = generation;
    bt_ctx.audio_playing = bound->state == BT_A2DP_AUDIO_STARTED;
    bt_ctx_unlock();
    return ESP_OK;
}

static esp_err_t prepare_connection_event(
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

static esp_err_t prepare_audio_event(const esp_a2d_cb_param_t *param,
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
        record_rejected_bound_event(bound->generation, bound->peer_mac);
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
        if ((err == ESP_OK || err == ESP_ERR_NOT_FOUND) &&
            clear_err != ESP_OK) {
            err = clear_err;
        }
    }
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
        record_rejected_bound_event(bound->generation, bound->peer_mac);
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


#ifdef ESP_PLATFORM
// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written
int32_t bt_events_a2dp_data_callback(uint8_t *buf, int32_t len)
{
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

    size_t req = (size_t)len;

    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, req, &bytes_read);
    if (ret != ESP_OK) {
        return 0;
    }

    return (int32_t)bytes_read;
}
#endif // ESP_PLATFORM

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
}

esp_err_t bt_events_a2dp_test_get_last_generation_diag_update_error(void)
{
    return s_test_last_generation_diag_update_error;
}

esp_err_t bt_events_a2dp_test_get_last_binding_clear_error(void)
{
    return s_test_last_binding_clear_error;
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

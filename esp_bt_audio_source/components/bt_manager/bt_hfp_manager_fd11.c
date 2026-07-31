#include "bt_hfp_manager.h"

#include <ctype.h>
#include <string.h>

#include "bt_duplex_state_mode.h"
#include "bt_hfp_audio.h"
#include "bt_hfp_connection.h"
#include "bt_manager_internal.h"
#include "hfp_i2s_output.h"
#include "platform_sync.h"
#include "util_safe.h"

#define BT_HFP_STATS_CAPTURE_RETRIES 4U

typedef struct {
    bt_hfp_manager_stats_t stats;
    bt_duplex_snapshot_t duplex;
    bool slc_pending;
    bool audio_control_pending;
    bool audio_control_api_active;
    bool incoming_accepting;
    uint32_t active_callbacks;
    hfp_i2s_output_state_t local_i2s_state;
} bt_hfp_raw_stats_t;

static bt_duplex_mode_t s_configured_mode = BT_DUPLEX_MODE_DISABLED;
static bt_hfp_manager_stats_t s_stats_baseline;
static uint64_t s_stats_reset_sequence;

static bool same_mac(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL || strlen(lhs) != 17U ||
        strlen(rhs) != 17U) return false;
    for (size_t index = 0U; index < 17U; ++index) {
        if (tolower((unsigned char)lhs[index]) !=
            tolower((unsigned char)rhs[index])) return false;
    }
    return true;
}

static bool valid_mac(const char *mac)
{
    uint8_t parsed[6];
    return mac != NULL && strlen(mac) == 17U && util_parse_mac(mac, parsed);
}

static bool valid_mode(bt_duplex_mode_t mode)
{
    return (int)mode >= 0 && mode < BT_DUPLEX_MODE_COUNT;
}

static bool stable_duplex_identity(const bt_duplex_snapshot_t *before,
                                   const bt_duplex_snapshot_t *after)
{
    if (before->peer_valid != after->peer_valid ||
        before->session_generation != after->session_generation ||
        before->requested_mode != after->requested_mode ||
        before->effective_mode != after->effective_mode ||
        before->a2dp_profile_state != after->a2dp_profile_state ||
        before->a2dp_audio_state != after->a2dp_audio_state ||
        before->hfp_profile_state != after->hfp_profile_state ||
        before->hfp_audio_state != after->hfp_audio_state ||
        before->codec != after->codec ||
        before->i2s_state != after->i2s_state ||
        before->health != after->health ||
        before->last_error != after->last_error) return false;
    if (before->peer_valid &&
        memcmp(before->peer_mac, after->peer_mac,
               sizeof(before->peer_mac)) != 0) return false;
    return memcmp(before->last_error_text, after->last_error_text,
                  sizeof(before->last_error_text)) == 0;
}

static esp_err_t optional_slc_snapshot(bt_hfp_connection_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    esp_err_t err = bt_hfp_connection_get_snapshot(out);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static esp_err_t optional_audio_control_snapshot(
    bt_hfp_audio_control_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    esp_err_t err = bt_hfp_audio_control_get_snapshot(out);
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    if (err != ESP_OK) return err;
    return bt_duplex_get_health_report_diagnostics(
        &out->health_report_failures,
        &out->last_health_report_error);
}

static esp_err_t optional_incoming_snapshot(bt_hfp_audio_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    esp_err_t err = bt_hfp_audio_get_snapshot(out);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static esp_err_t optional_i2s_snapshot(hfp_i2s_output_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = HFP_I2S_OUTPUT_UNINITIALIZED;
    esp_err_t err = hfp_i2s_output_get_snapshot(out);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static void map_slc(bt_hfp_manager_slc_stats_t *out,
                    const bt_hfp_connection_snapshot_t *in)
{
    out->accepted_connect_requests = in->accepted_connect_requests;
    out->accepted_disconnect_requests = in->accepted_disconnect_requests;
    out->immediate_failures = in->immediate_failures;
    out->remote_rejections = in->remote_rejections;
    out->watchdog_timeouts = in->watchdog_timeouts;
    out->stale_operation_events = in->stale_operation_events;
    out->wrong_peer_events = in->wrong_peer_events;
    out->dispatch_failures = in->dispatch_failures;
}

static void map_control(bt_hfp_manager_audio_control_stats_t *out,
                        const bt_hfp_audio_control_snapshot_t *in)
{
#define COPY(field) out->field = in->field
    COPY(start_calls); COPY(stop_calls); COPY(successful_starts);
    COPY(successful_stops); COPY(start_failures); COPY(stop_failures);
    COPY(dispatch_failures); COPY(immediate_failures); COPY(request_timeouts);
    COPY(event_timeouts); COPY(stale_events); COPY(wrong_peer_events);
    COPY(unexpected_connected_events); COPY(rollback_attempts);
    COPY(rollback_failures); COPY(cleanup_disconnect_requests);
    COPY(cleanup_disconnect_failures); COPY(i2s_start_failures);
    COPY(i2s_stop_failures); COPY(i2s_state_sync_failures);
    COPY(health_report_failures);
    COPY(last_health_report_error);
#undef COPY
}

static void map_incoming(bt_hfp_manager_incoming_stats_t *out,
                         const bt_hfp_audio_snapshot_t *in)
{
#define COPY(field) out->field = in->field
    COPY(registration_failures); COPY(activation_failures);
    COPY(incoming_callbacks); COPY(accepted_frames); COPY(accepted_bytes);
    COPY(dropped_frames); COPY(dropped_bytes); COPY(invalid_frames);
    COPY(invalid_bytes); COPY(inactive_frames); COPY(inactive_bytes);
    COPY(stale_handle_frames); COPY(stale_handle_bytes); COPY(bad_frames);
    COPY(bad_bytes); COPY(unsupported_codec_frames);
    COPY(unsupported_codec_bytes); COPY(ring_rejected_frames);
    COPY(ring_rejected_bytes); COPY(callback_over_budget);
    COPY(callback_overlap_rejections); COPY(callback_last_us);
#undef COPY
    out->callback_max_us_lifetime = in->callback_max_us;
}

static void map_i2s(bt_hfp_manager_i2s_stats_t *out,
                    const hfp_i2s_output_snapshot_t *in)
{
#define COPY(field) out->field = in->field
    COPY(start_calls); COPY(stop_calls); COPY(start_failures);
    COPY(stop_timeouts); COPY(write_calls); COPY(write_failures);
    COPY(short_writes); COPY(write_lost_bytes); COPY(silence_intervals);
    COPY(silence_samples); COPY(degraded_events); COPY(push_calls);
    COPY(push_failures); COPY(stale_pushes); COPY(invalid_pushes);
    COPY(quarantine_events);
#undef COPY
    out->ring_written_bytes = in->ring.total_written_bytes;
    out->ring_read_bytes = in->ring.total_read_bytes;
    out->ring_overflow_frames = in->ring.overflow_frames;
    out->ring_overflow_bytes = in->ring.overflow_bytes;
    out->ring_underflow_events = in->ring.underflow_events;
    out->ring_underflow_bytes = in->ring.underflow_bytes;
    out->ring_stale_operations = in->ring.stale_generation_operations;
    out->ring_invalid_operations = in->ring.invalid_operations;
    out->ring_current_used = in->ring.current_used;
    out->ring_peak_used_lifetime = in->ring.peak_used;
}

static esp_err_t capture_absolute_stats(bt_hfp_raw_stats_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    for (unsigned attempt = 0U; attempt < BT_HFP_STATS_CAPTURE_RETRIES;
         ++attempt) {
        bt_duplex_snapshot_t before;
        bt_duplex_snapshot_t after;
        bt_hfp_connection_snapshot_t slc;
        bt_hfp_audio_control_snapshot_t control;
        bt_hfp_audio_snapshot_t incoming;
        hfp_i2s_output_snapshot_t i2s;

        esp_err_t err = bt_duplex_get_snapshot(&before);
        if (err != ESP_OK) return err;
        if ((err = optional_slc_snapshot(&slc)) != ESP_OK) return err;
        if ((err = optional_audio_control_snapshot(&control)) != ESP_OK) return err;
        if ((err = optional_incoming_snapshot(&incoming)) != ESP_OK) return err;
        if ((err = optional_i2s_snapshot(&i2s)) != ESP_OK) return err;
        if ((err = bt_duplex_get_snapshot(&after)) != ESP_OK) return err;
        if (!stable_duplex_identity(&before, &after)) continue;

        memset(out, 0, sizeof(*out));
        out->duplex = after;
        out->stats.generation = after.session_generation;
        out->stats.peer_valid = after.peer_valid;
        if (after.peer_valid) {
            util_safe_copy_str(out->stats.peer_mac,
                               sizeof(out->stats.peer_mac), after.peer_mac);
        }
        out->stats.duplex = after.counters;
        map_slc(&out->stats.slc, &slc);
        map_control(&out->stats.audio_control, &control);
        map_incoming(&out->stats.incoming, &incoming);
        map_i2s(&out->stats.i2s, &i2s);
        out->slc_pending = slc.pending;
        out->audio_control_pending = control.pending;
        out->audio_control_api_active = control.api_active;
        out->incoming_accepting = incoming.accepting_incoming;
        out->active_callbacks = incoming.active_callbacks;
        out->local_i2s_state = i2s.state;
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

#define REG(group, field) ((current)->group.field < (baseline)->group.field)
static bool counters_regressed(const bt_hfp_manager_stats_t *current,
                               const bt_hfp_manager_stats_t *baseline)
{
    return
        REG(duplex, stale_generation_events) || REG(duplex, wrong_peer_events) ||
        REG(duplex, illegal_transitions) || REG(duplex, invalid_arguments) ||
        REG(duplex, recoveries) || REG(duplex, incoming_frames) ||
        REG(duplex, incoming_bytes) || REG(duplex, incoming_dropped_frames) ||
        REG(duplex, incoming_dropped_bytes) || REG(duplex, i2s_underflows) ||
        REG(duplex, i2s_timeouts) ||
        REG(slc, accepted_connect_requests) ||
        REG(slc, accepted_disconnect_requests) || REG(slc, immediate_failures) ||
        REG(slc, remote_rejections) || REG(slc, watchdog_timeouts) ||
        REG(slc, stale_operation_events) || REG(slc, wrong_peer_events) ||
        REG(slc, dispatch_failures) ||
        REG(audio_control, start_calls) || REG(audio_control, stop_calls) ||
        REG(audio_control, successful_starts) ||
        REG(audio_control, successful_stops) ||
        REG(audio_control, start_failures) || REG(audio_control, stop_failures) ||
        REG(audio_control, dispatch_failures) ||
        REG(audio_control, immediate_failures) ||
        REG(audio_control, request_timeouts) ||
        REG(audio_control, event_timeouts) || REG(audio_control, stale_events) ||
        REG(audio_control, wrong_peer_events) ||
        REG(audio_control, unexpected_connected_events) ||
        REG(audio_control, rollback_attempts) ||
        REG(audio_control, rollback_failures) ||
        REG(audio_control, cleanup_disconnect_requests) ||
        REG(audio_control, cleanup_disconnect_failures) ||
        REG(audio_control, i2s_start_failures) ||
        REG(audio_control, i2s_stop_failures) ||
        REG(audio_control, i2s_state_sync_failures) ||
        REG(audio_control, health_report_failures) ||
        REG(incoming, registration_failures) ||
        REG(incoming, activation_failures) || REG(incoming, incoming_callbacks) ||
        REG(incoming, accepted_frames) || REG(incoming, accepted_bytes) ||
        REG(incoming, dropped_frames) || REG(incoming, dropped_bytes) ||
        REG(incoming, invalid_frames) || REG(incoming, invalid_bytes) ||
        REG(incoming, inactive_frames) || REG(incoming, inactive_bytes) ||
        REG(incoming, stale_handle_frames) ||
        REG(incoming, stale_handle_bytes) || REG(incoming, bad_frames) ||
        REG(incoming, bad_bytes) ||
        REG(incoming, unsupported_codec_frames) ||
        REG(incoming, unsupported_codec_bytes) ||
        REG(incoming, ring_rejected_frames) ||
        REG(incoming, ring_rejected_bytes) ||
        REG(incoming, callback_over_budget) ||
        REG(incoming, callback_overlap_rejections) ||
        REG(incoming, callback_max_us_lifetime) ||
        REG(i2s, start_calls) || REG(i2s, stop_calls) ||
        REG(i2s, start_failures) || REG(i2s, stop_timeouts) ||
        REG(i2s, write_calls) || REG(i2s, write_failures) ||
        REG(i2s, short_writes) || REG(i2s, write_lost_bytes) ||
        REG(i2s, silence_intervals) || REG(i2s, silence_samples) ||
        REG(i2s, degraded_events) || REG(i2s, push_calls) ||
        REG(i2s, push_failures) || REG(i2s, stale_pushes) ||
        REG(i2s, invalid_pushes) || REG(i2s, quarantine_events) ||
        REG(i2s, ring_written_bytes) || REG(i2s, ring_read_bytes) ||
        REG(i2s, ring_overflow_frames) || REG(i2s, ring_overflow_bytes) ||
        REG(i2s, ring_underflow_events) || REG(i2s, ring_underflow_bytes) ||
        REG(i2s, ring_stale_operations) || REG(i2s, ring_invalid_operations) ||
        REG(i2s, ring_peak_used_lifetime);
}
#undef REG

#define SUB(group, field) \
    (current)->group.field -= (baseline)->group.field
static void subtract_baseline(bt_hfp_manager_stats_t *current,
                              const bt_hfp_manager_stats_t *baseline)
{
    SUB(duplex, stale_generation_events); SUB(duplex, wrong_peer_events);
    SUB(duplex, illegal_transitions); SUB(duplex, invalid_arguments);
    SUB(duplex, recoveries); SUB(duplex, incoming_frames);
    SUB(duplex, incoming_bytes); SUB(duplex, incoming_dropped_frames);
    SUB(duplex, incoming_dropped_bytes); SUB(duplex, i2s_underflows);
    SUB(duplex, i2s_timeouts);

    SUB(slc, accepted_connect_requests); SUB(slc, accepted_disconnect_requests);
    SUB(slc, immediate_failures); SUB(slc, remote_rejections);
    SUB(slc, watchdog_timeouts); SUB(slc, stale_operation_events);
    SUB(slc, wrong_peer_events); SUB(slc, dispatch_failures);

    SUB(audio_control, start_calls); SUB(audio_control, stop_calls);
    SUB(audio_control, successful_starts); SUB(audio_control, successful_stops);
    SUB(audio_control, start_failures); SUB(audio_control, stop_failures);
    SUB(audio_control, dispatch_failures); SUB(audio_control, immediate_failures);
    SUB(audio_control, request_timeouts); SUB(audio_control, event_timeouts);
    SUB(audio_control, stale_events); SUB(audio_control, wrong_peer_events);
    SUB(audio_control, unexpected_connected_events);
    SUB(audio_control, rollback_attempts); SUB(audio_control, rollback_failures);
    SUB(audio_control, cleanup_disconnect_requests);
    SUB(audio_control, cleanup_disconnect_failures);
    SUB(audio_control, i2s_start_failures); SUB(audio_control, i2s_stop_failures);
    SUB(audio_control, i2s_state_sync_failures);
    SUB(audio_control, health_report_failures);
    if (current->audio_control.health_report_failures == 0U) {
        current->audio_control.last_health_report_error = ESP_OK;
    }

    SUB(incoming, registration_failures); SUB(incoming, activation_failures);
    SUB(incoming, incoming_callbacks); SUB(incoming, accepted_frames);
    SUB(incoming, accepted_bytes); SUB(incoming, dropped_frames);
    SUB(incoming, dropped_bytes); SUB(incoming, invalid_frames);
    SUB(incoming, invalid_bytes); SUB(incoming, inactive_frames);
    SUB(incoming, inactive_bytes); SUB(incoming, stale_handle_frames);
    SUB(incoming, stale_handle_bytes); SUB(incoming, bad_frames);
    SUB(incoming, bad_bytes); SUB(incoming, unsupported_codec_frames);
    SUB(incoming, unsupported_codec_bytes); SUB(incoming, ring_rejected_frames);
    SUB(incoming, ring_rejected_bytes); SUB(incoming, callback_over_budget);
    SUB(incoming, callback_overlap_rejections);

    SUB(i2s, start_calls); SUB(i2s, stop_calls); SUB(i2s, start_failures);
    SUB(i2s, stop_timeouts); SUB(i2s, write_calls); SUB(i2s, write_failures);
    SUB(i2s, short_writes); SUB(i2s, write_lost_bytes);
    SUB(i2s, silence_intervals); SUB(i2s, silence_samples);
    SUB(i2s, degraded_events); SUB(i2s, push_calls); SUB(i2s, push_failures);
    SUB(i2s, stale_pushes); SUB(i2s, invalid_pushes);
    SUB(i2s, quarantine_events); SUB(i2s, ring_written_bytes);
    SUB(i2s, ring_read_bytes); SUB(i2s, ring_overflow_frames);
    SUB(i2s, ring_overflow_bytes); SUB(i2s, ring_underflow_events);
    SUB(i2s, ring_underflow_bytes); SUB(i2s, ring_stale_operations);
    SUB(i2s, ring_invalid_operations);
}
#undef SUB

void bt_manager_hfp_runtime_reset(void)
{
    s_configured_mode = BT_DUPLEX_MODE_DISABLED;
    memset(&s_stats_baseline, 0, sizeof(s_stats_baseline));
    s_stats_reset_sequence = 0U;
    bt_manager_hfp_policy_runtime_reset();
}

bt_duplex_mode_t bt_manager_hfp_configured_mode_locked(void)
{
    return s_configured_mode;
}

esp_err_t bt_manager_hfp_get_configured_mode(bt_duplex_mode_t *mode_out)
{
    if (mode_out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    *mode_out = s_configured_mode;
    bt_ctx_unlock();
    return ESP_OK;
}

esp_err_t bt_manager_hfp_set_mode(bt_duplex_mode_t mode)
{
    if (!valid_mode(mode)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    bt_duplex_snapshot_t duplex;
    err = bt_duplex_get_snapshot(&duplex);
    if (err == ESP_OK && duplex.peer_valid) {
        err = bt_duplex_set_mode(duplex.session_generation,
                                 duplex.peer_mac, mode);
        if (err == ESP_OK) err = bt_manager_hfp_policy_refresh_locked();
    }
    if (err == ESP_OK) s_configured_mode = mode;
    bt_ctx_unlock();
    return err;
}

esp_err_t bt_manager_hfp_get_status(bt_hfp_manager_status_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));
    out->manager_initialized = true;
    out->configured_mode = s_configured_mode;
    err = bt_duplex_get_snapshot(&out->duplex);
    if (err == ESP_OK) bt_manager_hfp_policy_copy_locked(&out->policy);
    bt_ctx_unlock();
    return err;
}

static esp_err_t snapshot_active_peer(char peer_out[BT_DUPLEX_MAC_STR_LEN],
                                      bt_duplex_mode_t *mode_out)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized || !bt_ctx.connected ||
        !valid_mac(bt_ctx.connected_mac)) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    util_safe_copy_str(peer_out, BT_DUPLEX_MAC_STR_LEN, bt_ctx.connected_mac);
    *mode_out = s_configured_mode;
    bt_ctx_unlock();
    return ESP_OK;
}

static esp_err_t ensure_command_duplex_session(const char *mac,
                                               bt_duplex_mode_t mode)
{
    bt_duplex_snapshot_t duplex;
    esp_err_t err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;
    if (!duplex.peer_valid) {
        uint32_t generation = 0U;
        err = bt_duplex_session_begin(mac, mode, &generation);
        if (err != ESP_OK) return err;
        err = bt_duplex_set_a2dp_profile_state(
            generation, mac, BT_A2DP_PROFILE_CONNECTING);
        if (err == ESP_OK) {
            err = bt_duplex_set_a2dp_profile_state(
                generation, mac, BT_A2DP_PROFILE_CONNECTED);
        }
        if (err == ESP_OK) err = bt_manager_hfp_policy_refresh();
        return err;
    }
    if (!same_mac(mac, duplex.peer_mac)) return ESP_ERR_INVALID_STATE;
    if (duplex.requested_mode != mode) {
        err = bt_duplex_set_mode(duplex.session_generation,
                                 duplex.peer_mac, mode);
        if (err == ESP_OK) err = bt_manager_hfp_policy_refresh();
        return err;
    }
    return bt_manager_hfp_policy_refresh();
}

esp_err_t bt_manager_hfp_connect(const char *mac)
{
    if (!valid_mac(mac)) return ESP_ERR_INVALID_ARG;
    char active_peer[BT_DUPLEX_MAC_STR_LEN] = {0};
    bt_duplex_mode_t mode = BT_DUPLEX_MODE_DISABLED;
    esp_err_t err = snapshot_active_peer(active_peer, &mode);
    if (err != ESP_OK) return err;
    if (!same_mac(mac, active_peer)) return ESP_ERR_INVALID_STATE;
    err = ensure_command_duplex_session(active_peer, mode);
    if (err != ESP_OK) return err;
    return bt_hfp_connect(active_peer);
}

esp_err_t bt_manager_hfp_disconnect(void) { return bt_hfp_disconnect(); }
esp_err_t bt_manager_hfp_audio_start(void) { return bt_hfp_audio_start(); }
esp_err_t bt_manager_hfp_audio_stop(void) { return bt_hfp_audio_stop(); }

esp_err_t bt_manager_hfp_get_stats(bt_hfp_manager_stats_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    for (unsigned attempt = 0U; attempt < BT_HFP_STATS_CAPTURE_RETRIES;
         ++attempt) {
        bt_hfp_manager_stats_t baseline;
        uint64_t sequence;
        esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
        if (err != ESP_OK) return err;
        if (!bt_ctx.initialized) {
            bt_ctx_unlock();
            return ESP_ERR_INVALID_STATE;
        }
        baseline = s_stats_baseline;
        sequence = s_stats_reset_sequence;
        bt_ctx_unlock();

        bt_hfp_raw_stats_t raw;
        if ((err = capture_absolute_stats(&raw)) != ESP_OK) return err;

        err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
        if (err != ESP_OK) return err;
        bool unchanged = sequence == s_stats_reset_sequence;
        bt_ctx_unlock();
        if (!unchanged) continue;
        if (counters_regressed(&raw.stats, &baseline)) {
            return ESP_ERR_INVALID_STATE;
        }
        raw.stats.reset_sequence = sequence;
        subtract_baseline(&raw.stats, &baseline);
        *out = raw.stats;
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t bt_manager_hfp_reset_stats(void)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    bool initialized = bt_ctx.initialized;
    bt_ctx_unlock();
    if (!initialized) return ESP_ERR_INVALID_STATE;

    bt_hfp_raw_stats_t raw;
    if ((err = capture_absolute_stats(&raw)) != ESP_OK) return err;
    if (raw.duplex.hfp_audio_state != BT_HFP_AUDIO_DISCONNECTED ||
        raw.duplex.i2s_state != BT_HFP_I2S_STOPPED || raw.slc_pending ||
        raw.audio_control_pending || raw.audio_control_api_active ||
        raw.incoming_accepting || raw.active_callbacks != 0U ||
        (raw.local_i2s_state != HFP_I2S_OUTPUT_UNINITIALIZED &&
         raw.local_i2s_state != HFP_I2S_OUTPUT_STOPPED)) {
        return ESP_ERR_INVALID_STATE;
    }

    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_stats_baseline = raw.stats;
    s_stats_reset_sequence++;
    if (s_stats_reset_sequence == 0U) s_stats_reset_sequence = 1U;
    bt_ctx_unlock();
    return ESP_OK;
}

#ifdef UNIT_TEST
void bt_manager_hfp_test_reset_diagnostics(void)
{
    bt_manager_hfp_runtime_reset();
}
#endif

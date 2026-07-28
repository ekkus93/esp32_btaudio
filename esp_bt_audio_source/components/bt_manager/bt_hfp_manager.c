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
    bool slc_pending;
    bool audio_control_pending;
    bool audio_control_api_active;
    bool incoming_accepting;
    uint32_t active_callbacks;
    hfp_i2s_output_state_t local_i2s_state;
} bt_hfp_raw_stats_t;

static bt_duplex_mode_t s_configured_mode = BT_DUPLEX_MODE_AUTO;
static bt_hfp_manager_stats_t s_stats_baseline;
static uint64_t s_stats_reset_sequence;

static bool same_mac(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL || strlen(lhs) != 17U ||
        strlen(rhs) != 17U) {
        return false;
    }
    for (size_t index = 0U; index < 17U; ++index) {
        if (tolower((unsigned char)lhs[index]) !=
            tolower((unsigned char)rhs[index])) {
            return false;
        }
    }
    return true;
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
        before->last_error != after->last_error) {
        return false;
    }
    if (before->peer_valid &&
        memcmp(before->peer_mac, after->peer_mac,
               sizeof(before->peer_mac)) != 0) {
        return false;
    }
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
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
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

static void map_slc_stats(bt_hfp_manager_slc_stats_t *out,
                          const bt_hfp_connection_snapshot_t *snapshot)
{
    out->accepted_connect_requests = snapshot->accepted_connect_requests;
    out->accepted_disconnect_requests = snapshot->accepted_disconnect_requests;
    out->immediate_failures = snapshot->immediate_failures;
    out->remote_rejections = snapshot->remote_rejections;
    out->watchdog_timeouts = snapshot->watchdog_timeouts;
    out->stale_operation_events = snapshot->stale_operation_events;
    out->wrong_peer_events = snapshot->wrong_peer_events;
    out->dispatch_failures = snapshot->dispatch_failures;
}

static void map_audio_control_stats(
    bt_hfp_manager_audio_control_stats_t *out,
    const bt_hfp_audio_control_snapshot_t *snapshot)
{
    out->start_calls = snapshot->start_calls;
    out->stop_calls = snapshot->stop_calls;
    out->successful_starts = snapshot->successful_starts;
    out->successful_stops = snapshot->successful_stops;
    out->start_failures = snapshot->start_failures;
    out->stop_failures = snapshot->stop_failures;
    out->dispatch_failures = snapshot->dispatch_failures;
    out->immediate_failures = snapshot->immediate_failures;
    out->request_timeouts = snapshot->request_timeouts;
    out->event_timeouts = snapshot->event_timeouts;
    out->stale_events = snapshot->stale_events;
    out->wrong_peer_events = snapshot->wrong_peer_events;
    out->unexpected_connected_events = snapshot->unexpected_connected_events;
    out->rollback_attempts = snapshot->rollback_attempts;
    out->rollback_failures = snapshot->rollback_failures;
    out->cleanup_disconnect_requests =
        snapshot->cleanup_disconnect_requests;
    out->cleanup_disconnect_failures =
        snapshot->cleanup_disconnect_failures;
    out->i2s_start_failures = snapshot->i2s_start_failures;
    out->i2s_stop_failures = snapshot->i2s_stop_failures;
}

static void map_incoming_stats(bt_hfp_manager_incoming_stats_t *out,
                               const bt_hfp_audio_snapshot_t *snapshot)
{
    out->registration_failures = snapshot->registration_failures;
    out->activation_failures = snapshot->activation_failures;
    out->incoming_callbacks = snapshot->incoming_callbacks;
    out->accepted_frames = snapshot->accepted_frames;
    out->accepted_bytes = snapshot->accepted_bytes;
    out->dropped_frames = snapshot->dropped_frames;
    out->dropped_bytes = snapshot->dropped_bytes;
    out->invalid_frames = snapshot->invalid_frames;
    out->invalid_bytes = snapshot->invalid_bytes;
    out->inactive_frames = snapshot->inactive_frames;
    out->inactive_bytes = snapshot->inactive_bytes;
    out->stale_handle_frames = snapshot->stale_handle_frames;
    out->stale_handle_bytes = snapshot->stale_handle_bytes;
    out->bad_frames = snapshot->bad_frames;
    out->bad_bytes = snapshot->bad_bytes;
    out->unsupported_codec_frames = snapshot->unsupported_codec_frames;
    out->unsupported_codec_bytes = snapshot->unsupported_codec_bytes;
    out->ring_rejected_frames = snapshot->ring_rejected_frames;
    out->ring_rejected_bytes = snapshot->ring_rejected_bytes;
    out->callback_over_budget = snapshot->callback_over_budget;
    out->callback_last_us = snapshot->callback_last_us;
    out->callback_max_us_lifetime = snapshot->callback_max_us;
}

static void map_i2s_stats(bt_hfp_manager_i2s_stats_t *out,
                          const hfp_i2s_output_snapshot_t *snapshot)
{
    out->start_calls = snapshot->start_calls;
    out->stop_calls = snapshot->stop_calls;
    out->start_failures = snapshot->start_failures;
    out->stop_timeouts = snapshot->stop_timeouts;
    out->write_calls = snapshot->write_calls;
    out->write_failures = snapshot->write_failures;
    out->short_writes = snapshot->short_writes;
    out->write_lost_bytes = snapshot->write_lost_bytes;
    out->silence_intervals = snapshot->silence_intervals;
    out->silence_samples = snapshot->silence_samples;
    out->degraded_events = snapshot->degraded_events;
    out->push_calls = snapshot->push_calls;
    out->push_failures = snapshot->push_failures;
    out->stale_pushes = snapshot->stale_pushes;
    out->invalid_pushes = snapshot->invalid_pushes;
    out->quarantine_events = snapshot->quarantine_events;
    out->ring_written_bytes = snapshot->ring.total_written_bytes;
    out->ring_read_bytes = snapshot->ring.total_read_bytes;
    out->ring_overflow_frames = snapshot->ring.overflow_frames;
    out->ring_overflow_bytes = snapshot->ring.overflow_bytes;
    out->ring_underflow_events = snapshot->ring.underflow_events;
    out->ring_underflow_bytes = snapshot->ring.underflow_bytes;
    out->ring_stale_operations = snapshot->ring.stale_generation_operations;
    out->ring_invalid_operations = snapshot->ring.invalid_operations;
    out->ring_current_used = snapshot->ring.current_used;
    out->ring_peak_used_lifetime = snapshot->ring.peak_used;
}

static esp_err_t capture_absolute_stats(bt_hfp_raw_stats_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;

    for (unsigned attempt = 0U;
         attempt < BT_HFP_STATS_CAPTURE_RETRIES;
         ++attempt) {
        bt_duplex_snapshot_t before;
        bt_duplex_snapshot_t after;
        bt_hfp_connection_snapshot_t slc;
        bt_hfp_audio_control_snapshot_t control;
        bt_hfp_audio_snapshot_t incoming;
        hfp_i2s_output_snapshot_t i2s;

        esp_err_t err = bt_duplex_get_snapshot(&before);
        if (err != ESP_OK) return err;
        err = optional_slc_snapshot(&slc);
        if (err != ESP_OK) return err;
        err = optional_audio_control_snapshot(&control);
        if (err != ESP_OK) return err;
        err = optional_incoming_snapshot(&incoming);
        if (err != ESP_OK) return err;
        err = optional_i2s_snapshot(&i2s);
        if (err != ESP_OK) return err;
        err = bt_duplex_get_snapshot(&after);
        if (err != ESP_OK) return err;

        if (!stable_duplex_identity(&before, &after)) continue;

        memset(out, 0, sizeof(*out));
        out->stats.generation = after.session_generation;
        out->stats.peer_valid = after.peer_valid;
        if (after.peer_valid) {
            util_safe_copy_str(out->stats.peer_mac,
                               sizeof(out->stats.peer_mac),
                               after.peer_mac);
        }
        out->stats.duplex = after.counters;
        map_slc_stats(&out->stats.slc, &slc);
        map_audio_control_stats(&out->stats.audio_control, &control);
        map_incoming_stats(&out->stats.incoming, &incoming);
        map_i2s_stats(&out->stats.i2s, &i2s);

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

#define SUB_COUNTER(current, baseline, field) \
    (current).field = (current).field >= (baseline).field \
        ? (current).field - (baseline).field : 0U

static void subtract_duplex(bt_duplex_counters_t *current,
                            const bt_duplex_counters_t *baseline)
{
    SUB_COUNTER((*current), (*baseline), stale_generation_events);
    SUB_COUNTER((*current), (*baseline), wrong_peer_events);
    SUB_COUNTER((*current), (*baseline), illegal_transitions);
    SUB_COUNTER((*current), (*baseline), invalid_arguments);
    SUB_COUNTER((*current), (*baseline), recoveries);
    SUB_COUNTER((*current), (*baseline), incoming_frames);
    SUB_COUNTER((*current), (*baseline), incoming_bytes);
    SUB_COUNTER((*current), (*baseline), incoming_dropped_frames);
    SUB_COUNTER((*current), (*baseline), incoming_dropped_bytes);
    SUB_COUNTER((*current), (*baseline), i2s_underflows);
    SUB_COUNTER((*current), (*baseline), i2s_timeouts);
}

static void subtract_slc(bt_hfp_manager_slc_stats_t *current,
                         const bt_hfp_manager_slc_stats_t *baseline)
{
    SUB_COUNTER((*current), (*baseline), accepted_connect_requests);
    SUB_COUNTER((*current), (*baseline), accepted_disconnect_requests);
    SUB_COUNTER((*current), (*baseline), immediate_failures);
    SUB_COUNTER((*current), (*baseline), remote_rejections);
    SUB_COUNTER((*current), (*baseline), watchdog_timeouts);
    SUB_COUNTER((*current), (*baseline), stale_operation_events);
    SUB_COUNTER((*current), (*baseline), wrong_peer_events);
    SUB_COUNTER((*current), (*baseline), dispatch_failures);
}

static void subtract_audio_control(
    bt_hfp_manager_audio_control_stats_t *current,
    const bt_hfp_manager_audio_control_stats_t *baseline)
{
    SUB_COUNTER((*current), (*baseline), start_calls);
    SUB_COUNTER((*current), (*baseline), stop_calls);
    SUB_COUNTER((*current), (*baseline), successful_starts);
    SUB_COUNTER((*current), (*baseline), successful_stops);
    SUB_COUNTER((*current), (*baseline), start_failures);
    SUB_COUNTER((*current), (*baseline), stop_failures);
    SUB_COUNTER((*current), (*baseline), dispatch_failures);
    SUB_COUNTER((*current), (*baseline), immediate_failures);
    SUB_COUNTER((*current), (*baseline), request_timeouts);
    SUB_COUNTER((*current), (*baseline), event_timeouts);
    SUB_COUNTER((*current), (*baseline), stale_events);
    SUB_COUNTER((*current), (*baseline), wrong_peer_events);
    SUB_COUNTER((*current), (*baseline), unexpected_connected_events);
    SUB_COUNTER((*current), (*baseline), rollback_attempts);
    SUB_COUNTER((*current), (*baseline), rollback_failures);
    SUB_COUNTER((*current), (*baseline), cleanup_disconnect_requests);
    SUB_COUNTER((*current), (*baseline), cleanup_disconnect_failures);
    SUB_COUNTER((*current), (*baseline), i2s_start_failures);
    SUB_COUNTER((*current), (*baseline), i2s_stop_failures);
}

static void subtract_incoming(bt_hfp_manager_incoming_stats_t *current,
                              const bt_hfp_manager_incoming_stats_t *baseline)
{
    const uint32_t last_us = current->callback_last_us;
    const uint32_t max_us = current->callback_max_us_lifetime;
    SUB_COUNTER((*current), (*baseline), registration_failures);
    SUB_COUNTER((*current), (*baseline), activation_failures);
    SUB_COUNTER((*current), (*baseline), incoming_callbacks);
    SUB_COUNTER((*current), (*baseline), accepted_frames);
    SUB_COUNTER((*current), (*baseline), accepted_bytes);
    SUB_COUNTER((*current), (*baseline), dropped_frames);
    SUB_COUNTER((*current), (*baseline), dropped_bytes);
    SUB_COUNTER((*current), (*baseline), invalid_frames);
    SUB_COUNTER((*current), (*baseline), invalid_bytes);
    SUB_COUNTER((*current), (*baseline), inactive_frames);
    SUB_COUNTER((*current), (*baseline), inactive_bytes);
    SUB_COUNTER((*current), (*baseline), stale_handle_frames);
    SUB_COUNTER((*current), (*baseline), stale_handle_bytes);
    SUB_COUNTER((*current), (*baseline), bad_frames);
    SUB_COUNTER((*current), (*baseline), bad_bytes);
    SUB_COUNTER((*current), (*baseline), unsupported_codec_frames);
    SUB_COUNTER((*current), (*baseline), unsupported_codec_bytes);
    SUB_COUNTER((*current), (*baseline), ring_rejected_frames);
    SUB_COUNTER((*current), (*baseline), ring_rejected_bytes);
    SUB_COUNTER((*current), (*baseline), callback_over_budget);
    current->callback_last_us = last_us;
    current->callback_max_us_lifetime = max_us;
}

static void subtract_i2s(bt_hfp_manager_i2s_stats_t *current,
                         const bt_hfp_manager_i2s_stats_t *baseline)
{
    const size_t current_used = current->ring_current_used;
    const size_t peak_used = current->ring_peak_used_lifetime;
    SUB_COUNTER((*current), (*baseline), start_calls);
    SUB_COUNTER((*current), (*baseline), stop_calls);
    SUB_COUNTER((*current), (*baseline), start_failures);
    SUB_COUNTER((*current), (*baseline), stop_timeouts);
    SUB_COUNTER((*current), (*baseline), write_calls);
    SUB_COUNTER((*current), (*baseline), write_failures);
    SUB_COUNTER((*current), (*baseline), short_writes);
    SUB_COUNTER((*current), (*baseline), write_lost_bytes);
    SUB_COUNTER((*current), (*baseline), silence_intervals);
    SUB_COUNTER((*current), (*baseline), silence_samples);
    SUB_COUNTER((*current), (*baseline), degraded_events);
    SUB_COUNTER((*current), (*baseline), push_calls);
    SUB_COUNTER((*current), (*baseline), push_failures);
    SUB_COUNTER((*current), (*baseline), stale_pushes);
    SUB_COUNTER((*current), (*baseline), invalid_pushes);
    SUB_COUNTER((*current), (*baseline), quarantine_events);
    SUB_COUNTER((*current), (*baseline), ring_written_bytes);
    SUB_COUNTER((*current), (*baseline), ring_read_bytes);
    SUB_COUNTER((*current), (*baseline), ring_overflow_frames);
    SUB_COUNTER((*current), (*baseline), ring_overflow_bytes);
    SUB_COUNTER((*current), (*baseline), ring_underflow_events);
    SUB_COUNTER((*current), (*baseline), ring_underflow_bytes);
    SUB_COUNTER((*current), (*baseline), ring_stale_operations);
    SUB_COUNTER((*current), (*baseline), ring_invalid_operations);
    current->ring_current_used = current_used;
    current->ring_peak_used_lifetime = peak_used;
}

#undef SUB_COUNTER

static bool counters_regressed(const bt_hfp_manager_stats_t *current,
                               const bt_hfp_manager_stats_t *baseline)
{
#define REGRESSED(group, field) \
    ((current)->group.field < (baseline)->group.field)
    return REGRESSED(duplex, incoming_frames) ||
           REGRESSED(duplex, incoming_bytes) ||
           REGRESSED(slc, accepted_connect_requests) ||
           REGRESSED(audio_control, start_calls) ||
           REGRESSED(incoming, incoming_callbacks) ||
           REGRESSED(i2s, start_calls) ||
           REGRESSED(i2s, ring_written_bytes);
#undef REGRESSED
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
    bt_ctx_unlock();
    return err;
}

static esp_err_t snapshot_active_peer(char peer_out[BT_DUPLEX_MAC_STR_LEN],
                                      bt_duplex_mode_t *mode_out)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized || !bt_ctx.connected ||
        bt_ctx.connected_mac[0] == '\0') {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    util_safe_copy_str(peer_out, BT_DUPLEX_MAC_STR_LEN,
                       bt_ctx.connected_mac);
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
        return err;
    }

    if (!same_mac(mac, duplex.peer_mac)) return ESP_ERR_INVALID_STATE;
    if (duplex.requested_mode != mode) {
        return bt_duplex_set_mode(duplex.session_generation,
                                  duplex.peer_mac, mode);
    }
    return ESP_OK;
}

esp_err_t bt_manager_hfp_connect(const char *mac)
{
    if (mac == NULL || strlen(mac) != 17U) return ESP_ERR_INVALID_ARG;

    char active_peer[BT_DUPLEX_MAC_STR_LEN] = {0};
    bt_duplex_mode_t mode = BT_DUPLEX_MODE_AUTO;
    esp_err_t err = snapshot_active_peer(active_peer, &mode);
    if (err != ESP_OK) return err;
    if (!same_mac(mac, active_peer)) return ESP_ERR_INVALID_STATE;

    err = ensure_command_duplex_session(active_peer, mode);
    if (err != ESP_OK) return err;
    return bt_hfp_connect(active_peer);
}

esp_err_t bt_manager_hfp_disconnect(void)
{
    return bt_hfp_disconnect();
}

esp_err_t bt_manager_hfp_audio_start(void)
{
    return bt_hfp_audio_start();
}

esp_err_t bt_manager_hfp_audio_stop(void)
{
    return bt_hfp_audio_stop();
}

esp_err_t bt_manager_hfp_get_stats(bt_hfp_manager_stats_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;

    for (unsigned attempt = 0U;
         attempt < BT_HFP_STATS_CAPTURE_RETRIES;
         ++attempt) {
        bt_hfp_manager_stats_t baseline;
        uint64_t sequence_before;

        esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
        if (err != ESP_OK) return err;
        if (!bt_ctx.initialized) {
            bt_ctx_unlock();
            return ESP_ERR_INVALID_STATE;
        }
        baseline = s_stats_baseline;
        sequence_before = s_stats_reset_sequence;
        bt_ctx_unlock();

        bt_hfp_raw_stats_t raw;
        err = capture_absolute_stats(&raw);
        if (err != ESP_OK) return err;

        err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
        if (err != ESP_OK) return err;
        const bool unchanged =
            sequence_before == s_stats_reset_sequence;
        bt_ctx_unlock();
        if (!unchanged) continue;

        if (counters_regressed(&raw.stats, &baseline)) {
            return ESP_ERR_INVALID_STATE;
        }

        raw.stats.reset_sequence = sequence_before;
        subtract_duplex(&raw.stats.duplex, &baseline.duplex);
        subtract_slc(&raw.stats.slc, &baseline.slc);
        subtract_audio_control(&raw.stats.audio_control,
                               &baseline.audio_control);
        subtract_incoming(&raw.stats.incoming, &baseline.incoming);
        subtract_i2s(&raw.stats.i2s, &baseline.i2s);
        *out = raw.stats;
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t bt_manager_hfp_reset_stats(void)
{
    bt_hfp_raw_stats_t raw;
    esp_err_t err = capture_absolute_stats(&raw);
    if (err != ESP_OK) return err;

    if (raw.stats.generation != 0U) {
        bt_hfp_manager_status_t status;
        err = bt_manager_hfp_get_status(&status);
        if (err != ESP_OK) return err;
        if (status.duplex.hfp_audio_state != BT_HFP_AUDIO_DISCONNECTED ||
            status.duplex.i2s_state != BT_HFP_I2S_STOPPED) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (raw.slc_pending || raw.audio_control_pending ||
        raw.audio_control_api_active || raw.incoming_accepting ||
        raw.active_callbacks != 0U ||
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
    s_configured_mode = BT_DUPLEX_MODE_AUTO;
    memset(&s_stats_baseline, 0, sizeof(s_stats_baseline));
    s_stats_reset_sequence = 0U;
}
#endif

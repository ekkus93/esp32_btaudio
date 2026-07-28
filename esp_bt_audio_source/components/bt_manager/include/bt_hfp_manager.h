#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bt_duplex_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool manager_initialized;
    bt_duplex_mode_t configured_mode;
    bt_duplex_snapshot_t duplex;
} bt_hfp_manager_status_t;

typedef struct {
    uint64_t accepted_connect_requests;
    uint64_t accepted_disconnect_requests;
    uint64_t immediate_failures;
    uint64_t remote_rejections;
    uint64_t watchdog_timeouts;
    uint64_t stale_operation_events;
    uint64_t wrong_peer_events;
    uint64_t dispatch_failures;
} bt_hfp_manager_slc_stats_t;

typedef struct {
    uint64_t start_calls;
    uint64_t stop_calls;
    uint64_t successful_starts;
    uint64_t successful_stops;
    uint64_t start_failures;
    uint64_t stop_failures;
    uint64_t dispatch_failures;
    uint64_t immediate_failures;
    uint64_t request_timeouts;
    uint64_t event_timeouts;
    uint64_t stale_events;
    uint64_t wrong_peer_events;
    uint64_t unexpected_connected_events;
    uint64_t rollback_attempts;
    uint64_t rollback_failures;
    uint64_t cleanup_disconnect_requests;
    uint64_t cleanup_disconnect_failures;
    uint64_t i2s_start_failures;
    uint64_t i2s_stop_failures;
} bt_hfp_manager_audio_control_stats_t;

typedef struct {
    uint64_t registration_failures;
    uint64_t activation_failures;
    uint64_t incoming_callbacks;
    uint64_t accepted_frames;
    uint64_t accepted_bytes;
    uint64_t dropped_frames;
    uint64_t dropped_bytes;
    uint64_t invalid_frames;
    uint64_t invalid_bytes;
    uint64_t inactive_frames;
    uint64_t inactive_bytes;
    uint64_t stale_handle_frames;
    uint64_t stale_handle_bytes;
    uint64_t bad_frames;
    uint64_t bad_bytes;
    uint64_t unsupported_codec_frames;
    uint64_t unsupported_codec_bytes;
    uint64_t ring_rejected_frames;
    uint64_t ring_rejected_bytes;
    uint64_t callback_over_budget;
    uint32_t callback_last_us;
    uint32_t callback_max_us_lifetime;
} bt_hfp_manager_incoming_stats_t;

typedef struct {
    uint64_t start_calls;
    uint64_t stop_calls;
    uint64_t start_failures;
    uint64_t stop_timeouts;
    uint64_t write_calls;
    uint64_t write_failures;
    uint64_t short_writes;
    uint64_t write_lost_bytes;
    uint64_t silence_intervals;
    uint64_t silence_samples;
    uint64_t degraded_events;
    uint64_t push_calls;
    uint64_t push_failures;
    uint64_t stale_pushes;
    uint64_t invalid_pushes;
    uint64_t quarantine_events;
    uint64_t ring_written_bytes;
    uint64_t ring_read_bytes;
    uint64_t ring_overflow_frames;
    uint64_t ring_overflow_bytes;
    uint64_t ring_underflow_events;
    uint64_t ring_underflow_bytes;
    uint64_t ring_stale_operations;
    uint64_t ring_invalid_operations;
    size_t ring_current_used;
    size_t ring_peak_used_lifetime;
} bt_hfp_manager_i2s_stats_t;

typedef struct {
    uint32_t generation;
    bool peer_valid;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
    uint64_t reset_sequence;
    bt_duplex_counters_t duplex;
    bt_hfp_manager_slc_stats_t slc;
    bt_hfp_manager_audio_control_stats_t audio_control;
    bt_hfp_manager_incoming_stats_t incoming;
    bt_hfp_manager_i2s_stats_t i2s;
} bt_hfp_manager_stats_t;

/* Request acceptance is distinct from callback-confirmed SLC completion. */
esp_err_t bt_manager_hfp_connect(const char *mac);
esp_err_t bt_manager_hfp_disconnect(void);

/* These APIs return only after FD-10 reaches a confirmed terminal result. */
esp_err_t bt_manager_hfp_audio_start(void);
esp_err_t bt_manager_hfp_audio_stop(void);

/* The configured mode is retained for the next HFP peer session. If an
 * authoritative session exists, the same update is applied atomically to its
 * requested/effective mode while HFP audio and I2S are stopped. */
esp_err_t bt_manager_hfp_set_mode(bt_duplex_mode_t mode);
esp_err_t bt_manager_hfp_get_configured_mode(bt_duplex_mode_t *mode_out);

/* One duplex-state lock acquisition supplies every generation-bound status
 * field, so callers cannot combine fields from different sessions. */
esp_err_t bt_manager_hfp_get_status(bt_hfp_manager_status_t *out);

/* Statistics are reported relative to the most recent successful reset.
 * Current gauges and lifetime maxima are explicitly named as such. */
esp_err_t bt_manager_hfp_get_stats(bt_hfp_manager_stats_t *out);

/* Reset is a non-destructive baseline operation. It is accepted only while
 * HFP audio and I2S are stopped and no HFP SLC/audio operation or incoming
 * callback is active. No live atomic counter is rewritten. */
esp_err_t bt_manager_hfp_reset_stats(void);

#ifdef UNIT_TEST
void bt_manager_hfp_test_reset_diagnostics(void);
#endif

#ifdef __cplusplus
}
#endif

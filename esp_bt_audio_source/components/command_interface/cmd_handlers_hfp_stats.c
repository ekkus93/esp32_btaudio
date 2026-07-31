#include "cmd_handlers.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#include "cmd_handlers_hfp_internal.h"

static bool send_stats_line(const char *result, const char *format, ...)
{
    char data[HFP_DATA_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(data, sizeof(data), format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(data)) {
        char safe_result[64];
        sanitize_field(result, safe_result, sizeof(safe_result));
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "STATS_LINE_TOO_LONG", safe_result);
        return false;
    }
    return cmd_send_response(CMD_STATUS_INFO, "HFP", result, data) ==
           CMD_SUCCESS;
}

bool send_stats_lines(const bt_hfp_manager_stats_t *stats)
{
#define SEND(result, ...) \
    do { if (!send_stats_line((result), __VA_ARGS__)) return false; } while (0)
    SEND("STATS_STATE",
         "GEN=%" PRIu32 ",PEER=%s,RESET_SEQUENCE=%" PRIu64,
         stats->generation,
         stats->peer_valid ? stats->peer_mac : "NONE",
         stats->reset_sequence);
    SEND("STATS_DUPLEX1",
         "STALE_GEN=%" PRIu64 ",WRONG_PEER=%" PRIu64
         ",ILLEGAL=%" PRIu64 ",INVALID=%" PRIu64
         ",RECOVERIES=%" PRIu64,
         stats->duplex.stale_generation_events,
         stats->duplex.wrong_peer_events,
         stats->duplex.illegal_transitions,
         stats->duplex.invalid_arguments,
         stats->duplex.recoveries);
    SEND("STATS_DUPLEX2",
         "RX_FRAMES=%" PRIu64 ",RX_BYTES=%" PRIu64
         ",DROP_FRAMES=%" PRIu64 ",DROP_BYTES=%" PRIu64
         ",I2S_UNDERFLOWS=%" PRIu64 ",I2S_TIMEOUTS=%" PRIu64,
         stats->duplex.incoming_frames,
         stats->duplex.incoming_bytes,
         stats->duplex.incoming_dropped_frames,
         stats->duplex.incoming_dropped_bytes,
         stats->duplex.i2s_underflows,
         stats->duplex.i2s_timeouts);
    SEND("STATS_SLC1",
         "CONNECT_ACCEPT=%" PRIu64 ",DISCONNECT_ACCEPT=%" PRIu64
         ",IMMEDIATE_FAIL=%" PRIu64 ",REMOTE_REJECT=%" PRIu64,
         stats->slc.accepted_connect_requests,
         stats->slc.accepted_disconnect_requests,
         stats->slc.immediate_failures,
         stats->slc.remote_rejections);
    SEND("STATS_SLC2",
         "WATCHDOG_TIMEOUT=%" PRIu64 ",STALE_EVENT=%" PRIu64
         ",WRONG_PEER=%" PRIu64 ",DISPATCH_FAIL=%" PRIu64,
         stats->slc.watchdog_timeouts,
         stats->slc.stale_operation_events,
         stats->slc.wrong_peer_events,
         stats->slc.dispatch_failures);
    SEND("STATS_AUDIO1",
         "START=%" PRIu64 ",STOP=%" PRIu64
         ",START_OK=%" PRIu64 ",STOP_OK=%" PRIu64
         ",START_FAIL=%" PRIu64 ",STOP_FAIL=%" PRIu64,
         stats->audio_control.start_calls,
         stats->audio_control.stop_calls,
         stats->audio_control.successful_starts,
         stats->audio_control.successful_stops,
         stats->audio_control.start_failures,
         stats->audio_control.stop_failures);
    SEND("STATS_AUDIO2",
         "DISPATCH_FAIL=%" PRIu64 ",IMMEDIATE_FAIL=%" PRIu64
         ",REQUEST_TIMEOUT=%" PRIu64 ",EVENT_TIMEOUT=%" PRIu64
         ",STALE=%" PRIu64 ",WRONG_PEER=%" PRIu64,
         stats->audio_control.dispatch_failures,
         stats->audio_control.immediate_failures,
         stats->audio_control.request_timeouts,
         stats->audio_control.event_timeouts,
         stats->audio_control.stale_events,
         stats->audio_control.wrong_peer_events);
    SEND("STATS_AUDIO3",
         "UNEXPECTED=%" PRIu64 ",ROLLBACK=%" PRIu64
         ",ROLLBACK_FAIL=%" PRIu64 ",CLEANUP_REQ=%" PRIu64
         ",CLEANUP_FAIL=%" PRIu64 ",I2S_START_FAIL=%" PRIu64
         ",I2S_STOP_FAIL=%" PRIu64 ",HEALTH_REPORT_FAIL=%" PRIu64
         ",LAST_HEALTH_REPORT_ERROR=%s",
         stats->audio_control.unexpected_connected_events,
         stats->audio_control.rollback_attempts,
         stats->audio_control.rollback_failures,
         stats->audio_control.cleanup_disconnect_requests,
         stats->audio_control.cleanup_disconnect_failures,
         stats->audio_control.i2s_start_failures,
         stats->audio_control.i2s_stop_failures,
         stats->audio_control.health_report_failures,
         esp_err_to_name(stats->audio_control.last_health_report_error));
    SEND("STATS_AUDIO4",
         "I2S_STATE_SYNC_FAIL=%" PRIu64,
         stats->audio_control.i2s_state_sync_failures);
    SEND("STATS_RX1",
         "CALLBACKS=%" PRIu64 ",ACCEPT_FRAMES=%" PRIu64
         ",ACCEPT_BYTES=%" PRIu64 ",DROP_FRAMES=%" PRIu64
         ",DROP_BYTES=%" PRIu64,
         stats->incoming.incoming_callbacks,
         stats->incoming.accepted_frames,
         stats->incoming.accepted_bytes,
         stats->incoming.dropped_frames,
         stats->incoming.dropped_bytes);
    SEND("STATS_RX2",
         "INVALID_FRAMES=%" PRIu64 ",INVALID_BYTES=%" PRIu64
         ",INACTIVE_FRAMES=%" PRIu64 ",INACTIVE_BYTES=%" PRIu64
         ",STALE_FRAMES=%" PRIu64 ",STALE_BYTES=%" PRIu64,
         stats->incoming.invalid_frames,
         stats->incoming.invalid_bytes,
         stats->incoming.inactive_frames,
         stats->incoming.inactive_bytes,
         stats->incoming.stale_handle_frames,
         stats->incoming.stale_handle_bytes);
    SEND("STATS_RX3",
         "BAD_FRAMES=%" PRIu64 ",BAD_BYTES=%" PRIu64
         ",UNSUPPORTED_FRAMES=%" PRIu64 ",UNSUPPORTED_BYTES=%" PRIu64
         ",RING_REJECT_FRAMES=%" PRIu64 ",RING_REJECT_BYTES=%" PRIu64,
         stats->incoming.bad_frames,
         stats->incoming.bad_bytes,
         stats->incoming.unsupported_codec_frames,
         stats->incoming.unsupported_codec_bytes,
         stats->incoming.ring_rejected_frames,
         stats->incoming.ring_rejected_bytes);
    SEND("STATS_RX4",
         "REG_FAIL=%" PRIu64 ",ACTIVATE_FAIL=%" PRIu64
         ",OVER_BUDGET=%" PRIu64 ",OVERLAP_REJECT=%" PRIu32
         ",LAST_US=%" PRIu32 ",MAX_US_LIFETIME=%" PRIu32,
         stats->incoming.registration_failures,
         stats->incoming.activation_failures,
         stats->incoming.callback_over_budget,
         stats->incoming.callback_overlap_rejections,
         stats->incoming.callback_last_us,
         stats->incoming.callback_max_us_lifetime);
    SEND("STATS_I2S1",
         "START=%" PRIu64 ",STOP=%" PRIu64
         ",START_FAIL=%" PRIu64 ",STOP_TIMEOUT=%" PRIu64,
         stats->i2s.start_calls,
         stats->i2s.stop_calls,
         stats->i2s.start_failures,
         stats->i2s.stop_timeouts);
    SEND("STATS_I2S2",
         "WRITE=%" PRIu64 ",WRITE_FAIL=%" PRIu64
         ",SHORT_WRITE=%" PRIu64 ",LOST_BYTES=%" PRIu64,
         stats->i2s.write_calls,
         stats->i2s.write_failures,
         stats->i2s.short_writes,
         stats->i2s.write_lost_bytes);
    SEND("STATS_I2S3",
         "SILENCE_INTERVALS=%" PRIu64 ",SILENCE_SAMPLES=%" PRIu64
         ",DEGRADED=%" PRIu64 ",QUARANTINE=%" PRIu64,
         stats->i2s.silence_intervals,
         stats->i2s.silence_samples,
         stats->i2s.degraded_events,
         stats->i2s.quarantine_events);
    SEND("STATS_I2S4",
         "PUSH=%" PRIu64 ",PUSH_FAIL=%" PRIu64
         ",STALE_PUSH=%" PRIu64 ",INVALID_PUSH=%" PRIu64,
         stats->i2s.push_calls,
         stats->i2s.push_failures,
         stats->i2s.stale_pushes,
         stats->i2s.invalid_pushes);
    SEND("STATS_I2S5",
         "RING_WRITE_BYTES=%" PRIu64 ",RING_READ_BYTES=%" PRIu64
         ",OVERFLOW_FRAMES=%" PRIu64 ",OVERFLOW_BYTES=%" PRIu64,
         stats->i2s.ring_written_bytes,
         stats->i2s.ring_read_bytes,
         stats->i2s.ring_overflow_frames,
         stats->i2s.ring_overflow_bytes);
    SEND("STATS_I2S6",
         "UNDERFLOW_EVENTS=%" PRIu64 ",UNDERFLOW_BYTES=%" PRIu64
         ",RING_STALE=%" PRIu64 ",RING_INVALID=%" PRIu64,
         stats->i2s.ring_underflow_events,
         stats->i2s.ring_underflow_bytes,
         stats->i2s.ring_stale_operations,
         stats->i2s.ring_invalid_operations);
    SEND("STATS_I2S7",
         "CURRENT_USED=%zu,PEAK_USED_LIFETIME=%zu",
         stats->i2s.ring_current_used,
         stats->i2s.ring_peak_used_lifetime);
#undef SEND
    return true;
}

static bool format_size_or_na(char *out, size_t out_size,
                              bool available, size_t value)
{
    return available
        ? format_checked(out, out_size, "%zu", value)
        : format_checked(out, out_size, "NA");
}

bool send_diagnostics_lines(
    const bt_hfp_manager_diagnostics_t *diagnostics)
{
    char free_internal[32];
    char minimum_free[32];
    char largest_internal[32];
    char hfp_stack[32];
    char i2s_stack[32];

    if (!format_size_or_na(free_internal, sizeof(free_internal),
                           diagnostics->heap_available,
                           diagnostics->free_internal_bytes) ||
        !format_size_or_na(minimum_free, sizeof(minimum_free),
                           diagnostics->heap_available,
                           diagnostics->minimum_free_heap_bytes_lifetime) ||
        !format_size_or_na(largest_internal, sizeof(largest_internal),
                           diagnostics->heap_available,
                           diagnostics->largest_internal_free_block_bytes) ||
        !format_size_or_na(
            hfp_stack, sizeof(hfp_stack),
            diagnostics->hfp_app_task_stack_available,
            diagnostics->hfp_app_task_min_free_stack_bytes_lifetime) ||
        !format_size_or_na(
            i2s_stack, sizeof(i2s_stack),
            diagnostics->i2s_writer_task_stack_available,
            diagnostics->i2s_writer_task_min_free_stack_bytes_lifetime)) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "STATS_LINE_TOO_LONG", "FD13_VALUE");
        return false;
    }

    if (!send_stats_line(
            "STATS_RESOURCE1",
            "HEAP_STATE=%s,FREE_INTERNAL_BYTES=%s,"
            "MIN_FREE_HEAP_BYTES_LIFETIME=%s,"
            "LARGEST_INTERNAL_BLOCK_BYTES=%s",
            diagnostics->heap_available ? "AVAILABLE" : "UNAVAILABLE",
            free_internal, minimum_free, largest_internal)) {
        return false;
    }

    if (!send_stats_line(
            "STATS_RESOURCE2",
            "HFP_APP_TASK_STATE=%s,"
            "HFP_APP_MIN_FREE_STACK_BYTES_LIFETIME=%s,"
            "I2S_WRITER_TASK_STATE=%s,"
            "I2S_WRITER_MIN_FREE_STACK_BYTES_LIFETIME=%s",
            diagnostics->hfp_app_task_stack_available
                ? "AVAILABLE" : "UNAVAILABLE",
            hfp_stack,
            diagnostics->i2s_writer_task_stack_available
                ? "AVAILABLE" : "UNAVAILABLE",
            i2s_stack)) {
        return false;
    }

    if (diagnostics->incoming_callback_available) {
        return send_stats_line(
            "STATS_CALLBACK",
            "STATE=AVAILABLE,BUDGET_US=%" PRIu32
            ",LAST_US=%" PRIu32 ",MAX_US_LIFETIME=%" PRIu32
            ",OVER_BUDGET_LIFETIME=%" PRIu64,
            diagnostics->incoming_callback_budget_us,
            diagnostics->incoming_callback_last_us,
            diagnostics->incoming_callback_max_us_lifetime,
            diagnostics->incoming_callback_over_budget_lifetime);
    }

    return send_stats_line(
        "STATS_CALLBACK",
        "STATE=UNAVAILABLE,BUDGET_US=NA,LAST_US=NA,"
        "MAX_US_LIFETIME=NA,OVER_BUDGET_LIFETIME=NA");
}

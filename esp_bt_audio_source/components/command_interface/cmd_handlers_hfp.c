#include "cmd_handlers.h"

#include <inttypes.h>
#include <strings.h>

#include "bt_hfp_manager.h"

#define HFP_DATA_SIZE 384U

static const char *wire_mode(bt_duplex_mode_t mode)
{
    switch (mode) {
    case BT_DUPLEX_MODE_DISABLED: return "DISABLED";
    case BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC: return "A2DP_MIC";
    case BT_DUPLEX_MODE_HFP_FULL_DUPLEX: return "HFP_FULL";
    case BT_DUPLEX_MODE_AUTO: return "AUTO";
    default: return "INVALID";
    }
}

static bool parse_mode(const char *text, bt_duplex_mode_t *mode_out)
{
    if (text == NULL || mode_out == NULL) return false;
    if (strcasecmp(text, "DISABLED") == 0) {
        *mode_out = BT_DUPLEX_MODE_DISABLED;
    } else if (strcasecmp(text, "A2DP_MIC") == 0) {
        *mode_out = BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC;
    } else if (strcasecmp(text, "HFP_FULL") == 0) {
        *mode_out = BT_DUPLEX_MODE_HFP_FULL_DUPLEX;
    } else if (strcasecmp(text, "AUTO") == 0) {
        *mode_out = BT_DUPLEX_MODE_AUTO;
    } else {
        return false;
    }
    return true;
}

static void sanitize_field(const char *source, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    if (source == NULL) source = "";
    size_t index = 0U;
    while (source[index] != '\0' && index + 1U < out_size) {
        char value = source[index];
        if (value == '|' || value == ',' || value == '\r' || value == '\n') {
            value = '_';
        }
        out[index++] = value;
    }
    out[index] = '\0';
}

static cmd_status_t send_esp_error(const char *detail, esp_err_t error)
{
    (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                            esp_err_to_name(error), detail);
    return CMD_SUCCESS;
}

static cmd_status_t invalid_count(void)
{
    (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                            "INVALID_ARGUMENT_COUNT", NULL);
    return CMD_SUCCESS;
}

static cmd_status_t handle_status(void)
{
    bt_hfp_manager_status_t status;
    esp_err_t err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return send_esp_error(NULL, err);

    char error_text[BT_DUPLEX_ERROR_TEXT_LEN];
    sanitize_field(status.duplex.last_error_text,
                   error_text, sizeof(error_text));
    char data[HFP_DATA_SIZE];
    int written = snprintf(
        data, sizeof(data),
        "GEN=%" PRIu32 ",PEER=%s,CONFIG_MODE=%s,REQUESTED=%s,"
        "EFFECTIVE=%s,A2DP_PROFILE=%s,A2DP_AUDIO=%s,HFP_PROFILE=%s,"
        "HFP_AUDIO=%s,CODEC=%s,I2S=%s,HEALTH=%s,LAST_ERROR=%s,"
        "ERROR_TEXT=%s",
        status.duplex.session_generation,
        status.duplex.peer_valid ? status.duplex.peer_mac : "NONE",
        wire_mode(status.configured_mode),
        wire_mode(status.duplex.requested_mode),
        wire_mode(status.duplex.effective_mode),
        bt_a2dp_profile_state_to_string(status.duplex.a2dp_profile_state),
        bt_a2dp_audio_state_to_string(status.duplex.a2dp_audio_state),
        bt_hfp_profile_state_to_string(status.duplex.hfp_profile_state),
        bt_hfp_audio_state_to_string(status.duplex.hfp_audio_state),
        bt_hfp_codec_to_string(status.duplex.codec),
        bt_hfp_i2s_state_to_string(status.duplex.i2s_state),
        bt_audio_health_to_string(status.duplex.health),
        esp_err_to_name(status.duplex.last_error),
        error_text[0] != '\0' ? error_text : "NONE");
    if (written < 0 || (size_t)written >= sizeof(data)) {
        return send_esp_error("STATUS", ESP_ERR_INVALID_SIZE);
    }
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "STATUS", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_connect(const char *mac)
{
    bt_hfp_manager_status_t before;
    esp_err_t err = bt_manager_hfp_get_status(&before);
    if (err != ESP_OK) return send_esp_error(mac, err);
    err = bt_manager_hfp_connect(mac);
    if (err != ESP_OK) return send_esp_error(mac, err);

    char data[128];
    if (before.duplex.peer_valid &&
        strcasecmp(before.duplex.peer_mac, mac) == 0 &&
        before.duplex.hfp_profile_state == BT_HFP_PROFILE_SLC_CONNECTED) {
        snprintf(data, sizeof(data), "MAC=%s,GEN=%" PRIu32,
                 mac, before.duplex.session_generation);
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "ALREADY_CONNECTED", data);
    } else {
        snprintf(data, sizeof(data),
                 "MAC=%s,COMPLETION=HFP_PROFILE_EVENT", mac);
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "CONNECT_ACCEPTED", data);
    }
    return CMD_SUCCESS;
}

static cmd_status_t handle_disconnect(void)
{
    bt_hfp_manager_status_t before;
    esp_err_t err = bt_manager_hfp_get_status(&before);
    if (err != ESP_OK) return send_esp_error(NULL, err);
    err = bt_manager_hfp_disconnect();
    if (err != ESP_OK) return send_esp_error(NULL, err);

    if (before.duplex.hfp_profile_state == BT_HFP_PROFILE_DISCONNECTED) {
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "ALREADY_DISCONNECTED", NULL);
    } else {
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "DISCONNECT_ACCEPTED",
                                "COMPLETION=HFP_PROFILE_EVENT");
    }
    return CMD_SUCCESS;
}

static cmd_status_t handle_audio_start(void)
{
    esp_err_t err = bt_manager_hfp_audio_start();
    if (err != ESP_OK) return send_esp_error(NULL, err);
    bt_hfp_manager_status_t status;
    err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return send_esp_error(NULL, err);

    char data[96];
    snprintf(data, sizeof(data), "GEN=%" PRIu32 ",CODEC=%s",
             status.duplex.session_generation,
             bt_hfp_codec_to_string(status.duplex.codec));
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "AUDIO_STARTED", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_audio_stop(void)
{
    esp_err_t err = bt_manager_hfp_audio_stop();
    if (err != ESP_OK) return send_esp_error(NULL, err);
    bt_hfp_manager_status_t status;
    err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return send_esp_error(NULL, err);

    char data[64];
    snprintf(data, sizeof(data), "GEN=%" PRIu32,
             status.duplex.session_generation);
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "AUDIO_STOPPED", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_mode(const char *mode_text)
{
    bt_duplex_mode_t mode;
    if (!parse_mode(mode_text, &mode)) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "UNKNOWN_MODE", mode_text);
        return CMD_SUCCESS;
    }
    esp_err_t err = bt_manager_hfp_set_mode(mode);
    if (err != ESP_OK) return send_esp_error(mode_text, err);

    bt_hfp_manager_status_t status;
    err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return send_esp_error(mode_text, err);
    char data[96];
    snprintf(data, sizeof(data), "CONFIG_MODE=%s,EFFECTIVE=%s",
             wire_mode(status.configured_mode),
             wire_mode(status.duplex.effective_mode));
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "MODE_SET", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_codec(void)
{
    bt_hfp_manager_status_t status;
    esp_err_t err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return send_esp_error(NULL, err);
    char data[96];
    snprintf(data, sizeof(data), "GEN=%" PRIu32 ",CODEC=%s",
             status.duplex.session_generation,
             bt_hfp_codec_to_string(status.duplex.codec));
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "CODEC", data);
    return CMD_SUCCESS;
}

static void send_line(const char *result, const char *format, ...)
{
    char data[HFP_DATA_SIZE];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(data, sizeof(data), format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(data)) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "STATS_LINE_TOO_LONG", result);
        return;
    }
    (void)cmd_send_response(CMD_STATUS_INFO, "HFP", result, data);
}

static void send_stats_lines(const bt_hfp_manager_stats_t *s)
{
    send_line("STATS_STATE",
              "GEN=%" PRIu32 ",PEER=%s,RESET_SEQUENCE=%" PRIu64,
              s->generation, s->peer_valid ? s->peer_mac : "NONE",
              s->reset_sequence);
    send_line("STATS_DUPLEX1",
              "STALE_GEN=%" PRIu64 ",WRONG_PEER=%" PRIu64
              ",ILLEGAL=%" PRIu64 ",INVALID=%" PRIu64
              ",RECOVERIES=%" PRIu64,
              s->duplex.stale_generation_events,
              s->duplex.wrong_peer_events,
              s->duplex.illegal_transitions,
              s->duplex.invalid_arguments,
              s->duplex.recoveries);
    send_line("STATS_DUPLEX2",
              "RX_FRAMES=%" PRIu64 ",RX_BYTES=%" PRIu64
              ",DROP_FRAMES=%" PRIu64 ",DROP_BYTES=%" PRIu64
              ",I2S_UNDERFLOWS=%" PRIu64 ",I2S_TIMEOUTS=%" PRIu64,
              s->duplex.incoming_frames, s->duplex.incoming_bytes,
              s->duplex.incoming_dropped_frames,
              s->duplex.incoming_dropped_bytes,
              s->duplex.i2s_underflows, s->duplex.i2s_timeouts);
    send_line("STATS_SLC",
              "CONNECT_ACCEPT=%" PRIu64 ",DISCONNECT_ACCEPT=%" PRIu64
              ",IMMEDIATE_FAIL=%" PRIu64 ",REMOTE_REJECT=%" PRIu64
              ",WATCHDOG_TIMEOUT=%" PRIu64 ",STALE_EVENT=%" PRIu64
              ",WRONG_PEER=%" PRIu64 ",DISPATCH_FAIL=%" PRIu64,
              s->slc.accepted_connect_requests,
              s->slc.accepted_disconnect_requests,
              s->slc.immediate_failures, s->slc.remote_rejections,
              s->slc.watchdog_timeouts, s->slc.stale_operation_events,
              s->slc.wrong_peer_events, s->slc.dispatch_failures);
    send_line("STATS_AUDIO1",
              "START=%" PRIu64 ",STOP=%" PRIu64
              ",START_OK=%" PRIu64 ",STOP_OK=%" PRIu64
              ",START_FAIL=%" PRIu64 ",STOP_FAIL=%" PRIu64,
              s->audio_control.start_calls, s->audio_control.stop_calls,
              s->audio_control.successful_starts,
              s->audio_control.successful_stops,
              s->audio_control.start_failures,
              s->audio_control.stop_failures);
    send_line("STATS_AUDIO2",
              "DISPATCH_FAIL=%" PRIu64 ",IMMEDIATE_FAIL=%" PRIu64
              ",REQUEST_TIMEOUT=%" PRIu64 ",EVENT_TIMEOUT=%" PRIu64
              ",STALE=%" PRIu64 ",WRONG_PEER=%" PRIu64,
              s->audio_control.dispatch_failures,
              s->audio_control.immediate_failures,
              s->audio_control.request_timeouts,
              s->audio_control.event_timeouts,
              s->audio_control.stale_events,
              s->audio_control.wrong_peer_events);
    send_line("STATS_AUDIO3",
              "UNEXPECTED=%" PRIu64 ",ROLLBACK=%" PRIu64
              ",ROLLBACK_FAIL=%" PRIu64 ",CLEANUP_REQ=%" PRIu64
              ",CLEANUP_FAIL=%" PRIu64 ",I2S_START_FAIL=%" PRIu64
              ",I2S_STOP_FAIL=%" PRIu64,
              s->audio_control.unexpected_connected_events,
              s->audio_control.rollback_attempts,
              s->audio_control.rollback_failures,
              s->audio_control.cleanup_disconnect_requests,
              s->audio_control.cleanup_disconnect_failures,
              s->audio_control.i2s_start_failures,
              s->audio_control.i2s_stop_failures);
    send_line("STATS_RX1",
              "CALLBACKS=%" PRIu64 ",ACCEPT_FRAMES=%" PRIu64
              ",ACCEPT_BYTES=%" PRIu64 ",DROP_FRAMES=%" PRIu64
              ",DROP_BYTES=%" PRIu64,
              s->incoming.incoming_callbacks,
              s->incoming.accepted_frames, s->incoming.accepted_bytes,
              s->incoming.dropped_frames, s->incoming.dropped_bytes);
    send_line("STATS_RX2",
              "INVALID_FRAMES=%" PRIu64 ",INVALID_BYTES=%" PRIu64
              ",INACTIVE_FRAMES=%" PRIu64 ",INACTIVE_BYTES=%" PRIu64
              ",STALE_FRAMES=%" PRIu64 ",STALE_BYTES=%" PRIu64,
              s->incoming.invalid_frames, s->incoming.invalid_bytes,
              s->incoming.inactive_frames, s->incoming.inactive_bytes,
              s->incoming.stale_handle_frames,
              s->incoming.stale_handle_bytes);
    send_line("STATS_RX3",
              "BAD_FRAMES=%" PRIu64 ",BAD_BYTES=%" PRIu64
              ",UNSUPPORTED_FRAMES=%" PRIu64
              ",UNSUPPORTED_BYTES=%" PRIu64
              ",RING_REJECT_FRAMES=%" PRIu64
              ",RING_REJECT_BYTES=%" PRIu64,
              s->incoming.bad_frames, s->incoming.bad_bytes,
              s->incoming.unsupported_codec_frames,
              s->incoming.unsupported_codec_bytes,
              s->incoming.ring_rejected_frames,
              s->incoming.ring_rejected_bytes);
    send_line("STATS_RX4",
              "REG_FAIL=%" PRIu64 ",ACTIVATE_FAIL=%" PRIu64
              ",OVER_BUDGET=%" PRIu64 ",LAST_US=%" PRIu32
              ",MAX_US_LIFETIME=%" PRIu32,
              s->incoming.registration_failures,
              s->incoming.activation_failures,
              s->incoming.callback_over_budget,
              s->incoming.callback_last_us,
              s->incoming.callback_max_us_lifetime);
    send_line("STATS_I2S1",
              "START=%" PRIu64 ",STOP=%" PRIu64
              ",START_FAIL=%" PRIu64 ",STOP_TIMEOUT=%" PRIu64
              ",WRITE=%" PRIu64 ",WRITE_FAIL=%" PRIu64,
              s->i2s.start_calls, s->i2s.stop_calls,
              s->i2s.start_failures, s->i2s.stop_timeouts,
              s->i2s.write_calls, s->i2s.write_failures);
    send_line("STATS_I2S2",
              "SHORT_WRITE=%" PRIu64 ",LOST_BYTES=%" PRIu64
              ",SILENCE_INTERVALS=%" PRIu64
              ",SILENCE_SAMPLES=%" PRIu64
              ",DEGRADED=%" PRIu64 ",QUARANTINE=%" PRIu64,
              s->i2s.short_writes, s->i2s.write_lost_bytes,
              s->i2s.silence_intervals, s->i2s.silence_samples,
              s->i2s.degraded_events, s->i2s.quarantine_events);
    send_line("STATS_I2S3",
              "PUSH=%" PRIu64 ",PUSH_FAIL=%" PRIu64
              ",STALE_PUSH=%" PRIu64 ",INVALID_PUSH=%" PRIu64
              ",RING_WRITE_BYTES=%" PRIu64
              ",RING_READ_BYTES=%" PRIu64,
              s->i2s.push_calls, s->i2s.push_failures,
              s->i2s.stale_pushes, s->i2s.invalid_pushes,
              s->i2s.ring_written_bytes, s->i2s.ring_read_bytes);
    send_line("STATS_I2S4",
              "OVERFLOW_FRAMES=%" PRIu64 ",OVERFLOW_BYTES=%" PRIu64
              ",UNDERFLOW_EVENTS=%" PRIu64
              ",UNDERFLOW_BYTES=%" PRIu64
              ",RING_STALE=%" PRIu64 ",RING_INVALID=%" PRIu64,
              s->i2s.ring_overflow_frames, s->i2s.ring_overflow_bytes,
              s->i2s.ring_underflow_events, s->i2s.ring_underflow_bytes,
              s->i2s.ring_stale_operations,
              s->i2s.ring_invalid_operations);
    send_line("STATS_I2S5",
              "CURRENT_USED=%zu,PEAK_USED_LIFETIME=%zu",
              s->i2s.ring_current_used,
              s->i2s.ring_peak_used_lifetime);
}

static cmd_status_t handle_stats(void)
{
    bt_hfp_manager_stats_t stats;
    esp_err_t err = bt_manager_hfp_get_stats(&stats);
    if (err != ESP_OK) return send_esp_error(NULL, err);
    send_stats_lines(&stats);
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "STATS", NULL);
    return CMD_SUCCESS;
}

static cmd_status_t handle_reset_stats(void)
{
    esp_err_t err = bt_manager_hfp_reset_stats();
    if (err != ESP_OK) return send_esp_error(NULL, err);
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "STATS_RESET", NULL);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_hfp(const cmd_context_t *ctx)
{
    if (ctx == NULL) return CMD_ERROR_INVALID_PARAM;
    if (ctx->param_count == 0) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "MISSING_SUBCOMMAND", NULL);
        return CMD_SUCCESS;
    }

    const char *subcommand = ctx->params[0];
    if (strcasecmp(subcommand, "STATUS") == 0) {
        return ctx->param_count == 1 ? handle_status() : invalid_count();
    }
    if (strcasecmp(subcommand, "CONNECT") == 0) {
        return ctx->param_count == 2
            ? handle_connect(ctx->params[1]) : invalid_count();
    }
    if (strcasecmp(subcommand, "DISCONNECT") == 0) {
        return ctx->param_count == 1 ? handle_disconnect() : invalid_count();
    }
    if (strcasecmp(subcommand, "AUDIO") == 0) {
        if (ctx->param_count != 2) return invalid_count();
        if (strcasecmp(ctx->params[1], "START") == 0) {
            return handle_audio_start();
        }
        if (strcasecmp(ctx->params[1], "STOP") == 0) {
            return handle_audio_stop();
        }
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "UNKNOWN_AUDIO_SUBCOMMAND", ctx->params[1]);
        return CMD_SUCCESS;
    }
    if (strcasecmp(subcommand, "MODE") == 0) {
        return ctx->param_count == 2
            ? handle_mode(ctx->params[1]) : invalid_count();
    }
    if (strcasecmp(subcommand, "CODEC") == 0) {
        return ctx->param_count == 1 ? handle_codec() : invalid_count();
    }
    if (strcasecmp(subcommand, "STATS") == 0) {
        return ctx->param_count == 1 ? handle_stats() : invalid_count();
    }
    if (strcasecmp(subcommand, "RESETSTATS") == 0) {
        return ctx->param_count == 1 ? handle_reset_stats() : invalid_count();
    }

    (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                            "UNKNOWN_SUBCOMMAND", subcommand);
    return CMD_SUCCESS;
}

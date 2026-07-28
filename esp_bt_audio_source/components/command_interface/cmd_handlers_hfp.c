#include "cmd_handlers.h"

#include <inttypes.h>
#include <strings.h>

#include "bt_hfp_manager.h"

static const char *wire_mode(bt_duplex_mode_t mode)
{
    switch (mode) {
    case BT_DUPLEX_MODE_DISABLED:
        return "DISABLED";
    case BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC:
        return "A2DP_MIC";
    case BT_DUPLEX_MODE_HFP_FULL_DUPLEX:
        return "HFP_FULL";
    case BT_DUPLEX_MODE_AUTO:
        return "AUTO";
    default:
        return "INVALID";
    }
}

static bool parse_mode(const char *text, bt_duplex_mode_t *mode_out)
{
    if (text == NULL || mode_out == NULL) return false;
    if (strcasecmp(text, "DISABLED") == 0) {
        *mode_out = BT_DUPLEX_MODE_DISABLED;
        return true;
    }
    if (strcasecmp(text, "A2DP_MIC") == 0) {
        *mode_out = BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC;
        return true;
    }
    if (strcasecmp(text, "HFP_FULL") == 0) {
        *mode_out = BT_DUPLEX_MODE_HFP_FULL_DUPLEX;
        return true;
    }
    if (strcasecmp(text, "AUTO") == 0) {
        *mode_out = BT_DUPLEX_MODE_AUTO;
        return true;
    }
    return false;
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
        out[index] = value;
        index++;
    }
    out[index] = '\0';
}

static cmd_status_t send_esp_error(const char *detail, esp_err_t error)
{
    cmd_send_response(CMD_STATUS_ERR, "HFP", esp_err_to_name(error), detail);
    return CMD_SUCCESS;
}

static cmd_status_t invalid_count(void)
{
    cmd_send_response(CMD_STATUS_ERR, "HFP", "INVALID_ARGUMENT_COUNT", NULL);
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
    const char *peer = status.duplex.peer_valid
        ? status.duplex.peer_mac : "NONE";

    char data[448];
    snprintf(data, sizeof(data),
             "GEN=%" PRIu32 ",PEER=%s,CONFIG_MODE=%s,REQUESTED=%s,"
             "EFFECTIVE=%s,A2DP_PROFILE=%s,A2DP_AUDIO=%s,"
             "HFP_PROFILE=%s,HFP_AUDIO=%s,CODEC=%s,I2S=%s,"
             "HEALTH=%s,LAST_ERROR=%s,ERROR_TEXT=%s",
             status.duplex.session_generation,
             peer,
             wire_mode(status.configured_mode),
             wire_mode(status.duplex.requested_mode),
             wire_mode(status.duplex.effective_mode),
             bt_a2dp_profile_state_to_string(
                 status.duplex.a2dp_profile_state),
             bt_a2dp_audio_state_to_string(
                 status.duplex.a2dp_audio_state),
             bt_hfp_profile_state_to_string(
                 status.duplex.hfp_profile_state),
             bt_hfp_audio_state_to_string(
                 status.duplex.hfp_audio_state),
             bt_hfp_codec_to_string(status.duplex.codec),
             bt_hfp_i2s_state_to_string(status.duplex.i2s_state),
             bt_audio_health_to_string(status.duplex.health),
             esp_err_to_name(status.duplex.last_error),
             error_text[0] != '\0' ? error_text : "NONE");
    cmd_send_response(CMD_STATUS_OK, "HFP", "STATUS", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_connect(const char *mac)
{
    bt_hfp_manager_status_t before;
    esp_err_t snapshot_err = bt_manager_hfp_get_status(&before);
    if (snapshot_err != ESP_OK) return send_esp_error(mac, snapshot_err);

    esp_err_t err = bt_manager_hfp_connect(mac);
    if (err != ESP_OK) return send_esp_error(mac, err);

    char data[128];
    if (before.duplex.peer_valid &&
        strcasecmp(before.duplex.peer_mac, mac) == 0 &&
        before.duplex.hfp_profile_state ==
            BT_HFP_PROFILE_SLC_CONNECTED) {
        snprintf(data, sizeof(data), "MAC=%s,GEN=%" PRIu32,
                 mac, before.duplex.session_generation);
        cmd_send_response(CMD_STATUS_OK, "HFP", "ALREADY_CONNECTED", data);
    } else {
        snprintf(data, sizeof(data),
                 "MAC=%s,COMPLETION=HFP_PROFILE_EVENT", mac);
        cmd_send_response(CMD_STATUS_OK, "HFP", "CONNECT_ACCEPTED", data);
    }
    return CMD_SUCCESS;
}

static cmd_status_t handle_disconnect(void)
{
    bt_hfp_manager_status_t before;
    esp_err_t snapshot_err = bt_manager_hfp_get_status(&before);
    if (snapshot_err != ESP_OK) return send_esp_error(NULL, snapshot_err);

    esp_err_t err = bt_manager_hfp_disconnect();
    if (err != ESP_OK) return send_esp_error(NULL, err);

    if (before.duplex.hfp_profile_state ==
        BT_HFP_PROFILE_DISCONNECTED) {
        cmd_send_response(CMD_STATUS_OK, "HFP",
                          "ALREADY_DISCONNECTED", NULL);
    } else {
        cmd_send_response(CMD_STATUS_OK, "HFP", "DISCONNECT_ACCEPTED",
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
    cmd_send_response(CMD_STATUS_OK, "HFP", "AUDIO_STARTED", data);
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
    cmd_send_response(CMD_STATUS_OK, "HFP", "AUDIO_STOPPED", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_mode(const char *mode_text)
{
    bt_duplex_mode_t mode;
    if (!parse_mode(mode_text, &mode)) {
        cmd_send_response(CMD_STATUS_ERR, "HFP", "UNKNOWN_MODE", mode_text);
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
    cmd_send_response(CMD_STATUS_OK, "HFP", "MODE_SET", data);
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
    cmd_send_response(CMD_STATUS_OK, "HFP", "CODEC", data);
    return CMD_SUCCESS;
}

static void send_stats_lines(const bt_hfp_manager_stats_t *stats)
{
    char data[448];
    snprintf(data, sizeof(data),
             "GEN=%" PRIu32 ",PEER=%s,RESET_SEQUENCE=%" PRIu64,
             stats->generation,
             stats->peer_valid ? stats->peer_mac : "NONE",
             stats->reset_sequence);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_STATE", data);

    snprintf(data, sizeof(data),
             "STALE_GEN=%" PRIu64 ",WRONG_PEER=%" PRIu64
             ",ILLEGAL=%" PRIu64 ",INVALID=%" PRIu64
             ",RECOVERIES=%" PRIu64 ",RX_FRAMES=%" PRIu64
             ",RX_BYTES=%" PRIu64 ",DROP_FRAMES=%" PRIu64
             ",DROP_BYTES=%" PRIu64 ",I2S_UNDERFLOWS=%" PRIu64
             ",I2S_TIMEOUTS=%" PRIu64,
             stats->duplex.stale_generation_events,
             stats->duplex.wrong_peer_events,
             stats->duplex.illegal_transitions,
             stats->duplex.invalid_arguments,
             stats->duplex.recoveries,
             stats->duplex.incoming_frames,
             stats->duplex.incoming_bytes,
             stats->duplex.incoming_dropped_frames,
             stats->duplex.incoming_dropped_bytes,
             stats->duplex.i2s_underflows,
             stats->duplex.i2s_timeouts);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_DUPLEX", data);

    snprintf(data, sizeof(data),
             "CONNECT_ACCEPT=%" PRIu64 ",DISCONNECT_ACCEPT=%" PRIu64
             ",IMMEDIATE_FAIL=%" PRIu64 ",REMOTE_REJECT=%" PRIu64
             ",WATCHDOG_TIMEOUT=%" PRIu64 ",STALE_EVENT=%" PRIu64
             ",WRONG_PEER=%" PRIu64 ",DISPATCH_FAIL=%" PRIu64,
             stats->slc.accepted_connect_requests,
             stats->slc.accepted_disconnect_requests,
             stats->slc.immediate_failures,
             stats->slc.remote_rejections,
             stats->slc.watchdog_timeouts,
             stats->slc.stale_operation_events,
             stats->slc.wrong_peer_events,
             stats->slc.dispatch_failures);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_SLC", data);

    snprintf(data, sizeof(data),
             "START=%" PRIu64 ",STOP=%" PRIu64
             ",START_OK=%" PRIu64 ",STOP_OK=%" PRIu64
             ",START_FAIL=%" PRIu64 ",STOP_FAIL=%" PRIu64
             ",DISPATCH_FAIL=%" PRIu64 ",IMMEDIATE_FAIL=%" PRIu64
             ",REQUEST_TIMEOUT=%" PRIu64 ",EVENT_TIMEOUT=%" PRIu64,
             stats->audio_control.start_calls,
             stats->audio_control.stop_calls,
             stats->audio_control.successful_starts,
             stats->audio_control.successful_stops,
             stats->audio_control.start_failures,
             stats->audio_control.stop_failures,
             stats->audio_control.dispatch_failures,
             stats->audio_control.immediate_failures,
             stats->audio_control.request_timeouts,
             stats->audio_control.event_timeouts);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_AUDIO1", data);

    snprintf(data, sizeof(data),
             "STALE=%" PRIu64 ",WRONG_PEER=%" PRIu64
             ",UNEXPECTED=%" PRIu64 ",ROLLBACK=%" PRIu64
             ",ROLLBACK_FAIL=%" PRIu64 ",CLEANUP_REQ=%" PRIu64
             ",CLEANUP_FAIL=%" PRIu64 ",I2S_START_FAIL=%" PRIu64
             ",I2S_STOP_FAIL=%" PRIu64,
             stats->audio_control.stale_events,
             stats->audio_control.wrong_peer_events,
             stats->audio_control.unexpected_connected_events,
             stats->audio_control.rollback_attempts,
             stats->audio_control.rollback_failures,
             stats->audio_control.cleanup_disconnect_requests,
             stats->audio_control.cleanup_disconnect_failures,
             stats->audio_control.i2s_start_failures,
             stats->audio_control.i2s_stop_failures);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_AUDIO2", data);

    snprintf(data, sizeof(data),
             "CALLBACKS=%" PRIu64 ",ACCEPT_FRAMES=%" PRIu64
             ",ACCEPT_BYTES=%" PRIu64 ",DROP_FRAMES=%" PRIu64
             ",DROP_BYTES=%" PRIu64 ",INVALID_FRAMES=%" PRIu64
             ",INVALID_BYTES=%" PRIu64 ",INACTIVE_FRAMES=%" PRIu64
             ",INACTIVE_BYTES=%" PRIu64,
             stats->incoming.incoming_callbacks,
             stats->incoming.accepted_frames,
             stats->incoming.accepted_bytes,
             stats->incoming.dropped_frames,
             stats->incoming.dropped_bytes,
             stats->incoming.invalid_frames,
             stats->incoming.invalid_bytes,
             stats->incoming.inactive_frames,
             stats->incoming.inactive_bytes);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_RX1", data);

    snprintf(data, sizeof(data),
             "STALE_FRAMES=%" PRIu64 ",STALE_BYTES=%" PRIu64
             ",BAD_FRAMES=%" PRIu64 ",BAD_BYTES=%" PRIu64
             ",UNSUPPORTED_FRAMES=%" PRIu64 ",UNSUPPORTED_BYTES=%" PRIu64
             ",RING_REJECT_FRAMES=%" PRIu64 ",RING_REJECT_BYTES=%" PRIu64
             ",REG_FAIL=%" PRIu64 ",ACTIVATE_FAIL=%" PRIu64
             ",OVER_BUDGET=%" PRIu64 ",LAST_US=%" PRIu32
             ",MAX_US_LIFETIME=%" PRIu32,
             stats->incoming.stale_handle_frames,
             stats->incoming.stale_handle_bytes,
             stats->incoming.bad_frames,
             stats->incoming.bad_bytes,
             stats->incoming.unsupported_codec_frames,
             stats->incoming.unsupported_codec_bytes,
             stats->incoming.ring_rejected_frames,
             stats->incoming.ring_rejected_bytes,
             stats->incoming.registration_failures,
             stats->incoming.activation_failures,
             stats->incoming.callback_over_budget,
             stats->incoming.callback_last_us,
             stats->incoming.callback_max_us_lifetime);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_RX2", data);

    snprintf(data, sizeof(data),
             "START=%" PRIu64 ",STOP=%" PRIu64
             ",START_FAIL=%" PRIu64 ",STOP_TIMEOUT=%" PRIu64
             ",WRITE=%" PRIu64 ",WRITE_FAIL=%" PRIu64
             ",SHORT_WRITE=%" PRIu64 ",LOST_BYTES=%" PRIu64
             ",SILENCE_INTERVALS=%" PRIu64 ",SILENCE_SAMPLES=%" PRIu64
             ",DEGRADED=%" PRIu64 ",QUARANTINE=%" PRIu64,
             stats->i2s.start_calls,
             stats->i2s.stop_calls,
             stats->i2s.start_failures,
             stats->i2s.stop_timeouts,
             stats->i2s.write_calls,
             stats->i2s.write_failures,
             stats->i2s.short_writes,
             stats->i2s.write_lost_bytes,
             stats->i2s.silence_intervals,
             stats->i2s.silence_samples,
             stats->i2s.degraded_events,
             stats->i2s.quarantine_events);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_I2S1", data);

    snprintf(data, sizeof(data),
             "PUSH=%" PRIu64 ",PUSH_FAIL=%" PRIu64
             ",STALE_PUSH=%" PRIu64 ",INVALID_PUSH=%" PRIu64
             ",RING_WRITE_BYTES=%" PRIu64 ",RING_READ_BYTES=%" PRIu64
             ",OVERFLOW_FRAMES=%" PRIu64 ",OVERFLOW_BYTES=%" PRIu64
             ",UNDERFLOW_EVENTS=%" PRIu64 ",UNDERFLOW_BYTES=%" PRIu64
             ",RING_STALE=%" PRIu64 ",RING_INVALID=%" PRIu64
             ",CURRENT_USED=%zu,PEAK_USED_LIFETIME=%zu",
             stats->i2s.push_calls,
             stats->i2s.push_failures,
             stats->i2s.stale_pushes,
             stats->i2s.invalid_pushes,
             stats->i2s.ring_written_bytes,
             stats->i2s.ring_read_bytes,
             stats->i2s.ring_overflow_frames,
             stats->i2s.ring_overflow_bytes,
             stats->i2s.ring_underflow_events,
             stats->i2s.ring_underflow_bytes,
             stats->i2s.ring_stale_operations,
             stats->i2s.ring_invalid_operations,
             stats->i2s.ring_current_used,
             stats->i2s.ring_peak_used_lifetime);
    cmd_send_response(CMD_STATUS_INFO, "HFP", "STATS_I2S2", data);
}

static cmd_status_t handle_stats(void)
{
    bt_hfp_manager_stats_t stats;
    esp_err_t err = bt_manager_hfp_get_stats(&stats);
    if (err != ESP_OK) return send_esp_error(NULL, err);
    send_stats_lines(&stats);
    cmd_send_response(CMD_STATUS_OK, "HFP", "STATS", NULL);
    return CMD_SUCCESS;
}

static cmd_status_t handle_reset_stats(void)
{
    esp_err_t err = bt_manager_hfp_reset_stats();
    if (err != ESP_OK) return send_esp_error(NULL, err);
    cmd_send_response(CMD_STATUS_OK, "HFP", "STATS_RESET", NULL);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_hfp(const cmd_context_t *ctx)
{
    if (ctx == NULL) return CMD_ERROR_INVALID_PARAM;
    if (ctx->param_count == 0) {
        cmd_send_response(CMD_STATUS_ERR, "HFP", "MISSING_SUBCOMMAND", NULL);
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
        cmd_send_response(CMD_STATUS_ERR, "HFP",
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

    cmd_send_response(CMD_STATUS_ERR, "HFP", "UNKNOWN_SUBCOMMAND", subcommand);
    return CMD_SUCCESS;
}

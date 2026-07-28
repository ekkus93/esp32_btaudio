#include "cmd_handlers.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "bt_hfp_manager.h"

#define HFP_DATA_BUFFER_SIZE 320U
#define HFP_STATUS_BUFFER_SIZE 448U

static const char *wire_mode(bt_duplex_mode_t value)
{
    switch (value) {
    case BT_DUPLEX_MODE_DISABLED: return "DISABLED";
    case BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC: return "A2DP_MIC";
    case BT_DUPLEX_MODE_HFP_FULL_DUPLEX: return "HFP_FULL";
    case BT_DUPLEX_MODE_AUTO: return "AUTO";
    default: return "UNKNOWN";
    }
}

static const char *wire_a2dp_profile(bt_a2dp_profile_state_t value)
{
    switch (value) {
    case BT_A2DP_PROFILE_DISCONNECTED: return "DISCONNECTED";
    case BT_A2DP_PROFILE_CONNECTING: return "CONNECTING";
    case BT_A2DP_PROFILE_CONNECTED: return "CONNECTED";
    case BT_A2DP_PROFILE_DISCONNECTING: return "DISCONNECTING";
    default: return "UNKNOWN";
    }
}

static const char *wire_a2dp_audio(bt_a2dp_audio_state_t value)
{
    switch (value) {
    case BT_A2DP_AUDIO_STOPPED: return "STOPPED";
    case BT_A2DP_AUDIO_STARTED: return "STARTED";
    case BT_A2DP_AUDIO_REMOTE_SUSPENDED: return "REMOTE_SUSPENDED";
    default: return "UNKNOWN";
    }
}

static const char *wire_hfp_profile(bt_hfp_profile_state_t value)
{
    switch (value) {
    case BT_HFP_PROFILE_UNINITIALIZED: return "UNINITIALIZED";
    case BT_HFP_PROFILE_DISCONNECTED: return "DISCONNECTED";
    case BT_HFP_PROFILE_CONNECTING: return "CONNECTING";
    case BT_HFP_PROFILE_SLC_CONNECTED: return "SLC_CONNECTED";
    case BT_HFP_PROFILE_DISCONNECTING: return "DISCONNECTING";
    case BT_HFP_PROFILE_FAULTED: return "FAULTED";
    default: return "UNKNOWN";
    }
}

static const char *wire_hfp_audio(bt_hfp_audio_state_t value)
{
    switch (value) {
    case BT_HFP_AUDIO_DISCONNECTED: return "DISCONNECTED";
    case BT_HFP_AUDIO_CONNECTING: return "CONNECTING";
    case BT_HFP_AUDIO_CONNECTED_CVSD: return "CONNECTED_CVSD";
    case BT_HFP_AUDIO_CONNECTED_MSBC: return "CONNECTED_MSBC";
    case BT_HFP_AUDIO_DISCONNECTING: return "DISCONNECTING";
    case BT_HFP_AUDIO_FAULTED: return "FAULTED";
    default: return "UNKNOWN";
    }
}

static const char *wire_codec(bt_hfp_codec_t value)
{
    switch (value) {
    case BT_HFP_CODEC_NONE: return "NONE";
    case BT_HFP_CODEC_CVSD: return "CVSD";
    case BT_HFP_CODEC_MSBC: return "MSBC";
    default: return "UNKNOWN";
    }
}

static const char *wire_i2s(bt_hfp_i2s_state_t value)
{
    switch (value) {
    case BT_HFP_I2S_STOPPED: return "STOPPED";
    case BT_HFP_I2S_STARTING: return "STARTING";
    case BT_HFP_I2S_RUNNING: return "RUNNING";
    case BT_HFP_I2S_STOPPING: return "STOPPING";
    case BT_HFP_I2S_FAULTED: return "FAULTED";
    case BT_HFP_I2S_QUARANTINED: return "QUARANTINED";
    default: return "UNKNOWN";
    }
}

static const char *wire_health(bt_audio_health_t value)
{
    switch (value) {
    case BT_AUDIO_HEALTH_OK: return "OK";
    case BT_AUDIO_HEALTH_DEGRADED: return "DEGRADED";
    case BT_AUDIO_HEALTH_FAULTED: return "FAULTED";
    case BT_AUDIO_HEALTH_QUARANTINED: return "QUARANTINED";
    default: return "UNKNOWN";
    }
}

static const char *wire_policy_state(bt_duplex_policy_state_t value)
{
    switch (value) {
    case BT_DUPLEX_POLICY_SATISFIED: return "SATISFIED";
    case BT_DUPLEX_POLICY_WAITING: return "WAITING";
    case BT_DUPLEX_POLICY_INCOMPATIBLE: return "INCOMPATIBLE";
    case BT_DUPLEX_POLICY_COMPATIBILITY_REQUIRED:
        return "COMPATIBILITY_REQUIRED";
    default: return "UNKNOWN";
    }
}

static const char *wire_policy_reason(bt_duplex_policy_reason_t value)
{
    switch (value) {
    case BT_DUPLEX_POLICY_REASON_REQUESTED_MODE: return "REQUESTED_MODE";
    case BT_DUPLEX_POLICY_REASON_WAITING_A2DP_CONNECTION:
        return "WAITING_A2DP_CONNECTION";
    case BT_DUPLEX_POLICY_REASON_WAITING_A2DP_STREAM:
        return "WAITING_A2DP_STREAM";
    case BT_DUPLEX_POLICY_REASON_WAITING_HFP_SLC:
        return "WAITING_HFP_SLC";
    case BT_DUPLEX_POLICY_REASON_WAITING_SCO: return "WAITING_SCO";
    case BT_DUPLEX_POLICY_REASON_HFP_DOWNLINK_NOT_IMPLEMENTED:
        return "HFP_DOWNLINK_NOT_IMPLEMENTED";
    case BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO:
        return "REMOTE_SUSPENDED_A2DP_DURING_SCO";
    case BT_DUPLEX_POLICY_REASON_A2DP_STOPPED_DURING_SCO:
        return "A2DP_STOPPED_DURING_SCO";
    case BT_DUPLEX_POLICY_REASON_A2DP_RESUMED: return "A2DP_RESUMED";
    case BT_DUPLEX_POLICY_REASON_SCO_STOPPED: return "SCO_STOPPED";
    default: return "UNKNOWN";
    }
}

static const char *wire_downlink_owner(bt_duplex_downlink_owner_t value)
{
    switch (value) {
    case BT_DUPLEX_DOWNLINK_OWNER_A2DP: return "A2DP";
    case BT_DUPLEX_DOWNLINK_OWNER_HFP: return "HFP";
    default: return "UNKNOWN";
    }
}

static bt_duplex_mode_t effective_mode(bt_duplex_mode_t mode)
{
    return mode == BT_DUPLEX_MODE_AUTO
        ? BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC : mode;
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
        unsigned char raw = (unsigned char)source[index];
        char value = (char)raw;
        if (value == '|' || value == ',' || raw < 0x20U || raw == 0x7fU) {
            value = '_';
        }
        out[index++] = value;
    }
    out[index] = '\0';
}

static bool format_checked(char *out, size_t out_size,
                           const char *format, ...)
{
    if (out == NULL || out_size == 0U || format == NULL) return false;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(out, out_size, format, args);
    va_end(args);
    return written >= 0 && (size_t)written < out_size;
}

static cmd_status_t send_esp_error(const char *detail, esp_err_t error)
{
    char safe_detail[CMD_MAX_PARAM_LEN];
    sanitize_field(detail, safe_detail, sizeof(safe_detail));
    (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                            esp_err_to_name(error), safe_detail);
    return CMD_SUCCESS;
}

static cmd_status_t invalid_count(void)
{
    (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                            "INVALID_ARGUMENT_COUNT", NULL);
    return CMD_SUCCESS;
}

static bool same_peer(const char *lhs, const char *rhs)
{
    return lhs != NULL && rhs != NULL && strcasecmp(lhs, rhs) == 0;
}

static bool send_policy_status(const bt_hfp_manager_status_t *status)
{
    if (status == NULL) return false;

    const bt_duplex_policy_snapshot_t *policy = &status->policy;
    const bool available = policy->initialized;
    char data[HFP_DATA_BUFFER_SIZE];
    if (!format_checked(
            data, sizeof(data),
            "STATE=%s,REASON=%s,REQUESTED=%s,EFFECTIVE=%s,"
            "DOWNLINK_OWNER=%s,HFP_DOWNLINK_REQUESTED=%u,GEN=%" PRIu32,
            available ? wire_policy_state(policy->state) : "UNAVAILABLE",
            available ? wire_policy_reason(policy->reason) : "NONE",
            available ? wire_mode(policy->requested) : "NONE",
            available ? wire_mode(policy->effective) : "NONE",
            available ? wire_downlink_owner(policy->downlink_owner) : "NONE",
            available && policy->request_hfp_downlink ? 1U : 0U,
            available ? policy->generation : 0U)) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "STATUS_POLICY_LINE_TOO_LONG", NULL);
        return false;
    }

    return cmd_send_response(CMD_STATUS_INFO, "HFP", "STATUS_POLICY", data) ==
           CMD_SUCCESS;
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

    char data[HFP_STATUS_BUFFER_SIZE];
    if (!format_checked(
            data, sizeof(data),
            "GEN=%" PRIu32 ",PEER=%s,CONFIG_MODE=%s,REQUESTED=%s,"
            "EFFECTIVE=%s,A2DP_PROFILE=%s,A2DP_AUDIO=%s,"
            "HFP_PROFILE=%s,HFP_AUDIO=%s,CODEC=%s,I2S=%s,"
            "HEALTH=%s,LAST_ERROR=%s,ERROR_TEXT=%s",
            status.duplex.session_generation,
            peer,
            wire_mode(status.configured_mode),
            wire_mode(status.duplex.requested_mode),
            wire_mode(status.duplex.effective_mode),
            wire_a2dp_profile(status.duplex.a2dp_profile_state),
            wire_a2dp_audio(status.duplex.a2dp_audio_state),
            wire_hfp_profile(status.duplex.hfp_profile_state),
            wire_hfp_audio(status.duplex.hfp_audio_state),
            wire_codec(status.duplex.codec),
            wire_i2s(status.duplex.i2s_state),
            wire_health(status.duplex.health),
            esp_err_to_name(status.duplex.last_error),
            error_text[0] != '\0' ? error_text : "NONE")) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "STATUS_LINE_TOO_LONG", NULL);
        return CMD_SUCCESS;
    }

    if (!send_policy_status(&status)) return CMD_SUCCESS;
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "STATUS", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_connect(const char *mac)
{
    bt_hfp_manager_status_t before;
    bool have_before = bt_manager_hfp_get_status(&before) == ESP_OK;
    if (have_before && before.duplex.peer_valid &&
        same_peer(before.duplex.peer_mac, mac) &&
        before.duplex.hfp_profile_state == BT_HFP_PROFILE_SLC_CONNECTED) {
        char safe_mac[CMD_MAX_PARAM_LEN];
        char data[128];
        sanitize_field(mac, safe_mac, sizeof(safe_mac));
        if (!format_checked(data, sizeof(data),
                            "MAC=%s,GEN=%" PRIu32,
                            safe_mac, before.duplex.session_generation)) {
            (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                    "CONNECT_LINE_TOO_LONG", NULL);
            return CMD_SUCCESS;
        }
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "ALREADY_CONNECTED", data);
        return CMD_SUCCESS;
    }

    esp_err_t err = bt_manager_hfp_connect(mac);
    if (err != ESP_OK) return send_esp_error(mac, err);

    char safe_mac[CMD_MAX_PARAM_LEN];
    char data[128];
    sanitize_field(mac, safe_mac, sizeof(safe_mac));
    if (!format_checked(data, sizeof(data),
                        "MAC=%s,COMPLETION=HFP_PROFILE_EVENT",
                        safe_mac)) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "CONNECT_LINE_TOO_LONG", NULL);
        return CMD_SUCCESS;
    }
    (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                            "CONNECT_ACCEPTED", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_disconnect(void)
{
    bt_hfp_manager_status_t before;
    bool have_before = bt_manager_hfp_get_status(&before) == ESP_OK;
    if (have_before &&
        before.duplex.hfp_profile_state == BT_HFP_PROFILE_DISCONNECTED) {
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "ALREADY_DISCONNECTED", NULL);
        return CMD_SUCCESS;
    }

    esp_err_t err = bt_manager_hfp_disconnect();
    if (err != ESP_OK) return send_esp_error(NULL, err);
    (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                            "DISCONNECT_ACCEPTED",
                            "COMPLETION=HFP_PROFILE_EVENT");
    return CMD_SUCCESS;
}

static cmd_status_t handle_audio_start(void)
{
    esp_err_t err = bt_manager_hfp_audio_start();
    if (err != ESP_OK) return send_esp_error(NULL, err);

    bt_hfp_manager_status_t status;
    err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) {
        char data[96];
        if (!format_checked(data, sizeof(data),
                            "STATUS_ERROR=%s", esp_err_to_name(err))) {
            data[0] = '\0';
        }
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "AUDIO_STARTED_STATUS_UNAVAILABLE", data);
        return CMD_SUCCESS;
    }

    char data[96];
    if (!format_checked(data, sizeof(data),
                        "GEN=%" PRIu32 ",CODEC=%s",
                        status.duplex.session_generation,
                        wire_codec(status.duplex.codec))) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "AUDIO_LINE_TOO_LONG", NULL);
        return CMD_SUCCESS;
    }
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "AUDIO_STARTED", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_audio_stop(void)
{
    esp_err_t err = bt_manager_hfp_audio_stop();
    if (err != ESP_OK) return send_esp_error(NULL, err);

    bt_hfp_manager_status_t status;
    err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) {
        char data[96];
        if (!format_checked(data, sizeof(data),
                            "STATUS_ERROR=%s", esp_err_to_name(err))) {
            data[0] = '\0';
        }
        (void)cmd_send_response(CMD_STATUS_OK, "HFP",
                                "AUDIO_STOPPED_STATUS_UNAVAILABLE", data);
        return CMD_SUCCESS;
    }

    char data[64];
    if (!format_checked(data, sizeof(data),
                        "GEN=%" PRIu32,
                        status.duplex.session_generation)) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "AUDIO_LINE_TOO_LONG", NULL);
        return CMD_SUCCESS;
    }
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "AUDIO_STOPPED", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_mode(const char *mode_text)
{
    bt_duplex_mode_t mode;
    if (!parse_mode(mode_text, &mode)) {
        char safe_mode[CMD_MAX_PARAM_LEN];
        sanitize_field(mode_text, safe_mode, sizeof(safe_mode));
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "UNKNOWN_MODE", safe_mode);
        return CMD_SUCCESS;
    }

    esp_err_t err = bt_manager_hfp_set_mode(mode);
    if (err != ESP_OK) return send_esp_error(mode_text, err);

    char data[96];
    if (!format_checked(data, sizeof(data),
                        "CONFIG_MODE=%s,EFFECTIVE=%s",
                        wire_mode(mode), wire_mode(effective_mode(mode)))) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "MODE_LINE_TOO_LONG", NULL);
        return CMD_SUCCESS;
    }
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "MODE_SET", data);
    return CMD_SUCCESS;
}

static cmd_status_t handle_codec(void)
{
    bt_hfp_manager_status_t status;
    esp_err_t err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return send_esp_error(NULL, err);

    char data[96];
    if (!format_checked(data, sizeof(data),
                        "GEN=%" PRIu32 ",CODEC=%s",
                        status.duplex.session_generation,
                        wire_codec(status.duplex.codec))) {
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "CODEC_LINE_TOO_LONG", NULL);
        return CMD_SUCCESS;
    }
    (void)cmd_send_response(CMD_STATUS_OK, "HFP", "CODEC", data);
    return CMD_SUCCESS;
}

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

static bool send_stats_lines(const bt_hfp_manager_stats_t *stats)
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
         ",I2S_STOP_FAIL=%" PRIu64,
         stats->audio_control.unexpected_connected_events,
         stats->audio_control.rollback_attempts,
         stats->audio_control.rollback_failures,
         stats->audio_control.cleanup_disconnect_requests,
         stats->audio_control.cleanup_disconnect_failures,
         stats->audio_control.i2s_start_failures,
         stats->audio_control.i2s_stop_failures);
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
         ",OVER_BUDGET=%" PRIu64 ",LAST_US=%" PRIu32
         ",MAX_US_LIFETIME=%" PRIu32,
         stats->incoming.registration_failures,
         stats->incoming.activation_failures,
         stats->incoming.callback_over_budget,
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

static bool send_diagnostics_lines(
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

static cmd_status_t handle_stats(void)
{
    bt_hfp_manager_stats_t stats;
    esp_err_t err = bt_manager_hfp_get_stats(&stats);
    if (err != ESP_OK) return send_esp_error(NULL, err);

    bt_hfp_manager_diagnostics_t diagnostics;
    err = bt_manager_hfp_get_diagnostics(&diagnostics);
    if (err != ESP_OK) return send_esp_error(NULL, err);

    if (!send_stats_lines(&stats)) return CMD_SUCCESS;
    if (!send_diagnostics_lines(&diagnostics)) return CMD_SUCCESS;
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
        char safe_audio[CMD_MAX_PARAM_LEN];
        sanitize_field(ctx->params[1], safe_audio, sizeof(safe_audio));
        (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                                "UNKNOWN_AUDIO_SUBCOMMAND", safe_audio);
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

    char safe_subcommand[CMD_MAX_PARAM_LEN];
    sanitize_field(subcommand, safe_subcommand, sizeof(safe_subcommand));
    (void)cmd_send_response(CMD_STATUS_ERR, "HFP",
                            "UNKNOWN_SUBCOMMAND", safe_subcommand);
    return CMD_SUCCESS;
}

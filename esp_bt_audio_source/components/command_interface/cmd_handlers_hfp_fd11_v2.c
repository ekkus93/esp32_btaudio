#include "cmd_handlers.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "bt_hfp_manager.h"
#include "cmd_handlers_hfp_internal.h"

#define HFP_STATUS_BUFFER_SIZE 448U

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

void sanitize_field(const char *source, char *out, size_t out_size)
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

bool format_checked(char *out, size_t out_size,
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

static cmd_status_t send_audio_status_unavailable(const char *operation,
                                                   esp_err_t error)
{
    char data[160];
    if (!format_checked(
            data, sizeof(data),
            "OPERATION=%s,LOWER_OPERATION=SUCCEEDED,STATUS_ERROR=%s",
            operation, esp_err_to_name(error))) {
        return cmd_send_response(CMD_STATUS_ERR, "HFP",
                                 "AUDIO_STATUS_LINE_TOO_LONG", NULL);
    }
    return cmd_send_response(CMD_STATUS_ERR, "HFP",
                             "AUDIO_STATUS_UNAVAILABLE", data);
}

static cmd_status_t handle_audio_start(void)
{
    esp_err_t err = bt_manager_hfp_audio_start();
    if (err != ESP_OK) return send_esp_error(NULL, err);

    bt_hfp_manager_status_t status;
    err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return send_audio_status_unavailable("START", err);

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
    if (err != ESP_OK) return send_audio_status_unavailable("STOP", err);

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

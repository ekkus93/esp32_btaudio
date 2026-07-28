#include "bt_hfp_event_contract.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "command_interface.h"

#define BT_HFP_EVENT_DATA_LEN 160U

static const char *profile_wire(bt_hfp_profile_state_t state)
{
    switch (state) {
    case BT_HFP_PROFILE_UNINITIALIZED: return "UNINITIALIZED";
    case BT_HFP_PROFILE_DISCONNECTED: return "DISCONNECTED";
    case BT_HFP_PROFILE_CONNECTING: return "CONNECTING";
    case BT_HFP_PROFILE_SLC_CONNECTED: return "SLC_CONNECTED";
    case BT_HFP_PROFILE_DISCONNECTING: return "DISCONNECTING";
    case BT_HFP_PROFILE_FAULTED: return "FAULTED";
    default: return NULL;
    }
}

static const char *audio_wire(bt_hfp_audio_state_t state)
{
    switch (state) {
    case BT_HFP_AUDIO_DISCONNECTED: return "DISCONNECTED";
    case BT_HFP_AUDIO_CONNECTING: return "CONNECTING";
    case BT_HFP_AUDIO_CONNECTED_CVSD: return "CONNECTED_CVSD";
    case BT_HFP_AUDIO_CONNECTED_MSBC: return "CONNECTED_MSBC";
    case BT_HFP_AUDIO_DISCONNECTING: return "DISCONNECTING";
    case BT_HFP_AUDIO_FAULTED: return "FAULTED";
    default: return NULL;
    }
}

static const char *codec_wire(bt_hfp_codec_t codec)
{
    switch (codec) {
    case BT_HFP_CODEC_NONE: return "NONE";
    case BT_HFP_CODEC_CVSD: return "CVSD";
    case BT_HFP_CODEC_MSBC: return "MSBC";
    default: return NULL;
    }
}

static const char *mode_wire(bt_duplex_mode_t mode)
{
    switch (mode) {
    case BT_DUPLEX_MODE_DISABLED: return "DISABLED";
    case BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC: return "A2DP_MIC";
    case BT_DUPLEX_MODE_HFP_FULL_DUPLEX: return "HFP_FULL";
    case BT_DUPLEX_MODE_AUTO: return "AUTO";
    default: return NULL;
    }
}

static const char *mode_reason_wire(bt_hfp_mode_event_reason_t reason)
{
    switch (reason) {
    case BT_HFP_MODE_EVENT_REASON_COMMAND: return "COMMAND";
    case BT_HFP_MODE_EVENT_REASON_SESSION_BEGIN: return "SESSION_BEGIN";
    case BT_HFP_MODE_EVENT_REASON_RECOVERY: return "RECOVERY";
    case BT_HFP_MODE_EVENT_REASON_POLICY: return "POLICY";
    case BT_HFP_MODE_EVENT_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO:
        return "REMOTE_SUSPENDED_A2DP_DURING_SCO";
    case BT_HFP_MODE_EVENT_REASON_A2DP_STOPPED_DURING_SCO:
        return "A2DP_STOPPED_DURING_SCO";
    case BT_HFP_MODE_EVENT_REASON_A2DP_RESUMED: return "A2DP_RESUMED";
    case BT_HFP_MODE_EVENT_REASON_SCO_STOPPED: return "SCO_STOPPED";
    default: return NULL;
    }
}

static const char *i2s_wire(bt_hfp_i2s_state_t state)
{
    switch (state) {
    case BT_HFP_I2S_STOPPED: return "STOPPED";
    case BT_HFP_I2S_STARTING: return "STARTING";
    case BT_HFP_I2S_RUNNING: return "RUNNING";
    case BT_HFP_I2S_STOPPING: return "STOPPING";
    case BT_HFP_I2S_FAULTED: return "FAULTED";
    case BT_HFP_I2S_QUARANTINED: return "QUARANTINED";
    default: return NULL;
    }
}

static const char *health_wire(bt_audio_health_t health)
{
    switch (health) {
    case BT_AUDIO_HEALTH_OK: return "OK";
    case BT_AUDIO_HEALTH_DEGRADED: return "DEGRADED";
    case BT_AUDIO_HEALTH_FAULTED: return "FAULTED";
    case BT_AUDIO_HEALTH_QUARANTINED: return "QUARANTINED";
    default: return NULL;
    }
}

/* Use comparisons rather than a switch because lightweight host ESP-IDF
 * headers may alias ESP_FAIL and ESP_ERR_NO_MEM. The production values are
 * distinct, but duplicate case labels would make focused host builds fail. */
static const char *error_wire(esp_err_t error)
{
    if (error == ESP_OK) return "ESP_OK";
#if !defined(ESP_ERR_NO_MEM) || ESP_ERR_NO_MEM != ESP_FAIL
    if (error == ESP_ERR_NO_MEM) return "ESP_ERR_NO_MEM";
#endif
    if (error == ESP_ERR_INVALID_ARG) return "ESP_ERR_INVALID_ARG";
    if (error == ESP_ERR_INVALID_STATE) return "ESP_ERR_INVALID_STATE";
#ifdef ESP_ERR_INVALID_SIZE
    if (error == ESP_ERR_INVALID_SIZE) return "ESP_ERR_INVALID_SIZE";
#endif
#ifdef ESP_ERR_NOT_FOUND
    if (error == ESP_ERR_NOT_FOUND) return "ESP_ERR_NOT_FOUND";
#endif
#ifdef ESP_ERR_NOT_SUPPORTED
    if (error == ESP_ERR_NOT_SUPPORTED) return "ESP_ERR_NOT_SUPPORTED";
#endif
#ifdef ESP_ERR_TIMEOUT
    if (error == ESP_ERR_TIMEOUT) return "ESP_ERR_TIMEOUT";
#endif
    if (error == ESP_FAIL) return "ESP_FAIL";
    return "ESP_ERR_UNKNOWN";
}

static const char *health_reason_wire(bt_audio_health_t severity,
                                      esp_err_t error)
{
    if (severity == BT_AUDIO_HEALTH_OK) return "RECOVERED";
#if !defined(ESP_ERR_NO_MEM) || ESP_ERR_NO_MEM != ESP_FAIL
    if (error == ESP_ERR_NO_MEM) return "NO_MEMORY";
#endif
    if (error == ESP_ERR_INVALID_ARG) return "INVALID_ARGUMENT";
    if (error == ESP_ERR_INVALID_STATE) return "INVALID_STATE";
#ifdef ESP_ERR_INVALID_SIZE
    if (error == ESP_ERR_INVALID_SIZE) return "INVALID_SIZE";
#endif
#ifdef ESP_ERR_NOT_FOUND
    if (error == ESP_ERR_NOT_FOUND) return "NOT_FOUND";
#endif
#ifdef ESP_ERR_NOT_SUPPORTED
    if (error == ESP_ERR_NOT_SUPPORTED) return "NOT_SUPPORTED";
#endif
#ifdef ESP_ERR_TIMEOUT
    if (error == ESP_ERR_TIMEOUT) return "TIMEOUT";
#endif
    if (error == ESP_FAIL) return "OPERATION_FAILED";
    if (error == ESP_OK) return "STATE_CHANGE";
    return "UNKNOWN_ERROR";
}

static esp_err_t canonical_peer(const char *peer_mac,
                                uint32_t session_generation,
                                char out[BT_DUPLEX_MAC_STR_LEN])
{
    if (peer_mac == NULL || peer_mac[0] == '\0') {
        if (session_generation != 0U) return ESP_ERR_INVALID_ARG;
        memcpy(out, "NONE", sizeof("NONE"));
        return ESP_OK;
    }
    if (strlen(peer_mac) != BT_DUPLEX_MAC_STR_LEN - 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0U; index < BT_DUPLEX_MAC_STR_LEN - 1U; ++index) {
        if ((index + 1U) % 3U == 0U) {
            if (peer_mac[index] != ':') return ESP_ERR_INVALID_ARG;
            out[index] = ':';
        } else {
            unsigned char value = (unsigned char)peer_mac[index];
            if (!isxdigit(value)) return ESP_ERR_INVALID_ARG;
            out[index] = (char)toupper(value);
        }
    }
    out[BT_DUPLEX_MAC_STR_LEN - 1U] = '\0';
    return ESP_OK;
}

static esp_err_t format_and_emit(const char *subtype,
                                 char *data,
                                 size_t data_size,
                                 const char *format,
                                 ...)
{
    if (subtype == NULL || data == NULL || data_size == 0U ||
        format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(data, data_size, format, args);
    va_end(args);
    if (written < 0) return ESP_FAIL;
    if ((size_t)written >= data_size) return ESP_ERR_INVALID_SIZE;

    cmd_status_t status = cmd_send_response(
        CMD_STATUS_EVENT, "HFP", subtype, data);
    if (status == CMD_SUCCESS) return ESP_OK;
    if (status == CMD_ERROR_INVALID_PARAM) return ESP_ERR_INVALID_ARG;
    if (status == CMD_ERROR_NOT_INITIALIZED) return ESP_ERR_INVALID_STATE;
    if (status == CMD_ERROR_TOO_MANY_PARAMS) return ESP_ERR_INVALID_SIZE;
    return ESP_FAIL;
}

esp_err_t bt_hfp_event_emit_profile(bt_hfp_profile_state_t state,
                                    const char *peer_mac,
                                    uint32_t session_generation)
{
    const char *state_text = profile_wire(state);
    char peer[BT_DUPLEX_MAC_STR_LEN];
    char data[BT_HFP_EVENT_DATA_LEN];
    if (state_text == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = canonical_peer(peer_mac, session_generation, peer);
    if (err != ESP_OK) return err;
    return format_and_emit("PROFILE", data, sizeof(data),
                           "%s|%s|%" PRIu32,
                           state_text, peer, session_generation);
}

esp_err_t bt_hfp_event_emit_audio(bt_hfp_audio_state_t state,
                                  bt_hfp_codec_t codec,
                                  uint32_t session_generation)
{
    const char *state_text = audio_wire(state);
    const char *codec_text = codec_wire(codec);
    char data[BT_HFP_EVENT_DATA_LEN];
    if (state_text == NULL || codec_text == NULL ||
        session_generation == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return format_and_emit("AUDIO", data, sizeof(data),
                           "%s|%s|%" PRIu32,
                           state_text, codec_text, session_generation);
}

esp_err_t bt_hfp_event_emit_mode(bt_duplex_mode_t old_mode,
                                 bt_duplex_mode_t new_mode,
                                 bt_hfp_mode_event_reason_t reason,
                                 uint32_t session_generation)
{
    const char *old_text = mode_wire(old_mode);
    const char *new_text = mode_wire(new_mode);
    const char *reason_text = mode_reason_wire(reason);
    char data[BT_HFP_EVENT_DATA_LEN];
    if (old_text == NULL || new_text == NULL || reason_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return format_and_emit("MODE", data, sizeof(data),
                           "%s|%s|%s|%" PRIu32,
                           old_text, new_text, reason_text,
                           session_generation);
}

esp_err_t bt_hfp_event_emit_i2s(bt_hfp_i2s_state_t state,
                                esp_err_t error,
                                uint32_t session_generation)
{
    const char *state_text = i2s_wire(state);
    char data[BT_HFP_EVENT_DATA_LEN];
    if (state_text == NULL || session_generation == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((state == BT_HFP_I2S_FAULTED ||
         state == BT_HFP_I2S_QUARANTINED) && error == ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state != BT_HFP_I2S_FAULTED &&
        state != BT_HFP_I2S_QUARANTINED && error != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return format_and_emit("I2S", data, sizeof(data),
                           "%s|%s|%" PRIu32,
                           state_text, error_wire(error),
                           session_generation);
}

esp_err_t bt_hfp_event_emit_health(bt_audio_health_t severity,
                                   esp_err_t reason_error,
                                   uint64_t count,
                                   uint32_t session_generation)
{
    const char *severity_text = health_wire(severity);
    const char *reason_text = health_reason_wire(severity, reason_error);
    char data[BT_HFP_EVENT_DATA_LEN];
    if (severity_text == NULL || count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return format_and_emit("HEALTH", data, sizeof(data),
                           "%s|%s|%" PRIu64 "|%" PRIu32,
                           severity_text, reason_text, count,
                           session_generation);
}

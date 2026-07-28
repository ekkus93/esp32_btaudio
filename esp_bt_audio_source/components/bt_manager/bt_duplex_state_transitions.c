#include "bt_duplex_state_internal.h"

static bool a2dp_profile_transition_ok(bt_a2dp_profile_state_t from,
                                       bt_a2dp_profile_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_A2DP_PROFILE_DISCONNECTED:
        return to == BT_A2DP_PROFILE_CONNECTING;
    case BT_A2DP_PROFILE_CONNECTING:
        return to == BT_A2DP_PROFILE_CONNECTED ||
               to == BT_A2DP_PROFILE_DISCONNECTED;
    case BT_A2DP_PROFILE_CONNECTED:
        return to == BT_A2DP_PROFILE_DISCONNECTING ||
               to == BT_A2DP_PROFILE_DISCONNECTED;
    case BT_A2DP_PROFILE_DISCONNECTING:
        return to == BT_A2DP_PROFILE_DISCONNECTED;
    default:
        return false;
    }
}

static bool a2dp_audio_transition_ok(bt_a2dp_audio_state_t from,
                                     bt_a2dp_audio_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_A2DP_AUDIO_STOPPED:
        return to == BT_A2DP_AUDIO_STARTED ||
               to == BT_A2DP_AUDIO_REMOTE_SUSPENDED;
    case BT_A2DP_AUDIO_STARTED:
        return to == BT_A2DP_AUDIO_STOPPED ||
               to == BT_A2DP_AUDIO_REMOTE_SUSPENDED;
    case BT_A2DP_AUDIO_REMOTE_SUSPENDED:
        return to == BT_A2DP_AUDIO_STARTED ||
               to == BT_A2DP_AUDIO_STOPPED;
    default:
        return false;
    }
}

static bool hfp_profile_transition_ok(bt_hfp_profile_state_t from,
                                      bt_hfp_profile_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_HFP_PROFILE_UNINITIALIZED:
        return to == BT_HFP_PROFILE_DISCONNECTED ||
               to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_DISCONNECTED:
        return to == BT_HFP_PROFILE_CONNECTING ||
               to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_CONNECTING:
        return to == BT_HFP_PROFILE_SLC_CONNECTED ||
               to == BT_HFP_PROFILE_DISCONNECTED ||
               to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_SLC_CONNECTED:
        return to == BT_HFP_PROFILE_DISCONNECTING ||
               to == BT_HFP_PROFILE_DISCONNECTED ||
               to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_DISCONNECTING:
        return to == BT_HFP_PROFILE_DISCONNECTED ||
               to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_FAULTED:
    default:
        return false;
    }
}

static bool hfp_audio_transition_ok(bt_hfp_audio_state_t from,
                                    bt_hfp_audio_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_HFP_AUDIO_DISCONNECTED:
        return to == BT_HFP_AUDIO_CONNECTING ||
               to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_CONNECTING:
        return to == BT_HFP_AUDIO_CONNECTED_CVSD ||
               to == BT_HFP_AUDIO_CONNECTED_MSBC ||
               to == BT_HFP_AUDIO_DISCONNECTING ||
               to == BT_HFP_AUDIO_DISCONNECTED ||
               to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_CONNECTED_CVSD:
    case BT_HFP_AUDIO_CONNECTED_MSBC:
        return to == BT_HFP_AUDIO_DISCONNECTING ||
               to == BT_HFP_AUDIO_DISCONNECTED ||
               to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_DISCONNECTING:
        return to == BT_HFP_AUDIO_DISCONNECTED ||
               to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_FAULTED:
    default:
        return false;
    }
}

static bool i2s_transition_ok(bt_hfp_i2s_state_t from,
                              bt_hfp_i2s_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_HFP_I2S_STOPPED:
        return to == BT_HFP_I2S_STARTING ||
               to == BT_HFP_I2S_FAULTED;
    case BT_HFP_I2S_STARTING:
        return to == BT_HFP_I2S_RUNNING ||
               to == BT_HFP_I2S_STOPPED ||
               to == BT_HFP_I2S_FAULTED ||
               to == BT_HFP_I2S_QUARANTINED;
    case BT_HFP_I2S_RUNNING:
        return to == BT_HFP_I2S_STOPPING ||
               to == BT_HFP_I2S_FAULTED;
    case BT_HFP_I2S_STOPPING:
        return to == BT_HFP_I2S_STOPPED ||
               to == BT_HFP_I2S_FAULTED ||
               to == BT_HFP_I2S_QUARANTINED;
    case BT_HFP_I2S_FAULTED:
        /* STOPPED is legal only after the local FD-08 stop API has proved that
         * all writer/channel resources were cleaned. Health remains faulted
         * until explicit higher-level recovery. */
        return to == BT_HFP_I2S_STOPPED ||
               to == BT_HFP_I2S_QUARANTINED;
    case BT_HFP_I2S_QUARANTINED:
    default:
        return false;
    }
}

esp_err_t bt_duplex_set_a2dp_profile_state(uint32_t generation,
                                           const char *peer_mac,
                                           bt_a2dp_profile_state_t state)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(state, BT_A2DP_PROFILE_STATE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        !a2dp_profile_transition_ok(
            g_bt_duplex_ctx.snapshot.a2dp_profile_state, state)) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.a2dp_profile_state = state;
    }
    return bt_duplex_unlock_result(err);
}

esp_err_t bt_duplex_set_a2dp_audio_state(uint32_t generation,
                                         const char *peer_mac,
                                         bt_a2dp_audio_state_t state)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(state, BT_A2DP_AUDIO_STATE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        !a2dp_audio_transition_ok(
            g_bt_duplex_ctx.snapshot.a2dp_audio_state, state)) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.a2dp_audio_state = state;
    }
    return bt_duplex_unlock_result(err);
}

esp_err_t bt_duplex_set_hfp_profile_state(uint32_t generation,
                                          const char *peer_mac,
                                          bt_hfp_profile_state_t state)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(state, BT_HFP_PROFILE_STATE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        !hfp_profile_transition_ok(
            g_bt_duplex_ctx.snapshot.hfp_profile_state, state)) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.hfp_profile_state = state;
    }
    return bt_duplex_unlock_result(err);
}

esp_err_t bt_duplex_set_hfp_audio_state(uint32_t generation,
                                        const char *peer_mac,
                                        bt_hfp_audio_state_t state)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(state, BT_HFP_AUDIO_STATE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        !hfp_audio_transition_ok(
            g_bt_duplex_ctx.snapshot.hfp_audio_state, state)) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.hfp_audio_state = state;
        if (state == BT_HFP_AUDIO_CONNECTED_CVSD) {
            g_bt_duplex_ctx.snapshot.codec = BT_HFP_CODEC_CVSD;
        } else if (state == BT_HFP_AUDIO_CONNECTED_MSBC) {
            g_bt_duplex_ctx.snapshot.codec = BT_HFP_CODEC_MSBC;
        } else if (state == BT_HFP_AUDIO_DISCONNECTED) {
            g_bt_duplex_ctx.snapshot.codec = BT_HFP_CODEC_NONE;
        }
    }
    return bt_duplex_unlock_result(err);
}

esp_err_t bt_duplex_set_i2s_state(uint32_t generation,
                                  const char *peer_mac,
                                  bt_hfp_i2s_state_t state)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(state, BT_HFP_I2S_STATE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        !i2s_transition_ok(g_bt_duplex_ctx.snapshot.i2s_state, state)) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.i2s_state = state;
    }
    return bt_duplex_unlock_result(err);
}

#include "bt_duplex_state.h"

#define STRING_CASE(value) case value: return #value

const char *bt_duplex_mode_to_string(bt_duplex_mode_t value)
{
    switch (value) {
    STRING_CASE(BT_DUPLEX_MODE_DISABLED);
    STRING_CASE(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC);
    STRING_CASE(BT_DUPLEX_MODE_HFP_FULL_DUPLEX);
    STRING_CASE(BT_DUPLEX_MODE_AUTO);
    default: return "BT_DUPLEX_MODE_UNKNOWN";
    }
}

const char *bt_a2dp_profile_state_to_string(bt_a2dp_profile_state_t value)
{
    switch (value) {
    STRING_CASE(BT_A2DP_PROFILE_DISCONNECTED);
    STRING_CASE(BT_A2DP_PROFILE_CONNECTING);
    STRING_CASE(BT_A2DP_PROFILE_CONNECTED);
    STRING_CASE(BT_A2DP_PROFILE_DISCONNECTING);
    default: return "BT_A2DP_PROFILE_UNKNOWN";
    }
}

const char *bt_a2dp_audio_state_to_string(bt_a2dp_audio_state_t value)
{
    switch (value) {
    STRING_CASE(BT_A2DP_AUDIO_STOPPED);
    STRING_CASE(BT_A2DP_AUDIO_STARTED);
    STRING_CASE(BT_A2DP_AUDIO_REMOTE_SUSPENDED);
    default: return "BT_A2DP_AUDIO_UNKNOWN";
    }
}

const char *bt_hfp_profile_state_to_string(bt_hfp_profile_state_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_PROFILE_UNINITIALIZED);
    STRING_CASE(BT_HFP_PROFILE_DISCONNECTED);
    STRING_CASE(BT_HFP_PROFILE_CONNECTING);
    STRING_CASE(BT_HFP_PROFILE_SLC_CONNECTED);
    STRING_CASE(BT_HFP_PROFILE_DISCONNECTING);
    STRING_CASE(BT_HFP_PROFILE_FAULTED);
    default: return "BT_HFP_PROFILE_UNKNOWN";
    }
}

const char *bt_hfp_audio_state_to_string(bt_hfp_audio_state_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_AUDIO_DISCONNECTED);
    STRING_CASE(BT_HFP_AUDIO_CONNECTING);
    STRING_CASE(BT_HFP_AUDIO_CONNECTED_CVSD);
    STRING_CASE(BT_HFP_AUDIO_CONNECTED_MSBC);
    STRING_CASE(BT_HFP_AUDIO_DISCONNECTING);
    STRING_CASE(BT_HFP_AUDIO_FAULTED);
    default: return "BT_HFP_AUDIO_UNKNOWN";
    }
}

const char *bt_hfp_codec_to_string(bt_hfp_codec_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_CODEC_NONE);
    STRING_CASE(BT_HFP_CODEC_CVSD);
    STRING_CASE(BT_HFP_CODEC_MSBC);
    default: return "BT_HFP_CODEC_UNKNOWN";
    }
}

const char *bt_hfp_i2s_state_to_string(bt_hfp_i2s_state_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_I2S_STOPPED);
    STRING_CASE(BT_HFP_I2S_STARTING);
    STRING_CASE(BT_HFP_I2S_RUNNING);
    STRING_CASE(BT_HFP_I2S_STOPPING);
    STRING_CASE(BT_HFP_I2S_FAULTED);
    STRING_CASE(BT_HFP_I2S_QUARANTINED);
    default: return "BT_HFP_I2S_UNKNOWN";
    }
}

const char *bt_audio_health_to_string(bt_audio_health_t value)
{
    switch (value) {
    STRING_CASE(BT_AUDIO_HEALTH_OK);
    STRING_CASE(BT_AUDIO_HEALTH_DEGRADED);
    STRING_CASE(BT_AUDIO_HEALTH_FAULTED);
    STRING_CASE(BT_AUDIO_HEALTH_QUARANTINED);
    default: return "BT_AUDIO_HEALTH_UNKNOWN";
    }
}

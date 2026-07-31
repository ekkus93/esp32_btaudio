#include "cmd_handlers_hfp_internal.h"

const char *wire_mode(bt_duplex_mode_t value)
{
    switch (value) {
    case BT_DUPLEX_MODE_DISABLED: return "DISABLED";
    case BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC: return "A2DP_MIC";
    case BT_DUPLEX_MODE_HFP_FULL_DUPLEX: return "HFP_FULL";
    case BT_DUPLEX_MODE_AUTO: return "AUTO";
    default: return "UNKNOWN";
    }
}

const char *wire_a2dp_profile(bt_a2dp_profile_state_t value)
{
    switch (value) {
    case BT_A2DP_PROFILE_DISCONNECTED: return "DISCONNECTED";
    case BT_A2DP_PROFILE_CONNECTING: return "CONNECTING";
    case BT_A2DP_PROFILE_CONNECTED: return "CONNECTED";
    case BT_A2DP_PROFILE_DISCONNECTING: return "DISCONNECTING";
    default: return "UNKNOWN";
    }
}

const char *wire_a2dp_audio(bt_a2dp_audio_state_t value)
{
    switch (value) {
    case BT_A2DP_AUDIO_STOPPED: return "STOPPED";
    case BT_A2DP_AUDIO_STARTED: return "STARTED";
    case BT_A2DP_AUDIO_REMOTE_SUSPENDED: return "REMOTE_SUSPENDED";
    default: return "UNKNOWN";
    }
}

const char *wire_hfp_profile(bt_hfp_profile_state_t value)
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

const char *wire_hfp_audio(bt_hfp_audio_state_t value)
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

const char *wire_codec(bt_hfp_codec_t value)
{
    switch (value) {
    case BT_HFP_CODEC_NONE: return "NONE";
    case BT_HFP_CODEC_CVSD: return "CVSD";
    case BT_HFP_CODEC_MSBC: return "MSBC";
    default: return "UNKNOWN";
    }
}

const char *wire_i2s(bt_hfp_i2s_state_t value)
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

const char *wire_health(bt_audio_health_t value)
{
    switch (value) {
    case BT_AUDIO_HEALTH_OK: return "OK";
    case BT_AUDIO_HEALTH_DEGRADED: return "DEGRADED";
    case BT_AUDIO_HEALTH_FAULTED: return "FAULTED";
    case BT_AUDIO_HEALTH_QUARANTINED: return "QUARANTINED";
    default: return "UNKNOWN";
    }
}

const char *wire_policy_state(bt_duplex_policy_state_t value)
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

const char *wire_policy_reason(bt_duplex_policy_reason_t value)
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

const char *wire_downlink_owner(bt_duplex_downlink_owner_t value)
{
    switch (value) {
    case BT_DUPLEX_DOWNLINK_OWNER_A2DP: return "A2DP";
    case BT_DUPLEX_DOWNLINK_OWNER_HFP: return "HFP";
    default: return "UNKNOWN";
    }
}

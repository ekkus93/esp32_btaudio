#include "unity.h"

#include "bt_duplex_state.h"

void test_all_state_strings_are_exhaustive(void)
{
    static const char *const duplex_modes[] = {
        "BT_DUPLEX_MODE_DISABLED",
        "BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC",
        "BT_DUPLEX_MODE_HFP_FULL_DUPLEX",
        "BT_DUPLEX_MODE_AUTO",
    };
    static const char *const a2dp_profiles[] = {
        "BT_A2DP_PROFILE_DISCONNECTED",
        "BT_A2DP_PROFILE_CONNECTING",
        "BT_A2DP_PROFILE_CONNECTED",
        "BT_A2DP_PROFILE_DISCONNECTING",
    };
    static const char *const a2dp_audio[] = {
        "BT_A2DP_AUDIO_STOPPED",
        "BT_A2DP_AUDIO_STARTED",
        "BT_A2DP_AUDIO_REMOTE_SUSPENDED",
    };
    static const char *const hfp_profiles[] = {
        "BT_HFP_PROFILE_UNINITIALIZED",
        "BT_HFP_PROFILE_DISCONNECTED",
        "BT_HFP_PROFILE_CONNECTING",
        "BT_HFP_PROFILE_SLC_CONNECTED",
        "BT_HFP_PROFILE_DISCONNECTING",
        "BT_HFP_PROFILE_FAULTED",
    };
    static const char *const hfp_audio[] = {
        "BT_HFP_AUDIO_DISCONNECTED",
        "BT_HFP_AUDIO_CONNECTING",
        "BT_HFP_AUDIO_CONNECTED_CVSD",
        "BT_HFP_AUDIO_CONNECTED_MSBC",
        "BT_HFP_AUDIO_DISCONNECTING",
        "BT_HFP_AUDIO_FAULTED",
    };
    static const char *const codecs[] = {
        "BT_HFP_CODEC_NONE",
        "BT_HFP_CODEC_CVSD",
        "BT_HFP_CODEC_MSBC",
    };
    static const char *const i2s_states[] = {
        "BT_HFP_I2S_STOPPED",
        "BT_HFP_I2S_STARTING",
        "BT_HFP_I2S_RUNNING",
        "BT_HFP_I2S_STOPPING",
        "BT_HFP_I2S_FAULTED",
        "BT_HFP_I2S_QUARANTINED",
    };
    static const char *const health_states[] = {
        "BT_AUDIO_HEALTH_OK",
        "BT_AUDIO_HEALTH_DEGRADED",
        "BT_AUDIO_HEALTH_FAULTED",
        "BT_AUDIO_HEALTH_QUARANTINED",
    };

    for (int i = 0; i < BT_DUPLEX_MODE_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(duplex_modes[i],
                                 bt_duplex_mode_to_string((bt_duplex_mode_t)i));
    }
    for (int i = 0; i < BT_A2DP_PROFILE_STATE_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(
            a2dp_profiles[i],
            bt_a2dp_profile_state_to_string((bt_a2dp_profile_state_t)i));
    }
    for (int i = 0; i < BT_A2DP_AUDIO_STATE_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(
            a2dp_audio[i],
            bt_a2dp_audio_state_to_string((bt_a2dp_audio_state_t)i));
    }
    for (int i = 0; i < BT_HFP_PROFILE_STATE_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(
            hfp_profiles[i],
            bt_hfp_profile_state_to_string((bt_hfp_profile_state_t)i));
    }
    for (int i = 0; i < BT_HFP_AUDIO_STATE_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(
            hfp_audio[i],
            bt_hfp_audio_state_to_string((bt_hfp_audio_state_t)i));
    }
    for (int i = 0; i < BT_HFP_CODEC_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(codecs[i],
                                 bt_hfp_codec_to_string((bt_hfp_codec_t)i));
    }
    for (int i = 0; i < BT_HFP_I2S_STATE_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(
            i2s_states[i],
            bt_hfp_i2s_state_to_string((bt_hfp_i2s_state_t)i));
    }
    for (int i = 0; i < BT_AUDIO_HEALTH_COUNT; ++i) {
        TEST_ASSERT_EQUAL_STRING(
            health_states[i],
            bt_audio_health_to_string((bt_audio_health_t)i));
    }

    TEST_ASSERT_EQUAL_STRING("BT_DUPLEX_MODE_UNKNOWN",
                             bt_duplex_mode_to_string(BT_DUPLEX_MODE_COUNT));
    TEST_ASSERT_EQUAL_STRING(
        "BT_A2DP_PROFILE_UNKNOWN",
        bt_a2dp_profile_state_to_string(BT_A2DP_PROFILE_STATE_COUNT));
    TEST_ASSERT_EQUAL_STRING(
        "BT_A2DP_AUDIO_UNKNOWN",
        bt_a2dp_audio_state_to_string(BT_A2DP_AUDIO_STATE_COUNT));
    TEST_ASSERT_EQUAL_STRING(
        "BT_HFP_PROFILE_UNKNOWN",
        bt_hfp_profile_state_to_string(BT_HFP_PROFILE_STATE_COUNT));
    TEST_ASSERT_EQUAL_STRING(
        "BT_HFP_AUDIO_UNKNOWN",
        bt_hfp_audio_state_to_string(BT_HFP_AUDIO_STATE_COUNT));
    TEST_ASSERT_EQUAL_STRING("BT_HFP_CODEC_UNKNOWN",
                             bt_hfp_codec_to_string(BT_HFP_CODEC_COUNT));
    TEST_ASSERT_EQUAL_STRING(
        "BT_HFP_I2S_UNKNOWN",
        bt_hfp_i2s_state_to_string(BT_HFP_I2S_STATE_COUNT));
    TEST_ASSERT_EQUAL_STRING(
        "BT_AUDIO_HEALTH_UNKNOWN",
        bt_audio_health_to_string(BT_AUDIO_HEALTH_COUNT));
}

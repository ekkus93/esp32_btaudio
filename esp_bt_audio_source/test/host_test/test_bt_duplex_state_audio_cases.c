#include "unity.h"

#include <string.h>

#include "bt_duplex_state.h"

#define PEER "AA:BB:CC:DD:EE:FF"

static uint32_t prepare_slc_session(void)
{
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &generation));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_a2dp_profile_state(
        generation, PEER, BT_A2DP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_a2dp_profile_state(
        generation, PEER, BT_A2DP_PROFILE_CONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_a2dp_audio_state(
        generation, PEER, BT_A2DP_AUDIO_STARTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_SLC_CONNECTED));
    return generation;
}

void test_audio_generation_rotation_preserves_session_state_and_telemetry(void)
{
    uint32_t first = prepare_slc_session();
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_record_incoming(first, PEER, 12U, true));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_health(
        first, PEER, BT_AUDIO_HEALTH_DEGRADED,
        ESP_ERR_TIMEOUT, "preserve this diagnostic"));

    uint32_t second = 0U;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_audio_session_begin(PEER, &second));
    TEST_ASSERT_NOT_EQUAL(first, second);

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_TRUE(snapshot.peer_valid);
    TEST_ASSERT_EQUAL_STRING(PEER, snapshot.peer_mac);
    TEST_ASSERT_EQUAL_UINT32(second, snapshot.session_generation);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_AUTO, snapshot.requested_mode);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_AUTO, snapshot.effective_mode);
    TEST_ASSERT_EQUAL(BT_A2DP_PROFILE_CONNECTED,
                      snapshot.a2dp_profile_state);
    TEST_ASSERT_EQUAL(BT_A2DP_AUDIO_STARTED, snapshot.a2dp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_SLC_CONNECTED,
                      snapshot.hfp_profile_state);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_DISCONNECTED,
                      snapshot.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, snapshot.i2s_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_DEGRADED, snapshot.health);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, snapshot.last_error);
    TEST_ASSERT_EQUAL_STRING("preserve this diagnostic",
                             snapshot.last_error_text);
    TEST_ASSERT_EQUAL_UINT64(1U, snapshot.counters.incoming_frames);
    TEST_ASSERT_EQUAL_UINT64(12U, snapshot.counters.incoming_bytes);
}

void test_audio_generation_rotation_rejects_invalid_transient_states(void)
{
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &generation));
    uint32_t next = 0U;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_duplex_audio_session_begin(PEER, &next));

    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_duplex_audio_session_begin(PEER, &next));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_DISCONNECTED));

    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_STARTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_RUNNING));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_duplex_audio_session_begin(PEER, &next));
}

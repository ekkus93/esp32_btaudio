#include "unity.h"

#include <pthread.h>
#include <string.h>

#include "bt_duplex_policy.h"

#define PEER "AA:BB:CC:DD:EE:FF"

void setUp(void)
{
    bt_duplex_state_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());
}

void tearDown(void)
{
    bt_duplex_state_deinit();
}

static uint32_t begin_session(bt_duplex_mode_t mode)
{
    uint32_t generation = 0;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(PEER, mode, &generation));
    TEST_ASSERT_NOT_EQUAL(0, generation);
    return generation;
}

void test_initial_state_is_deterministic(void)
{
    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_FALSE(snapshot.peer_valid);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.session_generation);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_DISABLED, snapshot.requested_mode);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_DISABLED, snapshot.effective_mode);
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_UNINITIALIZED, snapshot.hfp_profile_state);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_DISCONNECTED, snapshot.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_CODEC_NONE, snapshot.codec);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, snapshot.i2s_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_OK, snapshot.health);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_error);
}

void test_same_peer_is_case_insensitive_and_different_peer_is_rejected(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    uint32_t next = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_requested_mode(generation, "aa:bb:cc:dd:ee:ff",
                                     BT_DUPLEX_MODE_HFP_FULL_DUPLEX));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        bt_duplex_session_begin("11:22:33:44:55:66", BT_DUPLEX_MODE_AUTO, &next));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.wrong_peer_events);
    TEST_ASSERT_EQUAL_STRING(PEER, snapshot.peer_mac);
}

void test_hfp_profile_legal_and_illegal_transitions_are_explicit(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_SLC_CONNECTED, snapshot.hfp_profile_state);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.illegal_transitions);
}

void test_stale_generation_is_ignored_and_counted(void)
{
    uint32_t first = begin_session(BT_DUPLEX_MODE_AUTO);
    uint32_t second = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_session_begin("aa:bb:cc:dd:ee:ff", BT_DUPLEX_MODE_AUTO, &second));
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        bt_duplex_set_effective_mode(first, PEER,
                                     BT_DUPLEX_MODE_HFP_FULL_DUPLEX));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT32(second, snapshot.session_generation);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC, snapshot.effective_mode);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.stale_generation_events);
}

void test_codec_is_derived_from_confirmed_audio_state(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_CONNECTED_CVSD));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_CODEC_CVSD, snapshot.codec);

    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_DISCONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_DISCONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_CODEC_NONE, snapshot.codec);
}

void test_fault_cannot_be_quietly_downgraded_and_recovery_rotates_generation(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_health(generation, PEER, BT_AUDIO_HEALTH_FAULTED,
                             ESP_FAIL, "injected fault"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        bt_duplex_set_health(generation, PEER, BT_AUDIO_HEALTH_OK,
                             ESP_OK, ""));

    uint32_t recovered_generation = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_recover(generation, PEER, &recovered_generation));
    TEST_ASSERT_NOT_EQUAL(generation, recovered_generation);

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_OK, snapshot.health);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_error);
    TEST_ASSERT_EQUAL_STRING("", snapshot.last_error_text);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.recoveries);
}

void test_recovery_rejects_live_audio_or_i2s_resources(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    uint32_t next = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        bt_duplex_recover(generation, PEER, &next));
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER, BT_HFP_AUDIO_DISCONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_duplex_set_i2s_state(generation, PEER, BT_HFP_I2S_STARTING));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        bt_duplex_recover(generation, PEER, &next));
}

static void *counter_writer(void *arg)
{
    uint32_t generation = *(uint32_t *)arg;
    for (size_t i = 0; i < 10000; ++i) {
        bt_duplex_record_incoming(generation, PEER, 2, true);
    }
    return NULL;
}

void test_snapshot_never_exposes_torn_64_bit_counter_group(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    pthread_t writer;
    TEST_ASSERT_EQUAL(0, pthread_create(&writer, NULL, counter_writer, &generation));
    for (size_t i = 0; i < 2000; ++i) {
        bt_duplex_snapshot_t snapshot;
        TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
        TEST_ASSERT_EQUAL_UINT64(snapshot.counters.incoming_frames * 2U,
                                 snapshot.counters.incoming_bytes);
    }
    TEST_ASSERT_EQUAL(0, pthread_join(writer, NULL));
    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(10000, snapshot.counters.incoming_frames);
    TEST_ASSERT_EQUAL_UINT64(20000, snapshot.counters.incoming_bytes);
}

void test_strings_are_stable_and_invalid_values_are_visible(void)
{
    TEST_ASSERT_EQUAL_STRING("BT_DUPLEX_MODE_AUTO",
                             bt_duplex_mode_to_string(BT_DUPLEX_MODE_AUTO));
    TEST_ASSERT_EQUAL_STRING("BT_HFP_PROFILE_SLC_CONNECTED",
        bt_hfp_profile_state_to_string(BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("BT_HFP_AUDIO_CONNECTED_CVSD",
        bt_hfp_audio_state_to_string(BT_HFP_AUDIO_CONNECTED_CVSD));
    TEST_ASSERT_EQUAL_STRING("BT_HFP_CODEC_MSBC",
                             bt_hfp_codec_to_string(BT_HFP_CODEC_MSBC));
    TEST_ASSERT_EQUAL_STRING("BT_HFP_I2S_QUARANTINED",
        bt_hfp_i2s_state_to_string(BT_HFP_I2S_QUARANTINED));
    TEST_ASSERT_EQUAL_STRING("BT_AUDIO_HEALTH_FAULTED",
                             bt_audio_health_to_string(BT_AUDIO_HEALTH_FAULTED));
    TEST_ASSERT_EQUAL_STRING("BT_DUPLEX_MODE_UNKNOWN",
                             bt_duplex_mode_to_string(BT_DUPLEX_MODE_COUNT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_deterministic);
    RUN_TEST(test_same_peer_is_case_insensitive_and_different_peer_is_rejected);
    RUN_TEST(test_hfp_profile_legal_and_illegal_transitions_are_explicit);
    RUN_TEST(test_stale_generation_is_ignored_and_counted);
    RUN_TEST(test_codec_is_derived_from_confirmed_audio_state);
    RUN_TEST(test_fault_cannot_be_quietly_downgraded_and_recovery_rotates_generation);
    RUN_TEST(test_recovery_rejects_live_audio_or_i2s_resources);
    RUN_TEST(test_snapshot_never_exposes_torn_64_bit_counter_group);
    RUN_TEST(test_strings_are_stable_and_invalid_values_are_visible);
    return UNITY_END();
}

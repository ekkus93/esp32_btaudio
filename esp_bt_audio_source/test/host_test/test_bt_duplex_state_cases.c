#include "unity.h"

#include <pthread.h>
#include <string.h>

#include "bt_duplex_state.h"

#define PEER "AA:BB:CC:DD:EE:FF"

void setUp(void)
{
    bt_duplex_state_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());
    bt_duplex_test_reset();
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
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_requested_mode(generation, "aa:bb:cc:dd:ee:ff",
                                     BT_DUPLEX_MODE_HFP_FULL_DUPLEX));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_session_begin("11:22:33:44:55:66", BT_DUPLEX_MODE_AUTO,
                                &next));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.wrong_peer_events);
}

void test_hfp_profile_legal_and_illegal_transitions_are_explicit(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_SLC_CONNECTED, snapshot.hfp_profile_state);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.illegal_transitions);
}

void test_same_peer_restart_is_rejected_while_transient_resources_are_live(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_i2s_state(generation, PEER, BT_HFP_I2S_STARTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_i2s_state(generation, PEER, BT_HFP_I2S_RUNNING));

    uint32_t next = 0;
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_session_begin(PEER, BT_DUPLEX_MODE_AUTO, &next));
    TEST_ASSERT_EQUAL_UINT32(0, next);

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT32(generation, snapshot.session_generation);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_RUNNING, snapshot.i2s_state);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.illegal_transitions);
}

void test_stale_generation_is_ignored_and_counted(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_set_hfp_profile_state(generation + 1U, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_CONNECTING, snapshot.hfp_profile_state);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.stale_generation_events);
}

void test_codec_is_derived_from_confirmed_audio_state(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_CONNECTED_CVSD));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_CODEC_CVSD, snapshot.codec);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_DISCONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_DISCONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_CODEC_NONE, snapshot.codec);
}

void test_fault_cannot_be_quietly_downgraded_and_recovery_rotates_generation(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_health(generation, PEER, BT_AUDIO_HEALTH_FAULTED,
                             ESP_FAIL, "forced fault"));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_set_health(generation, PEER, BT_AUDIO_HEALTH_OK,
                             ESP_OK, NULL));

    uint32_t recovered_generation = 0;
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_recover(generation, PEER, &recovered_generation));
    TEST_ASSERT_NOT_EQUAL(generation, recovered_generation);

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_OK, snapshot.health);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.counters.recoveries);
}

void test_recovery_rejects_healthy_or_live_resources(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    uint32_t next_generation = 0;
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_recover(generation, PEER, &next_generation));

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_profile_state(generation, PEER,
                                        BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_hfp_audio_state(generation, PEER,
                                      BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_health(generation, PEER, BT_AUDIO_HEALTH_FAULTED,
                             ESP_FAIL, "live fault"));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_recover(generation, PEER, &next_generation));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_FAULTED, snapshot.health);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_CONNECTING, snapshot.hfp_audio_state);
}

typedef struct {
    uint32_t generation;
    bool failed;
} concurrency_context_t;

static void *counter_writer(void *arg)
{
    concurrency_context_t *context = (concurrency_context_t *)arg;
    for (size_t index = 0; index < 20000U; ++index) {
        if (bt_duplex_record_incoming(context->generation, PEER, 17U, true) !=
            ESP_OK) {
            context->failed = true;
            break;
        }
    }
    return NULL;
}

static void *snapshot_reader(void *arg)
{
    concurrency_context_t *context = (concurrency_context_t *)arg;
    for (size_t index = 0; index < 20000U; ++index) {
        bt_duplex_snapshot_t snapshot;
        if (bt_duplex_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.counters.incoming_bytes !=
                snapshot.counters.incoming_frames * 17U) {
            context->failed = true;
            break;
        }
    }
    return NULL;
}

void test_snapshot_never_exposes_torn_64_bit_counter_group(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    concurrency_context_t context = {
        .generation = generation,
        .failed = false,
    };
    pthread_t writer;
    pthread_t reader;
    TEST_ASSERT_EQUAL(0, pthread_create(&writer, NULL, counter_writer, &context));
    TEST_ASSERT_EQUAL(0, pthread_create(&reader, NULL, snapshot_reader, &context));
    TEST_ASSERT_EQUAL(0, pthread_join(writer, NULL));
    TEST_ASSERT_EQUAL(0, pthread_join(reader, NULL));
    TEST_ASSERT_FALSE(context.failed);
}

void test_negative_enum_values_are_rejected(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_duplex_set_requested_mode(generation, PEER,
                                     (bt_duplex_mode_t)-1));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_duplex_set_a2dp_profile_state(generation, PEER,
                                         (bt_a2dp_profile_state_t)-1));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_duplex_set_health(generation, PEER,
                             (bt_audio_health_t)-1, ESP_FAIL, "negative"));
}

void test_a2dp_transitions_are_checked(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_set_a2dp_profile_state(generation, PEER,
                                         BT_A2DP_PROFILE_CONNECTED));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_a2dp_profile_state(generation, PEER,
                                         BT_A2DP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_a2dp_profile_state(generation, PEER,
                                         BT_A2DP_PROFILE_CONNECTED));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_duplex_set_a2dp_audio_state(generation, PEER,
                                       BT_A2DP_AUDIO_STARTED));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_duplex_set_a2dp_profile_state(generation, PEER,
                                         BT_A2DP_PROFILE_DISCONNECTED));
}

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bt_hfp_audio.h"
#include "hfp_i2s_output.h"

#define PEER "AA:BB:CC:DD:EE:FF"
#define OTHER_PEER "11:22:33:44:55:66"
#define GENERATION 41U
#define SYNC_HANDLE 0x1234U

void mock_hfp_audio_i2s_reset(void);
void mock_hfp_audio_i2s_set_accept(bool accept);
void mock_hfp_audio_i2s_set_expected_generation(uint32_t generation);
unsigned mock_hfp_audio_i2s_calls(void);
uint32_t mock_hfp_audio_i2s_last_generation(void);
size_t mock_hfp_audio_i2s_last_samples(void);
const int16_t *mock_hfp_audio_i2s_last_pcm(void);

static esp_err_t s_register_result;
static unsigned s_register_calls;
static bool s_reenter_registration;
static esp_err_t s_nested_registration_result;
static int64_t s_now_values[16];
static size_t s_now_count;
static size_t s_now_index;

static esp_err_t mock_register_callback(void)
{
    s_register_calls++;
    if (s_reenter_registration) {
        s_reenter_registration = false;
        s_nested_registration_result = bt_hfp_audio_register_callback();
    }
    return s_register_result;
}

static int64_t mock_now_us(void)
{
    if (s_now_index < s_now_count) return s_now_values[s_now_index++];
    return (int64_t)(s_now_index++ * 100U);
}

static void install_ops(void)
{
    const bt_hfp_audio_platform_ops_t ops = {
        .register_callback = mock_register_callback,
        .now_us = mock_now_us,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_test_set_platform_ops(&ops));
}

static bt_duplex_snapshot_t ready_snapshot(void)
{
    bt_duplex_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.peer_valid = true;
    memcpy(snapshot.peer_mac, PEER, sizeof(PEER));
    snapshot.session_generation = GENERATION;
    snapshot.hfp_profile_state = BT_HFP_PROFILE_SLC_CONNECTED;
    snapshot.hfp_audio_state = BT_HFP_AUDIO_CONNECTED_CVSD;
    snapshot.codec = BT_HFP_CODEC_CVSD;
    snapshot.i2s_state = BT_HFP_I2S_RUNNING;
    return snapshot;
}

static void register_and_activate(void)
{
    bt_duplex_snapshot_t snapshot = ready_snapshot();
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_apply_duplex_state(
        &snapshot, PEER, SYNC_HANDLE, 120U));
    mock_hfp_audio_i2s_set_expected_generation(GENERATION);
}

void setUp(void)
{
    bt_hfp_audio_test_reset();
    mock_hfp_audio_i2s_reset();
    s_register_result = ESP_OK;
    s_register_calls = 0U;
    s_reenter_registration = false;
    s_nested_registration_result = ESP_OK;
    memset(s_now_values, 0, sizeof(s_now_values));
    s_now_values[0] = 1000;
    s_now_values[1] = 1100;
    s_now_count = 2U;
    s_now_index = 0U;
    install_ops();
}

void tearDown(void)
{
    bt_hfp_audio_test_reset();
}

void test_callback_registration_success_and_failure_are_visible(void)
{
    bt_hfp_audio_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    TEST_ASSERT_EQUAL_UINT(1, s_register_calls);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_TRUE(snapshot.callback_registered);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.registration_failures);

    bt_hfp_audio_test_reset();
    s_register_result = ESP_FAIL;
    s_register_calls = 0U;
    install_ops();
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_register_callback());
    TEST_ASSERT_EQUAL_UINT(1, s_register_calls);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_FALSE(snapshot.callback_registered);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.registration_failures);
    TEST_ASSERT_EQUAL(ESP_FAIL, snapshot.last_error);
}

void test_callback_registration_reentry_is_rejected_until_success_is_published(void)
{
    bt_hfp_audio_snapshot_t snapshot;
    s_reenter_registration = true;

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, s_nested_registration_result);
    TEST_ASSERT_EQUAL_UINT(1U, s_register_calls);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_TRUE(snapshot.callback_registered);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_error);
    TEST_ASSERT_EQUAL_UINT64(0U, snapshot.registration_failures);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    TEST_ASSERT_EQUAL_UINT(1U, s_register_calls);
}

void test_activation_requires_same_peer_slc_cvsd_and_running_i2s(void)
{
    bt_hfp_audio_snapshot_t audio;
    bt_duplex_snapshot_t snapshot = ready_snapshot();
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_hfp_audio_apply_duplex_state(
                          &snapshot, OTHER_PEER, SYNC_HANDLE, 120U));
    snapshot.hfp_profile_state = BT_HFP_PROFILE_CONNECTING;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_hfp_audio_apply_duplex_state(
                          &snapshot, PEER, SYNC_HANDLE, 120U));
    snapshot = ready_snapshot();
    snapshot.i2s_state = BT_HFP_I2S_STOPPED;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_hfp_audio_apply_duplex_state(
                          &snapshot, PEER, SYNC_HANDLE, 120U));
    snapshot = ready_snapshot();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_hfp_audio_apply_duplex_state(
                          &snapshot, PEER,
                          BT_HFP_AUDIO_INVALID_SYNC_HANDLE, 120U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&audio));
    TEST_ASSERT_FALSE(audio.accepting_incoming);
    TEST_ASSERT_EQUAL_UINT64(4, audio.activation_failures);
}

void test_wrong_peer_event_does_not_disable_active_session(void)
{
    const int16_t pcm[] = {1, 2};
    bt_hfp_audio_snapshot_t audio;
    bt_duplex_snapshot_t snapshot = ready_snapshot();
    register_and_activate();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_hfp_audio_apply_duplex_state(
                          &snapshot, OTHER_PEER, SYNC_HANDLE + 1U, 60U));
    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, (const uint8_t *)pcm, sizeof(pcm), sizeof(pcm), false);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&audio));
    TEST_ASSERT_TRUE(audio.accepting_incoming);
    TEST_ASSERT_EQUAL_UINT32(GENERATION, audio.generation);
    TEST_ASSERT_EQUAL_UINT32(SYNC_HANDLE, audio.sync_conn_handle);
    TEST_ASSERT_EQUAL_UINT64(1, audio.accepted_frames);
    TEST_ASSERT_EQUAL_UINT(1, mock_hfp_audio_i2s_calls());
}

void test_null_zero_odd_oversize_and_capacity_mismatch_are_rejected(void)
{
    uint8_t data[HFP_I2S_CVSD_MAX_INPUT_SAMPLES * sizeof(int16_t) + 2U] = {0};
    bt_hfp_audio_snapshot_t snapshot;
    register_and_activate();

    bt_hfp_audio_test_handle_incoming(SYNC_HANDLE, NULL, 2U, 2U, false);
    bt_hfp_audio_test_handle_incoming(SYNC_HANDLE, data, 0U, sizeof(data), false);
    bt_hfp_audio_test_handle_incoming(SYNC_HANDLE, data, 3U, sizeof(data), false);
    bt_hfp_audio_test_handle_incoming(SYNC_HANDLE, data, sizeof(data),
                                      sizeof(data), false);
    bt_hfp_audio_test_handle_incoming(SYNC_HANDLE, data, 4U, 2U, false);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(5, snapshot.incoming_callbacks);
    TEST_ASSERT_EQUAL_UINT64(5, snapshot.invalid_frames);
    TEST_ASSERT_EQUAL_UINT64(5, snapshot.dropped_frames);
    TEST_ASSERT_EQUAL_UINT(0, mock_hfp_audio_i2s_calls());
}

void test_inactive_and_deactivated_callbacks_are_rejected(void)
{
    const int16_t pcm[] = {1, 2};
    bt_hfp_audio_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, (const uint8_t *)pcm, sizeof(pcm), sizeof(pcm), false);

    bt_duplex_snapshot_t duplex = ready_snapshot();
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_apply_duplex_state(
        &duplex, PEER, SYNC_HANDLE, 120U));
    bt_hfp_audio_profile_stopping();
    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, (const uint8_t *)pcm, sizeof(pcm), sizeof(pcm), false);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.inactive_frames);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.dropped_frames);
    TEST_ASSERT_FALSE(snapshot.accepting_incoming);
}

void test_wrong_sync_handle_is_rejected(void)
{
    const int16_t pcm[] = {1, 2, 3};
    bt_hfp_audio_snapshot_t snapshot;
    register_and_activate();
    bt_hfp_audio_test_handle_incoming(
        (uint16_t)(SYNC_HANDLE + 1U), (const uint8_t *)pcm,
        sizeof(pcm), sizeof(pcm), false);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.stale_handle_frames);
    TEST_ASSERT_EQUAL_UINT64(sizeof(pcm), snapshot.stale_handle_bytes);
    TEST_ASSERT_EQUAL_UINT(0, mock_hfp_audio_i2s_calls());
}

void test_bad_frame_is_rejected(void)
{
    const int16_t pcm[] = {1, 2, 3};
    bt_hfp_audio_snapshot_t snapshot;
    register_and_activate();
    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, (const uint8_t *)pcm, sizeof(pcm), sizeof(pcm), true);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.bad_frames);
    TEST_ASSERT_EQUAL_UINT64(sizeof(pcm), snapshot.bad_bytes);
    TEST_ASSERT_EQUAL_UINT(0, mock_hfp_audio_i2s_calls());
}

void test_unexpected_msbc_is_rejected_visibly(void)
{
    bt_hfp_audio_snapshot_t audio;
    bt_duplex_snapshot_t snapshot = ready_snapshot();
    snapshot.hfp_audio_state = BT_HFP_AUDIO_CONNECTED_MSBC;
    snapshot.codec = BT_HFP_CODEC_MSBC;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED,
                      bt_hfp_audio_apply_duplex_state(
                          &snapshot, PEER, SYNC_HANDLE, 120U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&audio));
    TEST_ASSERT_FALSE(audio.accepting_incoming);
    TEST_ASSERT_EQUAL_UINT64(1, audio.activation_failures);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, audio.last_error);
}

void test_valid_unaligned_cvsd_frame_is_copied_whole(void)
{
    const int16_t expected[] = {1, -2, 32767, -32768};
    uint8_t raw[sizeof(expected) + 1U];
    bt_hfp_audio_snapshot_t snapshot;
    memcpy(raw + 1U, expected, sizeof(expected));
    register_and_activate();

    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, raw + 1U, sizeof(expected), sizeof(expected), false);

    TEST_ASSERT_EQUAL_UINT(1, mock_hfp_audio_i2s_calls());
    TEST_ASSERT_EQUAL_UINT32(GENERATION,
                             mock_hfp_audio_i2s_last_generation());
    TEST_ASSERT_EQUAL_UINT(sizeof(expected) / sizeof(expected[0]),
                           mock_hfp_audio_i2s_last_samples());
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, mock_hfp_audio_i2s_last_pcm(),
                                  sizeof(expected) / sizeof(expected[0]));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.accepted_frames);
    TEST_ASSERT_EQUAL_UINT64(sizeof(expected), snapshot.accepted_bytes);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.dropped_frames);
}

void test_ring_rejection_counts_exact_frame_and_bytes(void)
{
    const int16_t pcm[] = {1, 2, 3, 4};
    bt_hfp_audio_snapshot_t snapshot;
    register_and_activate();
    mock_hfp_audio_i2s_set_accept(false);
    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, (const uint8_t *)pcm, sizeof(pcm), sizeof(pcm), false);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.ring_rejected_frames);
    TEST_ASSERT_EQUAL_UINT64(sizeof(pcm), snapshot.ring_rejected_bytes);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.dropped_frames);
    TEST_ASSERT_EQUAL_UINT64(sizeof(pcm), snapshot.dropped_bytes);
}

void test_stale_generation_is_rejected_by_generation_bound_i2s(void)
{
    const int16_t pcm[] = {1, 2, 3, 4};
    bt_hfp_audio_snapshot_t snapshot;
    register_and_activate();
    mock_hfp_audio_i2s_set_expected_generation(GENERATION + 1U);
    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, (const uint8_t *)pcm, sizeof(pcm), sizeof(pcm), false);
    TEST_ASSERT_EQUAL_UINT32(GENERATION,
                             mock_hfp_audio_i2s_last_generation());
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.ring_rejected_frames);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.accepted_frames);
}

void test_callback_timing_tracks_last_max_and_budget(void)
{
    const int16_t pcm[] = {1, 2};
    bt_hfp_audio_snapshot_t snapshot;
    register_and_activate();
    s_now_values[0] = 100U;
    s_now_values[1] = 100U + BT_HFP_AUDIO_CALLBACK_BUDGET_US + 1U;
    s_now_count = 2U;
    s_now_index = 0U;
    bt_hfp_audio_test_handle_incoming(
        SYNC_HANDLE, (const uint8_t *)pcm, sizeof(pcm), sizeof(pcm), false);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT32(BT_HFP_AUDIO_CALLBACK_BUDGET_US + 1U,
                             snapshot.callback_last_us);
    TEST_ASSERT_EQUAL_UINT32(BT_HFP_AUDIO_CALLBACK_BUDGET_US + 1U,
                             snapshot.callback_max_us);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.callback_over_budget);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.active_callbacks);
}

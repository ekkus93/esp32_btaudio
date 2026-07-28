#include "unity.h"

#include <string.h>

#include "bt_duplex_state.h"
#include "bt_hfp_manager.h"
#include "bt_hfp_manager_dependencies.h"

#define PEER "AA:BB:CC:DD:EE:FF"
#define OTHER "11:22:33:44:55:66"

static uint32_t begin_session(bt_duplex_mode_t mode)
{
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_session_begin(PEER, mode, &generation));
    return generation;
}

void setUp(void)
{
    bt_duplex_state_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());
    TEST_ASSERT_EQUAL(ESP_OK,
                      mock_bt_hfp_manager_dependencies_init(PEER));
    bt_manager_hfp_test_reset_diagnostics();
}

void tearDown(void)
{
    mock_bt_hfp_manager_dependencies_deinit();
    bt_duplex_state_deinit();
}

void test_manager_status_returns_configured_mode_and_one_duplex_snapshot(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_CONNECTING));

    bt_hfp_manager_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_status(&status));
    TEST_ASSERT_TRUE(status.manager_initialized);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_AUTO, status.configured_mode);
    TEST_ASSERT_EQUAL_UINT32(generation, status.duplex.session_generation);
    TEST_ASSERT_EQUAL_STRING(PEER, status.duplex.peer_mac);
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_CONNECTING,
                      status.duplex.hfp_profile_state);
}

void test_manager_mode_updates_atomically_and_rejects_live_audio(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_set_mode(
        BT_DUPLEX_MODE_HFP_FULL_DUPLEX));

    bt_hfp_manager_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_status(&status));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
                      status.configured_mode);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
                      status.duplex.requested_mode);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
                      status.duplex.effective_mode);

    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_manager_hfp_set_mode(
        BT_DUPLEX_MODE_DISABLED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_status(&status));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
                      status.configured_mode);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
                      status.duplex.requested_mode);
}

void test_manager_connect_carries_configured_mode_into_new_session(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_set_mode(
        BT_DUPLEX_MODE_HFP_FULL_DUPLEX));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_connect(PEER));
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_slc_connect_calls());
    TEST_ASSERT_EQUAL_STRING(PEER, mock_bt_hfp_manager_last_slc_peer());

    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_TRUE(duplex.peer_valid);
    TEST_ASSERT_EQUAL_STRING(PEER, duplex.peer_mac);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
                      duplex.requested_mode);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
                      duplex.effective_mode);
    TEST_ASSERT_EQUAL(BT_A2DP_PROFILE_CONNECTED,
                      duplex.a2dp_profile_state);
}

void test_manager_connect_rejects_non_active_peer_before_lower_request(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_connect(OTHER));
    TEST_ASSERT_EQUAL_UINT(0U, mock_bt_hfp_manager_slc_connect_calls());
}

void test_manager_connect_rejects_malformed_mac_exactly(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_manager_hfp_connect(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_manager_hfp_connect("AA-BB-CC-DD-EE-FF"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_manager_hfp_connect("GG:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_manager_hfp_connect("AA:BB:CC:DD:EE"));
    TEST_ASSERT_EQUAL_UINT(0U, mock_bt_hfp_manager_slc_connect_calls());
}

void test_manager_wrappers_preserve_exact_lower_errors(void)
{
    mock_bt_hfp_manager_set_slc_connect_result(ESP_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_manager_hfp_connect(PEER));
    mock_bt_hfp_manager_set_slc_disconnect_result(ESP_ERR_INVALID_STATE);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_manager_hfp_disconnect());
    mock_bt_hfp_manager_set_audio_start_result(ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, bt_manager_hfp_audio_start());
    mock_bt_hfp_manager_set_audio_stop_result(ESP_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_manager_hfp_audio_stop());
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_slc_disconnect_calls());
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_audio_start_calls());
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_audio_stop_calls());
}

static void install_absolute_module_stats(uint64_t base)
{
    bt_hfp_connection_snapshot_t slc;
    memset(&slc, 0, sizeof(slc));
    slc.accepted_connect_requests = base + 1U;
    slc.accepted_disconnect_requests = base + 2U;
    mock_bt_hfp_manager_set_slc_snapshot(&slc, ESP_OK);

    bt_hfp_audio_control_snapshot_t control;
    memset(&control, 0, sizeof(control));
    control.start_calls = base + 3U;
    control.stop_calls = base + 4U;
    control.successful_starts = base + 5U;
    mock_bt_hfp_manager_set_audio_control_snapshot(&control, ESP_OK);

    bt_hfp_audio_snapshot_t incoming;
    memset(&incoming, 0, sizeof(incoming));
    incoming.incoming_callbacks = base + 6U;
    incoming.accepted_frames = base + 7U;
    incoming.accepted_bytes = base + 8U;
    incoming.callback_last_us = 11U;
    incoming.callback_max_us = 99U;
    mock_bt_hfp_manager_set_incoming_snapshot(&incoming, ESP_OK);

    hfp_i2s_output_snapshot_t i2s;
    memset(&i2s, 0, sizeof(i2s));
    i2s.initialized = true;
    i2s.state = HFP_I2S_OUTPUT_STOPPED;
    i2s.start_calls = base + 9U;
    i2s.write_lost_bytes = base + 10U;
    i2s.ring.total_written_bytes = base + 11U;
    i2s.ring.current_used = 12U;
    i2s.ring.peak_used = 123U;
    mock_bt_hfp_manager_set_i2s_snapshot(&i2s, ESP_OK);
}

void test_manager_stats_reset_uses_non_destructive_baseline(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_record_incoming(generation, PEER, 100U, true));
    install_absolute_module_stats(100U);

    bt_hfp_connection_snapshot_t slc_before;
    bt_hfp_audio_snapshot_t incoming_before;
    hfp_i2s_output_snapshot_t i2s_before;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&slc_before));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&incoming_before));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&i2s_before));

    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_reset_stats());

    bt_hfp_connection_snapshot_t slc_after;
    bt_hfp_audio_snapshot_t incoming_after;
    hfp_i2s_output_snapshot_t i2s_after;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&slc_after));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&incoming_after));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&i2s_after));
    TEST_ASSERT_EQUAL_MEMORY(&slc_before, &slc_after, sizeof(slc_before));
    TEST_ASSERT_EQUAL_MEMORY(&incoming_before, &incoming_after,
                             sizeof(incoming_before));
    TEST_ASSERT_EQUAL_MEMORY(&i2s_before, &i2s_after, sizeof(i2s_before));

    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_record_incoming(generation, PEER, 20U, true));
    install_absolute_module_stats(110U);

    bt_hfp_manager_stats_t stats;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT64(1U, stats.reset_sequence);
    TEST_ASSERT_EQUAL_UINT64(1U, stats.duplex.incoming_frames);
    TEST_ASSERT_EQUAL_UINT64(20U, stats.duplex.incoming_bytes);
    TEST_ASSERT_EQUAL_UINT64(10U,
        stats.slc.accepted_connect_requests);
    TEST_ASSERT_EQUAL_UINT64(10U, stats.audio_control.start_calls);
    TEST_ASSERT_EQUAL_UINT64(10U, stats.incoming.incoming_callbacks);
    TEST_ASSERT_EQUAL_UINT64(10U, stats.i2s.start_calls);
    TEST_ASSERT_EQUAL_UINT32(11U, stats.incoming.callback_last_us);
    TEST_ASSERT_EQUAL_UINT32(99U,
                             stats.incoming.callback_max_us_lifetime);
    TEST_ASSERT_EQUAL_UINT(12U, stats.i2s.ring_current_used);
    TEST_ASSERT_EQUAL_UINT(123U, stats.i2s.ring_peak_used_lifetime);
}

void test_manager_resetstats_rejects_live_slc_audio_callback_and_i2s(void)
{
    bt_hfp_connection_snapshot_t slc;
    memset(&slc, 0, sizeof(slc));
    slc.pending = true;
    mock_bt_hfp_manager_set_slc_snapshot(&slc, ESP_OK);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_reset_stats());

    mock_bt_hfp_manager_dependencies_reset_modules();
    bt_hfp_audio_control_snapshot_t control;
    memset(&control, 0, sizeof(control));
    control.api_active = true;
    mock_bt_hfp_manager_set_audio_control_snapshot(&control, ESP_OK);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_reset_stats());

    mock_bt_hfp_manager_dependencies_reset_modules();
    bt_hfp_audio_snapshot_t incoming;
    memset(&incoming, 0, sizeof(incoming));
    incoming.active_callbacks = 1U;
    mock_bt_hfp_manager_set_incoming_snapshot(&incoming, ESP_OK);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_reset_stats());

    mock_bt_hfp_manager_dependencies_reset_modules();
    hfp_i2s_output_snapshot_t i2s;
    memset(&i2s, 0, sizeof(i2s));
    i2s.initialized = true;
    i2s.state = HFP_I2S_OUTPUT_RUNNING;
    mock_bt_hfp_manager_set_i2s_snapshot(&i2s, ESP_OK);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_reset_stats());
}

void test_manager_resetstats_rejects_authoritative_streaming_state(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_reset_stats());
}

void test_manager_stats_detects_regressed_lifetime_source(void)
{
    install_absolute_module_stats(100U);
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_reset_stats());
    install_absolute_module_stats(10U);

    bt_hfp_manager_stats_t stats;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_get_stats(&stats));
}

void test_manager_stats_detects_regressed_lifetime_maximum(void)
{
    bt_hfp_audio_snapshot_t incoming;
    memset(&incoming, 0, sizeof(incoming));
    incoming.callback_max_us = 100U;
    mock_bt_hfp_manager_set_incoming_snapshot(&incoming, ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_reset_stats());

    incoming.callback_max_us = 99U;
    mock_bt_hfp_manager_set_incoming_snapshot(&incoming, ESP_OK);
    bt_hfp_manager_stats_t stats;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_get_stats(&stats));
}

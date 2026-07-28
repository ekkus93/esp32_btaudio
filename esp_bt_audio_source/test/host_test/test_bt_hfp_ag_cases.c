#include "unity.h"

#include <string.h>

#include "bt_duplex_state.h"
#include "bt_hfp_ag.h"

#define PEER "AA:BB:CC:DD:EE:FF"
#define OTHER_PEER "11:22:33:44:55:66"

typedef enum {
    MOCK_EVENT_SUCCESS = 0,
    MOCK_EVENT_NONE,
    MOCK_EVENT_FAILURE,
    MOCK_IMMEDIATE_FAILURE,
} mock_result_mode_t;

static mock_result_mode_t s_init_mode;
static mock_result_mode_t s_deinit_mode;
static esp_err_t s_register_result;
static esp_err_t s_unknown_at_result;
static unsigned s_register_calls;
static unsigned s_init_calls;
static unsigned s_deinit_calls;
static unsigned s_unknown_at_calls;

static esp_err_t mock_register_callback(void)
{
    s_register_calls++;
    return s_register_result;
}

static esp_err_t mock_profile_init(void)
{
    s_init_calls++;
    if (s_init_mode == MOCK_IMMEDIATE_FAILURE) return ESP_FAIL;
    if (s_init_mode == MOCK_EVENT_SUCCESS) {
        bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_INIT_SUCCESS);
    } else if (s_init_mode == MOCK_EVENT_FAILURE) {
        bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_INIT_FAILED);
    }
    return ESP_OK;
}

static esp_err_t mock_profile_deinit(void)
{
    s_deinit_calls++;
    if (s_deinit_mode == MOCK_IMMEDIATE_FAILURE) return ESP_FAIL;
    if (s_deinit_mode == MOCK_EVENT_SUCCESS) {
        bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_DEINIT_SUCCESS);
    } else if (s_deinit_mode == MOCK_EVENT_FAILURE) {
        bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_DEINIT_FAILED);
    }
    return ESP_OK;
}

static esp_err_t mock_unknown_at_error(const char *peer_mac)
{
    TEST_ASSERT_EQUAL_STRING(OTHER_PEER, peer_mac);
    s_unknown_at_calls++;
    return s_unknown_at_result;
}

void setUp(void)
{
    bt_hfp_ag_force_cleanup_after_stack_shutdown();
    bt_duplex_state_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());

    s_init_mode = MOCK_EVENT_SUCCESS;
    s_deinit_mode = MOCK_EVENT_SUCCESS;
    s_register_result = ESP_OK;
    s_unknown_at_result = ESP_OK;
    s_register_calls = 0;
    s_init_calls = 0;
    s_deinit_calls = 0;
    s_unknown_at_calls = 0;

    const bt_hfp_ag_platform_ops_t ops = {
        .register_callback = mock_register_callback,
        .profile_init = mock_profile_init,
        .profile_deinit = mock_profile_deinit,
        .unknown_at_error = mock_unknown_at_error,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_test_set_platform_ops(&ops));
}

void tearDown(void)
{
    bt_hfp_ag_force_cleanup_after_stack_shutdown();
    bt_duplex_state_deinit();
}

void test_hfp_profile_init_requires_callback_confirmation(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10));
    bt_hfp_ag_snapshot_t hfp;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_READY, hfp.lifecycle);
    TEST_ASSERT_TRUE(hfp.profile_ready);
    TEST_ASSERT_TRUE(hfp.callback_registered);
    TEST_ASSERT_TRUE(hfp.profile_init_request_accepted);
    TEST_ASSERT_EQUAL_UINT(1, s_register_calls);
    TEST_ASSERT_EQUAL_UINT(1, s_init_calls);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_DISCONNECTED,
                      duplex.hfp_profile_state);
}

void test_hfp_callback_registration_failure_is_visible(void)
{
    s_register_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_ag_profile_init(10));
    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_FAULTED, hfp.lifecycle);
    TEST_ASSERT_FALSE(hfp.profile_ready);
    TEST_ASSERT_FALSE(hfp.profile_init_request_accepted);
    TEST_ASSERT_EQUAL_UINT(0, s_init_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_ag_profile_deinit(10));
    TEST_ASSERT_EQUAL_UINT(0, s_deinit_calls);
}

void test_hfp_init_request_failure_is_visible(void)
{
    s_init_mode = MOCK_IMMEDIATE_FAILURE;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_ag_profile_init(10));
    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_FAULTED, hfp.lifecycle);
    TEST_ASSERT_EQUAL(ESP_FAIL, hfp.last_error);
    TEST_ASSERT_FALSE(hfp.profile_init_request_accepted);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_ag_profile_deinit(10));
    TEST_ASSERT_EQUAL_UINT(0, s_deinit_calls);
}

void test_hfp_init_callback_failure_is_visible(void)
{
    s_init_mode = MOCK_EVENT_FAILURE;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_ag_profile_init(10));
    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL_UINT64(1, hfp.profile_events);
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_FAULTED, hfp.lifecycle);
    TEST_ASSERT_TRUE(hfp.profile_init_request_accepted);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_deinit(10));
    TEST_ASSERT_EQUAL_UINT(1, s_deinit_calls);
}

void test_hfp_init_timeout_does_not_claim_ready(void)
{
    s_init_mode = MOCK_EVENT_NONE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_hfp_ag_profile_init(10));
    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL_UINT64(1, hfp.init_timeouts);
    TEST_ASSERT_FALSE(hfp.profile_ready);
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_FAULTED, hfp.lifecycle);
    TEST_ASSERT_TRUE(hfp.profile_init_request_accepted);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_deinit(10));
    TEST_ASSERT_EQUAL_UINT(1, s_deinit_calls);
}

void test_hfp_repeated_init_is_rejected(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_ag_profile_init(10));
    TEST_ASSERT_EQUAL_UINT(1, s_register_calls);
    TEST_ASSERT_EQUAL_UINT(1, s_init_calls);
}

void test_hfp_profile_deinit_requires_callback_confirmation(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_deinit(10));
    bt_hfp_ag_snapshot_t hfp;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_UNINITIALIZED,
                      hfp.lifecycle);
    TEST_ASSERT_FALSE(hfp.profile_ready);
    TEST_ASSERT_FALSE(hfp.profile_init_request_accepted);
    TEST_ASSERT_EQUAL_UINT(1, s_deinit_calls);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_UNINITIALIZED,
                      duplex.hfp_profile_state);
}

void test_hfp_deinit_request_failure_is_visible(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10));
    s_deinit_mode = MOCK_IMMEDIATE_FAILURE;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_ag_profile_deinit(10));
    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_FAULTED, hfp.lifecycle);
    TEST_ASSERT_EQUAL(ESP_FAIL, hfp.last_error);
    TEST_ASSERT_TRUE(hfp.profile_init_request_accepted);
}

void test_hfp_events_update_authoritative_same_peer_state(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10));
    uint32_t generation = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_session_begin(PEER, BT_DUPLEX_MODE_AUTO,
                                              &generation));
    bt_hfp_ag_handle_connection_state(PEER,
                                      BT_HFP_AG_CONNECTION_CONNECTING);
    bt_hfp_ag_handle_connection_state(PEER,
                                      BT_HFP_AG_CONNECTION_RFCOMM_CONNECTED);
    bt_hfp_ag_handle_connection_state(PEER,
                                      BT_HFP_AG_CONNECTION_SLC_CONNECTED);
    bt_hfp_ag_handle_codec_event(PEER, BT_HFP_AG_CODEC_CVSD);
    bt_hfp_ag_handle_audio_state(PEER, BT_HFP_AG_AUDIO_CONNECTING);
    bt_hfp_ag_handle_audio_state(PEER, BT_HFP_AG_AUDIO_CONNECTED_CVSD);
    bt_hfp_ag_handle_volume_event(PEER, false, 9);
    bt_hfp_ag_handle_volume_event(PEER, true, 7);

    bt_duplex_snapshot_t duplex;
    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL_UINT32(generation, duplex.session_generation);
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_SLC_CONNECTED,
                      duplex.hfp_profile_state);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_CONNECTED_CVSD,
                      duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_CODEC_CVSD, duplex.codec);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL(BT_HFP_AG_CODEC_CVSD, hfp.last_codec_event);
    TEST_ASSERT_EQUAL_INT(9, hfp.speaker_volume);
    TEST_ASSERT_EQUAL_INT(7, hfp.microphone_volume);
}

void test_hfp_wrong_peer_and_response_failures_are_visible(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10));
    uint32_t generation = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_session_begin(PEER, BT_DUPLEX_MODE_AUTO,
                                              &generation));
    (void)generation;
    bt_hfp_ag_handle_connection_state(OTHER_PEER,
                                      BT_HFP_AG_CONNECTION_CONNECTING);
    s_unknown_at_result = ESP_FAIL;
    bt_hfp_ag_handle_unknown_at(OTHER_PEER);

    bt_duplex_snapshot_t duplex;
    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL_UINT64(1, duplex.counters.wrong_peer_events);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL_UINT64(1, hfp.unknown_at_events);
    TEST_ASSERT_EQUAL_UINT64(1, hfp.response_failures);
    TEST_ASSERT_EQUAL(ESP_FAIL, hfp.last_error);
    TEST_ASSERT_EQUAL_UINT(1, s_unknown_at_calls);
}

void test_hfp_invalid_and_unhandled_events_are_counted(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10));
    bt_hfp_ag_handle_invalid_event();
    bt_hfp_ag_handle_unhandled_event();
    bt_hfp_ag_handle_codec_event(PEER, (bt_hfp_ag_codec_t)99);
    bt_hfp_ag_handle_volume_event(PEER, false, 99);
    bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_INIT_SUCCESS);

    bt_hfp_ag_snapshot_t hfp;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL_UINT64(3, hfp.invalid_events);
    TEST_ASSERT_EQUAL_UINT64(2, hfp.unhandled_events);
}

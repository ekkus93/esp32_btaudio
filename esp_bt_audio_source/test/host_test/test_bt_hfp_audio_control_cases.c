#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bt_duplex_state.h"
#include "bt_hfp_audio.h"

#define PEER "AA:BB:CC:DD:EE:FF"
#define OTHER_PEER "11:22:33:44:55:66"
#define SYNC_HANDLE 0x2244U

void mock_hfp_audio_control_dependencies_reset(void);
void mock_hfp_audio_control_set_dispatch_accept(bool accept);
void mock_hfp_audio_control_set_callback_registered(bool registered);
void mock_hfp_audio_control_set_apply_result(esp_err_t result);
void mock_hfp_audio_control_set_i2s_init_result(esp_err_t result);
void mock_hfp_audio_control_set_i2s_start_result(esp_err_t result,
                                                 bool quarantine);
void mock_hfp_audio_control_set_i2s_stop_result(esp_err_t result,
                                                bool quarantine);
void mock_hfp_audio_control_force_i2s_running(uint32_t generation,
                                              const char *peer);
unsigned mock_hfp_audio_control_dispatch_calls(void);
unsigned mock_hfp_audio_control_i2s_init_calls(void);
unsigned mock_hfp_audio_control_i2s_start_calls(void);
unsigned mock_hfp_audio_control_i2s_stop_calls(void);
unsigned mock_hfp_audio_control_profile_stopping_calls(void);
unsigned mock_hfp_audio_control_apply_calls(void);
uint32_t mock_hfp_audio_control_apply_generation(void);
uint16_t mock_hfp_audio_control_apply_handle(void);
const char *mock_hfp_audio_control_apply_peer(void);

static esp_err_t s_connect_result;
static esp_err_t s_disconnect_result;
static bt_hfp_ag_audio_state_t s_connect_event;
static bt_hfp_ag_audio_state_t s_disconnect_event;
static bool s_emit_connect_event;
static bool s_emit_disconnect_event;
static unsigned s_connect_calls;
static unsigned s_disconnect_calls;
static unsigned s_i2s_starts_seen_at_connect;
static uint32_t s_initial_generation;

static esp_err_t mock_audio_connect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    s_connect_calls++;
    s_i2s_starts_seen_at_connect =
        mock_hfp_audio_control_i2s_start_calls();
    if (s_connect_result == ESP_OK && s_emit_connect_event) {
        (void)bt_hfp_audio_control_handle_event(
            PEER, s_connect_event, SYNC_HANDLE, 120U);
    }
    return s_connect_result;
}

static esp_err_t mock_audio_disconnect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    s_disconnect_calls++;
    if (s_disconnect_result == ESP_OK && s_emit_disconnect_event) {
        (void)bt_hfp_audio_control_handle_event(
            PEER, s_disconnect_event,
            BT_HFP_AUDIO_INVALID_SYNC_HANDLE, 0U);
    }
    return s_disconnect_result;
}

static uint32_t make_slc_session(void)
{
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &generation));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_SLC_CONNECTED));
    return generation;
}

static void install_control_ops(void)
{
    const bt_hfp_audio_control_platform_ops_t ops = {
        .audio_connect = mock_audio_connect,
        .audio_disconnect = mock_audio_disconnect,
    };
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_hfp_audio_control_test_set_platform_ops(&ops));
}

void setUp(void)
{
    bt_hfp_audio_control_test_reset();
    bt_duplex_state_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());
    mock_hfp_audio_control_dependencies_reset();

    s_connect_result = ESP_OK;
    s_disconnect_result = ESP_OK;
    s_connect_event = BT_HFP_AG_AUDIO_CONNECTED_CVSD;
    s_disconnect_event = BT_HFP_AG_AUDIO_DISCONNECTED;
    s_emit_connect_event = true;
    s_emit_disconnect_event = true;
    s_connect_calls = 0U;
    s_disconnect_calls = 0U;
    s_i2s_starts_seen_at_connect = 0U;
    s_initial_generation = make_slc_session();
    install_control_ops();
}

void tearDown(void)
{
    bt_hfp_audio_control_test_reset();
    bt_duplex_state_deinit();
}

void test_audio_start_requires_slc_and_registered_callback(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        s_initial_generation, PEER, BT_HFP_PROFILE_DISCONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        s_initial_generation, PEER, BT_HFP_PROFILE_DISCONNECTED));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_audio_start());
    TEST_ASSERT_EQUAL_UINT(0U, s_connect_calls);
    TEST_ASSERT_EQUAL_UINT(0U, mock_hfp_audio_control_i2s_start_calls());

    bt_hfp_audio_control_test_reset();
    install_control_ops();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        s_initial_generation, PEER, BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        s_initial_generation, PEER, BT_HFP_PROFILE_SLC_CONNECTED));
    mock_hfp_audio_control_set_callback_registered(false);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_audio_start());
    TEST_ASSERT_EQUAL_UINT(0U, s_connect_calls);
}

void test_audio_start_starts_i2s_before_sco_and_waits_for_confirmation(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_start());

    bt_duplex_snapshot_t duplex;
    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_NOT_EQUAL(s_initial_generation, duplex.session_generation);
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_SLC_CONNECTED,
                      duplex.hfp_profile_state);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_CONNECTED_CVSD,
                      duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_RUNNING, duplex.i2s_state);
    TEST_ASSERT_EQUAL_UINT(1U, s_connect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, s_i2s_starts_seen_at_connect);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_apply_calls());
    TEST_ASSERT_EQUAL_UINT32(duplex.session_generation,
                             mock_hfp_audio_control_apply_generation());
    TEST_ASSERT_EQUAL_UINT16(SYNC_HANDLE,
                             mock_hfp_audio_control_apply_handle());
    TEST_ASSERT_EQUAL_STRING(PEER,
                             mock_hfp_audio_control_apply_peer());
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_OPERATION_CONFIRMED, control.state);
    TEST_ASSERT_EQUAL_UINT64(1U, control.successful_starts);
}

void test_duplicate_audio_start_is_rejected_without_second_request(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_audio_start());
    TEST_ASSERT_EQUAL_UINT(1U, s_connect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_i2s_start_calls());
}

void test_i2s_start_failure_prevents_sco_request(void)
{
    mock_hfp_audio_control_set_i2s_start_result(ESP_FAIL, false);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_start());

    bt_duplex_snapshot_t duplex;
    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_EQUAL_UINT(0U, s_connect_calls);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_DISCONNECTED,
                      duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, duplex.i2s_state);
    TEST_ASSERT_EQUAL_UINT64(1U, control.i2s_start_failures);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_FAULTED, duplex.health);
}

void test_immediate_sco_connect_failure_rolls_back_i2s(void)
{
    s_connect_result = ESP_FAIL;
    s_emit_connect_event = false;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_start());

    bt_duplex_snapshot_t duplex;
    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_EQUAL_UINT(1U, s_connect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_i2s_stop_calls());
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_DISCONNECTED,
                      duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, duplex.i2s_state);
    TEST_ASSERT_EQUAL_UINT64(1U, control.immediate_failures);
    TEST_ASSERT_EQUAL_UINT64(1U, control.rollback_attempts);
}

void test_confirmed_connected_route_failure_rolls_back_and_disconnects(void)
{
    mock_hfp_audio_control_set_apply_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_start());

    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_EQUAL_UINT(1U, s_connect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, s_disconnect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_i2s_stop_calls());
    TEST_ASSERT_EQUAL_UINT64(1U, control.rollback_attempts);
}

void test_audio_stop_handles_connecting_and_connected_states(void)
{
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_audio_session_begin(PEER, &generation));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_STARTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_RUNNING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    mock_hfp_audio_control_force_i2s_running(generation, PEER);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_stop());
    TEST_ASSERT_EQUAL_UINT(1U, s_disconnect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_i2s_stop_calls());

    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_DISCONNECTED,
                      duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, duplex.i2s_state);

    bt_hfp_audio_control_test_reset();
    install_control_ops();
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_start());
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_stop());
    TEST_ASSERT_EQUAL_UINT(2U, s_disconnect_calls);
}

void test_audio_disconnect_timeout_faults_but_stops_i2s_and_preserves_peer(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_start());
    s_emit_disconnect_event = false;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_hfp_audio_stop());

    bt_duplex_snapshot_t duplex;
    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_TRUE(duplex.peer_valid);
    TEST_ASSERT_EQUAL_STRING(PEER, duplex.peer_mac);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_FAULTED, duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, duplex.i2s_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_FAULTED, duplex.health);
    TEST_ASSERT_EQUAL_UINT64(1U, control.event_timeouts);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_i2s_stop_calls());
}

void test_i2s_stop_timeout_is_quarantined_and_takes_precedence(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_start());
    mock_hfp_audio_control_set_i2s_stop_result(ESP_ERR_TIMEOUT, true);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_hfp_audio_stop());

    bt_duplex_snapshot_t duplex;
    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_DISCONNECTED,
                      duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_QUARANTINED, duplex.i2s_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_QUARANTINED, duplex.health);
    TEST_ASSERT_EQUAL_UINT64(1U, control.i2s_stop_failures);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, control.last_error);
}

void test_late_old_connected_event_after_timeout_is_ignored(void)
{
    s_emit_connect_event = false;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_hfp_audio_start());
    unsigned apply_before = mock_hfp_audio_control_apply_calls();

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_handle_event(
        PEER, BT_HFP_AG_AUDIO_CONNECTED_CVSD, SYNC_HANDLE, 120U));

    bt_hfp_audio_control_snapshot_t control;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL_UINT(apply_before,
                           mock_hfp_audio_control_apply_calls());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(1U, control.stale_events);
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_FAULTED, duplex.hfp_audio_state);
}

void test_dispatch_failure_rolls_back_without_lower_request(void)
{
    mock_hfp_audio_control_set_dispatch_accept(false);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, bt_hfp_audio_start());
    TEST_ASSERT_EQUAL_UINT(0U, s_connect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_i2s_stop_calls());

    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_EQUAL_UINT64(1U, control.dispatch_failures);
}

void test_msbc_confirmation_is_visibly_rejected(void)
{
    s_connect_event = BT_HFP_AG_AUDIO_CONNECTED_MSBC;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, bt_hfp_audio_start());

    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_FAULTED, duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_FAULTED, duplex.health);
    TEST_ASSERT_EQUAL_UINT(1U, s_disconnect_calls);
    TEST_ASSERT_EQUAL_UINT(1U, mock_hfp_audio_control_i2s_stop_calls());
}

void test_wrong_peer_event_does_not_complete_operation(void)
{
    s_emit_connect_event = false;
    s_connect_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_start());
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_handle_event(
        OTHER_PEER, BT_HFP_AG_AUDIO_CONNECTED_CVSD, SYNC_HANDLE, 120U));

    bt_hfp_audio_control_snapshot_t control;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_get_snapshot(&control));
    TEST_ASSERT_EQUAL_UINT64(1U, control.wrong_peer_events);
}

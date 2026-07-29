#include <string.h>

#include "unity.h"
#include "bt_events_a2dp.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "mock_a2dp.h"
#include "mock_avrc.h"
#include "mock_gap.h"

extern void nvs_storage_mock_reset(void);
extern void bt_manager_test_reset_forces(void);
extern void bt_manager_test_reset_btstate_mock(void);
extern void bt_manager_test_set_hfp_policy_status(bool peer_valid,
                                                   const char *peer,
                                                   uint32_t generation,
                                                   esp_err_t result);
extern void bt_manager_test_set_hfp_policy_results(esp_err_t profile_result,
                                                    esp_err_t audio_result);
extern void bt_manager_test_set_hfp_profile_created_generation(
    uint32_t generation);

static const uint8_t PEER_A_BDA[6] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const uint8_t PEER_B_BDA[6] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66
};
static const char *PEER_A = "aa:bb:cc:dd:ee:ff";

static void send_connection(esp_a2d_connection_state_t state,
                            const uint8_t bda[6],
                            esp_a2d_conn_hdl_t handle)
{
    esp_a2d_cb_param_t param = {0};
    param.conn_stat.state = state;
    memcpy(param.conn_stat.remote_bda, bda, 6U);
    param.conn_stat.conn_hdl = handle;
    bt_events_handle_a2dp_connection(&param);
}

static void send_audio(esp_a2d_audio_state_t state,
                       const uint8_t bda[6],
                       esp_a2d_conn_hdl_t handle)
{
    esp_a2d_cb_param_t param = {0};
    param.audio_stat.state = state;
    memcpy(param.audio_stat.remote_bda, bda, 6U);
    param.audio_stat.conn_hdl = handle;
    bt_events_handle_a2dp_audio(&param);
}

static bt_events_a2dp_binding_snapshot_t binding_snapshot(void)
{
    bt_events_a2dp_binding_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&snapshot));
    return snapshot;
}

static void establish_bound_session(void)
{
    bt_manager_test_set_hfp_policy_status(false, NULL, 0U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_profile_created_generation(41U);
    send_connection(ESP_A2D_CONNECTION_STATE_CONNECTED, PEER_A_BDA, 7);

    const bt_events_a2dp_binding_snapshot_t snapshot = binding_snapshot();
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_STRING(PEER_A, snapshot.peer_mac);
    TEST_ASSERT_EQUAL_UINT(7U, snapshot.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(41U, snapshot.last_duplex_generation);
}

void setUp(void)
{
    mock_a2dp_reset();
    mock_avrc_reset();
    mock_gap_reset();
    nvs_storage_mock_reset();
    bt_manager_test_reset_forces();
    bt_manager_test_reset_btstate_mock();
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_test_init_mutex());
    TEST_ASSERT_EQUAL(ESP_OK, bt_events_a2dp_test_reset_binding());
}

void tearDown(void)
{
    if (bt_ctx.initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    } else {
        bt_manager_test_deinit_mutex();
    }
}

void test_each_binding_diagnostic_increments_by_exactly_one(void)
{
    establish_bound_session();
    const bt_events_a2dp_binding_snapshot_t baseline = binding_snapshot();

    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_B_BDA, 7);
    const bt_events_a2dp_binding_snapshot_t after_wrong_peer =
        binding_snapshot();
    TEST_ASSERT_EQUAL_UINT64(baseline.wrong_peer_rejections + 1U,
                             after_wrong_peer.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(baseline.stale_handle_rejections,
                             after_wrong_peer.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(baseline.generation_sync_failures,
                             after_wrong_peer.generation_sync_failures);
    TEST_ASSERT_EQUAL_UINT64(baseline.missing_binding_rejections,
                             after_wrong_peer.missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(baseline.late_terminal_events_ignored,
                             after_wrong_peer.late_terminal_events_ignored);

    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 8);
    const bt_events_a2dp_binding_snapshot_t after_stale_handle =
        binding_snapshot();
    TEST_ASSERT_EQUAL_UINT64(after_wrong_peer.stale_handle_rejections + 1U,
                             after_stale_handle.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(after_wrong_peer.wrong_peer_rejections,
                             after_stale_handle.wrong_peer_rejections);

    bt_manager_test_set_hfp_policy_status(true, PEER_A, 41U,
                                           ESP_ERR_TIMEOUT);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 7);
    const bt_events_a2dp_binding_snapshot_t after_sync_failure =
        binding_snapshot();
    TEST_ASSERT_EQUAL_UINT64(after_stale_handle.generation_sync_failures + 1U,
                             after_sync_failure.generation_sync_failures);
    TEST_ASSERT_EQUAL_UINT64(after_stale_handle.wrong_peer_rejections,
                             after_sync_failure.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(after_stale_handle.stale_handle_rejections,
                             after_sync_failure.stale_handle_rejections);

    bt_manager_test_set_hfp_policy_status(true, PEER_A, 41U, ESP_OK);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED, PEER_A_BDA, 7);
    const bt_events_a2dp_binding_snapshot_t after_disconnect =
        binding_snapshot();
    TEST_ASSERT_FALSE(after_disconnect.valid);

    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 7);
    const bt_events_a2dp_binding_snapshot_t after_missing =
        binding_snapshot();
    TEST_ASSERT_EQUAL_UINT64(after_disconnect.missing_binding_rejections + 1U,
                             after_missing.missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(after_disconnect.late_terminal_events_ignored,
                             after_missing.late_terminal_events_ignored);

    send_audio(ESP_A2D_AUDIO_STATE_STOPPED, PEER_A_BDA, 7);
    const bt_events_a2dp_binding_snapshot_t final = binding_snapshot();
    TEST_ASSERT_EQUAL_UINT64(after_missing.late_terminal_events_ignored + 1U,
                             final.late_terminal_events_ignored);
    TEST_ASSERT_EQUAL_UINT64(after_missing.missing_binding_rejections,
                             final.missing_binding_rejections);

    TEST_ASSERT_EQUAL_UINT64(baseline.wrong_peer_rejections + 1U,
                             final.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(baseline.stale_handle_rejections + 1U,
                             final.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(baseline.generation_sync_failures + 1U,
                             final.generation_sync_failures);
    TEST_ASSERT_EQUAL_UINT64(baseline.missing_binding_rejections + 1U,
                             final.missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(baseline.late_terminal_events_ignored + 1U,
                             final.late_terminal_events_ignored);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_binding_diagnostic_increments_by_exactly_one);
    return UNITY_END();
}

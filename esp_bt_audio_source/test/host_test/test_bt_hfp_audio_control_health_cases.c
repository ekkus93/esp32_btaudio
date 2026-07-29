#include "unity.h"

#include <stdint.h>

#include "bt_duplex_state.h"
#include "bt_hfp_audio.h"

#define HEALTH_PEER "AA:BB:CC:DD:EE:FF"
#define HEALTH_SYNC_HANDLE 0x2244U

void mock_hfp_audio_control_set_i2s_start_result(esp_err_t result,
                                                 bool quarantine);
void mock_hfp_audio_control_set_i2s_stop_result(esp_err_t result,
                                                bool quarantine);
void mock_hfp_audio_control_force_i2s_running(uint32_t generation,
                                              const char *peer);

static esp_err_t no_event_connect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    return ESP_OK;
}

static esp_err_t failing_connect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    return ESP_FAIL;
}

static esp_err_t successful_disconnect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    return ESP_OK;
}

static esp_err_t failing_disconnect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    return ESP_FAIL;
}

static void reinstall_control_ops(
    esp_err_t (*connect_fn)(esp_bd_addr_t remote_bda),
    esp_err_t (*disconnect_fn)(esp_bd_addr_t remote_bda))
{
    bt_hfp_audio_control_test_reset();
    const bt_hfp_audio_control_platform_ops_t ops = {
        .audio_connect = connect_fn,
        .audio_disconnect = disconnect_fn,
    };
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_hfp_audio_control_test_set_platform_ops(&ops));
}

static uint64_t health_report_failure_count(esp_err_t *last_error_out)
{
    uint64_t failures = 0U;
    esp_err_t last_error = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_health_report_diagnostics(
        &failures, &last_error));
    if (last_error_out != NULL) *last_error_out = last_error;
    return failures;
}

static void inject_health_timeout(void)
{
    bt_duplex_test_set_health_report_result(ESP_ERR_TIMEOUT);
}

static uint32_t force_confirmed_audio_state(void)
{
    bt_duplex_snapshot_t current;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&current));
    uint32_t generation = current.session_generation;
    if (current.hfp_audio_state == BT_HFP_AUDIO_DISCONNECTED) {
        TEST_ASSERT_EQUAL(ESP_OK,
            bt_duplex_audio_session_begin(HEALTH_PEER, &generation));
        TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
            generation, HEALTH_PEER, BT_HFP_I2S_STARTING));
        TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
            generation, HEALTH_PEER, BT_HFP_I2S_RUNNING));
        TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
            generation, HEALTH_PEER, BT_HFP_AUDIO_CONNECTING));
        TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
            generation, HEALTH_PEER, BT_HFP_AUDIO_CONNECTED_CVSD));
    }
    mock_hfp_audio_control_force_i2s_running(generation, HEALTH_PEER);
    return generation;
}

void test_health_report_failure_after_i2s_start_failure_is_visible(void)
{
    const uint64_t failures_before = health_report_failure_count(NULL);
    inject_health_timeout();
    mock_hfp_audio_control_set_i2s_start_result(ESP_FAIL, false);

    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_start());

    esp_err_t last_error = ESP_OK;
    TEST_ASSERT_EQUAL_UINT64(failures_before + 1U,
        health_report_failure_count(&last_error));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, last_error);

    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_OK, duplex.health);
}

void test_health_report_failure_after_i2s_stop_failure_is_visible(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_start());
    const uint64_t failures_before = health_report_failure_count(NULL);
    inject_health_timeout();
    mock_hfp_audio_control_set_i2s_stop_result(ESP_FAIL, false);

    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_stop());

    esp_err_t last_error = ESP_OK;
    TEST_ASSERT_EQUAL_UINT64(failures_before + 1U,
        health_report_failure_count(&last_error));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, last_error);
}

void test_health_report_failure_after_connect_event_timeout_is_visible(void)
{
    reinstall_control_ops(no_event_connect, successful_disconnect);
    inject_health_timeout();

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_hfp_audio_start());

    esp_err_t last_error = ESP_OK;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(
        1U, health_report_failure_count(&last_error));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, last_error);
}

void test_health_report_failure_after_disconnect_event_timeout_is_visible(void)
{
    (void)force_confirmed_audio_state();
    reinstall_control_ops(no_event_connect, successful_disconnect);
    inject_health_timeout();

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_hfp_audio_stop());

    esp_err_t last_error = ESP_OK;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(
        1U, health_report_failure_count(&last_error));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, last_error);

    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_AUDIO_FAULTED, duplex.hfp_audio_state);
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, duplex.i2s_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_OK, duplex.health);
}

void test_health_report_failure_during_rollback_is_visible(void)
{
    reinstall_control_ops(failing_connect, successful_disconnect);
    mock_hfp_audio_control_set_i2s_stop_result(ESP_FAIL, false);
    inject_health_timeout();

    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_audio_start());

    esp_err_t last_error = ESP_OK;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(
        1U, health_report_failure_count(&last_error));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, last_error);
}

void test_health_report_failure_during_remote_cleanup_is_visible(void)
{
    reinstall_control_ops(no_event_connect, failing_disconnect);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_init());
    inject_health_timeout();

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_control_handle_event(
        HEALTH_PEER, BT_HFP_AG_AUDIO_CONNECTED_CVSD,
        HEALTH_SYNC_HANDLE, 120U));

    esp_err_t last_error = ESP_OK;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(
        1U, health_report_failure_count(&last_error));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, last_error);
}

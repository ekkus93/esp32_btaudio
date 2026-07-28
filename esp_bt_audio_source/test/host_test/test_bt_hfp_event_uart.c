#include <string.h>

#include "unity.h"

#include "bt_hfp_event_contract.h"
#include "mock_uart.h"

#define PRIMARY 1
#define SECONDARY 0

void setUp(void)
{
    mock_uart_init_port(PRIMARY, 115200);
    mock_uart_init_port(SECONDARY, 115200);
    mock_uart_reset_tx_port(PRIMARY);
    mock_uart_reset_tx_port(SECONDARY);
}

void tearDown(void)
{
}

void test_fd12_event_records_broadcast_unchanged_to_both_command_uarts(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_profile(
        BT_HFP_PROFILE_SLC_CONNECTED, "aa:bb:cc:dd:ee:ff", 21U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_audio(
        BT_HFP_AUDIO_CONNECTED_CVSD, BT_HFP_CODEC_CVSD, 21U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_mode(
        BT_DUPLEX_MODE_AUTO, BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC,
        BT_HFP_MODE_EVENT_REASON_POLICY, 21U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_i2s(
        BT_HFP_I2S_RUNNING, ESP_OK, 21U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_health(
        BT_AUDIO_HEALTH_DEGRADED, ESP_ERR_TIMEOUT, 4U, 21U));

    const char *primary = mock_uart_get_tx_data_port(PRIMARY);
    const char *secondary = mock_uart_get_tx_data_port(SECONDARY);
    const char *records[] = {
        "EVENT|HFP|PROFILE|SLC_CONNECTED|AA:BB:CC:DD:EE:FF|21\r\n",
        "EVENT|HFP|AUDIO|CONNECTED_CVSD|CVSD|21\r\n",
        "EVENT|HFP|MODE|AUTO|A2DP_MIC|POLICY|21\r\n",
        "EVENT|HFP|I2S|RUNNING|ESP_OK|21\r\n",
        "EVENT|HFP|HEALTH|DEGRADED|TIMEOUT|4|21\r\n",
    };
    for (size_t index = 0U; index < sizeof(records) / sizeof(records[0]);
         ++index) {
        TEST_ASSERT_NOT_NULL(strstr(primary, records[index]));
        TEST_ASSERT_NOT_NULL(strstr(secondary, records[index]));
    }
    TEST_ASSERT_EQUAL_STRING(primary, secondary);
}

void test_partial_broadcast_failure_is_reported_without_hiding_delivery(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_driver_delete(SECONDARY));

    TEST_ASSERT_EQUAL(ESP_FAIL, bt_hfp_event_emit_profile(
        BT_HFP_PROFILE_CONNECTING, "AA:BB:CC:DD:EE:FF", 22U));

    TEST_ASSERT_NOT_NULL(strstr(
        mock_uart_get_tx_data_port(PRIMARY),
        "EVENT|HFP|PROFILE|CONNECTING|AA:BB:CC:DD:EE:FF|22\r\n"));
    TEST_ASSERT_EQUAL_STRING("", mock_uart_get_tx_data_port(SECONDARY));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fd12_event_records_broadcast_unchanged_to_both_command_uarts);
    RUN_TEST(test_partial_broadcast_failure_is_reported_without_hiding_delivery);
    return UNITY_END();
}

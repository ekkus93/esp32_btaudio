#include "unity.h"

#include "bt_duplex_state.h"

#define PEER "AA:BB:CC:DD:EE:FF"

void test_faulted_i2s_can_report_proven_stop_without_health_recovery(void)
{
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &generation));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_STARTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_RUNNING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_FAULTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_health(
        generation, PEER, BT_AUDIO_HEALTH_FAULTED,
        ESP_FAIL, "writer failed before cleanup"));

    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_STOPPED));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_HFP_I2S_STOPPED, snapshot.i2s_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_FAULTED, snapshot.health);
    TEST_ASSERT_EQUAL(ESP_FAIL, snapshot.last_error);
    TEST_ASSERT_EQUAL_STRING("writer failed before cleanup",
                             snapshot.last_error_text);
}

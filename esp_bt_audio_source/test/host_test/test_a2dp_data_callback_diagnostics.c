#include <string.h>

#include "unity.h"
#include "bt_events_a2dp.h"

extern void bt_manager_test_reset_btstate_mock(void);
extern void bt_manager_test_set_audio_processor_read_result(
    esp_err_t result, size_t bytes_read);
extern unsigned bt_manager_test_get_audio_processor_read_calls(void);

void setUp(void)
{
    bt_manager_test_reset_btstate_mock();
    bt_events_a2dp_test_reset_data_diagnostics();
}

void tearDown(void)
{
}

static bt_events_a2dp_data_diag_snapshot_t data_snapshot(void)
{
    bt_events_a2dp_data_diag_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_data_diagnostics(&snapshot));
    return snapshot;
}

static void test_success_returns_bytes_without_failure_diagnostic(void)
{
    uint8_t buffer[32];
    memset(buffer, 0xA5, sizeof(buffer));
    bt_manager_test_set_audio_processor_read_result(ESP_OK, 12U);

    TEST_ASSERT_EQUAL_INT32(12, bt_events_a2dp_data_callback(buffer, 32));
    TEST_ASSERT_EQUAL_UINT(1U,
                           bt_manager_test_get_audio_processor_read_calls());
    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(0U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_audio_read_error);
    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.suppressed_audio_read_error_logs);
}

static void test_failure_returns_zero_and_records_exact_error(void)
{
    uint8_t buffer[32];
    bt_manager_test_set_audio_processor_read_result(ESP_ERR_TIMEOUT, 0U);

    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(buffer, 32));
    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(1U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, snapshot.last_audio_read_error);
    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.suppressed_audio_read_error_logs);
}

static void test_repeated_failures_are_counted_with_bounded_logging(void)
{
    uint8_t buffer[8];
    bt_manager_test_set_audio_processor_read_result(ESP_ERR_NOT_SUPPORTED, 0U);

    for (unsigned i = 0U; i < 65U; ++i) {
        TEST_ASSERT_EQUAL_INT32(0,
                                bt_events_a2dp_data_callback(buffer, 8));
    }

    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(65U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, snapshot.last_audio_read_error);
    TEST_ASSERT_EQUAL_UINT32(63U,
                             snapshot.suppressed_audio_read_error_logs);
}

static void test_invalid_requests_do_not_call_audio_processor_or_count_read_failure(void)
{
    uint8_t buffer[8];
    bt_manager_test_set_audio_processor_read_result(ESP_ERR_TIMEOUT, 0U);

    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(NULL, 8));
    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(buffer, -1));
    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(buffer, 0));
    TEST_ASSERT_EQUAL_UINT(0U,
                           bt_manager_test_get_audio_processor_read_calls());
    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(0U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_audio_read_error);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_success_returns_bytes_without_failure_diagnostic);
    RUN_TEST(test_failure_returns_zero_and_records_exact_error);
    RUN_TEST(test_repeated_failures_are_counted_with_bounded_logging);
    RUN_TEST(test_invalid_requests_do_not_call_audio_processor_or_count_read_failure);
    return UNITY_END();
}

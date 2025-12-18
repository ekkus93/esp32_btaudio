#include "unity.h"
#include "audio_i2s.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

static audio_i2s_config_t cfg;

void setUp(void)
{
    mock_i2s_std_reset_state();
    cfg = (audio_i2s_config_t)AUDIO_I2S_DEFAULT_CONFIG();
    /* Ensure we start fresh */
    audio_i2s_deinit();
}

void tearDown(void)
{
    audio_i2s_deinit();
}

static void test_start_before_init_should_fail(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_i2s_start());
}

static void test_stop_before_init_should_fail(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_i2s_stop());
}

static void test_double_init_should_report_invalid_state(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_i2s_init(&cfg));
}

static void test_read_before_start_should_fail(void)
{
    uint8_t buf[8];
    size_t bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_i2s_read(buf, sizeof(buf), &bytes, 10));
}

static void test_start_stop_should_succeed(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_start_stop_start_again_should_succeed(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
}

static void test_stop_after_start_then_read_should_fail(void)
{
    uint8_t buf[8];
    size_t bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_i2s_read(buf, sizeof(buf), &bytes, 5));
}

static void test_reinit_after_deinit_should_succeed(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_deinit());

    mock_i2s_std_reset_state();
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
}

static void test_read_success_reports_bytes(void)
{
    uint8_t buf[8];
    size_t bytes = 0;
    mock_i2s_std_set_next_read_result(ESP_OK, sizeof(buf));

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_read(buf, sizeof(buf), &bytes, 10));
    TEST_ASSERT_EQUAL(sizeof(buf), bytes);
}

static void test_read_timeout_propagates_error(void)
{
    uint8_t buf[8];
    size_t bytes = 123;
    mock_i2s_std_set_next_read_result(ESP_ERR_TIMEOUT, 0);

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, audio_i2s_read(buf, sizeof(buf), &bytes, 5));
    TEST_ASSERT_EQUAL(0u, bytes);
}

static void test_read_error_propagates_and_reports_bytes(void)
{
    uint8_t buf[8];
    size_t bytes = 0;
    mock_i2s_std_set_next_read_result(ESP_FAIL, 4);

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_read(buf, sizeof(buf), &bytes, 5));
    TEST_ASSERT_EQUAL(4u, bytes);
}

static void test_read_timeout_leaves_running_and_stop_ok(void)
{
    uint8_t buf[8];
    size_t bytes = 0;
    mock_i2s_std_set_next_read_result(ESP_ERR_TIMEOUT, 0);

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, audio_i2s_read(buf, sizeof(buf), &bytes, 5));
    TEST_ASSERT_EQUAL(0u, bytes);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_read_zero_length_sets_zero_and_ok(void)
{
    size_t bytes = 123; /* sentinel */

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_read(NULL, 0, &bytes, 5));
    TEST_ASSERT_EQUAL(0u, bytes);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_read_ok_zero_bytes_returns_timeout(void)
{
    uint8_t buf[8];
    size_t bytes = 999;
    mock_i2s_std_set_next_read_result(ESP_OK, 0);

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, audio_i2s_read(buf, sizeof(buf), &bytes, 5));
    TEST_ASSERT_EQUAL(0u, bytes);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_read_partial_updates_bytes(void)
{
    uint8_t buf[8];
    size_t bytes = 0;
    mock_i2s_std_set_next_read_result(ESP_OK, 3);

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_read(buf, sizeof(buf), &bytes, 5));
    TEST_ASSERT_EQUAL(3u, bytes);
}

static void test_read_with_null_dest_should_fail(void)
{
    size_t bytes = 42;

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_i2s_read(NULL, 8, &bytes, 5));
    TEST_ASSERT_EQUAL(42u, bytes);
}

static void test_read_with_null_bytes_ptr_should_succeed(void)
{
    uint8_t buf[8];
    mock_i2s_std_set_next_read_result(ESP_OK, sizeof(buf));

    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_read(buf, sizeof(buf), NULL, 5));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_start_when_already_running_should_be_ok(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    /* Idempotent start should succeed when already running */
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_start_failures_twice_then_success(void)
{
    mock_i2s_std_set_next_enable_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_start());

    mock_i2s_std_set_next_enable_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_start());

    mock_i2s_std_set_next_enable_result(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_stop_when_not_running_should_be_ok(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_start_error_propagates_and_stop_remains_ok(void)
{
    mock_i2s_std_set_next_enable_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_start());
    /* Not running, so stop should still be a no-op OK */
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_start_failure_then_recover_start_success(void)
{
    mock_i2s_std_set_next_enable_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_start());

    mock_i2s_std_set_next_enable_result(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_stop_error_propagates(void)
{
    mock_i2s_std_reset_state();
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    mock_i2s_std_set_next_disable_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_stop());
    /* After a failed stop, try again with success to clear running state */
    mock_i2s_std_set_next_disable_result(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

static void test_init_failure_on_channel_create_recovers(void)
{
    mock_i2s_std_set_next_new_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_init(&cfg));
    /* After failure, a fresh attempt should succeed */
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_deinit());
}

static void test_init_failure_on_std_mode_recovers(void)
{
    mock_i2s_std_set_next_init_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_deinit());
}

static void test_double_stop_should_be_ok(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_start());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_i2s_stop());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_before_init_should_fail);
    RUN_TEST(test_stop_before_init_should_fail);
    RUN_TEST(test_double_init_should_report_invalid_state);
    RUN_TEST(test_read_before_start_should_fail);
    RUN_TEST(test_start_stop_should_succeed);
    RUN_TEST(test_start_stop_start_again_should_succeed);
    RUN_TEST(test_stop_after_start_then_read_should_fail);
    RUN_TEST(test_reinit_after_deinit_should_succeed);
    RUN_TEST(test_read_success_reports_bytes);
    RUN_TEST(test_read_timeout_propagates_error);
    RUN_TEST(test_read_error_propagates_and_reports_bytes);
    RUN_TEST(test_read_timeout_leaves_running_and_stop_ok);
    RUN_TEST(test_read_zero_length_sets_zero_and_ok);
    RUN_TEST(test_read_ok_zero_bytes_returns_timeout);
    RUN_TEST(test_read_partial_updates_bytes);
    RUN_TEST(test_read_with_null_dest_should_fail);
    RUN_TEST(test_read_with_null_bytes_ptr_should_succeed);
    RUN_TEST(test_start_when_already_running_should_be_ok);
    RUN_TEST(test_start_failures_twice_then_success);
    RUN_TEST(test_stop_when_not_running_should_be_ok);
    RUN_TEST(test_start_error_propagates_and_stop_remains_ok);
    RUN_TEST(test_start_failure_then_recover_start_success);
    RUN_TEST(test_stop_error_propagates);
    RUN_TEST(test_init_failure_on_channel_create_recovers);
    RUN_TEST(test_init_failure_on_std_mode_recovers);
    RUN_TEST(test_double_stop_should_be_ok);
    return UNITY_END();
}

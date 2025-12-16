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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_before_init_should_fail);
    RUN_TEST(test_stop_before_init_should_fail);
    RUN_TEST(test_double_init_should_report_invalid_state);
    RUN_TEST(test_read_before_start_should_fail);
    RUN_TEST(test_read_success_reports_bytes);
    RUN_TEST(test_read_timeout_propagates_error);
    return UNITY_END();
}

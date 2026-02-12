/**
 * Phase 4.4: I2S Manager Mock Queue Edge Cases
 * 
 * Tests mock queue functionality for CONFIG_BT_MOCK_TESTING builds:
 * - i2s_manager_mock_push() queue full scenario
 * - i2s_manager_mock_push() NULL data parameter
 * - Mock queue with mixed sample rates/bit depths
 */

#include "unity.h"
#include "i2s_manager.h"
#include "audio_util.h"
#include "esp_err.h"
#include <string.h>

/* Mock functions */
extern void mock_i2s_std_reset_state(void);

/* Buffer sizes matching i2s_manager internal expectations */
#define RAW_BUF_SIZE    2048
#define PROC_BUF_SIZE   4096
#define WORK_BYTES      4096

static uint8_t s_raw_buf[RAW_BUF_SIZE];
static uint8_t s_proc_buf[PROC_BUF_SIZE];
static uint8_t s_proc_buf2[PROC_BUF_SIZE];

/* Test data buffers */
#define TEST_DATA_SIZE 256
static uint8_t s_test_data[TEST_DATA_SIZE];

void setUp(void)
{
    mock_i2s_std_reset_state();
    i2s_manager_deinit();  /* Clean slate */
    
    /* Initialize test data with pattern */
    for (size_t i = 0; i < TEST_DATA_SIZE; i++) {
        s_test_data[i] = (uint8_t)(i & 0xFF);
    }
}

void tearDown(void)
{
    i2s_manager_deinit();
}

/**
 * Test: i2s_manager_mock_push() with NULL data parameter
 * 
 * Scenario:
 * - Initialize i2s_manager
 * - Call i2s_manager_mock_push() with NULL data pointer
 * 
 * Expected:
 * - Should return ESP_ERR_INVALID_STATE
 * - Queue should remain empty
 */
void test_i2s_manager_mock_push_null_data_should_return_invalid_state(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = 2,
        .i2s_port = 0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = -1
    };

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = RAW_BUF_SIZE,
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = WORK_BYTES
    };

    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Push with NULL data - should fail */
    ret = i2s_manager_mock_push(NULL, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * Test: i2s_manager_mock_push() with zero length
 * 
 * Scenario:
 * - Initialize i2s_manager
 * - Call i2s_manager_mock_push() with len = 0
 * 
 * Expected:
 * - Should return ESP_ERR_INVALID_STATE
 */
void test_i2s_manager_mock_push_zero_length_should_return_invalid_state(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = 2,
        .i2s_port = 0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = -1
    };

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = RAW_BUF_SIZE,
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = WORK_BYTES
    };

    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Push with zero length - should fail */
    ret = i2s_manager_mock_push(s_test_data, 0, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * Test: Queue full scenario
 * 
 * Scenario:
 * - Initialize i2s_manager (creates queue with capacity 8)
 * - Push 8 items to fill the queue
 * - Try to push 9th item
 * 
 * Expected:
 * - First 8 pushes succeed with ESP_OK
 * - 9th push fails with ESP_ERR_TIMEOUT (queue full)
 */
void test_i2s_manager_mock_push_queue_full_should_return_timeout(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = 2,
        .i2s_port = 0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = -1
    };

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = RAW_BUF_SIZE,
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = WORK_BYTES
    };

    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Fill the queue (capacity is 8) */
    for (int i = 0; i < 8; i++) {
        ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Queue should accept first 8 items");
    }

    /* 9th push should fail - queue is full */
    ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, ret);
}

/**
 * Test: Mock queue with mixed sample rates
 * 
 * Scenario:
 * - Initialize i2s_manager with 44.1kHz
 * - Push mock data at 48kHz
 * - Call i2s_source_fill() to consume and convert
 * 
 * Expected:
 * - Push succeeds
 * - i2s_source_fill() converts 48kHz → 44.1kHz successfully
 * - Returns converted data (non-zero bytes)
 */
void test_i2s_manager_mock_queue_mixed_sample_rates_should_convert(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,  /* Manager expects 44.1kHz */
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = 2,
        .i2s_port = 0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = -1
    };

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = RAW_BUF_SIZE,
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = WORK_BYTES
    };

    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Push mock data at 48kHz (different from manager's 44.1kHz) */
    ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_48K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Fill from mock queue - should convert 48kHz → 44.1kHz */
    uint8_t dst_buf[1024];
    size_t filled = i2s_source_fill(dst_buf, sizeof(dst_buf));

    /* Should have converted and returned data */
    TEST_ASSERT_GREATER_THAN(0, filled);
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(dst_buf), filled);
}

/**
 * Test: Mock queue with mixed bit depths
 * 
 * Scenario:
 * - Initialize i2s_manager with 16-bit
 * - Push mock data at 24-bit
 * - Call i2s_source_fill() to consume and convert
 * 
 * Expected:
 * - Push succeeds
 * - i2s_source_fill() converts 24-bit → 16-bit successfully
 * - Returns converted data (non-zero bytes)
 */
void test_i2s_manager_mock_queue_mixed_bit_depths_should_convert(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,  /* Manager expects 16-bit */
        .channels = 2,
        .i2s_port = 0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = -1
    };

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = RAW_BUF_SIZE,
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = WORK_BYTES
    };

    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Push mock data at 24-bit (different from manager's 16-bit) */
    ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_24, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Fill from mock queue - should convert 24-bit → 16-bit */
    uint8_t dst_buf[1024];
    size_t filled = i2s_source_fill(dst_buf, sizeof(dst_buf));

    /* Should have converted and returned data */
    TEST_ASSERT_GREATER_THAN(0, filled);
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(dst_buf), filled);
}

/**
 * Test: Mock queue push when not initialized
 * 
 * Scenario:
 * - Do NOT initialize i2s_manager
 * - Try to push mock data
 * 
 * Expected:
 * - Should return ESP_ERR_INVALID_STATE
 */
void test_i2s_manager_mock_push_not_initialized_should_return_invalid_state(void)
{
    /* No initialization - try to push */
    esp_err_t ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * Test: Consume from queue reduces availability for next push
 * 
 * Scenario:
 * - Fill queue to capacity (8 items)
 * - Consume one item via i2s_source_fill()
 * - Try to push again
 * 
 * Expected:
 * - Initial 8 pushes succeed
 * - 9th push fails (queue full)
 * - After consuming 1 item, push succeeds again
 */
void test_i2s_manager_mock_queue_consume_frees_space(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = 2,
        .i2s_port = 0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = -1
    };

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = RAW_BUF_SIZE,
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = WORK_BYTES
    };

    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Fill the queue completely (8 items) */
    for (int i = 0; i < 8; i++) {
        ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }

    /* 9th push should fail */
    ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, ret);

    /* Consume one item */
    uint8_t dst_buf[1024];
    size_t filled = i2s_source_fill(dst_buf, sizeof(dst_buf));
    TEST_ASSERT_GREATER_THAN(0, filled);

    /* Now push should succeed again (space freed) */
    ret = i2s_manager_mock_push(s_test_data, TEST_DATA_SIZE, AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_i2s_manager_mock_push_null_data_should_return_invalid_state);
    RUN_TEST(test_i2s_manager_mock_push_zero_length_should_return_invalid_state);
    RUN_TEST(test_i2s_manager_mock_push_queue_full_should_return_timeout);
    RUN_TEST(test_i2s_manager_mock_queue_mixed_sample_rates_should_convert);
    RUN_TEST(test_i2s_manager_mock_queue_mixed_bit_depths_should_convert);
    RUN_TEST(test_i2s_manager_mock_push_not_initialized_should_return_invalid_state);
    RUN_TEST(test_i2s_manager_mock_queue_consume_frees_space);
    
    return UNITY_END();
}

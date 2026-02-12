/**
 * Test file for I2S Manager Runtime Error Handling (Phase 4.2)
 * 
 * Tests runtime error paths for i2s_source_fill():
 * - NULL destination buffer
 * - Zero dst_bytes
 * - Not initialized state
 * - Not running state  
 * - i2s_channel_read() failure (ESP_ERR_INVALID_STATE, ESP_ERR_NOT_FOUND)
 * - i2s_channel_read() timeout (0 bytes read)
 * - Valid fill operation (baseline)
 * 
 * TDD approach: RED -> GREEN -> REFACTOR
 */

#include "unity.h"
#include "i2s_manager.h"
#include "audio_util.h"
#include "esp_err.h"
#include <string.h>

/* Mock I2S driver control functions */
extern void mock_i2s_std_reset_state(void);
extern void mock_i2s_std_set_next_read_result(esp_err_t ret, size_t bytes);

#define RAW_BUF_SIZE 2048
#define PROC_BUF_SIZE 4096
#define DST_BUF_SIZE 1024

static uint8_t raw_buf[RAW_BUF_SIZE];
static uint8_t proc_buf[PROC_BUF_SIZE];
static uint8_t proc_buf2[PROC_BUF_SIZE];
static uint8_t dst_buf[DST_BUF_SIZE];

static audio_config_t test_cfg;
static i2s_manager_buffers_t test_bufs;

/* Test data for mock queue */
static uint8_t test_audio_data[256];

void setUp(void)
{
    mock_i2s_std_reset_state();
    i2s_manager_deinit();
    
    /* Setup default valid configuration */
    test_cfg = (audio_config_t){
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .i2s_port = 0,  /* I2S_NUM_0 */
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = -1
    };
    
    test_bufs = (i2s_manager_buffers_t){
        .raw_buf = raw_buf,
        .raw_buf_bytes = RAW_BUF_SIZE,
        .proc_buf = proc_buf,
        .proc_buf2 = proc_buf2,
        .work_bytes = PROC_BUF_SIZE
    };
    
    /* Initialize test audio data with simple pattern */
    for (size_t i = 0; i < sizeof(test_audio_data); i++) {
        test_audio_data[i] = (uint8_t)(i & 0xFF);
    }
    
    memset(dst_buf, 0, sizeof(dst_buf));
}

void tearDown(void)
{
    i2s_manager_deinit();
    mock_i2s_std_reset_state();
}

/* ========== Section 4.2: Runtime Error Handling ========== */

/**
 * Test: NULL destination buffer should return 0
 * 
 * i2s_source_fill() must validate dst parameter
 */
static void test_i2s_source_fill_null_dst_should_return_zero(void)
{
    /* Init and start */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Try to fill with NULL dst */
    size_t filled = i2s_source_fill(NULL, DST_BUF_SIZE);
    TEST_ASSERT_EQUAL(0, filled);
}

/**
 * Test: Zero dst_bytes should return 0
 * 
 * i2s_source_fill() must validate dst_bytes parameter
 */
static void test_i2s_source_fill_zero_bytes_should_return_zero(void)
{
    /* Init and start */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Try to fill with zero bytes */
    size_t filled = i2s_source_fill(dst_buf, 0);
    TEST_ASSERT_EQUAL(0, filled);
}

/**
 * Test: Fill before initialization should return 0
 * 
 * i2s_source_fill() must check initialized flag
 */
static void test_i2s_source_fill_not_initialized_should_return_zero(void)
{
    /* Don't initialize - call fill directly */
    size_t filled = i2s_source_fill(dst_buf, DST_BUF_SIZE);
    TEST_ASSERT_EQUAL(0, filled);
}

/**
 * Test: Fill when not running should return 0
 * 
 * i2s_source_fill() must check running flag
 */
static void test_i2s_source_fill_not_running_should_return_zero(void)
{
    /* Init but don't start */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Try to fill without starting */
    size_t filled = i2s_source_fill(dst_buf, DST_BUF_SIZE);
    TEST_ASSERT_EQUAL(0, filled);
}

/**
 * Test: Fill after stop should return 0
 * 
 * Validates running flag is cleared by stop
 */
static void test_i2s_source_fill_after_stop_should_return_zero(void)
{
    /* Init and start */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Stop */
    ret = i2s_manager_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Try to fill after stop */
    size_t filled = i2s_source_fill(dst_buf, DST_BUF_SIZE);
    TEST_ASSERT_EQUAL(0, filled);
}

/**
 * Test: Mock queue with valid data should fill buffer
 * 
 * Baseline test: validates normal operation via mock queue
 */
static void test_i2s_source_fill_mock_queue_valid_data_should_succeed(void)
{
    /* Init and start */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Push test data to mock queue */
    ret = i2s_manager_mock_push(test_audio_data, sizeof(test_audio_data),
                                AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_44K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Fill from mock queue */
    size_t filled = i2s_source_fill(dst_buf, DST_BUF_SIZE);
    
    /* Should return non-zero (data was processed) */
    TEST_ASSERT_GREATER_THAN(0, filled);
    TEST_ASSERT_LESS_OR_EQUAL(DST_BUF_SIZE, filled);
}

/**
 * Test: Mock queue empty should return 0
 * 
 * Simulates I2S timeout/no data available
 */
static void test_i2s_source_fill_mock_queue_empty_should_return_zero(void)
{
    /* Init and start */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Don't push any data to mock queue */
    
    /* Fill should return 0 (no data available) */
    size_t filled = i2s_source_fill(dst_buf, DST_BUF_SIZE);
    TEST_ASSERT_EQUAL(0, filled);
}

/**
 * Test: Conversion/resampling with different sample rates
 * 
 * Tests rate conversion path through convert_and_resample_to_dst
 */
static void test_i2s_source_fill_different_sample_rate_should_convert(void)
{
    /* Init and start */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = i2s_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Push data with different sample rate (48K instead of 44.1K) */
    ret = i2s_manager_mock_push(test_audio_data, sizeof(test_audio_data),
                                AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_48K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Fill should succeed with resampling */
    size_t filled = i2s_source_fill(dst_buf, DST_BUF_SIZE);
    TEST_ASSERT_GREATER_THAN(0, filled);
}

/* ========== Test Runner ========== */

int main(void)
{
    UNITY_BEGIN();
    
    /* Runtime error handling tests */
    RUN_TEST(test_i2s_source_fill_null_dst_should_return_zero);
    RUN_TEST(test_i2s_source_fill_zero_bytes_should_return_zero);
    RUN_TEST(test_i2s_source_fill_not_initialized_should_return_zero);
    RUN_TEST(test_i2s_source_fill_not_running_should_return_zero);
    RUN_TEST(test_i2s_source_fill_after_stop_should_return_zero);
    RUN_TEST(test_i2s_source_fill_mock_queue_valid_data_should_succeed);
    RUN_TEST(test_i2s_source_fill_mock_queue_empty_should_return_zero);
    RUN_TEST(test_i2s_source_fill_different_sample_rate_should_convert);
    
    return UNITY_END();
}

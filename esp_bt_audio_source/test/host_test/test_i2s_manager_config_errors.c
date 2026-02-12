/**
 * Test file for I2S Manager Configuration Errors (Phase 4.1)
 * 
 * Tests configuration error paths for i2s_manager.c:
 * - NULL config parameter
 * - Invalid I2S port number
 * - i2s_new_channel failure
 * - i2s_channel_init_std_mode failure
 * - Unsupported sample rates
 * - Unsupported bit depths
 * 
 * TDD approach: RED -> GREEN -> REFACTOR
 */

#include "unity.h"
#include "i2s_manager.h"
#include "audio_util.h"
#include "esp_err.h"
#include <string.h>

/* Mock I2S driver for testing */
extern void mock_i2s_std_reset_state(void);
extern void mock_i2s_std_set_next_new_result(esp_err_t ret);
extern void mock_i2s_std_set_next_init_result(esp_err_t ret);

#define RAW_BUF_SIZE 2048
#define PROC_BUF_SIZE 4096

static uint8_t raw_buf[RAW_BUF_SIZE];
static uint8_t proc_buf[PROC_BUF_SIZE];
static uint8_t proc_buf2[PROC_BUF_SIZE];

static audio_config_t test_cfg;
static i2s_manager_buffers_t test_bufs;

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
}

void tearDown(void)
{
    i2s_manager_deinit();
    mock_i2s_std_reset_state();
}

/* ========== Section 4.1: Configuration Errors ========== */

/**
 * Test: NULL config parameter should return ESP_ERR_INVALID_ARG
 * 
 * Production code already has this check in configure_i2s()
 */
static void test_i2s_manager_init_null_config_should_fail(void)
{
    esp_err_t ret = i2s_manager_init(NULL, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * Test: NULL buffers parameter should return ESP_ERR_INVALID_ARG
 */
static void test_i2s_manager_init_null_buffers_should_fail(void)
{
    esp_err_t ret = i2s_manager_init(&test_cfg, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * Test: Invalid I2S port number should fail
 * 
 * ESP32 has I2S_NUM_0 (0) and I2S_NUM_1 (1).
 * Using port 99 should fail at i2s_new_channel().
 */
static void test_i2s_manager_init_invalid_port_should_fail(void)
{
    test_cfg.i2s_port = 99;  /* Invalid port number */
    
    /* Mock will detect invalid port in i2s_new_channel */
    mock_i2s_std_set_next_new_result(ESP_ERR_INVALID_ARG);
    
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
}

/**
 * Test: i2s_new_channel failure should propagate error
 * 
 * Simulates hardware failure or resource exhaustion
 */
static void test_i2s_manager_init_new_channel_fails_should_propagate_error(void)
{
    mock_i2s_std_set_next_new_result(ESP_ERR_NO_MEM);
    
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ret);
}

/**
 * Test: i2s_channel_init_std_mode failure should propagate error
 * 
 * This tests the second stage of configuration.
 * Production code should cleanup the channel on failure.
 */
static void test_i2s_manager_init_std_mode_fails_should_propagate_error(void)
{
    mock_i2s_std_set_next_init_result(ESP_ERR_INVALID_STATE);
    
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * Test: Unsupported sample rate (11025 Hz) should work or fail gracefully
 * 
 * Note: ESP-IDF may support this, so we test behavior consistency
 */
static void test_i2s_manager_init_unsupported_sample_rate_11025hz(void)
{
    test_cfg.sample_rate = 11025;  /* Unusual rate */
    
    /* This may succeed or fail depending on ESP-IDF version */
    /* We just verify it doesn't crash and returns valid esp_err_t */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    
    /* Should be either ESP_OK or a valid error code */
    TEST_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_ARG || ret == ESP_ERR_NOT_SUPPORTED);
}

/**
 * Test: Unsupported sample rate (192000 Hz) should work or fail gracefully
 * 
 * High sample rate that may exceed hardware capabilities
 */
static void test_i2s_manager_init_unsupported_sample_rate_192000hz(void)
{
    test_cfg.sample_rate = 192000;  /* Very high rate */
    
    /* This may succeed or fail depending on ESP-IDF version and HW */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    
    /* Should be either ESP_OK or a valid error code */
    TEST_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_ARG || ret == ESP_ERR_NOT_SUPPORTED);
}

/**
 * Test: Unsupported bit depth (8-bit) should fail or be accepted
 * 
 * Current code maps to I2S_DATA_BIT_WIDTH_16BIT by default
 */
static void test_i2s_manager_init_unsupported_bit_depth_8bit(void)
{
    test_cfg.bit_depth = 8;  /* Not explicitly handled in switch */
    
    /* Should either succeed with default mapping or fail */
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    
    TEST_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_ARG);
}

/**
 * Test: Valid configuration should succeed
 * 
 * Baseline test to ensure mocks work correctly
 */
static void test_i2s_manager_init_valid_config_should_succeed(void)
{
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test: Reinit after i2s_new_channel failure should work
 * 
 * Tests cleanup and recovery after configuration failure
 */
static void test_i2s_manager_reinit_after_new_channel_failure_should_succeed(void)
{
    /* First init fails */
    mock_i2s_std_set_next_new_result(ESP_ERR_NO_MEM);
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ret);
    
    /* Deinit to cleanup */
    i2s_manager_deinit();
    
    /* Reset mock and retry */
    mock_i2s_std_reset_state();
    ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test: Reinit after i2s_channel_init_std_mode failure should work
 * 
 * Tests cleanup of channel after second-stage failure
 */
static void test_i2s_manager_reinit_after_std_mode_failure_should_succeed(void)
{
    /* First init fails at std_mode */
    mock_i2s_std_set_next_init_result(ESP_ERR_INVALID_STATE);
    esp_err_t ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    /* Deinit to cleanup */
    i2s_manager_deinit();
    
    /* Reset mock and retry */
    mock_i2s_std_reset_state();
    ret = i2s_manager_init(&test_cfg, &test_bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/* ========== Test Runner ========== */

int main(void)
{
    UNITY_BEGIN();
    
    /* Configuration error tests */
    RUN_TEST(test_i2s_manager_init_null_config_should_fail);
    RUN_TEST(test_i2s_manager_init_null_buffers_should_fail);
    RUN_TEST(test_i2s_manager_init_invalid_port_should_fail);
    RUN_TEST(test_i2s_manager_init_new_channel_fails_should_propagate_error);
    RUN_TEST(test_i2s_manager_init_std_mode_fails_should_propagate_error);
    RUN_TEST(test_i2s_manager_init_unsupported_sample_rate_11025hz);
    RUN_TEST(test_i2s_manager_init_unsupported_sample_rate_192000hz);
    RUN_TEST(test_i2s_manager_init_unsupported_bit_depth_8bit);
    RUN_TEST(test_i2s_manager_init_valid_config_should_succeed);
    RUN_TEST(test_i2s_manager_reinit_after_new_channel_failure_should_succeed);
    RUN_TEST(test_i2s_manager_reinit_after_std_mode_failure_should_succeed);
    
    return UNITY_END();
}

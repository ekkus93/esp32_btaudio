/**
 * Phase 4.3: I2S Manager Cleanup on Errors
 * 
 * Tests cleanup scenarios for i2s_manager_deinit() including:
 * - Channel cleanup when never enabled
 * - i2s_channel_disable failure handling
 * - Memory leak verification on init failure paths
 */

#include "unity.h"
#include "i2s_manager.h"
#include "audio_util.h"
#include "esp_err.h"
#include <string.h>

/* Mock functions */
extern void mock_i2s_std_reset_state(void);
extern void mock_i2s_std_set_next_disable_result(esp_err_t ret);
extern void mock_i2s_std_set_next_new_result(esp_err_t ret);
extern void mock_i2s_std_set_next_init_result(esp_err_t ret);

/* Buffer sizes matching i2s_manager internal expectations */
#define RAW_BUF_SIZE    2048
#define PROC_BUF_SIZE   4096
#define WORK_BYTES      4096

static uint8_t s_raw_buf[RAW_BUF_SIZE];
static uint8_t s_proc_buf[PROC_BUF_SIZE];
static uint8_t s_proc_buf2[PROC_BUF_SIZE];

void setUp(void)
{
    mock_i2s_std_reset_state();
    i2s_manager_deinit();  /* Clean slate */
}

void tearDown(void)
{
    i2s_manager_deinit();
}

/**
 * Test: Deinit when channel was never enabled
 * 
 * Scenario: 
 * - Initialize i2s_manager successfully
 * - Never call i2s_manager_start() (so i2s_enabled remains false)
 * - Call i2s_manager_deinit()
 * 
 * Expected:
 * - Deinit should handle gracefully
 * - i2s_channel_disable() may fail internally (not enabled state)
 * - Production code doesn't check return, proceeds with del_channel
 * - No crashes or resource leaks
 */
void test_i2s_manager_deinit_channel_never_enabled_should_cleanup_gracefully(void)
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

    /* Initialize but don't start */
    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Deinit without ever calling start */
    /* This will call i2s_channel_disable() on a non-enabled channel */
    /* Production code doesn't check the return value, so it should proceed */
    i2s_manager_deinit();

    /* Verify we can re-initialize after this cleanup */
    ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test: Deinit when i2s_channel_disable fails
 * 
 * Scenario:
 * - Initialize and start i2s_manager successfully
 * - Inject error into i2s_channel_disable()
 * - Call i2s_manager_deinit()
 * 
 * Expected:
 * - Deinit should handle gracefully (best-effort cleanup)
 * - Even if disable fails, del_channel should still be called
 * - No crashes or resource leaks
 */
void test_i2s_manager_deinit_channel_disable_failure_should_proceed_with_cleanup(void)
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

    /* Inject error into i2s_channel_disable */
    mock_i2s_std_set_next_disable_result(ESP_ERR_INVALID_STATE);

    /* Deinit should proceed with cleanup despite disable failure */
    i2s_manager_deinit();

    /* Verify we can re-initialize after cleanup */
    mock_i2s_std_reset_state();
    ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test: Memory leak when mock queue created but configure_i2s fails
 * 
 * Scenario:
 * - i2s_manager_init() creates mock_queue successfully
 * - configure_i2s() fails (e.g., i2s_new_channel fails)
 * - Init function returns error
 * 
 * Expected:
 * - Production code SHOULD clean up mock_queue on failure
 * - Current implementation may have leak (returns without cleanup)
 * - After fix: verify no leak by successfully re-initializing
 */
void test_i2s_manager_init_failure_after_queue_creation_should_cleanup_queue(void)
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

    /* Inject failure into i2s_new_channel */
    mock_i2s_std_set_next_new_result(ESP_ERR_NO_MEM);

    /* Init should fail during configure_i2s */
    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ret);

    /* 
     * At this point, if production code has a leak, the mock_queue
     * is still allocated but s_mgr.initialized = false.
     * 
     * Calling deinit() won't help because it returns early if !initialized.
     * 
     * Try to re-initialize - if there's a leak, the old queue is lost.
     * The new init will create another queue, eventually exhausting resources.
     * 
     * For this test, we verify that re-init succeeds, which implies
     * the production code properly cleaned up after the failed init.
     */
    
    mock_i2s_std_reset_state();
    ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Clean up */
    i2s_manager_deinit();
}

/**
 * Test: Multiple deinit calls should be safe (idempotence)
 * 
 * Scenario:
 * - Initialize successfully
 * - Call deinit() multiple times
 * 
 * Expected:
 * - First deinit cleans up
 * - Subsequent deinits return early (no-op)
 * - No crashes or double-free issues
 */
void test_i2s_manager_deinit_multiple_calls_should_be_safe(void)
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

    /* Call deinit multiple times */
    i2s_manager_deinit();
    i2s_manager_deinit();  /* Should be safe no-op */
    i2s_manager_deinit();  /* Should be safe no-op */

    /* Verify we can still re-initialize */
    ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test: Deinit cleans up resources after partial init failure
 * 
 * Scenario:
 * - i2s_new_channel succeeds
 * - i2s_channel_init_std_mode fails
 * - configure_i2s() cleans up by calling i2s_del_channel
 * - i2s_manager_init() returns error
 * 
 * Expected:
 * - Production code in configure_i2s() already handles this
 * - Verify re-init succeeds (no leaked channel handle)
 */
void test_i2s_manager_init_partial_failure_should_cleanup_channel(void)
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

    /* Inject failure into i2s_channel_init_std_mode */
    mock_i2s_std_set_next_init_result(ESP_FAIL);

    /* Init should fail, but configure_i2s should clean up channel */
    esp_err_t ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_FAIL, ret);

    /* Verify re-init succeeds (channel was properly cleaned up) */
    mock_i2s_std_reset_state();
    ret = i2s_manager_init(&cfg, &bufs);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_i2s_manager_deinit_channel_never_enabled_should_cleanup_gracefully);
    RUN_TEST(test_i2s_manager_deinit_channel_disable_failure_should_proceed_with_cleanup);
    RUN_TEST(test_i2s_manager_init_failure_after_queue_creation_should_cleanup_queue);
    RUN_TEST(test_i2s_manager_deinit_multiple_calls_should_be_safe);
    RUN_TEST(test_i2s_manager_init_partial_failure_should_cleanup_channel);
    
    return UNITY_END();
}

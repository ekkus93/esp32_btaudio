/**
 * @file test_cooperative_shutdown.c
 * @brief Integration tests for CODE_REVIEW 2602101453 P0.1.6 - Cooperative Shutdown
 *
 * These tests validate that the cooperative shutdown mechanism (replacing unsafe
 * vTaskDelete()) works correctly:
 * - No memory leaks (chunk_buf freed)
 * - No deadlocks (task exits cleanly)
 * - No hangs (event signaling works)
 * - Fast shutdown (< 100ms typical)
 * - Stress resilience (rapid cycles, concurrent operations)
 *
 * WHY: The old vTaskDelete() implementation could:
 * 1. Deadlock the system (task killed while holding spinlock)
 * 2. Leak resources (512-byte chunk_buf never freed)
 * 3. Corrupt state (task killed mid-update)
 *
 * Cooperative shutdown fixes this by:
 * - Task checks stop flag each iteration
 * - Task wakes immediately on xTaskNotifyGive()
 * - Task frees resources before exit
 * - Task signals completion via event group
 * - Task self-deletes safely with vTaskDelete(NULL)
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "audio_processor.h"
#include "driver/i2s_std.h"

static const char *TAG = "COOP_SHUTDOWN_TEST";

#define I2S_SAMPLE_RATE AUDIO_SAMPLE_RATE_44K
#define I2S_BIT_DEPTH   AUDIO_BIT_DEPTH_16
#define I2S_CHANNELS    AUDIO_CHANNEL_STEREO
#define I2S_PORT        I2S_NUM_0

/* Test delay helper */
static void test_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* Get current free heap (internal DRAM) */
static uint32_t get_free_heap_dram(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
}

/* Get current free heap (any capability - includes PSRAM if enabled) */
static uint32_t get_free_heap_any(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

/**
 * @brief P0.1.6.1: Basic start/stop cycle test
 *
 * WHY: Verify cooperative shutdown works in simplest case
 * HOW: Init → Start → Stop → Deinit
 * EXPECT: ESP_OK on all calls, completes in < 500ms
 */
static void test_cooperative_shutdown_basic(void)
{
    ESP_LOGI(TAG, "TEST: test_cooperative_shutdown_basic");
    
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Let task run for a bit to ensure it's actually started */
    test_delay_ms(100);

    /* Measure stop time */
    TickType_t start_ticks = xTaskGetTickCount();
    ret = audio_processor_stop();
    TickType_t stop_ticks = xTaskGetTickCount();
    
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Stop should complete quickly (< 100ms) with cooperative shutdown */
    uint32_t stop_time_ms = (stop_ticks - start_ticks) * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Stop time: %lu ms", (unsigned long)stop_time_ms);
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(100, stop_time_ms, "Stop took too long");

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "PASS: test_cooperative_shutdown_basic");
}

/**
 * @brief P0.1.6.2: Multiple start/stop cycles - no leaks
 *
 * WHY: Verify chunk_buf is freed on every stop (was leaked with vTaskDelete)
 * HOW: Run 20 start/stop cycles, measure heap before and after
 * EXPECT: No heap decrease > 1KB (512 bytes/cycle would leak 10KB in 20 cycles)
 */
static void test_cooperative_shutdown_no_leaks(void)
{
    ESP_LOGI(TAG, "TEST: test_cooperative_shutdown_no_leaks");
    
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Baseline heap measurement */
    uint32_t heap_before = get_free_heap_dram();
    ESP_LOGI(TAG, "Heap before cycles: %lu bytes", (unsigned long)heap_before);

    const int CYCLES = 20;
    for (int i = 0; i < CYCLES; i++) {
        ret = audio_processor_start();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Start failed during leak test");
        
        /* Let task run briefly */
        test_delay_ms(50);
        
        ret = audio_processor_stop();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Stop failed during leak test");
        
        /* Small delay between cycles */
        test_delay_ms(20);
    }

    /* Final heap measurement */
    uint32_t heap_after = get_free_heap_dram();
    ESP_LOGI(TAG, "Heap after %d cycles: %lu bytes", CYCLES, (unsigned long)heap_after);
    
    /* Allow some heap variation due to fragmentation, logging, etc.
     * Old code leaked 512 bytes/cycle = 10KB over 20 cycles
     * We allow 1KB tolerance for normal variation */
    int32_t heap_delta = (int32_t)heap_before - (int32_t)heap_after;
    ESP_LOGI(TAG, "Heap delta: %ld bytes", (long)heap_delta);
    
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(1024, heap_delta, 
        "Heap decreased more than 1KB - possible memory leak");

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "PASS: test_cooperative_shutdown_no_leaks");
}

/**
 * @brief P0.1.6.3: Rapid start/stop cycles - no deadlock
 *
 * WHY: Stress test for race conditions and deadlock scenarios
 * HOW: 50 cycles with minimal delays (task barely gets to run)
 * EXPECT: All calls succeed, no hangs, completes in < 10 seconds
 */
static void test_cooperative_shutdown_rapid_cycles(void)
{
    ESP_LOGI(TAG, "TEST: test_cooperative_shutdown_rapid_cycles");
    
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TickType_t test_start = xTaskGetTickCount();
    
    const int RAPID_CYCLES = 50;
    uint32_t total_stop_time_ms = 0;
    
    for (int i = 0; i < RAPID_CYCLES; i++) {
        ret = audio_processor_start();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Start failed during rapid cycles");
        
        /* Minimal run time - task may not even produce audio */
        test_delay_ms(10);
        
        TickType_t stop_start = xTaskGetTickCount();
        ret = audio_processor_stop();
        TickType_t stop_end = xTaskGetTickCount();
        
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Stop failed during rapid cycles");
        
        uint32_t stop_time = (stop_end - stop_start) * portTICK_PERIOD_MS;
        total_stop_time_ms += stop_time;
        
        /* Minimal delay between cycles */
        test_delay_ms(5);
    }
    
    TickType_t test_end = xTaskGetTickCount();
    uint32_t total_time_ms = (test_end - test_start) * portTICK_PERIOD_MS;
    uint32_t avg_stop_time_ms = total_stop_time_ms / RAPID_CYCLES;
    
    ESP_LOGI(TAG, "Completed %d rapid cycles in %lu ms", RAPID_CYCLES, (unsigned long)total_time_ms);
    ESP_LOGI(TAG, "Average stop time: %lu ms", (unsigned long)avg_stop_time_ms);
    
    /* Sanity check: test should complete in reasonable time */
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(10000, total_time_ms, 
        "Rapid cycles took too long - possible hang or deadlock");
    
    /* Average stop time should be quick with cooperative shutdown */
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(50, avg_stop_time_ms,
        "Average stop time too high - cooperative shutdown may not be working");

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "PASS: test_cooperative_shutdown_rapid_cycles");
}

/**
 * @brief P0.1.6.4: Stop during active audio production
 *
 * WHY: Verify stop works safely when task is actively producing audio
 * HOW: Start with synth mode (continuous audio), let it produce, then stop
 * EXPECT: Stop succeeds quickly without corruption or deadlock
 */
static void test_cooperative_shutdown_during_active_audio(void)
{
    ESP_LOGI(TAG, "TEST: test_cooperative_shutdown_during_active_audio");
    
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Enable synth mode for continuous audio generation */
    audio_processor_set_synth_mode(true);
    
    /* Let synth run for a while to ensure task is actively producing audio */
    test_delay_ms(200);
    
    /* Stop while audio is actively being produced */
    TickType_t stop_start = xTaskGetTickCount();
    ret = audio_processor_stop();
    TickType_t stop_end = xTaskGetTickCount();
    
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    uint32_t stop_time_ms = (stop_end - stop_start) * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Stop during active synth: %lu ms", (unsigned long)stop_time_ms);
    
    /* Should still be fast */
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(100, stop_time_ms, 
        "Stop during active audio took too long");

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "PASS: test_cooperative_shutdown_during_active_audio");
}

/**
 * @brief P0.1.6.5: Idempotent stop - multiple stops are safe
 *
 * WHY: Verify stop is idempotent (calling multiple times doesn't crash)
 * HOW: Start once, call stop multiple times
 * EXPECT: First stop returns ESP_OK, subsequent stops return ESP_ERR_INVALID_STATE
 */
static void test_cooperative_shutdown_idempotent_stop(void)
{
    ESP_LOGI(TAG, "TEST: test_cooperative_shutdown_idempotent_stop");
    
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    test_delay_ms(50);
    
    /* First stop should succeed */
    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Second stop should return INVALID_STATE (not running) */
    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_STATE, ret,
        "Second stop should return INVALID_STATE");
    
    /* Third stop should also return INVALID_STATE */
    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_STATE, ret,
        "Third stop should return INVALID_STATE");

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "PASS: test_cooperative_shutdown_idempotent_stop");
}

/**
 * @brief P0.1.6.6: Start/stop with different delays
 *
 * WHY: Test various timings to catch edge cases
 * HOW: Run cycles with delays from 1ms to 500ms
 * EXPECT: All succeed regardless of how long task has been running
 */
static void test_cooperative_shutdown_various_timings(void)
{
    ESP_LOGI(TAG, "TEST: test_cooperative_shutdown_various_timings");
    
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Test with various run durations */
    const uint32_t delays_ms[] = {1, 5, 10, 20, 50, 100, 200, 500};
    const int num_delays = sizeof(delays_ms) / sizeof(delays_ms[0]);
    
    for (int i = 0; i < num_delays; i++) {
        ret = audio_processor_start();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Start failed in timing test");
        
        test_delay_ms(delays_ms[i]);
        
        ret = audio_processor_stop();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Stop failed in timing test");
        
        ESP_LOGI(TAG, "Cycle with %lu ms delay: OK", (unsigned long)delays_ms[i]);
        
        test_delay_ms(10);  /* Small gap between tests */
    }

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "PASS: test_cooperative_shutdown_various_timings");
}

/* ========================================================================
 * Test Registration
 * ======================================================================== */

void run_cooperative_shutdown_tests(void)
{
    ESP_LOGI(TAG, "=== CODE_REVIEW 2602101453 P0.1.6: Cooperative Shutdown Tests ===");
    
    RUN_TEST(test_cooperative_shutdown_basic);
    RUN_TEST(test_cooperative_shutdown_no_leaks);
    RUN_TEST(test_cooperative_shutdown_rapid_cycles);
    RUN_TEST(test_cooperative_shutdown_during_active_audio);
    RUN_TEST(test_cooperative_shutdown_idempotent_stop);
    RUN_TEST(test_cooperative_shutdown_various_timings);
    
    ESP_LOGI(TAG, "=== Cooperative Shutdown Tests Complete ===");
}

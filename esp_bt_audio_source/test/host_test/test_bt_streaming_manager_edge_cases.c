/**
 * test_bt_streaming_manager_edge_cases.c
 * 
 * TDD tests for bt_streaming_manager.c edge cases and error paths.
 * Phase 5.3: BT Streaming Manager Edge Cases
 * 
 * Coverage:
 * - bt_audio_data_callback() error handling
 * - Underrun statistics accuracy
 * - State machine edge cases (already stopped, pause/resume sequences)
 * - Audio processor integration errors
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "esp_err.h"
#include "esp_a2dp_api.h"
#include "bt_source.h"
#include "audio_processor.h"
#include "mock_a2dp.h"

/* Forward declare streaming manager APIs */
esp_err_t bt_streaming_start(void);
esp_err_t bt_streaming_stop(void);
esp_err_t bt_streaming_pause(void);
esp_err_t bt_streaming_resume(void);
esp_err_t bt_get_streaming_info(bt_streaming_info_t* info);
void bt_streaming_manager_init(void);
void bt_streaming_manager_reset_state_for_test(void);
void bt_streaming_manager_force_state_for_test(bt_streaming_state_t state);
void bt_streaming_manager_set_callback_for_test(bt_stream_callback_t cb, void *user_data);

/* Helpers from mocks */
void bt_manager_test_set_connection_state(int v);
void bt_manager_test_reset_btstate_mock(void);
void mock_task_set_tick(uint32_t ticks);

/* Test helper for audio processor failure injection */
void audio_processor_test_set_read_error(esp_err_t error);
void audio_processor_test_clear_read_error(void);

/* Callback context */
typedef struct {
    int calls;
    bool last_streaming;
    bt_streaming_info_t last_info;
} stream_cb_ctx_t;

static stream_cb_ctx_t g_cb_ctx;

static void reset_cb_ctx(void)
{
    memset(&g_cb_ctx, 0, sizeof(g_cb_ctx));
}

static void test_stream_callback(bool streaming, const bt_streaming_info_t *info, void *user_data)
{
    (void)user_data;
    g_cb_ctx.calls++;
    g_cb_ctx.last_streaming = streaming;
    if (info) {
        memcpy(&g_cb_ctx.last_info, info, sizeof(bt_streaming_info_t));
    }
}

void setUp(void)
{
    mock_a2dp_reset();
    bt_manager_test_reset_btstate_mock();
    bt_streaming_manager_reset_state_for_test();
    bt_streaming_manager_init();
    audio_processor_test_clear_read_error();
    reset_cb_ctx();
}

void tearDown(void)
{
    mock_a2dp_reset();
    audio_processor_test_clear_read_error();
}

/**
 * TDD Test 1: bt_audio_data_callback handles audio_processor_read() error
 * 
 * Behavior: When audio_processor_read() returns ESP_FAIL, the callback should:
 * - Zero-fill the buffer
 * - Return the requested length
 * - Update stats with zero bytes_produced and full bytes_silence
 */
void test_bt_audio_data_callback_handles_audio_processor_read_error(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    
    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);
    
    // Arrange: Inject error into audio_processor_read()
    audio_processor_test_set_read_error(ESP_FAIL);
    
    uint8_t out[32];
    memset(out, 0xCC, sizeof(out)); // Fill with pattern
    
    // Act: Call data callback
    int32_t written = data_cb(out, (int32_t)sizeof(out));
    
    // Assert: Returned full length
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(out), written);
    
    // Assert: Buffer zero-filled
    for (size_t i = 0; i < sizeof(out); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, out[i]);
    }
    
    // Assert: Stats updated correctly
    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(out), info.bytes_requested);
    TEST_ASSERT_EQUAL_UINT32(0, info.bytes_produced); // No audio produced
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(out), info.bytes_silence); // All silence
    TEST_ASSERT_EQUAL_UINT32(1, info.packets_sent);
}

/**
 * TDD Test 2: Underrun statistics accuracy
 * 
 * Behavior: Underrun stats (underrun_count, total_callbacks) should be accurate:
 * - total_callbacks increments on every data callback
 * - underrun_count increments only when bytes_read < requested
 * - Underrun rate = underrun_count / total_callbacks
 */
void test_bt_underrun_statistics_accuracy(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    
    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);
    
    // Arrange: Inject audio for first callback (no underrun)
    const int16_t sample1[] = {1000, -1000, 2000, -2000};
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(
        (const uint8_t*)sample1, sizeof(sample1)));
    
    uint8_t buf1[sizeof(sample1)] = {0};
    
    // Act: First callback - no underrun
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf1), data_cb(buf1, (int32_t)sizeof(buf1)));
    
    // Assert: No underrun yet
    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(1, info.total_callbacks);
    TEST_ASSERT_EQUAL_UINT32(0, info.underrun_count); // No underrun
    
    // Act: Second callback - underrun (no audio injected)
    uint8_t buf2[16] = {0};
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf2), data_cb(buf2, (int32_t)sizeof(buf2)));
    
    // Assert: One underrun
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(2, info.total_callbacks);
    TEST_ASSERT_EQUAL_UINT32(1, info.underrun_count); // First underrun
    
    // Act: Third callback - partial underrun (inject less than requested)
    const int16_t sample3[] = {3000, -3000}; // Only 4 bytes
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(
        (const uint8_t*)sample3, sizeof(sample3)));
    
    uint8_t buf3[16] = {0}; // Request 16 bytes, get 4
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf3), data_cb(buf3, (int32_t)sizeof(buf3)));
    
    // Assert: Second underrun (partial fill counts as underrun)
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(3, info.total_callbacks);
    TEST_ASSERT_EQUAL_UINT32(2, info.underrun_count); // Second underrun
    TEST_ASSERT_EQUAL_UINT32(sizeof(sample3), info.bytes_produced - sizeof(sample1));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(buf3) - sizeof(sample3)), 
                             info.bytes_silence - sizeof(buf2));
}

/**
 * TDD Test 3: bt_streaming_stop when already stopped
 * 
 * Behavior: Calling bt_streaming_stop() when already stopped should:
 * - Return ESP_OK (idempotent)
 * - NOT call esp_a2d_media_ctrl() again
 * - State remains STOPPED
 */
void test_bt_streaming_stop_when_already_stopped_is_idempotent(void)
{
    // Act: Stop when already stopped (initial state is STOPPED)
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_stop());
    
    // Assert: No media_ctrl call
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_media_ctrl_calls());
    
    // Assert: State remains STOPPED
    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, info.state);
}

/**
 * TDD Test 4: Complete state machine sequence START → PAUSE → RESUME → STOP
 * 
 * Behavior: Validate full state machine transitions:
 * - START: STOPPED → STARTING
 * - PAUSE: STREAMING → PAUSED
 * - RESUME: PAUSED → STREAMING
 * - STOP: STREAMING → STOPPING → STOPPED
 */
void test_bt_state_machine_complete_sequence_start_pause_resume_stop(void)
{
    bt_streaming_manager_set_callback_for_test(test_stream_callback, NULL);
    bt_manager_test_set_connection_state(1); // Connected
    
    // Act: START
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_start());
    
    // Assert: STARTING state
    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STARTING, info.state);
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_START, mock_a2dp_get_last_media_ctrl());
    
    // Arrange: Force to STREAMING (simulates A2DP callback)
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    
    // Act: PAUSE
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_pause());
    
    // Assert: PAUSED state
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, info.state);
    TEST_ASSERT_TRUE(info.paused);
    TEST_ASSERT_EQUAL(2, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_SUSPEND, mock_a2dp_get_last_media_ctrl());
    
    // Act: RESUME
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_resume());
    
    // Assert: STREAMING state
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, info.state);
    TEST_ASSERT_FALSE(info.paused);
    TEST_ASSERT_EQUAL(3, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_START, mock_a2dp_get_last_media_ctrl());
    
    // Act: STOP
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_stop());
    
    // Assert: STOPPING state
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPING, info.state);
    TEST_ASSERT_EQUAL(4, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_STOP, mock_a2dp_get_last_media_ctrl());
}

/**
 * TDD Test 5: Multiple underruns in sequence update stats correctly
 * 
 * Behavior: Sequential underruns should accumulate correctly:
 * - Each underrun increments underrun_count
 * - Each callback increments total_callbacks
 * - bytes_silence accumulates across underruns
 */
void test_bt_multiple_underruns_accumulate_stats(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    
    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);
    
    // Act: Three underruns in a row (no audio injected)
    uint8_t buf[16] = {0};
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf), data_cb(buf, (int32_t)sizeof(buf)));
    }
    
    // Assert: All three counted as underruns
    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(3, info.total_callbacks);
    TEST_ASSERT_EQUAL_UINT32(3, info.underrun_count);
    TEST_ASSERT_EQUAL_UINT32(0, info.bytes_produced);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(3 * sizeof(buf)), info.bytes_silence);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(3 * sizeof(buf)), info.bytes_requested);
}

/**
 * TDD Test 6: Underrun rate calculation
 * 
 * Behavior: Underrun rate should be calculated as underrun_count / total_callbacks:
 * - 2 normal callbacks + 1 underrun = 33.3% underrun rate
 */
void test_bt_underrun_rate_calculation(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    
    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);
    
    // Arrange: Two successful callbacks
    const int16_t sample[] = {1000, -1000};
    uint8_t buf[4] = {0};
    
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(
            (const uint8_t*)sample, sizeof(sample)));
        TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf), data_cb(buf, (int32_t)sizeof(buf)));
    }
    
    // Act: One underrun
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf), data_cb(buf, (int32_t)sizeof(buf)));
    
    // Assert: 1/3 = ~33% underrun rate
    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(3, info.total_callbacks);
    TEST_ASSERT_EQUAL_UINT32(1, info.underrun_count);
    // Underrun rate = 1/3 = 0.333...
    float underrun_rate = (float)info.underrun_count / (float)info.total_callbacks;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.333f, underrun_rate);
}

/**
 * TDD Test 7: Stream duration accumulates correctly across pause/resume
 * 
 * Behavior: stream_duration should track total streaming time:
 * - Starts at 0 when STARTING
 * - Accumulates during STREAMING (updated on data callbacks)
 * - Preserved across PAUSE/RESUME
 * - Reset on STOPPED
 * 
 * Note: Production code checks `if (s_stream_start_time > 0)` before calculating duration,
 * so we must ensure start time is > 0. We start tick at 1 instead of 0.
 */
void test_bt_stream_duration_across_pause_resume(void)
{
    // Arrange: Set tick to 1 (not 0, to avoid s_stream_start_time == 0 check)
    mock_task_set_tick(1);
    bt_manager_test_set_connection_state(1);
    
    // Act: Start streaming (sets s_stream_start_time to current tick = 1ms)
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_start());
    
    // Force to STREAMING state to enable data callbacks
    // (normally done by A2DP state machine callback)
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    
    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);
    
    // Arrange: Move time forward to 101ms
    mock_task_set_tick(101);
    
    // Act: Call data callback to trigger duration update
    // Duration should be calculated as: current_time (101) - start_time (1) = 100ms
    uint8_t buf[16] = {0};
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf), data_cb(buf, (int32_t)sizeof(buf)));
    
    // Assert: Duration should be 100ms
    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(100, info.stream_duration);
    
    // Act: Pause
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_pause());
    
    // Simulate 50ms pause
    mock_task_set_tick(151);
    
    // Act: Resume
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_resume());
    
    // Simulate 50ms more streaming (total 200ms from start)
    mock_task_set_tick(201);
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(buf), data_cb(buf, (int32_t)sizeof(buf)));
    
    // Assert: Duration = current_time (201) - start_time (1) = 200ms
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(200, info.stream_duration);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_audio_data_callback_handles_audio_processor_read_error);
    RUN_TEST(test_bt_underrun_statistics_accuracy);
    RUN_TEST(test_bt_streaming_stop_when_already_stopped_is_idempotent);
    RUN_TEST(test_bt_state_machine_complete_sequence_start_pause_resume_stop);
    RUN_TEST(test_bt_multiple_underruns_accumulate_stats);
    RUN_TEST(test_bt_underrun_rate_calculation);
    RUN_TEST(test_bt_stream_duration_across_pause_resume);
    return UNITY_END();
}

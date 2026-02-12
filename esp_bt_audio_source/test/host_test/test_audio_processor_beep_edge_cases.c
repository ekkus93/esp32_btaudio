/**
 * @file test_audio_processor_beep_edge_cases.c
 * @brief Phase 3.1: audio_processor_beep.c Edge Case Tests (TDD)
 *
 * Tests edge cases and error paths in audio_processor_beep.c including:
 * - Beep when not initialized
 * - Beep during WAV playback
 * - Source restoration logic (SYNTH/I2S)
 * - Invariant violation handling (both sources active)
 * - Duration clamping (0 ms, > 20000 ms)
 * - s_drop_ring_audio flag behavior
 * - beep_manager_play() failure handling
 */

#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"
#include "i2s_manager.h"
#include "beep_manager.h"
#include "esp_log.h"
#include <string.h>

/* Test helpers to access internal state */
extern bool s_force_synth;
extern bool s_beep_restore_synth;
extern bool s_beep_restore_i2s;
extern volatile bool s_drop_ring_audio;
extern bool s_is_initialized;
extern bool s_is_running;
extern size_t s_beep_remaining_bytes;
extern audio_config_t s_audio_config;  /* Production audio config */

/* Mock/stub control for testing */
static bool mock_wav_playback_active = false;
static esp_err_t mock_beep_manager_play_result = ESP_OK;
static bool mock_i2s_manager_running = false;

/* Override wav_playback_is_active() for testing */
bool wav_playback_is_active(void)
{
    return mock_wav_playback_active;
}

/* Mock i2s_manager functions */
bool i2s_manager_is_running(void)
{
    return mock_i2s_manager_running;
}

static int i2s_start_call_count = 0;
static int i2s_stop_call_count = 0;

esp_err_t i2s_manager_start(void)
{
    i2s_start_call_count++;
    mock_i2s_manager_running = true;
    return ESP_OK;
}

esp_err_t i2s_manager_stop(void)
{
    i2s_stop_call_count++;
    mock_i2s_manager_running = false;
    return ESP_OK;
}

/* Mock beep_manager functions */
static beep_done_cb_t mock_beep_done_callback = NULL;
static void *mock_beep_done_ctx = NULL;
static beep_request_t last_beep_request;
static audio_config_t last_audio_config;

esp_err_t beep_manager_play(const beep_request_t *req, const audio_config_t *cfg)
{
    if (mock_beep_manager_play_result != ESP_OK) {
        return mock_beep_manager_play_result;
    }
    
    if (req) {
        memcpy(&last_beep_request, req, sizeof(beep_request_t));
    }
    if (cfg) {
        memcpy(&last_audio_config, cfg, sizeof(audio_config_t));
    }
    return ESP_OK;
}

void beep_manager_set_done_callback(beep_done_cb_t cb, void *ctx)
{
    mock_beep_done_callback = cb;
    mock_beep_done_ctx = ctx;
}

void beep_manager_stop(void)
{
    /* No-op for testing */
}

bool beep_overlay_is_active(void)
{
    return false; /* Simplified for testing */
}

/* Minimal stubs for audio_processor_init/deinit used by test setUp/tearDown */
esp_err_t audio_processor_init(const audio_config_t *cfg)
{
    (void)cfg;
    s_is_initialized = true;
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    s_is_initialized = false;
    return ESP_OK;
}

/* Test fixture */
void setUp(void)
{
    /* Reset all mocks */
    mock_wav_playback_active = false;
    mock_beep_manager_play_result = ESP_OK;
    mock_i2s_manager_running = false;
    mock_beep_done_callback = NULL;
    mock_beep_done_ctx = NULL;
    i2s_start_call_count = 0;
    i2s_stop_call_count = 0;
    
    /* Reset internal state */
    s_force_synth = false;
    s_beep_restore_synth = false;
    s_beep_restore_i2s = false;
    s_drop_ring_audio = false;
    s_is_initialized = true;  /* Mark as initialized for tests */
    s_is_running = true;      /* Mark as running for restore tests */
    
    /* Initialize audio configuration (required for beep duration calculation) */
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
    s_audio_config.volume = 50;
    s_audio_config.mute = false;
}

void tearDown(void)
{
    audio_processor_beep_reset();
}

/* ============================================================================
 * Test 1: Beep when not initialized
 * ============================================================================ */
void test_beep_not_initialized(void)
{
    /* Arrange: Mark as not initialized */
    s_is_initialized = false;
    
    /* Act */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, result);
}

/* ============================================================================
 * Test 2: Beep while WAV playback active
 * ============================================================================ */
void test_beep_during_wav_playback(void)
{
    /* Arrange: Simulate WAV playback active */
    mock_wav_playback_active = true;
    
    /* Act */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, result);
}

/* ============================================================================
 * Test 3: Beep interrupts SYNTH, restores SYNTH after
 * ============================================================================ */
void test_beep_restore_synth_source(void)
{
    /* Arrange: Set SYNTH as active source */
    s_force_synth = true;
    
    /* Act: Start beep */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert: Beep started successfully */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: SYNTH was preempted (turned off) */
    TEST_ASSERT_FALSE(s_force_synth);
    
    /* Assert: Restore flag is set */
    TEST_ASSERT_TRUE(s_beep_restore_synth);
    TEST_ASSERT_FALSE(s_beep_restore_i2s);
    
    /* Act: Simulate beep completion by calling the done callback */
    TEST_ASSERT_NOT_NULL(mock_beep_done_callback);
    mock_beep_done_callback(mock_beep_done_ctx);
    
    /* Assert: SYNTH was restored */
    TEST_ASSERT_TRUE(s_force_synth);
    
    /* Assert: Restore flags were cleared */
    TEST_ASSERT_FALSE(s_beep_restore_synth);
    TEST_ASSERT_FALSE(s_beep_restore_i2s);
}

/* ============================================================================
 * Test 4: Beep interrupts I2S, restores I2S after
 * ============================================================================ */
void test_beep_restore_i2s_source(void)
{
    /* Arrange: Set I2S as active source */
    mock_i2s_manager_running = true;
    i2s_stop_call_count = 0;
    i2s_start_call_count = 0;
    
    /* Act: Start beep */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert: Beep started successfully */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: I2S was stopped (preempted) */
    TEST_ASSERT_EQUAL_INT(1, i2s_stop_call_count);
    
    /* Assert: Restore flag is set */
    TEST_ASSERT_FALSE(s_beep_restore_synth);
    TEST_ASSERT_TRUE(s_beep_restore_i2s);
    
    /* Arrange: Mark processor as running for restore to work */
    extern bool s_is_running;
    s_is_running = true;
    
    /* Act: Simulate beep completion */
    TEST_ASSERT_NOT_NULL(mock_beep_done_callback);
    mock_beep_done_callback(mock_beep_done_ctx);
    
    /* Assert: I2S was restarted */
    TEST_ASSERT_EQUAL_INT(1, i2s_start_call_count);
    
    /* Assert: Restore flags were cleared */
    TEST_ASSERT_FALSE(s_beep_restore_synth);
    TEST_ASSERT_FALSE(s_beep_restore_i2s);
}

/* ============================================================================
 * Test 5: Invariant violation - both SYNTH and I2S active
 * ============================================================================ */
void test_beep_synth_i2s_both_active_invariant(void)
{
    /* Arrange: Create invariant violation - both sources active */
    s_force_synth = true;
    mock_i2s_manager_running = true;
    
    /* Act: Start beep */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert: Beep started successfully (invariant handled) */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: SYNTH restore flag is set (SYNTH wins priority) */
    TEST_ASSERT_TRUE(s_beep_restore_synth);
    
    /* Assert: I2S restore flag is NOT set (I2S loses in conflict) */
    TEST_ASSERT_FALSE(s_beep_restore_i2s);
    
    /* Assert: I2S was NOT stopped (invariant just clears the flag, doesn't touch I2S) */
    TEST_ASSERT_EQUAL_INT(0, i2s_stop_call_count);
}

/* ============================================================================
 * Test 6: Beep when neither source active - stays silent after
 * ============================================================================ */
void test_beep_when_neither_source_active(void)
{
    /* Arrange: Neither SYNTH nor I2S active */
    s_force_synth = false;
    mock_i2s_manager_running = false;
    
    /* Act: Start beep */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert: Beep started successfully */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: No restore flags set */
    TEST_ASSERT_FALSE(s_beep_restore_synth);
    TEST_ASSERT_FALSE(s_beep_restore_i2s);
    
    /* Act: Simulate beep completion */
    TEST_ASSERT_NOT_NULL(mock_beep_done_callback);
    mock_beep_done_callback(mock_beep_done_ctx);
    
    /* Assert: Still no sources active (correct idle behavior) */
    TEST_ASSERT_FALSE(s_force_synth);
    TEST_ASSERT_EQUAL_INT(0, i2s_start_call_count);
}

/* ============================================================================
 * Test 7: Duration clamping - zero duration
 * ============================================================================ */
void test_beep_duration_clamping_zero(void)
{
    /* Act: Request beep with 0 ms duration */
    esp_err_t result = audio_processor_beep_tone(0, 1000.0);
    
    /* Assert: Beep started successfully */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: Duration was clamped to 50 ms */
    TEST_ASSERT_EQUAL_UINT32(50, last_beep_request.duration_ms);
}

/* ============================================================================
 * Test 8: Duration clamping - over maximum
 * ============================================================================ */
void test_beep_duration_clamping_over_max(void)
{
    /* Act: Request beep with 20001 ms duration (over limit) */
    esp_err_t result = audio_processor_beep_tone(20001, 1000.0);
    
    /* Assert: Beep started successfully */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: Duration was clamped to 20000 ms */
    TEST_ASSERT_EQUAL_UINT32(20000, last_beep_request.duration_ms);
}

/* ============================================================================
 * Test 9: s_drop_ring_audio flag is set
 * ============================================================================ */
void test_beep_drops_ring_audio(void)
{
    /* Arrange: Ensure flag starts false */
    s_drop_ring_audio = false;
    
    /* Act: Start beep */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert: Beep started successfully */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: Drop flag was set (F1.4.2) */
    TEST_ASSERT_TRUE(s_drop_ring_audio);
}

/* ============================================================================
 * Test 10: beep_manager_play() failure handling
 * ============================================================================ */
void test_beep_manager_play_failure(void)
{
    /* Arrange: Set SYNTH active to test cleanup */
    s_force_synth = true;
    
    /* Arrange: Make beep_manager_play() fail */
    mock_beep_manager_play_result = ESP_ERR_NO_MEM;
    
    /* Act: Attempt to start beep */
    esp_err_t result = audio_processor_beep_tone(100, 1000.0);
    
    /* Assert: Failure is propagated */
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, result);
    
    /* Assert: SYNTH was still preempted (stopped before beep_manager call) */
    TEST_ASSERT_FALSE(s_force_synth);
    
    /* Assert: Restore flag is still set (will be cleared by reset/next beep) */
    TEST_ASSERT_TRUE(s_beep_restore_synth);
    
    /* Note: In production, user would need to manually restore or start new beep */
}

/* ============================================================================
 * Test 11: Frequency defaulting to 1000 Hz
 * ============================================================================ */
void test_beep_zero_frequency_defaults_to_1000hz(void)
{
    /* Act: Request beep with 0 Hz frequency */
    esp_err_t result = audio_processor_beep_tone(100, 0.0);
    
    /* Assert: Beep started successfully */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    /* Assert: Frequency was defaulted to 1000.0 Hz (check within 1 Hz tolerance) */
    /* Unity doesn't support TEST_ASSERT_EQUAL_DOUBLE, so check integer part */
    TEST_ASSERT_EQUAL_INT(1000, (int)last_beep_request.freq_hz);
}

/* ============================================================================
 * Test 12: Done callback clears remaining bytes
 * ============================================================================ */
void test_beep_done_callback_clears_remaining_bytes(void)
{
    /* Arrange: Start a beep to set remaining_bytes */
    audio_processor_beep_tone(100, 1000.0);
    
    /* Arrange: Manually set remaining bytes (simulating mid-beep state) */
    extern size_t s_beep_remaining_bytes;
    /* Note: spinlock_t not available in host tests, but critical sections are no-ops anyway */
    s_beep_remaining_bytes = 1000;
    
    /* Act: Trigger done callback */
    TEST_ASSERT_NOT_NULL(mock_beep_done_callback);
    mock_beep_done_callback(mock_beep_done_ctx);
    
    /* Assert: Remaining bytes cleared to 0 */
    TEST_ASSERT_EQUAL_UINT(0, s_beep_remaining_bytes);
}

int main(void)
{
    UNITY_BEGIN();
    
    /* Edge case tests */
    RUN_TEST(test_beep_not_initialized);
    RUN_TEST(test_beep_during_wav_playback);
    RUN_TEST(test_beep_restore_synth_source);
    RUN_TEST(test_beep_restore_i2s_source);
    RUN_TEST(test_beep_synth_i2s_both_active_invariant);
    RUN_TEST(test_beep_when_neither_source_active);
    RUN_TEST(test_beep_duration_clamping_zero);
    RUN_TEST(test_beep_duration_clamping_over_max);
    RUN_TEST(test_beep_drops_ring_audio);
    RUN_TEST(test_beep_manager_play_failure);
    RUN_TEST(test_beep_zero_frequency_defaults_to_1000hz);
    RUN_TEST(test_beep_done_callback_clears_remaining_bytes);
    
    return UNITY_END();
}

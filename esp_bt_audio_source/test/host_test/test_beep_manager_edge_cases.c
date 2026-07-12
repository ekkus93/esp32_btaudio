/**
 * test_beep_manager_edge_cases.c - Phase 3.2 TDD tests for beep_manager.c
 * 
 * Tests edge cases and boundary conditions in beep_manager.c:
 * - Extreme frequencies (0.1 Hz, 20000 Hz)
 * - Very short duration (< 2*fade_frames triggers special fade logic)
 * - Zero duration (defaults to 50ms)
 * - Duration clamping (>20000ms → 20000ms)
 * - Zero sample rate (invalid config)
 * - Buffer alignment issues
 * - Concurrent beep requests (ESP_ERR_INVALID_STATE)
 * - Stop when not initialized
 * - Fade envelope edge cases
 */

#include "unity.h"
#include "beep_manager.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static audio_config_t s_test_config;
static bool s_done_callback_called;
static void *s_done_callback_ctx;

void setUp(void)
{
    /* Reset beep manager state */
    beep_manager_deinit();
    beep_manager_init();
    
    /* Setup standard test config */
    s_test_config.sample_rate = 44100;
    s_test_config.bit_depth = AUDIO_BIT_DEPTH_16;
    s_test_config.channels = AUDIO_CHANNEL_STEREO;
    
    /* Reset done callback tracking */
    s_done_callback_called = false;
    s_done_callback_ctx = NULL;
    beep_manager_set_done_callback(NULL, NULL);
}

void tearDown(void)
{
    beep_manager_deinit();
}

/* Done callback for testing */
static void test_done_callback(void *ctx)
{
    s_done_callback_called = true;
    s_done_callback_ctx = ctx;
}

/* ── UT-8: state accessors, stop paths, 32-bit mixing, unsupported depth ── */

void test_beep_get_state_and_manager_stop(void)
{
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
    beep_request_t req = { .duration_ms = 100, .freq_hz = 1000.0, .amplitude = 5000 };
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &s_test_config));
    TEST_ASSERT_EQUAL(BEEP_STATE_PLAYING, beep_manager_get_state());
    beep_manager_stop();
    TEST_ASSERT_FALSE(beep_overlay_is_active());
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
}

void test_beep_overlay_stop_deactivates(void)
{
    beep_request_t req = { .duration_ms = 100, .freq_hz = 1000.0, .amplitude = 5000 };
    TEST_ASSERT_EQUAL(ESP_OK, beep_overlay_start(&req, &s_test_config));
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    beep_overlay_stop();
    TEST_ASSERT_FALSE(beep_overlay_is_active());
}

void test_beep_unsupported_bit_depth_rejected(void)
{
    audio_config_t cfg24 = s_test_config;
    cfg24.bit_depth = AUDIO_BIT_DEPTH_24; /* bytes_per_sample() → 0 → NOT_SUPPORTED */
    beep_request_t req = { .duration_ms = 50, .freq_hz = 1000.0, .amplitude = 5000 };
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, beep_overlay_start(&req, &cfg24));
}

void test_beep_overlay_fill_mixes_32bit(void)
{
    audio_config_t cfg32 = s_test_config;
    cfg32.bit_depth = AUDIO_BIT_DEPTH_32;
    cfg32.channels = AUDIO_CHANNEL_STEREO; /* 8-byte frame */

    beep_request_t req = { .duration_ms = 20, .freq_hz = 1000.0, .amplitude = 5000 };
    TEST_ASSERT_EQUAL(ESP_OK, beep_overlay_start(&req, &cfg32));

    /* Preload base samples; the mix scales base by 0.7 and adds the beep. At
     * frame 0 the fade envelope is 0, so the first sample is deterministically
     * base*7/10 = 700 — proving the 32-bit mixing path ran. */
    int32_t buf[64];
    for (int i = 0; i < 64; ++i) buf[i] = 1000;
    beep_overlay_fill((uint8_t *)buf, sizeof(buf), &cfg32);

    TEST_ASSERT_EQUAL_INT32(700, buf[0]); /* left ch, frame 0 */
    TEST_ASSERT_EQUAL_INT32(700, buf[1]); /* right ch, frame 0 */
    TEST_ASSERT_TRUE(beep_overlay_is_active());
}

/* ============================================================================
 * Test 1: Extreme frequency - very low (0.1 Hz)
 * ============================================================================ */
void test_beep_extreme_frequency_very_low(void)
{
    /* Arrange */
    beep_request_t req = {
        .duration_ms = 100,
        .freq_hz = 0.1,  /* Extremely low frequency */
        .amplitude = 5000
    };
    
    /* Act */
    esp_err_t result = beep_manager_play(&req, &s_test_config);
    
    /* Assert: Should succeed (no frequency validation beyond > 0) */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Assert: Verify beep generates without crashing */
    uint8_t buffer[4096] = {0};
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
    
    /* Note: With 0.1 Hz and 44100 Hz sample rate, phase increment is tiny */
    /* phase_inc = 2π * 0.1 / 44100 = ~0.0000142 radians per sample */
}

/* ============================================================================
 * Test 2: Extreme frequency - very high (20000 Hz)
 * ============================================================================ */
void test_beep_extreme_frequency_very_high(void)
{
    /* Arrange */
    beep_request_t req = {
        .duration_ms = 100,
        .freq_hz = 20000.0,  /* Near Nyquist limit (22050 Hz for 44.1kHz) */
        .amplitude = 5000
    };
    
    /* Act */
    esp_err_t result = beep_manager_play(&req, &s_test_config);
    
    /* Assert: Should succeed */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Assert: Generate samples without overflow */
    uint8_t buffer[4096] = {0};
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
}

/* ============================================================================
 * Test 3: Very short duration (< 2*fade) triggers special fade logic
 * ============================================================================ */
void test_beep_very_short_duration_less_than_two_fade(void)
{
    /* Arrange: BEEP_FADE_MS = 10ms, so 2*fade = 20ms */
    beep_request_t req = {
        .duration_ms = 15,  /* Less than 2*fade (20ms) */
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    /* Act */
    esp_err_t result = beep_manager_play(&req, &s_test_config);
    
    /* Assert: Should succeed */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Assert: fade_env() returns 1.0 for all frames when total_frames <= 2*fade_frames */
    /* This prevents weird artifacts on very short beeps */
    uint8_t buffer[8192] = {0};
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
}

/* ============================================================================
 * Test 4: Zero duration defaults to 50ms
 * ============================================================================ */
void test_beep_zero_duration_defaults_to_50ms(void)
{
    /* Arrange */
    beep_request_t req = {
        .duration_ms = 0,  /* Should default to BEEP_DEFAULT_DURATION_MS (50ms) */
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    /* Act */
    esp_err_t result = beep_manager_play(&req, &s_test_config);
    
    /* Assert: Should succeed */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Assert: Beep should last ~50ms worth of frames */
    /* 50ms * 44100 samples/sec = 2205 frames * 4 bytes/frame (16-bit stereo) = 8820 bytes */
    uint8_t buffer[20000] = {0};
    
    /* First fill: generates all frames */
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
    
    /* Second fill: detects completion at start and fires callback */
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
    
    /* After second fill, beep should be done */
    TEST_ASSERT_FALSE(beep_overlay_is_active());
}

/* ============================================================================
 * Test 5: Duration clamping >20000ms → 20000ms
 * ============================================================================ */
void test_beep_duration_clamping_over_max(void)
{
    /* Arrange */
    beep_request_t req = {
        .duration_ms = 25000,  /* Exceeds BEEP_MAX_DURATION_MS (20000ms) */
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    /* Act */
    esp_err_t result = beep_manager_play(&req, &s_test_config);
    
    /* Assert: Should succeed with clamped duration */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Note: Validation of 20000ms (vs 25000ms) would require checking s_overlay.total_frames */
    /* For host test, we verify it doesn't fail and starts */
}

/* ============================================================================
 * Test 6: Zero sample rate returns ESP_ERR_INVALID_ARG
 * ============================================================================ */
void test_beep_zero_sample_rate_invalid_arg(void)
{
    /* Arrange */
    beep_request_t req = {
        .duration_ms = 100,
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    audio_config_t bad_config = {
        .sample_rate = 0,  /* Invalid */
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO
    };
    
    /* Act */
    esp_err_t result = beep_manager_play(&req, &bad_config);
    
    /* Assert: Should return ESP_ERR_INVALID_ARG */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    TEST_ASSERT_FALSE(beep_overlay_is_active());
}

/* ============================================================================
 * Test 7: Concurrent beep request returns ESP_ERR_INVALID_STATE
 * ============================================================================ */
void test_beep_concurrent_request_returns_invalid_state(void)
{
    /* Arrange: Start first beep */
    beep_request_t req1 = {
        .duration_ms = 200,
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    esp_err_t result1 = beep_manager_play(&req1, &s_test_config);
    TEST_ASSERT_EQUAL(ESP_OK, result1);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Act: Try to start second beep while first is active */
    beep_request_t req2 = {
        .duration_ms = 100,
        .freq_hz = 2000.0,
        .amplitude = 3000
    };
    
    esp_err_t result2 = beep_manager_play(&req2, &s_test_config);
    
    /* Assert: Second request should fail */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, result2);
    TEST_ASSERT_TRUE(beep_overlay_is_active());  /* First beep still active */
}

/* ============================================================================
 * Test 8: Stop when not initialized (should not crash)
 * ============================================================================ */
void test_beep_stop_when_not_initialized(void)
{
    /* Arrange: Deinit beep manager */
    beep_manager_deinit();
    
    /* Act: Call stop when not initialized */
    beep_manager_stop();
    
    /* Assert: Should not crash (no assertion to verify beyond survival) */
    TEST_PASS();
}

/* ============================================================================
 * Test 9: beep_overlay_fill with NULL buffer (should return gracefully)
 * ============================================================================ */
void test_beep_overlay_fill_null_buffer(void)
{
    /* Arrange: Start a beep */
    beep_request_t req = {
        .duration_ms = 100,
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    beep_manager_play(&req, &s_test_config);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Act: Call fill with NULL buffer */
    beep_overlay_fill(NULL, 1024, &s_test_config);
    
    /* Assert: Should not crash, beep still active */
    TEST_ASSERT_TRUE(beep_overlay_is_active());
}

/* ============================================================================
 * Test 10: beep_overlay_fill with zero bytes (should return gracefully)
 * ============================================================================ */
void test_beep_overlay_fill_zero_bytes(void)
{
    /* Arrange: Start a beep */
    beep_request_t req = {
        .duration_ms = 100,
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    beep_manager_play(&req, &s_test_config);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Act: Call fill with zero bytes */
    uint8_t buffer[1024];
    beep_overlay_fill(buffer, 0, &s_test_config);
    
    /* Assert: Should not crash, beep still active */
    TEST_ASSERT_TRUE(beep_overlay_is_active());
}

/* ============================================================================
 * Test 11: beep_overlay_fill with NULL config (should return gracefully)
 * ============================================================================ */
void test_beep_overlay_fill_null_config(void)
{
    /* Arrange: Start a beep */
    beep_request_t req = {
        .duration_ms = 100,
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    beep_manager_play(&req, &s_test_config);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Act: Call fill with NULL config */
    uint8_t buffer[1024];
    beep_overlay_fill(buffer, sizeof(buffer), NULL);
    
    /* Assert: Should not crash, beep still active */
    TEST_ASSERT_TRUE(beep_overlay_is_active());
}

/* ============================================================================
 * Test 12: Done callback fires on natural completion
 * ============================================================================ */
void test_beep_done_callback_fires_on_completion(void)
{
    /* Arrange */
    int test_context = 42;
    beep_manager_set_done_callback(test_done_callback, &test_context);
    
    beep_request_t req = {
        .duration_ms = 10,  /* Very short for quick completion */
        .freq_hz = 1000.0,
        .amplitude = 5000
    };
    
    beep_manager_play(&req, &s_test_config);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Act: Fill enough to complete beep */
    /* 10ms * 44100 samples/sec = 441 frames * 4 bytes/frame = 1764 bytes */
    uint8_t buffer[10000] = {0};
    
    /* First fill: generates all 441 frames */
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
    
    /* Beep still active until next fill detects completion */
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    TEST_ASSERT_FALSE(s_done_callback_called);  /* Not yet */
    
    /* Second fill: detects completion at start, fires callback, sets active=false */
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
    
    /* Assert: Beep should be done and callback should have fired */
    TEST_ASSERT_FALSE(beep_overlay_is_active());
    TEST_ASSERT_TRUE(s_done_callback_called);
    TEST_ASSERT_EQUAL_PTR(&test_context, s_done_callback_ctx);
}

/* ============================================================================
 * Test 13: Zero amplitude defaults to 7500
 * ============================================================================ */
void test_beep_zero_amplitude_defaults_to_7500(void)
{
    /* Arrange */
    beep_request_t req = {
        .duration_ms = 100,
        .freq_hz = 1000.0,
        .amplitude = 0  /* Should default to BEEP_DEFAULT_AMPLITUDE (7500) */
    };
    
    /* Act */
    esp_err_t result = beep_manager_play(&req, &s_test_config);
    
    /* Assert: Should succeed */
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(beep_overlay_is_active());
    
    /* Assert: Generate samples (validation that amplitude=7500 would require comparing sample values) */
    uint8_t buffer[4096] = {0};
    beep_overlay_fill(buffer, sizeof(buffer), &s_test_config);
}

/* ============================================================================
 * Unity Test Runner
 * ============================================================================ */

int main(void)
{
    UNITY_BEGIN();
    
    /* Extreme frequencies */
    RUN_TEST(test_beep_extreme_frequency_very_low);
    RUN_TEST(test_beep_extreme_frequency_very_high);
    
    /* Duration edge cases */
    RUN_TEST(test_beep_very_short_duration_less_than_two_fade);
    RUN_TEST(test_beep_zero_duration_defaults_to_50ms);
    RUN_TEST(test_beep_duration_clamping_over_max);
    
    /* Config validation */
    RUN_TEST(test_beep_zero_sample_rate_invalid_arg);
    
    /* State management */
    RUN_TEST(test_beep_concurrent_request_returns_invalid_state);
    RUN_TEST(test_beep_stop_when_not_initialized);
    
    /* Buffer handling */
    RUN_TEST(test_beep_overlay_fill_null_buffer);
    RUN_TEST(test_beep_overlay_fill_zero_bytes);
    RUN_TEST(test_beep_overlay_fill_null_config);
    
    /* Callbacks */
    RUN_TEST(test_beep_done_callback_fires_on_completion);
    
    /* Amplitude defaulting */
    RUN_TEST(test_beep_zero_amplitude_defaults_to_7500);

    /* UT-8: state accessors, stop paths, 32-bit mixing, unsupported depth */
    RUN_TEST(test_beep_get_state_and_manager_stop);
    RUN_TEST(test_beep_overlay_stop_deactivates);
    RUN_TEST(test_beep_unsupported_bit_depth_rejected);
    RUN_TEST(test_beep_overlay_fill_mixes_32bit);

    return UNITY_END();
}

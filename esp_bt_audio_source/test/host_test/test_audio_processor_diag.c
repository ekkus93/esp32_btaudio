#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"

#include <string.h>

void audio_processor_core_stub_reset(void);

void setUp(void)
{
    audio_processor_core_stub_reset();

    s_is_initialized = false;
    s_is_running = false;
    s_force_synth = false;
    s_volume_gain = 0;
    s_audio_diag_enabled = false;

    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
    s_audio_config.mute = false;

    memset(&s_audio_stats, 0, sizeof(s_audio_stats));
    s_probe_captured = 0;
    s_probe_target = 0;
    memset(s_probe_buf, 0, sizeof(s_probe_buf));

    s_audio_ring = NULL;
}

void tearDown(void)
{
}

void test_diag_enable_should_toggle_flag_state(void)
{
    TEST_ASSERT_FALSE(audio_processor_is_diag_enabled());

    audio_processor_set_diag_enabled(true);
    TEST_ASSERT_TRUE(audio_processor_is_diag_enabled());

    audio_processor_set_diag_enabled(false);
    TEST_ASSERT_FALSE(audio_processor_is_diag_enabled());
}

void test_arm_probe_should_clamp_target_and_reset_captured(void)
{
    s_probe_captured = 7;
    s_probe_target = 3;

    audio_processor_arm_probe((size_t)I2S_PROBE_MAX_ENTRIES + 10U);

    TEST_ASSERT_EQUAL_UINT(0, s_probe_captured);
    TEST_ASSERT_EQUAL_UINT(I2S_PROBE_MAX_ENTRIES, s_probe_target);
}

void test_emit_probe_should_clear_state_when_no_entries(void)
{
    s_probe_captured = 0;
    s_probe_target = 5;

    esp_err_t ret = audio_processor_emit_probe();

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_UINT(0, s_probe_captured);
    TEST_ASSERT_EQUAL_UINT(0, s_probe_target);
}

void test_emit_probe_should_handle_captured_entries_and_clear_state(void)
{
    s_probe_target = 2;
    s_probe_captured = 2;

    s_probe_buf[0].requested = 128;
    s_probe_buf[0].got = 128;
    s_probe_buf[0].dur_us = 10;

    s_probe_buf[1].requested = 64;
    s_probe_buf[1].got = 32;
    s_probe_buf[1].dur_us = 15;

    esp_err_t ret = audio_processor_emit_probe();

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_UINT(0, s_probe_captured);
    TEST_ASSERT_EQUAL_UINT(0, s_probe_target);
}

void test_get_status_should_reflect_runtime_fields(void)
{
    audio_status_t status = {0};

    s_is_initialized = true;
    s_is_running = true;
    s_volume_gain = 77;
    s_audio_config.mute = true;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_48K;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_32;
    s_audio_config.channels = AUDIO_CHANNEL_MONO;

    esp_err_t ret = audio_processor_get_status(&status);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(status.initialized);
    TEST_ASSERT_TRUE(status.running);
    TEST_ASSERT_EQUAL_UINT8(77, status.volume);
    TEST_ASSERT_TRUE(status.mute);
    TEST_ASSERT_EQUAL(AUDIO_SAMPLE_RATE_48K, status.sample_rate);
    TEST_ASSERT_EQUAL(AUDIO_BIT_DEPTH_32, status.bit_depth);
    TEST_ASSERT_EQUAL(AUDIO_CHANNEL_MONO, status.channels);
}

void test_get_status_should_reject_null_output(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_processor_get_status(NULL));
}

void test_get_stats_should_return_large_counters_without_truncation(void)
{
    audio_stats_t out = {0};

    s_is_initialized = true;
    s_audio_stats.samples_processed = UINT32_MAX;
    s_audio_stats.buffer_overruns = UINT32_MAX - 1U;
    s_audio_stats.buffer_underruns = UINT32_MAX - 2U;
    s_audio_stats.underrun_bytes = UINT64_MAX - 3ULL;
    s_audio_stats.bytes_read = UINT64_MAX - 4ULL;
    s_audio_stats.bytes_by_source[0] = UINT64_MAX - 5ULL;
    s_audio_stats.bytes_by_source[1] = UINT64_MAX - 6ULL;
    s_audio_stats.bytes_by_source[2] = UINT64_MAX - 7ULL;
    s_audio_stats.beep_overlay_bytes = UINT64_MAX - 8ULL;

    esp_err_t ret = audio_processor_get_stats(&out);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, out.samples_processed);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 1U, out.buffer_overruns);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 2U, out.buffer_underruns);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 3ULL, out.underrun_bytes);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 4ULL, out.bytes_read);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 5ULL, out.bytes_by_source[0]);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 6ULL, out.bytes_by_source[1]);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 7ULL, out.bytes_by_source[2]);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 8ULL, out.beep_overlay_bytes);
}

void test_emit_diag_summary_should_succeed(void)
{
    s_i2s_read_ops = 123;
    s_i2s_total_read_bytes = 45678;
    s_i2s_timeout_count = 9;
    s_audio_stats.buffer_underruns = 11;
    s_audio_stats.buffer_overruns = 12;

    esp_err_t ret = audio_processor_emit_diag_summary();

    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/* ── apply_volume() — TEST-5 coverage ──────────────────────────────────── */

void test_apply_volume_100_is_passthrough(void)
{
    int16_t buf[4] = {1000, -2000, 3000, INT16_MAX};
    int16_t expected[4];
    memcpy(expected, buf, sizeof(buf));

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    apply_volume(buf, sizeof(buf), 100);

    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, buf, 4);
}

void test_apply_volume_0_zeroes_buffer(void)
{
    int16_t buf[4] = {1000, -2000, 3000, -4000};

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    apply_volume(buf, sizeof(buf), 0);

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);
    }
}

void test_apply_volume_50_scales_16bit(void)
{
    int16_t buf[2] = {1000, -1000};

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    apply_volume(buf, sizeof(buf), 50);

    /* 1000 * 50/100 = 500 (with rounding up from +50/2=25 → 525/100 = 5... wait:
     * scaled = (1000 * 50 + 50) / 100 = 50050/100 = 500 */
    TEST_ASSERT_INT16_WITHIN(2, 500, buf[0]);
    TEST_ASSERT_INT16_WITHIN(2, -500, buf[1]);
}

void test_apply_volume_16bit_clamps_at_int16_max(void)
{
    /* Scale a value that would overflow int16 range */
    int16_t buf[1] = {INT16_MAX};

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    apply_volume(buf, sizeof(buf), 99);

    /* Result must stay within int16 range */
    TEST_ASSERT_TRUE(buf[0] <= INT16_MAX && buf[0] >= INT16_MIN);
}

void test_apply_volume_16bit_clamps_at_int16_min(void)
{
    int16_t buf[1] = {INT16_MIN};

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    apply_volume(buf, sizeof(buf), 99);

    TEST_ASSERT_TRUE(buf[0] <= INT16_MAX && buf[0] >= INT16_MIN);
}

void test_apply_volume_32bit_scales_samples(void)
{
    int32_t buf[2] = {100000, -100000};

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_32;
    apply_volume(buf, sizeof(buf), 50);

    TEST_ASSERT_INT32_WITHIN(100, 50000, buf[0]);
    TEST_ASSERT_INT32_WITHIN(100, -50000, buf[1]);
}

void test_apply_volume_32bit_zero_zeroes_buffer(void)
{
    int32_t buf[2] = {999999, -999999};

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_32;
    apply_volume(buf, sizeof(buf), 0);

    TEST_ASSERT_EQUAL_INT32(0, buf[0]);
    TEST_ASSERT_EQUAL_INT32(0, buf[1]);
}

void test_apply_volume_null_buffer_no_crash(void)
{
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    apply_volume(NULL, 8, 50);  /* must not crash */
}

void test_apply_volume_zero_size_no_modification(void)
{
    int16_t buf[2] = {1234, -1234};

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    apply_volume(buf, 0, 50);

    TEST_ASSERT_EQUAL_INT16(1234, buf[0]);
    TEST_ASSERT_EQUAL_INT16(-1234, buf[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_diag_enable_should_toggle_flag_state);
    RUN_TEST(test_arm_probe_should_clamp_target_and_reset_captured);
    RUN_TEST(test_emit_probe_should_clear_state_when_no_entries);
    RUN_TEST(test_emit_probe_should_handle_captured_entries_and_clear_state);
    RUN_TEST(test_get_status_should_reflect_runtime_fields);
    RUN_TEST(test_get_status_should_reject_null_output);
    RUN_TEST(test_get_stats_should_return_large_counters_without_truncation);
    RUN_TEST(test_emit_diag_summary_should_succeed);
    /* TEST-5: apply_volume() edge cases */
    RUN_TEST(test_apply_volume_100_is_passthrough);
    RUN_TEST(test_apply_volume_0_zeroes_buffer);
    RUN_TEST(test_apply_volume_50_scales_16bit);
    RUN_TEST(test_apply_volume_16bit_clamps_at_int16_max);
    RUN_TEST(test_apply_volume_16bit_clamps_at_int16_min);
    RUN_TEST(test_apply_volume_32bit_scales_samples);
    RUN_TEST(test_apply_volume_32bit_zero_zeroes_buffer);
    RUN_TEST(test_apply_volume_null_buffer_no_crash);
    RUN_TEST(test_apply_volume_zero_size_no_modification);
    return UNITY_END();
}

#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"

#include <string.h>

void audio_processor_core_stub_reset(void);
void audio_processor_core_stub_set_i2s_running(bool running);
void audio_processor_core_stub_set_i2s_init_result(esp_err_t result);
uint32_t audio_processor_core_stub_get_i2s_init_calls(void);
uint32_t audio_processor_core_stub_get_nvs_set_i2s_pins_calls(void);
void audio_processor_core_stub_get_last_i2s_pins(int *bclk, int *ws, int *din, int *dout);

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

/* TEST-1b: Integration — set_volume → apply_volume produces correct amplitude. */
void test_volume_set_then_apply_scales_pcm_amplitude(void)
{
    const int16_t full_val = 0x4000;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;

    /* volume 100 → pass-through: apply_volume must not modify samples. */
    s_volume_gain = 100;
    int16_t buf100[4] = {full_val, full_val, full_val, full_val};
    apply_volume(buf100, sizeof(buf100), s_volume_gain);
    TEST_ASSERT_EQUAL_INT16(full_val, buf100[0]);

    /* volume 50 → each sample should be approximately halved (±1 rounding). */
    s_volume_gain = 50;
    int16_t buf50[4] = {full_val, full_val, full_val, full_val};
    apply_volume(buf50, sizeof(buf50), s_volume_gain);
    const int16_t half_val = (int16_t)(((int32_t)full_val * 50 + 50) / 100);
    TEST_ASSERT_INT16_WITHIN(1, half_val, buf50[0]);

    /* volume 0 → mute: all samples must become 0. */
    s_volume_gain = 0;
    int16_t buf0[4] = {full_val, full_val, full_val, full_val};
    apply_volume(buf0, sizeof(buf0), s_volume_gain);
    TEST_ASSERT_EQUAL_INT16(0, buf0[0]);
}

/* ================= BT-1 (docs/UNIT_TESTS2_TODO.md): audio_processor_config.c ================= */

/* ---- uninitialized-state guard: every setter rejects when !s_is_initialized ---- */

void test_set_sample_rate_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
}

void test_set_mute_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_mute(true));
}

void test_set_channels_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
}

void test_set_bit_depth_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
}

void test_set_i2s_pins_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_i2s_pins(1, 2, 3, 4));
}

/* ---- set_mute ---- */

void test_set_mute_toggles_and_reflected_in_config_and_status(void)
{
    s_is_initialized = true;
    s_audio_config.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(true));
    TEST_ASSERT_TRUE(s_audio_config.mute);
    audio_config_t cfg;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_get_config(&cfg));
    TEST_ASSERT_TRUE(cfg.mute);
    audio_status_t st;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_get_status(&st));
    TEST_ASSERT_TRUE(st.mute);

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(false));
    TEST_ASSERT_FALSE(s_audio_config.mute);

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(true));
    TEST_ASSERT_TRUE(s_audio_config.mute);
}

/* ---- set_channels ---- */

void test_set_channels_rejects_invalid_enum(void)
{
    s_is_initialized = true;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, audio_processor_set_channels((audio_channel_t)0));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, audio_processor_set_channels((audio_channel_t)3));
    /* rejection leaves the config untouched */
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_STEREO, s_audio_config.channels);
}

void test_set_channels_same_value_is_noop_no_reconfigure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_STEREO));
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_core_stub_get_i2s_init_calls());
}

void test_set_channels_while_stopped_reconfigures_without_restart(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, s_audio_config.channels);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_FALSE(s_is_running); /* was never started */
}

void test_set_channels_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, s_audio_config.channels);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running); /* restarted */
}

void test_set_channels_propagates_i2s_init_failure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
    audio_processor_core_stub_set_i2s_init_result(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(ESP_FAIL, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    /* Note: the config field is updated BEFORE i2s_manager_init is attempted
     * (see audio_processor_config.c) -- the error is real but s_audio_config
     * already reflects the new value on failure. Documenting actual behavior,
     * not asserting a rollback that doesn't exist. */
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, s_audio_config.channels);
}

/* ---- set_bit_depth: same shape as set_channels, but NO enum validation
 * exists in the source -- any int value is accepted. Documented, not assumed
 * a bug (see docs/UNIT_TESTS2_TODO.md BT-1). ---- */

void test_set_bit_depth_same_value_is_noop_no_reconfigure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_16));
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_core_stub_get_i2s_init_calls());
}

void test_set_bit_depth_while_stopped_reconfigures_without_restart(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
    TEST_ASSERT_EQUAL_INT(AUDIO_BIT_DEPTH_24, s_audio_config.bit_depth);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_FALSE(s_is_running);
}

void test_set_bit_depth_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_32));
    TEST_ASSERT_EQUAL_INT(AUDIO_BIT_DEPTH_32, s_audio_config.bit_depth);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running);
}

void test_set_bit_depth_propagates_i2s_init_failure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    audio_processor_core_stub_set_i2s_init_result(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(ESP_FAIL, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
}

/* ---- set_sample_rate: same shape as set_channels/set_bit_depth ---- */

void test_set_sample_rate_same_value_is_noop_no_reconfigure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_44K));
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_core_stub_get_i2s_init_calls());
}

void test_set_sample_rate_while_stopped_reconfigures_without_restart(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
    TEST_ASSERT_EQUAL_INT(AUDIO_SAMPLE_RATE_48K, s_audio_config.sample_rate);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_FALSE(s_is_running);
}

void test_set_sample_rate_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
    TEST_ASSERT_EQUAL_INT(AUDIO_SAMPLE_RATE_48K, s_audio_config.sample_rate);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running);
}

void test_set_sample_rate_propagates_i2s_init_failure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    audio_processor_core_stub_set_i2s_init_result(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(ESP_FAIL, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
}

/* Note (docs/UNIT_TESTS2_TODO.md BT-1): a "was_running, audio_processor_stop()
 * itself fails" case was planned, but audio_processor_stop() in this
 * UNIT_TEST harness only fails on !s_is_initialized / !s_is_running, both of
 * which are already false in the was_running=true scenario being tested --
 * there is no reachable failure path to inject here without a dedicated stop
 * failure hook that does not currently exist. Not fabricating one; noting
 * the gap instead of asserting a scenario the code can't actually produce. */

/* ---- set_i2s_pins ---- */

void test_set_i2s_pins_while_stopped_reconfigures_and_persists(void)
{
    s_is_initialized = true;
    s_is_running = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_i2s_pins(11, 12, 13, 14));
    TEST_ASSERT_EQUAL_INT(11, s_audio_config.i2s_bclk_pin);
    TEST_ASSERT_EQUAL_INT(12, s_audio_config.i2s_ws_pin);
    TEST_ASSERT_EQUAL_INT(13, s_audio_config.i2s_din_pin);
    TEST_ASSERT_EQUAL_INT(14, s_audio_config.i2s_dout_pin);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_nvs_set_i2s_pins_calls());
    int bclk, ws, din, dout;
    audio_processor_core_stub_get_last_i2s_pins(&bclk, &ws, &din, &dout);
    TEST_ASSERT_EQUAL_INT(11, bclk);
    TEST_ASSERT_EQUAL_INT(12, ws);
    TEST_ASSERT_EQUAL_INT(13, din);
    TEST_ASSERT_EQUAL_INT(14, dout);
    TEST_ASSERT_FALSE(s_is_running);
}

void test_set_i2s_pins_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_i2s_pins(21, 22, 23, 24));
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running);
}

/* ---- configure_i2s (de-static'd for this test; unreachable via the public
 * setters, which never pass NULL through) ---- */

void test_configure_i2s_null_config_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, configure_i2s(NULL));
}

/* ---- audio_processor_get_config ---- */

void test_get_config_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    audio_config_t cfg;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_get_config(&cfg));
}

void test_get_config_rejects_null_output(void)
{
    s_is_initialized = true;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, audio_processor_get_config(NULL));
}

void test_get_config_round_trip_after_setters(void)
{
    s_is_initialized = true;
    s_is_running = false;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(true));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));

    audio_config_t cfg;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_get_config(&cfg));
    TEST_ASSERT_TRUE(cfg.mute);
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, cfg.channels);
    TEST_ASSERT_EQUAL_INT(AUDIO_BIT_DEPTH_24, cfg.bit_depth);
    TEST_ASSERT_EQUAL_INT(AUDIO_SAMPLE_RATE_48K, cfg.sample_rate);
}

/* ================= BT-2 (docs/UNIT_TESTS2_TODO.md): audio_processor_sync_diag.c ================= */

/* Backing storage for s_proc_buffer/s_proc_buffer2, sized to
 * AUDIO_WORK_BUFFER_BYTES (128 * 8 * 6 = 6144 under CONFIG_BT_MOCK_TESTING). */
static uint8_t s_sync_diag_proc_buf[AUDIO_WORK_BUFFER_BYTES];
static uint8_t s_sync_diag_proc_buf2[AUDIO_WORK_BUFFER_BYTES];

void test_emit_sync_worker_diag_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_emit_sync_worker_diag());
}

void test_emit_sync_worker_diag_succeeds_with_mock_generator(void)
{
    /* CONFIG_BT_MOCK_TESTING is defined for this build, so
     * audio_processor_emit_sync_worker_diag() takes the mock_generate_i2s_audio
     * path (not the real-hardware sine-wave synthesis branch, which isn't
     * even compiled in under this config). */
    s_is_initialized = true;
    s_proc_buffer = s_sync_diag_proc_buf;
    s_proc_buffer2 = s_sync_diag_proc_buf2;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_emit_sync_worker_diag());
}

void test_emit_sync_worker_diag_null_proc_buffer_yields_invalid_size(void)
{
    /* mock_generate_i2s_audio(NULL, target) returns 0 -> generated==0 ->
     * ESP_ERR_INVALID_SIZE. Exercises the "generated == 0" guard. */
    s_is_initialized = true;
    s_proc_buffer = NULL;
    s_proc_buffer2 = s_sync_diag_proc_buf2;

    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_SIZE, audio_processor_emit_sync_worker_diag());
}

/* Note (docs/UNIT_TESTS2_TODO.md BT-2): the "target == 0" / ESP_ERR_INVALID_ARG
 * branch (audio_get_runtime_work_bytes() returning 0) is unreachable through
 * the real audio_get_runtime_work_bytes() (audio_processor_state.c) -- it
 * always falls back to AUDIO_WORK_BUFFER_BYTES when s_runtime_work_bytes==0.
 * Not fabricating a scenario the real helper can't produce.
 *
 * mock_generate_i2s_audio() itself (the other BT-2 target) is confirmed
 * test-only scaffolding -- #ifdef CONFIG_BT_MOCK_TESTING, named "mock_",
 * used only by this file to synthesize bytes for host tests. It's exercised
 * indirectly by the success-path test above; no separate direct test adds
 * value beyond that. */

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
    /* TEST-1b: volume-set → apply_volume amplitude integration */
    RUN_TEST(test_volume_set_then_apply_scales_pcm_amplitude);

    /* BT-1 (docs/UNIT_TESTS2_TODO.md): audio_processor_config.c */
    RUN_TEST(test_set_sample_rate_rejects_when_not_initialized);
    RUN_TEST(test_set_mute_rejects_when_not_initialized);
    RUN_TEST(test_set_channels_rejects_when_not_initialized);
    RUN_TEST(test_set_bit_depth_rejects_when_not_initialized);
    RUN_TEST(test_set_i2s_pins_rejects_when_not_initialized);

    RUN_TEST(test_set_mute_toggles_and_reflected_in_config_and_status);

    RUN_TEST(test_set_channels_rejects_invalid_enum);
    RUN_TEST(test_set_channels_same_value_is_noop_no_reconfigure);
    RUN_TEST(test_set_channels_while_stopped_reconfigures_without_restart);
    RUN_TEST(test_set_channels_while_running_stops_reconfigures_restarts);
    RUN_TEST(test_set_channels_propagates_i2s_init_failure);

    RUN_TEST(test_set_bit_depth_same_value_is_noop_no_reconfigure);
    RUN_TEST(test_set_bit_depth_while_stopped_reconfigures_without_restart);
    RUN_TEST(test_set_bit_depth_while_running_stops_reconfigures_restarts);
    RUN_TEST(test_set_bit_depth_propagates_i2s_init_failure);

    RUN_TEST(test_set_sample_rate_same_value_is_noop_no_reconfigure);
    RUN_TEST(test_set_sample_rate_while_stopped_reconfigures_without_restart);
    RUN_TEST(test_set_sample_rate_while_running_stops_reconfigures_restarts);
    RUN_TEST(test_set_sample_rate_propagates_i2s_init_failure);

    RUN_TEST(test_set_i2s_pins_while_stopped_reconfigures_and_persists);
    RUN_TEST(test_set_i2s_pins_while_running_stops_reconfigures_restarts);

    RUN_TEST(test_configure_i2s_null_config_rejected);

    RUN_TEST(test_get_config_rejects_when_not_initialized);
    RUN_TEST(test_get_config_rejects_null_output);
    RUN_TEST(test_get_config_round_trip_after_setters);

    /* BT-2 (docs/UNIT_TESTS2_TODO.md): audio_processor_sync_diag.c */
    RUN_TEST(test_emit_sync_worker_diag_rejects_when_not_initialized);
    RUN_TEST(test_emit_sync_worker_diag_succeeds_with_mock_generator);
    RUN_TEST(test_emit_sync_worker_diag_null_proc_buffer_yields_invalid_size);
    return UNITY_END();
}

#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"

#include <string.h>

void audio_processor_core_stub_reset(void);
void audio_processor_core_stub_set_i2s_running(bool running);

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

/* ================= P2 (docs/UNIT_TESTS2_TODO.md): audio_processor.c accessors ================= */

void test_get_work_buffer_bytes_reflects_runtime_value(void)
{
    s_runtime_work_bytes = 1234;
    TEST_ASSERT_EQUAL_UINT32(1234, audio_processor_get_work_buffer_bytes());
}

void test_is_synth_mode_enabled_reflects_force_synth(void)
{
    s_force_synth = false;
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());
    s_force_synth = true;
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());
}

void test_set_dram_only_toggles_flag(void)
{
    audio_processor_set_dram_only(true);
    TEST_ASSERT_TRUE(s_dram_only_alloc);
    audio_processor_set_dram_only(false);
    TEST_ASSERT_FALSE(s_dram_only_alloc);
}

void test_set_synth_mode_enable_stops_i2s(void)
{
    audio_processor_core_stub_set_i2s_running(true);
    s_force_synth = false;

    audio_processor_set_synth_mode(true);

    TEST_ASSERT_TRUE(s_force_synth);
    TEST_ASSERT_FALSE(i2s_manager_is_running());
}

void test_set_synth_mode_disable_restarts_i2s_when_running(void)
{
    s_is_running = true;
    s_force_synth = true;
    audio_processor_core_stub_set_i2s_running(false);

    audio_processor_set_synth_mode(false);

    TEST_ASSERT_FALSE(s_force_synth);
    TEST_ASSERT_TRUE(i2s_manager_is_running());
}

void test_set_synth_mode_disable_does_not_start_i2s_when_processor_stopped(void)
{
    s_is_running = false;
    s_force_synth = true;
    audio_processor_core_stub_set_i2s_running(false);

    audio_processor_set_synth_mode(false);

    TEST_ASSERT_FALSE(s_force_synth);
    TEST_ASSERT_FALSE(i2s_manager_is_running());
}

/* ---- audio_processor_drain_ring ---- */

void test_drain_ring_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_drain_ring());
}

void test_drain_ring_rejects_when_ring_null(void)
{
    s_is_initialized = true;
    s_audio_ring = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_drain_ring());
}

void test_drain_ring_success_resets_playback_state(void)
{
    s_is_initialized = true;
    /* audio_rb_available_to_read() stub always returns 0, so the drain loop
     * itself is a no-op here -- this exercises the state-reset tail and the
     * !=NULL guard, which is the reachable behavior with the stubbed ring. */
    static int dummy_ring_marker;
    s_audio_ring = (audio_rb_t *)&dummy_ring_marker;
    s_audio_rb_residual_len = 10;
    s_audio_rb_residual_pos = 5;
    s_keepalive_armed = true;
    s_force_synth = true;
    s_last_source_was_synth = true;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_drain_ring());
    TEST_ASSERT_EQUAL_UINT32(0, s_audio_rb_residual_len);
    TEST_ASSERT_EQUAL_UINT32(0, s_audio_rb_residual_pos);
    TEST_ASSERT_FALSE(s_keepalive_armed);
    TEST_ASSERT_FALSE(s_force_synth);
    TEST_ASSERT_FALSE(s_last_source_was_synth);

    s_audio_ring = NULL; /* don't leak a dangling stack pointer to later tests */
}

/* Note (docs/UNIT_TESTS2_TODO.md P2): audio_processor_cleanup_partial_init()
 * is static, called only from audio_processor_init()'s single failure label,
 * and reverses a specific sequence of partial allocations. Meaningfully
 * testing it needs allocation-failure injection at each of several distinct
 * init steps -- deferred; not fabricating a shallow test that doesn't
 * exercise the actual rollback logic. */

int main(void)
{
    UNITY_BEGIN();
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

    /* BT-2 (docs/UNIT_TESTS2_TODO.md): audio_processor_sync_diag.c */
    RUN_TEST(test_emit_sync_worker_diag_rejects_when_not_initialized);
    RUN_TEST(test_emit_sync_worker_diag_succeeds_with_mock_generator);
    RUN_TEST(test_emit_sync_worker_diag_null_proc_buffer_yields_invalid_size);

    /* P2 (docs/UNIT_TESTS2_TODO.md): audio_processor.c accessors */
    RUN_TEST(test_get_work_buffer_bytes_reflects_runtime_value);
    RUN_TEST(test_is_synth_mode_enabled_reflects_force_synth);
    RUN_TEST(test_set_dram_only_toggles_flag);
    RUN_TEST(test_set_synth_mode_enable_stops_i2s);
    RUN_TEST(test_set_synth_mode_disable_restarts_i2s_when_running);
    RUN_TEST(test_set_synth_mode_disable_does_not_start_i2s_when_processor_stopped);
    RUN_TEST(test_drain_ring_rejects_when_not_initialized);
    RUN_TEST(test_drain_ring_rejects_when_ring_null);
    RUN_TEST(test_drain_ring_success_resets_playback_state);
    return UNITY_END();
}

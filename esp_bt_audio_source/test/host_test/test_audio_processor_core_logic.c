#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"

void audio_processor_core_stub_set_i2s_running(bool running);
void audio_processor_core_stub_set_beep_active(bool active);
void audio_processor_core_stub_set_i2s_fill_bytes(size_t bytes);
void audio_processor_core_stub_set_synth_fill_bytes(size_t bytes);
void audio_processor_core_stub_reset(void);
void audio_processor_core_stub_set_nvs_set_volume_result(esp_err_t result);
uint32_t audio_processor_core_stub_get_nvs_set_volume_calls(void);
uint8_t audio_processor_core_stub_get_last_nvs_set_volume_value(void);
void audio_processor_core_stub_set_uart_active(bool active);
void audio_processor_core_stub_set_uart_fill_bytes(size_t bytes);

void setUp(void)
{
    audio_processor_core_stub_reset();
    audio_processor_test_reset_core_logic_state();
    s_is_initialized = true;
    s_force_synth = false;
    s_beep_remaining_bytes = 0;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
}

void tearDown(void)
{
}

void test_get_active_source_should_prioritize_beep_over_synth_and_i2s(void)
{
    s_force_synth = true;
    s_beep_remaining_bytes = 128;
    audio_processor_core_stub_set_i2s_running(true);
    audio_processor_core_stub_set_beep_active(true);

    int source = audio_processor_test_get_active_source_id();

    /* SILENCE == 3 since AUDIO_SOURCE_UART was inserted at index 2 */
    TEST_ASSERT_EQUAL_INT(3, source);
}

void test_get_active_source_should_prioritize_synth_over_i2s_when_no_beep(void)
{
    s_force_synth = true;
    s_beep_remaining_bytes = 0;
    audio_processor_core_stub_set_beep_active(false);
    audio_processor_core_stub_set_i2s_running(true);

    int source = audio_processor_test_get_active_source_id();

    TEST_ASSERT_EQUAL_INT(1, source);
}

void test_produce_audio_chunk_should_track_source_switch_count_and_bytes_by_source(void)
{
    uint8_t out[64] = {0};

    s_force_synth = false;
    audio_processor_core_stub_set_beep_active(false);
    audio_processor_core_stub_set_i2s_running(true);
    audio_processor_core_stub_set_i2s_fill_bytes(sizeof(out));
    audio_processor_core_stub_set_synth_fill_bytes(sizeof(out));

    size_t produced_i2s = audio_processor_test_produce_audio_chunk(out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(out), produced_i2s);

    s_force_synth = true;
    size_t produced_synth = audio_processor_test_produce_audio_chunk(out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(out), produced_synth);

    audio_stats_t stats = {0};
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(2, stats.source_switch_count);
    TEST_ASSERT_EQUAL_UINT(sizeof(out), stats.bytes_by_source[0]);
    TEST_ASSERT_EQUAL_UINT(sizeof(out), stats.bytes_by_source[1]);
}

void test_watermark_hysteresis_should_pause_at_high_and_resume_at_low(void)
{
    uint32_t pause_transitions = 0;

    bool paused = audio_processor_test_compute_engine_paused(false, AUDIO_RB_HIGH_WATERMARK, &pause_transitions);
    TEST_ASSERT_TRUE(paused);
    TEST_ASSERT_EQUAL_UINT32(1, pause_transitions);

    pause_transitions = 0;
    paused = audio_processor_test_compute_engine_paused(paused, AUDIO_RB_LOW_WATERMARK + 1, &pause_transitions);
    TEST_ASSERT_TRUE(paused);
    TEST_ASSERT_EQUAL_UINT32(0, pause_transitions);

    pause_transitions = 0;
    paused = audio_processor_test_compute_engine_paused(paused, AUDIO_RB_LOW_WATERMARK, &pause_transitions);
    TEST_ASSERT_FALSE(paused);
    TEST_ASSERT_EQUAL_UINT32(0, pause_transitions);
}

void test_ring_edge_conditions_should_gate_chunk_production(void)
{
    TEST_ASSERT_FALSE(audio_processor_test_should_produce_chunk(false, 0));
    TEST_ASSERT_FALSE(audio_processor_test_should_produce_chunk(false, AUDIO_ENGINE_CHUNK_BYTES - 1));
    TEST_ASSERT_TRUE(audio_processor_test_should_produce_chunk(false, AUDIO_ENGINE_CHUNK_BYTES));
    TEST_ASSERT_FALSE(audio_processor_test_should_produce_chunk(true, AUDIO_ENGINE_CHUNK_BYTES * 2));
}

void test_volume_commit_should_propagate_nvs_failure_in_test_hook(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_volume(77));
    audio_processor_core_stub_set_nvs_set_volume_result(ESP_FAIL);

    esp_err_t ret = audio_processor_test_commit_volume_now();

    TEST_ASSERT_EQUAL(ESP_FAIL, ret);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_nvs_set_volume_calls());
    TEST_ASSERT_EQUAL_UINT8(77, audio_processor_core_stub_get_last_nvs_set_volume_value());
}

void test_produce_audio_chunk_should_handle_beep_overlay_failure_without_overlay_stats(void)
{
    uint8_t out[32] = {0};

    s_force_synth = false;
    s_beep_remaining_bytes = sizeof(out);
    audio_processor_core_stub_set_i2s_running(true);
    audio_processor_core_stub_set_i2s_fill_bytes(sizeof(out));
    audio_processor_core_stub_set_beep_active(true);
    audio_processor_test_set_force_beep_overlay_fail(true);

    size_t produced = audio_processor_test_produce_audio_chunk(out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT(sizeof(out), produced);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_UINT(sizeof(out), s_beep_remaining_bytes);

    audio_stats_t stats = {0};
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(0, stats.beep_overlay_count);
    TEST_ASSERT_EQUAL_UINT64(0, stats.beep_overlay_bytes);
}

void test_audio_volume_set_reflects_in_volume_gain_state(void)
{
    /* Verify audio_processor_set_volume correctly updates the internal gain state.
     * apply_volume() (in audio_processor_diag.c) is tested for amplitude scaling
     * in test_audio_processor_diag.c; here we confirm only the state transition. */
    audio_processor_set_volume(100);
    TEST_ASSERT_EQUAL_UINT8(100, s_volume_gain);

    audio_processor_set_volume(50);
    TEST_ASSERT_EQUAL_UINT8(50, s_volume_gain);

    audio_processor_set_volume(0);
    TEST_ASSERT_EQUAL_UINT8(0, s_volume_gain);

    /* Clamping: values > 100 must be clamped to 100. */
    audio_processor_set_volume(200);
    TEST_ASSERT_EQUAL_UINT8(100, s_volume_gain);
}

/* ── UARTAUDIO step 4: AUDIO_SOURCE_UART priority + dispatch ────────────── */

void test_get_active_source_uart_beats_synth_and_i2s(void)
{
    /* Starting a UART stream is the most recent explicit intent, so an
     * active stream outranks forced SYNTH; SYNTH ON must not interrupt it. */
    s_force_synth = true;
    audio_processor_core_stub_set_beep_active(false);
    audio_processor_core_stub_set_i2s_running(true);
    audio_processor_core_stub_set_uart_active(true);

    /* enum order: I2S=0, SYNTH=1, UART=2, SILENCE=3 */
    TEST_ASSERT_EQUAL_INT(2, audio_processor_test_get_active_source_id());
}

void test_get_active_source_beep_beats_uart(void)
{
    s_force_synth = false;
    audio_processor_core_stub_set_beep_active(true);
    audio_processor_core_stub_set_uart_active(true);

    /* beep plays over silence, never mixed with the UART stream */
    TEST_ASSERT_EQUAL_INT(3, audio_processor_test_get_active_source_id());
}

void test_get_active_source_falls_back_when_uart_inactive(void)
{
    s_force_synth = false;
    audio_processor_core_stub_set_beep_active(false);
    audio_processor_core_stub_set_uart_active(false);
    audio_processor_core_stub_set_i2s_running(true);

    TEST_ASSERT_EQUAL_INT(0, audio_processor_test_get_active_source_id());
}

void test_produce_audio_chunk_dispatches_to_uart_fill(void)
{
    uint8_t out[64] = {0};

    s_force_synth = false;
    audio_processor_core_stub_set_beep_active(false);
    audio_processor_core_stub_set_i2s_running(false);
    audio_processor_core_stub_set_uart_active(true);
    audio_processor_core_stub_set_uart_fill_bytes(sizeof(out));

    size_t produced = audio_processor_test_produce_audio_chunk(out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT(sizeof(out), produced);
    TEST_ASSERT_EQUAL_HEX8(0x44, out[0]); /* uart stub pattern */

    audio_stats_t stats = {0};
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(sizeof(out), stats.bytes_by_source[2]);
}

/* ── UT-10: idle-I2S synth fallback, engine-pause transitions, tag counters ── */

void test_idle_i2s_failures_forces_synth_after_threshold(void)
{
    bool synth_after = false;
    int failures_after = -1;
    /* failures over threshold, no beep, synth off → fallback engages: synth on,
     * failure counter reset to 0. */
    audio_processor_test_idle_i2s_failures(I2S_FAILURE_THRESHOLD + 5, false, 0,
                                           &synth_after, &failures_after);
    TEST_ASSERT_TRUE(synth_after);
    TEST_ASSERT_EQUAL_INT(0, failures_after);
}

void test_idle_i2s_failures_no_fallback_when_beep_active(void)
{
    bool synth_after = true;
    int failures_after = -1;
    /* beep pending → fallback suppressed; state left as-is. */
    audio_processor_test_idle_i2s_failures(I2S_FAILURE_THRESHOLD + 5, false, 128,
                                           &synth_after, &failures_after);
    TEST_ASSERT_FALSE(synth_after);
    TEST_ASSERT_EQUAL_INT(I2S_FAILURE_THRESHOLD + 5, failures_after);
}

void test_idle_i2s_failures_no_fallback_below_threshold(void)
{
    bool synth_after = true;
    int failures_after = -1;
    audio_processor_test_idle_i2s_failures(3, false, 0, &synth_after, &failures_after);
    TEST_ASSERT_FALSE(synth_after);
    TEST_ASSERT_EQUAL_INT(3, failures_after);
}

void test_idle_i2s_failures_no_change_when_synth_already_on(void)
{
    bool synth_after = false;
    int failures_after = -1;
    /* synth already enabled → condition (!s_force_synth) false, no reset. */
    audio_processor_test_idle_i2s_failures(I2S_FAILURE_THRESHOLD + 5, true, 0,
                                           &synth_after, &failures_after);
    TEST_ASSERT_TRUE(synth_after);
    TEST_ASSERT_EQUAL_INT(I2S_FAILURE_THRESHOLD + 5, failures_after);
}

void test_compute_engine_paused_transition_and_midrange(void)
{
    uint32_t trans = 99;
    /* cross into pause from unpaused → transition counted once */
    TEST_ASSERT_TRUE(audio_processor_test_compute_engine_paused(false, AUDIO_RB_HIGH_WATERMARK, &trans));
    TEST_ASSERT_EQUAL_UINT32(1, trans);

    /* already paused above high → still paused, no new transition */
    trans = 99;
    TEST_ASSERT_TRUE(audio_processor_test_compute_engine_paused(true, AUDIO_RB_HIGH_WATERMARK, &trans));
    TEST_ASSERT_EQUAL_UINT32(0, trans);

    /* below low → resume regardless of prior state */
    TEST_ASSERT_FALSE(audio_processor_test_compute_engine_paused(true, AUDIO_RB_LOW_WATERMARK, NULL));

    /* mid-range holds the previous state (hysteresis) */
    const size_t mid = (AUDIO_RB_LOW_WATERMARK + AUDIO_RB_HIGH_WATERMARK) / 2;
    TEST_ASSERT_TRUE(audio_processor_test_compute_engine_paused(true, mid, NULL));
    TEST_ASSERT_FALSE(audio_processor_test_compute_engine_paused(false, mid, NULL));
}

void test_should_produce_chunk_boundary(void)
{
    TEST_ASSERT_FALSE(audio_processor_test_should_produce_chunk(true, AUDIO_ENGINE_CHUNK_BYTES * 4)); /* paused */
    TEST_ASSERT_TRUE(audio_processor_test_should_produce_chunk(false, AUDIO_ENGINE_CHUNK_BYTES));     /* exactly enough */
    TEST_ASSERT_FALSE(audio_processor_test_should_produce_chunk(false, AUDIO_ENGINE_CHUNK_BYTES - 1)); /* just short */
}

void test_tag_miss_count_reset(void)
{
    audio_processor_test_reset_tag_miss_count();
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_get_active_source_should_prioritize_beep_over_synth_and_i2s);
    RUN_TEST(test_get_active_source_should_prioritize_synth_over_i2s_when_no_beep);
    RUN_TEST(test_produce_audio_chunk_should_track_source_switch_count_and_bytes_by_source);
    RUN_TEST(test_watermark_hysteresis_should_pause_at_high_and_resume_at_low);
    RUN_TEST(test_ring_edge_conditions_should_gate_chunk_production);
    RUN_TEST(test_volume_commit_should_propagate_nvs_failure_in_test_hook);
    RUN_TEST(test_produce_audio_chunk_should_handle_beep_overlay_failure_without_overlay_stats);
    RUN_TEST(test_audio_volume_set_reflects_in_volume_gain_state);
    RUN_TEST(test_get_active_source_uart_beats_synth_and_i2s);
    RUN_TEST(test_get_active_source_beep_beats_uart);
    RUN_TEST(test_get_active_source_falls_back_when_uart_inactive);
    RUN_TEST(test_produce_audio_chunk_dispatches_to_uart_fill);
    RUN_TEST(test_idle_i2s_failures_forces_synth_after_threshold);
    RUN_TEST(test_idle_i2s_failures_no_fallback_when_beep_active);
    RUN_TEST(test_idle_i2s_failures_no_fallback_below_threshold);
    RUN_TEST(test_idle_i2s_failures_no_change_when_synth_already_on);
    RUN_TEST(test_compute_engine_paused_transition_and_midrange);
    RUN_TEST(test_should_produce_chunk_boundary);
    RUN_TEST(test_tag_miss_count_reset);
    return UNITY_END();
}

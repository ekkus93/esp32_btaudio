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

    TEST_ASSERT_EQUAL_INT(2, source);
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
    return UNITY_END();
}

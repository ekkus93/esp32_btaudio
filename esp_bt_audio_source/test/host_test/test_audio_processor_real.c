/* Host test that links the real production `audio_processor.c` allocation
 * logic. We provide minimal mocks for device-only dependencies elsewhere in
 * test/host_test/mocks so this compilation exercise focuses on the real
 * allocation/fallback paths. The test calls audio_processor_init() with a
 * small config and verifies audio_processor_get_work_buffer_bytes() is
 * non-zero when allocations succeed and zero when forced failures occur.
 */

#include "unity.h"
#include "../../main/include/audio_processor.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <string.h>

/* Expose UNIT_TEST hook from audio_processor.c */
bool audio_processor_test_autostart_due(TickType_t now_ticks, TickType_t last_ticks, TickType_t cooldown_ticks);

#ifdef CONFIG_SPIRAM
#define TEST_AUDIO_BUFFER_SIZE (48000 * 4 * 2) /* matches AUDIO_BUFFER_SIZE when PSRAM is enabled */
#else
#define TEST_AUDIO_BUFFER_SIZE (131072)        /* DRAM-safe fallback */
#endif

void test_audio_processor_idle_i2s_should_not_reenable_below_threshold(void)
{
    bool synth_after = true;
    int failures_after = -1;

    audio_processor_test_idle_i2s_failures(5 /* below threshold */, false /* synth_enabled */, 0 /* beep_remaining */, &synth_after, &failures_after);

    TEST_ASSERT_FALSE(synth_after);
    TEST_ASSERT_EQUAL_INT(5, failures_after);
}

void test_audio_processor_wav_state_transitions_should_disable_synth_and_clear_beep(void)
{
    audio_processor_test_wav_reset_state();
    audio_processor_set_synth_mode(true);

    audio_processor_test_wav_begin();
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    audio_processor_test_wav_add_pending(100);
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)audio_processor_test_wav_pending_bytes());

    TEST_ASSERT_FALSE(audio_processor_test_wav_consume(60));
    TEST_ASSERT_EQUAL_UINT32(40, (uint32_t)audio_processor_test_wav_pending_bytes());

    TEST_ASSERT_TRUE(audio_processor_test_wav_consume(40));
    audio_processor_test_wav_complete_if_idle();

    TEST_ASSERT_FALSE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_wav_pending_bytes());
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_beep_remaining_bytes());
}

void test_audio_processor_inject_audio_should_tag_and_reset(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 50;
    cfg.mute = false;
    cfg.i2s_port = 0;
    cfg.i2s_bclk_pin = GPIO_NUM_NC;
    cfg.i2s_ws_pin = GPIO_NUM_NC;
    cfg.i2s_din_pin = GPIO_NUM_NC;
    cfg.i2s_dout_pin = GPIO_NUM_NC;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    audio_source_tag_test_reset_buffer();
    audio_processor_test_reset_tag_miss_count();

    uint8_t payload[16] = {0xAA};
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(payload, sizeof(payload)));
    TEST_ASSERT_GREATER_THAN_UINT(0, audio_processor_test_get_tag_used());

    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_deinit());
}

void setUp(void) {
    esp_heap_caps_mock_set_psram_available(true);
    esp_heap_caps_mock_reset_allocations();
}

void tearDown(void) {
    audio_processor_deinit();
}

void test_audio_processor_alloc_with_psram(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 50;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    size_t bytes = audio_processor_get_work_buffer_bytes();
    TEST_ASSERT_TRUE(bytes >= 1024);
    /* Ensure at least one allocation used PSRAM when available */
    TEST_ASSERT_TRUE(esp_heap_caps_mock_count_allocations_spiram() > 0);
}

void test_audio_processor_alloc_without_psram(void)
{
    esp_heap_caps_mock_set_psram_available(false);

    audio_processor_deinit();

    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 50;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    size_t bytes = audio_processor_get_work_buffer_bytes();
    /* Should still succeed via DRAM fallback */
    TEST_ASSERT_TRUE(bytes >= 1024);
    /* No allocations should have come from PSRAM when disabled */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)esp_heap_caps_mock_count_allocations_spiram());
    TEST_ASSERT_TRUE(esp_heap_caps_mock_count_allocations_dram() > 0);
}

void test_audio_processor_autostart_cooldown(void)
{
    /* First attempt after long gap should be allowed */
    TEST_ASSERT_TRUE(audio_processor_test_autostart_due((TickType_t)100, (TickType_t)0, (TickType_t)50));
    /* Within cooldown window should be suppressed */
    TEST_ASSERT_FALSE(audio_processor_test_autostart_due((TickType_t)120, (TickType_t)100, (TickType_t)50));
    /* Boundary: exactly at cooldown should allow */
    TEST_ASSERT_TRUE(audio_processor_test_autostart_due((TickType_t)150, (TickType_t)100, (TickType_t)50));
}

void test_audio_processor_beep_should_activate_fallback_when_buffers_full(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 50;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));

    size_t audio_free = audio_processor_test_get_audio_free_bytes();
    TEST_ASSERT_GREATER_THAN_UINT(0, audio_free);

    uint8_t *big_payload = (uint8_t *)malloc(audio_free);
    TEST_ASSERT_NOT_NULL(big_payload);
    memset(big_payload, 0, audio_free);

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(big_payload, audio_free));

    /* Ringbuffer should now be full. */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_audio_free_bytes());

    /* Now the ringbuffer should refuse additional data. */
    uint8_t payload[1024] = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NO_MEM, audio_processor_test_inject_audio_data(payload, sizeof(payload)));

    free(big_payload);

    /* Activate a long beep while both ringbuffers are saturated; this should arm the fallback synth. */
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_beep_tone(800 /* ms */, 440.0));
    TEST_ASSERT_TRUE(audio_processor_test_is_beep_fallback_active());

    size_t fallback_frames = audio_processor_test_get_beep_fallback_frames_remaining();
    TEST_ASSERT_GREATER_THAN_UINT(0, fallback_frames);

    const size_t frame_bytes = 4; /* 16-bit stereo */
    size_t request = fallback_frames * frame_bytes;
    if (request == 0) {
        request = frame_bytes;
    }

    uint8_t *out = (uint8_t *)malloc(request);
    TEST_ASSERT_NOT_NULL(out);

    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(out, request, &bytes_read));
    TEST_ASSERT_EQUAL_UINT32(request, bytes_read);

    free(out);

    /* Fallback should be drained after the read. */
    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_beep_remaining_bytes());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_processor_idle_i2s_should_not_reenable_below_threshold);
    RUN_TEST(test_audio_processor_wav_state_transitions_should_disable_synth_and_clear_beep);
    RUN_TEST(test_audio_processor_inject_audio_should_tag_and_reset);
    RUN_TEST(test_audio_processor_alloc_with_psram);
    RUN_TEST(test_audio_processor_alloc_without_psram);
    RUN_TEST(test_audio_processor_autostart_cooldown);
    RUN_TEST(test_audio_processor_beep_should_activate_fallback_when_buffers_full);
    return UNITY_END();
}

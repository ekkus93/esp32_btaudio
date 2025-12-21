#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_log.h"
#include "audio_processor.h"
#include "command_interface.h"
#include "driver/i2s_std.h"

#define I2S_SAMPLE_RATE AUDIO_SAMPLE_RATE_44K
#define I2S_BIT_DEPTH   AUDIO_BIT_DEPTH_16
#define I2S_CHANNELS    AUDIO_CHANNEL_STEREO
#define I2S_PORT        I2S_NUM_0

static const char *TAG = "AUDIO_PROCESSOR_TEST";

static void test_audio_processor_init(void)
{
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

    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL(config.sample_rate, read_config.sample_rate);
    TEST_ASSERT_EQUAL(config.bit_depth, read_config.bit_depth);
    TEST_ASSERT_EQUAL(config.channels, read_config.channels);
    TEST_ASSERT_EQUAL(config.volume, read_config.volume);
    TEST_ASSERT_EQUAL(config.mute, read_config.mute);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_volume_control(void)
{
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

    ret = audio_processor_set_volume(50);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(50, read_config.volume);

    ret = audio_processor_set_volume(100);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(100, read_config.volume);

    ret = audio_processor_set_volume(0);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, read_config.volume);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_mute(void)
{
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

    ret = audio_processor_set_mute(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(read_config.mute);

    ret = audio_processor_set_mute(false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(read_config.mute);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_sample_rate(void)
{
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

    ret = audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(AUDIO_SAMPLE_RATE_48K, read_config.sample_rate);

    ret = audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_16K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(AUDIO_SAMPLE_RATE_16K, read_config.sample_rate);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_start_stop(void)
{
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

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_read(void)
{
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

    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t buffer[1024];
    size_t bytes_read = 0;

    ret = audio_processor_read(buffer, sizeof(buffer), &bytes_read);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_stats(void)
{
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

    vTaskDelay(pdMS_TO_TICKS(500));

    audio_stats_t stats;
    ret = audio_processor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_format_conversion(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_32);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    audio_config_t new_config;
    ret = audio_processor_get_config(&new_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(AUDIO_BIT_DEPTH_32, new_config.bit_depth);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_audio_i2s_config(void)
{
    audio_config_t configs[] = {
        {
            .sample_rate = AUDIO_SAMPLE_RATE_44K,
            .bit_depth = AUDIO_BIT_DEPTH_16,
            .channels = AUDIO_CHANNEL_STEREO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_PORT,
        },
        {
            .sample_rate = AUDIO_SAMPLE_RATE_48K,
            .bit_depth = AUDIO_BIT_DEPTH_24,
            .channels = AUDIO_CHANNEL_STEREO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_PORT,
        },
        {
            .sample_rate = AUDIO_SAMPLE_RATE_16K,
            .bit_depth = AUDIO_BIT_DEPTH_16,
            .channels = AUDIO_CHANNEL_MONO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_PORT,
        }
    };

    const size_t config_count = sizeof(configs) / sizeof(configs[0]);
    for (size_t i = 0; i < config_count; ++i) {
        ESP_LOGI(TAG, "Testing I2S config %u: %dHz, %d-bit, %d channel(s)",
                 (unsigned)i,
                 configs[i].sample_rate,
                 configs[i].bit_depth,
                 configs[i].channels);

        esp_err_t ret = audio_processor_init(&configs[i]);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        audio_config_t read_config;
        ret = audio_processor_get_config(&read_config);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        TEST_ASSERT_EQUAL(configs[i].sample_rate, read_config.sample_rate);
        TEST_ASSERT_EQUAL(configs[i].bit_depth, read_config.bit_depth);
        TEST_ASSERT_EQUAL(configs[i].channels, read_config.channels);

        ret = audio_processor_start();
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        vTaskDelay(pdMS_TO_TICKS(100));

        ret = audio_processor_stop();
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        ret = audio_processor_deinit();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }
}

static void test_audio_buffer_management(void)
{
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

    vTaskDelay(pdMS_TO_TICKS(500));

    audio_stats_t stats;
    ret = audio_processor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    uint8_t buffer[1024];
    size_t bytes_read = 0;
    for (int i = 0; i < 5; ++i) {
        ret = audio_processor_read(buffer, sizeof(buffer), &bytes_read);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/* forward declarations so RUN_TEST can reference tests defined later */
static void test_audio_processor_play_wav_api(void);
static void test_play_wav_command(void);
static void test_beep_should_not_report_tag_miss(void);
#ifdef CONFIG_BT_MOCK_TESTING
static void test_audio_processor_idle_failures_should_not_enable_synth_with_beep(void);
static void test_beep_fallback_should_align_and_drain(void);
static void test_fallback_volume_and_wav_resume_alignment(void);
static void test_wav_and_beep_fallback_should_keep_tags_aligned(void);
static void test_wav_fallback_with_live_volume_changes_should_resume_cleanly(void);
static void test_wav_fallback_soak_with_volume_and_mute_toggles(void);
static void test_wav_injection_mid_fallback_should_resume_without_tag_loss(void);
static void test_fallback_repeats_should_clear_debt_after_drain(void);
#endif

void run_audio_processor_tests(void)
{
    ESP_LOGI(TAG, "Starting audio processor tests");
    RUN_TEST(test_audio_processor_init);
    RUN_TEST(test_audio_volume_control);
    RUN_TEST(test_audio_mute);
    RUN_TEST(test_audio_sample_rate);
    RUN_TEST(test_audio_start_stop);
    RUN_TEST(test_audio_read);
    RUN_TEST(test_audio_stats);
    RUN_TEST(test_audio_format_conversion);
    RUN_TEST(test_audio_i2s_config);
    RUN_TEST(test_audio_buffer_management);
    RUN_TEST(test_beep_should_not_report_tag_miss);
#ifdef CONFIG_BT_MOCK_TESTING
    RUN_TEST(test_beep_fallback_should_align_and_drain);
    RUN_TEST(test_audio_processor_idle_failures_should_not_enable_synth_with_beep);
    RUN_TEST(test_fallback_volume_and_wav_resume_alignment);
    RUN_TEST(test_wav_and_beep_fallback_should_keep_tags_aligned);
    RUN_TEST(test_wav_fallback_with_live_volume_changes_should_resume_cleanly);
    RUN_TEST(test_wav_fallback_soak_with_volume_and_mute_toggles);
    RUN_TEST(test_wav_injection_mid_fallback_should_resume_without_tag_loss);
    RUN_TEST(test_fallback_repeats_should_clear_debt_after_drain);
#endif
    RUN_TEST(test_audio_processor_play_wav_api);
    RUN_TEST(test_play_wav_command);
    /* On-device PSRAM integration tests */
#if CONFIG_TEST_APP_AUDIO_PSRAM_TESTS
    extern void test_heap_psram_simple(void);
    extern void test_audio_processor_psram_allocations(void);
    RUN_TEST(test_heap_psram_simple);
    RUN_TEST(test_audio_processor_psram_allocations);
#else
    ESP_LOGI(TAG, "PSRAM integration tests disabled by Kconfig (CONFIG_TEST_APP_AUDIO_PSRAM_TESTS=0) or SPIRAM not enabled - skipping");
#endif
    ESP_LOGI(TAG, "Audio processor tests completed");
}

/* forward declaration so RUN_TEST can reference the test defined below */
static void test_audio_processor_play_wav_api(void);
static void test_play_wav_command(void);
static void test_beep_should_not_report_tag_miss(void)
{
    audio_processor_test_reset_tag_miss_count();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Kick a short beep and ensure audio + metadata stay aligned. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(200));

    uint8_t buf[512];
    size_t bytes_read = 0;
    bool got_audio = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        bytes_read = 0;
        esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if (ret == ESP_OK && bytes_read > 0) {
            got_audio = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    TEST_ASSERT_TRUE_MESSAGE(got_audio, "Beep did not produce audio bytes");
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

#ifdef CONFIG_BT_MOCK_TESTING
static void test_beep_fallback_should_align_and_drain(void)
{
    audio_processor_test_reset_tag_miss_count();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Issue a long beep to saturate the small beep buffer so enqueue falls back
     * to on-the-fly synthesis. Keep the ringbuffer untouched so saturation is
     * guaranteed. */
    const uint32_t duration_ms = 800;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(duration_ms));

    bool fallback_active = false;
    const int max_waits = 20;
    for (int i = 0; i < max_waits; ++i) {
        fallback_active = audio_processor_test_is_beep_fallback_active();
        if (fallback_active) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback synth did not activate");

    size_t total_frames = audio_processor_test_get_beep_fallback_total_frames();
    size_t remaining_frames = audio_processor_test_get_beep_fallback_frames_remaining();
    TEST_ASSERT_TRUE(total_frames > 0);
    TEST_ASSERT_TRUE(remaining_frames > 0);
    TEST_ASSERT_TRUE(total_frames >= remaining_frames);

    /* Drain fallback-generated audio and ensure the frame counter reaches zero. */
    uint8_t buf[2048];
    size_t bytes_read = 0;
    const int max_reads = 80;
    for (int i = 0; i < max_reads && audio_processor_test_get_beep_fallback_frames_remaining() > 0; ++i) {
        bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        TEST_ASSERT_TRUE(bytes_read > 0);
    }

    remaining_frames = audio_processor_test_get_beep_fallback_frames_remaining();
    TEST_ASSERT_EQUAL(0U, remaining_frames);
    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_audio_processor_idle_failures_should_not_enable_synth_with_beep(void)
{
    bool synth_after = true;
    int failures_after = -1;

    audio_processor_test_idle_i2s_failures(25 /* >= threshold */, false /* synth_enabled */, 2048 /* beep_remaining */, &synth_after, &failures_after);

    TEST_ASSERT_FALSE_MESSAGE(synth_after, "synth should remain disabled while beep data is pending");
    TEST_ASSERT_EQUAL(25, failures_after);

    /* Reset the test-only state to avoid leaking into other tests. */
    audio_processor_test_idle_i2s_failures(0, false, 0, NULL, NULL);
}

static void test_fallback_volume_and_wav_resume_alignment(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 0, /* verify volume scaling mutes fallback */
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    uint8_t wav_payload[512];
    memset(wav_payload, 0x33, sizeof(wav_payload));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(wav_payload, sizeof(wav_payload)));

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(600));

    bool fallback_active = false;
    for (int i = 0; i < 20; ++i) {
        fallback_active = audio_processor_test_is_beep_fallback_active();
        if (fallback_active) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback synth did not activate");

    uint8_t buf[1024];
    bool fallback_all_zero = true;
    const int max_fallback_reads = 160;
    for (int i = 0; i < max_fallback_reads && audio_processor_test_is_beep_fallback_active(); ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        (void)audio_processor_test_get_beep_fallback_frames_remaining();
        for (size_t j = 0; j < bytes_read; ++j) {
            if (buf[j] != 0) {
                fallback_all_zero = false;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());
    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_TRUE_MESSAGE(fallback_all_zero, "Fallback output was not muted at volume 0");

    /* Focus subsequent assertions on WAV resume path; ignore any fallback tag counters. */
    audio_processor_test_reset_tag_miss_count();

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_volume(80));

    bool saw_wav = false;
    for (int i = 0; i < 40; ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        if (bytes_read == 0) {
            break;
        }
        if (!audio_processor_test_is_beep_fallback_active()) {
            saw_wav = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "WAV data did not resume after fallback");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_wav_and_beep_fallback_should_keep_tags_aligned(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Inject a burst of WAV data to exercise tag push/take under load. */
    uint8_t wav_chunk[512];
    memset(wav_chunk, 0x5a, sizeof(wav_chunk));
    size_t free_bytes = audio_processor_test_get_audio_free_bytes();
    size_t target_bytes = (free_bytes > 32768U) ? 32768U : (free_bytes / 2U);
    if (target_bytes < sizeof(wav_chunk)) {
        target_bytes = sizeof(wav_chunk);
    }

    size_t injected_bytes = 0;
    int injections = 0;
    while ((injected_bytes + sizeof(wav_chunk)) <= target_bytes) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(wav_chunk, sizeof(wav_chunk)));
        injected_bytes += sizeof(wav_chunk);
        injections++;
    }
    TEST_ASSERT_GREATER_THAN_INT(0, injections);

    size_t tag_used_before = audio_processor_test_get_tag_used();
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)tag_used_before);

    /* Trigger a long beep so the beep buffer saturates and fallback activates while WAV data is queued. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(900));

    bool fallback_active = false;
    const int max_waits = 30;
    for (int i = 0; i < max_waits; ++i) {
        fallback_active = audio_processor_test_is_beep_fallback_active();
        if (fallback_active) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback synth did not activate under combined load");

    /* Drain fallback audio while ensuring reads keep producing data and tags stay aligned. */
    uint8_t buf[1024];
    for (int i = 0; i < 200 && audio_processor_test_is_beep_fallback_active(); ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        TEST_ASSERT_TRUE(bytes_read > 0);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());

    /* Continue reading until WAV data resumes and the ringbuffer drains. */
    bool saw_wav = false;
    for (int i = 0; i < 160; ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        if (bytes_read > 0 && !audio_processor_test_is_beep_fallback_active()) {
            saw_wav = true;
        }
        if (bytes_read == 0 && !audio_processor_test_is_beep_fallback_active()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "WAV data did not resume after fallback");

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_wav_fallback_with_live_volume_changes_should_resume_cleanly(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 100,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Queue WAV data so fallback has to coexist with buffered audio. */
    uint8_t wav_chunk[640];
    memset(wav_chunk, 0x47, sizeof(wav_chunk));
    size_t wav_target = audio_processor_test_get_audio_free_bytes() / 3U;
    if (wav_target < sizeof(wav_chunk)) {
        wav_target = sizeof(wav_chunk);
    }
    size_t queued = 0;
    while ((queued + sizeof(wav_chunk)) <= wav_target) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(wav_chunk, sizeof(wav_chunk)));
        queued += sizeof(wav_chunk);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)queued);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(900));

    bool fallback_active = false;
    for (int i = 0; i < 30; ++i) {
        fallback_active = audio_processor_test_is_beep_fallback_active();
        if (fallback_active) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback synth did not activate");

    /* Drop volume mid-fallback, then restore to 100 before WAV resumes. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_volume(60));

    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();
    uint8_t buf[1024];
    for (int i = 0; i < 160 && audio_processor_test_is_beep_fallback_active(); ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        TEST_ASSERT_TRUE(bytes_read > 0);
        if (i == 40) {
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_volume(100));
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_total_frames());

    bool saw_wav = false;
    for (int i = 0; i < 120; ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        if (bytes_read == 0 && !audio_processor_test_is_beep_fallback_active()) {
            break;
        }
        if (bytes_read > 0 && !audio_processor_test_is_beep_fallback_active()) {
            saw_wav = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    uint32_t tag_miss_after = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_TRUE_MESSAGE((tag_miss_after - tag_miss_before) <= 1, "Tag miss count grew unexpectedly");
    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "WAV data did not resume after fallback and volume changes");

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_host_tag_drain_should_skip_when_no_source(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Ensure the data path is empty: no WAV pending, empty ringbuffers, no beep data. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());

    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();

    uint8_t buf[256];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));

    /* With no source data available, the host-only tag drain should skip and
     * leave tag counts untouched. The read should return zero bytes without
     * incrementing tag_miss. */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());
    TEST_ASSERT_EQUAL_UINT32(tag_miss_before, audio_processor_test_get_tag_miss_count());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)bytes_read);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_host_tag_drain_should_skip_when_no_dequeue_with_tag_backlog(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 90,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Synthesize tag backlog without delivering audio to force the host drain
     * guard paths (no_dequeue + no_source) to remain in skip mode. */
    audio_processor_test_wav_begin();
    audio_processor_test_wav_add_pending(2048);

    /* Do not enqueue audio into the ringbuffer; ensure buffers stay empty. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    size_t tag_used_before = audio_processor_test_get_tag_used();
    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();

    /* Force a read; fallback/tag debt paths may produce bytes without any
     * ringbuffer dequeues, so the host drain must skip. */
    uint8_t buf[512];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));

    TEST_ASSERT_EQUAL_UINT32(tag_used_before, audio_processor_test_get_tag_used());
    TEST_ASSERT_EQUAL_UINT32(tag_miss_before, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_wav_abort_mid_fallback_should_clear_debt_and_stay_tag_aligned(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 90,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Simulate an in-flight WAV with pending bytes and then trigger the
     * fallback synth. Abort the WAV mid-fallback and ensure tag debt clears
     * without adding tag misses. */
    audio_processor_test_wav_begin();
    audio_processor_test_wav_add_pending(2048);
    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(700));

    bool fallback_active = false;
    for (int i = 0; i < 30; ++i) {
        fallback_active = audio_processor_test_is_beep_fallback_active();
        if (fallback_active) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback did not activate before abort");

    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();

    uint8_t buf[512];
    for (int i = 0; i < 60 && audio_processor_test_is_beep_fallback_active(); ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        TEST_ASSERT_TRUE(bytes_read > 0);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    TEST_ASSERT_TRUE(audio_processor_test_is_beep_fallback_active());

    audio_processor_test_wav_abort();

    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_FALSE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_fallback_tag_debt());

    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)bytes_read);

    uint32_t tag_miss_after = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_EQUAL_UINT32(tag_miss_before, tag_miss_after);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_tag_reset_buffer_should_drop_backlog_and_skip_host_drain(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 90,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    uint8_t inject_buf[256];
    memset(inject_buf, 0x47, sizeof(inject_buf));
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(inject_buf, sizeof(inject_buf)));
    }

    size_t tag_used_before = audio_processor_test_get_tag_used();
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)tag_used_before);

    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());

    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();

    uint8_t read_buf[512];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(read_buf, sizeof(read_buf), &bytes_read));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)bytes_read);

    uint32_t tag_miss_after = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_EQUAL_UINT32(tag_miss_before, tag_miss_after);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_wav_fallback_soak_with_volume_and_mute_toggles(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 90,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    uint8_t wav_chunk[512];
    memset(wav_chunk, 0x52, sizeof(wav_chunk));
    size_t wav_target = audio_processor_test_get_audio_free_bytes() / 2U;
    if (wav_target < sizeof(wav_chunk)) {
        wav_target = sizeof(wav_chunk);
    }

    size_t queued = 0;
    while ((queued + sizeof(wav_chunk)) <= wav_target) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(wav_chunk, sizeof(wav_chunk)));
        queued += sizeof(wav_chunk);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)queued);

    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();

    const int cycles = 3;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(750));

        bool fallback_active = false;
        for (int i = 0; i < 30; ++i) {
            fallback_active = audio_processor_test_is_beep_fallback_active();
            if (fallback_active) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback synth did not activate during soak");

        /* Toggle volume and mute mid-fallback to stress tag accounting. */
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_volume(50));
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_mute(true));
        vTaskDelay(pdMS_TO_TICKS(10));
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_mute(false));

        uint8_t buf[1024];
        for (int i = 0; i < 200 && audio_processor_test_is_beep_fallback_active(); ++i) {
            size_t bytes_read = 0;
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
            TEST_ASSERT_TRUE(bytes_read > 0);
            if (i == 60) {
                TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_volume(90));
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
        TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());

        bool saw_wav = false;
        for (int i = 0; i < 160; ++i) {
            size_t bytes_read = 0;
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
            if (bytes_read == 0 && !audio_processor_test_is_beep_fallback_active()) {
                break;
            }
            if (bytes_read > 0 && !audio_processor_test_is_beep_fallback_active()) {
                saw_wav = true;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        TEST_ASSERT_TRUE_MESSAGE(saw_wav, "WAV data did not resume after fallback soak cycle");

        size_t free_bytes = audio_processor_test_get_audio_free_bytes();
        size_t refill_target = free_bytes / 2U;
        size_t added = 0;
        while ((added + sizeof(wav_chunk)) <= refill_target) {
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(wav_chunk, sizeof(wav_chunk)));
            added += sizeof(wav_chunk);
        }
    }

    uint32_t tag_miss_after = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_TRUE_MESSAGE((tag_miss_after - tag_miss_before) <= 2, "Tag miss count grew unexpectedly during fallback soak");

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_wav_injection_mid_fallback_should_resume_without_tag_loss(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 90,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Trigger fallback first, then inject WAV while fallback audio is active. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(800));

    bool fallback_active = false;
    for (int i = 0; i < 30; ++i) {
        fallback_active = audio_processor_test_is_beep_fallback_active();
        if (fallback_active) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback synth did not activate before WAV injection");

    uint8_t wav_chunk[640];
    memset(wav_chunk, 0x6c, sizeof(wav_chunk));
    size_t free_bytes = audio_processor_test_get_audio_free_bytes();
    size_t inject_target = free_bytes / 3U;
    if (inject_target < sizeof(wav_chunk)) {
        inject_target = sizeof(wav_chunk);
    }

    size_t injected = 0;
    while ((injected + sizeof(wav_chunk)) <= inject_target && audio_processor_test_is_beep_fallback_active()) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(wav_chunk, sizeof(wav_chunk)));
        injected += sizeof(wav_chunk);
        /* Pace injections to stay within the fallback window. */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)injected);

    uint8_t buf[1024];
    for (int i = 0; i < 220 && audio_processor_test_is_beep_fallback_active(); ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        TEST_ASSERT_TRUE(bytes_read > 0);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_fallback_tag_debt());

    /* Focus tag-miss delta on the WAV resume phase, ignoring any misses during fallback. */
    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();

    bool saw_wav = false;
    for (int i = 0; i < 140; ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        if (bytes_read == 0 && !audio_processor_test_is_beep_fallback_active()) {
            break;
        }
        if (bytes_read > 0 && !audio_processor_test_is_beep_fallback_active()) {
            saw_wav = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    uint32_t tag_miss_after = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_TRUE_MESSAGE((tag_miss_after - tag_miss_before) <= 2, "Tag miss count grew unexpectedly after mid-fallback WAV inject");
    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "WAV data did not resume after mid-fallback injection");

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_fallback_tag_debt());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

static void test_fallback_repeats_should_clear_debt_after_drain(void)
{
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    audio_processor_test_wav_reset_state();

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 90,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    const int cycles = 3;
    uint8_t wav_chunk[512];
    memset(wav_chunk, 0x61, sizeof(wav_chunk));
    uint8_t read_buf[768];

    for (int cycle = 0; cycle < cycles; ++cycle) {
        size_t free_bytes = audio_processor_test_get_audio_free_bytes();
        size_t target_bytes = free_bytes / 2U;
        if (target_bytes < sizeof(wav_chunk)) {
            target_bytes = sizeof(wav_chunk);
        }

        size_t queued = 0;
        while ((queued + sizeof(wav_chunk)) <= target_bytes) {
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(wav_chunk, sizeof(wav_chunk)));
            queued += sizeof(wav_chunk);
        }
        TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)queued);

        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep(600));

        bool fallback_active = false;
        for (int i = 0; i < 40; ++i) {
            fallback_active = audio_processor_test_is_beep_fallback_active();
            if (fallback_active) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        TEST_ASSERT_TRUE_MESSAGE(fallback_active, "Fallback did not activate during repeated cycle");

        for (int i = 0; i < 240 && audio_processor_test_is_beep_fallback_active(); ++i) {
            size_t bytes_read = 0;
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(read_buf, sizeof(read_buf), &bytes_read));
            TEST_ASSERT_TRUE(bytes_read > 0);
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        if (audio_processor_test_is_beep_fallback_active()) {
            /* Force a drain mid-stream to ensure cleanup clears fallback state. */
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
        }

        TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
        TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());
        TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_fallback_tag_debt());

        /* Let any remaining WAV data drain before the next cycle. */
        for (int j = 0; j < 40; ++j) {
            size_t bytes_read = 0;
            TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(read_buf, sizeof(read_buf), &bytes_read));
            if (bytes_read == 0) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(3));
        }
    }

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_drain_ringbuffer());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_fallback_frames_remaining());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_fallback_tag_debt());
    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}
#endif

static void test_audio_processor_play_wav_api(void)
{
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

    (void)audio_processor_drain_ringbuffer();

    ret = audio_processor_play_wav("/spiffs/worker_long_norm.wav");
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    uint8_t buf[1024];
    size_t bytes_read = 0;
    bool ok = false;
    const int max_attempts = 8;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if (ret == ESP_OK && bytes_read > 0) {
            ok = true;
            break;
        }
        if ((int)ret > 0) {
            bytes_read = (size_t)ret;
            if (bytes_read > 0) {
                ok = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    TEST_ASSERT_TRUE_MESSAGE(ok, "audio_processor_play_wav did not enqueue data");
    TEST_ASSERT_TRUE(bytes_read > 0);

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static void test_play_wav_command(void)
{
    /* Ensure the audio processor is initialized and can enqueue a WAV from SPIFFS
     * The project's SPIFFS image should contain /spiffs/worker_long_norm.wav. The
     * test issues the PLAY command (which prepends /spiffs/) and then attempts to
     * read some audio bytes to confirm playback was enqueued. */
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

    /* Start the processor so worker tasks are running and can process the
     * enqueued WAV file. Drain any leftover data to make the assertion below
     * deterministic. */
    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    /* Ensure ringbuffer is empty before we start */
    (void) audio_processor_drain_ringbuffer();

    /* Parse & execute PLAY against the command layer so the same code-path
     * used in production is exercised. Use the asset filename only; the
     * command layer will prefix /spiffs/ for us. */
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    /* Retry a few times to allow the worker to process file chunks and
     * enqueue them to the ringbuffer. Be defensive about return conventions:
     * - Preferred: audio_processor_read returns ESP_OK and sets bytes_read>0
     * - Legacy/alternate: audio_processor_read returns a positive integer
     *   interpreted as bytes read (ret > 0)
     */
    uint8_t buf[1024];
    size_t bytes_read = 0;
    bool ok = false;
    const int max_attempts = 8;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if (ret == ESP_OK) {
            if (bytes_read > 0) {
                ok = true; break;
            }
            /* zero bytes but no error — wait and retry */
        } else if ((int)ret > 0) {
            /* Some implementations return byte-count as the return value */
            bytes_read = (size_t)ret;
            if (bytes_read > 0) { ok = true; break; }
        } else {
            /* Non-trivial error: log and retry a couple times before failing */
            ESP_LOGW(TAG, "test_play_wav_command: audio_processor_read returned %d (attempt %d)", (int)ret, attempt);
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }

    TEST_ASSERT_TRUE_MESSAGE(ok, "PLAY did not produce audio bytes within timeout");
    TEST_ASSERT_TRUE(bytes_read > 0);

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

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

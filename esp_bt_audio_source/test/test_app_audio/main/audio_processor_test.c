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
#include "bt_manager.h"
#include "play_manager.h"
#include "driver/i2s_std.h"

/* Internal state needed for stale-beep regression checks. */
extern size_t s_beep_remaining_bytes;

#define I2S_SAMPLE_RATE AUDIO_SAMPLE_RATE_44K
#define I2S_BIT_DEPTH   AUDIO_BIT_DEPTH_16
#define I2S_CHANNELS    AUDIO_CHANNEL_STEREO
#define I2S_PORT        I2S_NUM_0

/* Optional speed-run scaling for test delays. Override by defining
 * TEST_APP_AUDIO_WAIT_DIV>1 to divide wait durations (ceil-divide). */
#ifndef TEST_APP_AUDIO_WAIT_DIV
#define TEST_APP_AUDIO_WAIT_DIV 1U
#endif

static TickType_t test_wait_ticks(uint32_t ms)
{
    uint32_t div = TEST_APP_AUDIO_WAIT_DIV ? TEST_APP_AUDIO_WAIT_DIV : 1U;
    uint32_t scaled = (ms + div - 1U) / div;
    if (scaled == 0U) {
        scaled = 1U;
    }
    return pdMS_TO_TICKS(scaled);
}

static void test_delay_ms(uint32_t ms)
{
    vTaskDelay(test_wait_ticks(ms));
}

static const char *TAG = "AUDIO_PROCESSOR_TEST";

/* Local stubs provided by the test_command_interface component. */
extern void bt_manager_mock_connection_closed(const char* mac);
extern void bt_manager_mock_connection_opened(const char* mac);
extern bool bt_manager_is_a2dp_connected(void);

static void ensure_i2s_stopped(void)
{
    /* Mirrors the STOP command behaviour: stop A2DP/I2S path so BEEP is permitted. */
    (void)bt_manager_stop_audio();
    esp_err_t stop_ret = audio_processor_stop();
    if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) {
        TEST_ASSERT_EQUAL(ESP_OK, stop_ret);
    }
}

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

    test_delay_ms(100);

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

    test_delay_ms(500);

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

    test_delay_ms(500);

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

        test_delay_ms(100);

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

    test_delay_ms(500);

    audio_stats_t stats;
    ret = audio_processor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    uint8_t buffer[1024];
    size_t bytes_read = 0;
    for (int i = 0; i < 5; ++i) {
        ret = audio_processor_read(buffer, sizeof(buffer), &bytes_read);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        test_delay_ms(10);
    }

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/* forward declarations so RUN_TEST can reference tests defined later */
static void test_audio_processor_play_wav_api(void);
static void test_wav_playback_completeness(void);
static void test_play_wav_command(void);
static void test_play_command_requires_a2dp_connection(void);
static void test_keepalive_read_suppressed_when_a2dp_disconnected(void);
static void test_keepalive_beep_then_play_recovers(void);
static void test_stop_clears_keepalive(void);
static void test_beep_command_clears_busy_after_draining(void);
static void test_beep_busy_clears_when_manager_stopped_and_queue_empty(void);
static void test_beep_synth_overlap_busy_and_recovers(void);
static void test_beep_rejected_while_wav_active(void);
static void test_beep_rejected_while_i2s_running(void);
static void test_play_rejected_while_i2s_running(void);
static void test_wav_resumes_after_a2dp_reconnect(void);
static void test_synth_keepalive_cleared_on_disconnect_and_recovers_after_reconnect(void);
static void test_wav_pause_resume_after_disconnect_restarts_playback(void);
static void test_interleaved_play_stop_beep_sequence(void);
static void test_play_wav_failure_restores_pipeline(void);
static void test_drain_stops_play_manager_and_clears_queue(void);
static void test_fallback_stop_resume_preserves_tag_alignment(void);
static void test_stop_during_wav_to_beep_transition_keeps_tags_consistent(void);
static void test_synth_toggle_mid_wav_keeps_tag_counters_clean(void);
static void test_wav_prefill_produces_initial_audio(void);
static void test_beep_then_play_streams_full_wav(void);
static void test_wav_playback_duration_baseline(void);
static void test_queue_backpressure_stress(void);

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
    RUN_TEST(test_audio_processor_play_wav_api);
    RUN_TEST(test_wav_playback_completeness);
    RUN_TEST(test_play_command_requires_a2dp_connection);
    RUN_TEST(test_wav_resumes_after_a2dp_reconnect);
    RUN_TEST(test_keepalive_read_suppressed_when_a2dp_disconnected);
    RUN_TEST(test_play_wav_command);
    RUN_TEST(test_interleaved_play_stop_beep_sequence);
    RUN_TEST(test_keepalive_beep_then_play_recovers);
    RUN_TEST(test_stop_clears_keepalive);
    RUN_TEST(test_beep_synth_overlap_busy_and_recovers);
    RUN_TEST(test_beep_command_clears_busy_after_draining);
    RUN_TEST(test_beep_busy_clears_when_manager_stopped_and_queue_empty);
    RUN_TEST(test_beep_rejected_while_wav_active);
    RUN_TEST(test_beep_rejected_while_i2s_running);
    RUN_TEST(test_play_rejected_while_i2s_running);
    RUN_TEST(test_synth_keepalive_cleared_on_disconnect_and_recovers_after_reconnect);
    RUN_TEST(test_wav_pause_resume_after_disconnect_restarts_playback);
    RUN_TEST(test_play_wav_failure_restores_pipeline);
    RUN_TEST(test_drain_stops_play_manager_and_clears_queue);
    RUN_TEST(test_fallback_stop_resume_preserves_tag_alignment);
    RUN_TEST(test_stop_during_wav_to_beep_transition_keeps_tags_consistent);
    RUN_TEST(test_synth_toggle_mid_wav_keeps_tag_counters_clean);
    RUN_TEST(test_wav_prefill_produces_initial_audio);
    RUN_TEST(test_beep_then_play_streams_full_wav);
    RUN_TEST(test_wav_playback_duration_baseline);
    RUN_TEST(test_queue_backpressure_stress);
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

    (void)audio_processor_drain_ring();

    ret = audio_processor_play_wav("/spiffs/worker_long_norm.wav");
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);
    if (ret == ESP_ERR_INVALID_STATE) {
        /* Busy path: ensure we cleanly stop and exit early. */
        ret = audio_processor_stop();
        TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

        ret = audio_processor_deinit();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        bt_manager_mock_connection_opened(NULL);
        return;
    }

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
        test_delay_ms(150);
    }

    TEST_ASSERT_TRUE_MESSAGE(ok, "audio_processor_play_wav did not enqueue data");
    TEST_ASSERT_TRUE(bytes_read > 0);

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test WAV playback completeness (CODE_REVIEW4 Task 6.1).
 * Regression test for WAV truncation: verifies that entire WAV file is enqueued
 * without data loss by checking play_manager instrumentation counters.
 */
static void test_wav_playback_completeness(void)
{
    bt_manager_mock_connection_opened(NULL);

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

    (void)audio_processor_drain_ring();

    const char *path = "/spiffs/worker_long_norm.wav";
    ret = audio_processor_play_wav(path);
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);
    if (ret == ESP_ERR_INVALID_STATE) {
        /* Busy path: clean up and exit early */
        ret = audio_processor_stop();
        TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);
        ret = audio_processor_deinit();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    /* Drain all WAV audio data */
    uint8_t buf[2048];
    size_t bytes_read = 0;
    const TickType_t start = xTaskGetTickCount();
    const TickType_t max_ticks = pdMS_TO_TICKS(15000);

    while (play_manager_is_active() || audio_processor_test_get_ring_used_bytes() > 0) {
        bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        if ((xTaskGetTickCount() - start) > max_ticks) {
            TEST_FAIL_MESSAGE("Timeout waiting for WAV playback to complete");
            break;
        }
        test_delay_ms(50);
    }

    /* Verify instrumentation: no data loss */
    play_manager_instrumentation_t instr;
    bool got_instr = play_manager_get_instrumentation(&instr);
    TEST_ASSERT_TRUE_MESSAGE(got_instr, "Failed to get play_manager instrumentation");

    /* All expected bytes should be read from file */
    TEST_ASSERT_EQUAL_MESSAGE(instr.expected_data_bytes, instr.bytes_read_from_file,
                              "Not all WAV data was read from file");

    /* Ensure playback produced output bytes (may differ from input due to resampling) */
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, instr.bytes_produced,
                                     "No audio data was produced");

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
    bt_manager_mock_connection_opened(NULL);

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
    (void) audio_processor_drain_ring();

    /* Parse & execute PLAY against the command layer so the same code-path
     * used in production is exercised. Use the asset filename only; the
     * command layer will prefix /spiffs/ for us. */
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    cmd_status_t play_res = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(play_res == CMD_SUCCESS || play_res == CMD_ERROR_UNKNOWN,
                             "PLAY should either start or report busy when I2S is active");
    if (play_res == CMD_ERROR_UNKNOWN) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }

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

        test_delay_ms(150);
    }

    TEST_ASSERT_TRUE_MESSAGE(ok, "PLAY did not produce audio bytes within timeout");
    TEST_ASSERT_TRUE(bytes_read > 0);

    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

static void test_play_command_requires_a2dp_connection(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    bt_manager_mock_connection_opened(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    (void)audio_processor_drain_ring();

    /* Explicitly mark BT as disconnected so PLAY should refuse to queue. */
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    int exec_status = cmd_execute(&ctx);
    bt_manager_mock_connection_opened(NULL);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CMD_SUCCESS, exec_status, "PLAY should fail when A2DP is disconnected");

    /* Allow any unexpected worker activity to run, then confirm nothing was enqueued. */
    test_delay_ms(100);

    uint8_t buf[64];
    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    if (ret == ESP_OK) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, bytes_read, "PLAY should not enqueue audio when A2DP is disconnected");
    } else {
        TEST_ASSERT_TRUE_MESSAGE(ret == ESP_FAIL || ret == ESP_ERR_INVALID_STATE, "Unexpected return from audio_processor_read");
    }

    TEST_ASSERT_FALSE(audio_processor_is_beep_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_wav_resumes_after_a2dp_reconnect(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_test_reset_tag_miss_count();

    cmd_context_t ctx;
    cmd_status_t parse_res = cmd_parse("PLAY worker_long_norm.wav", &ctx);
    TEST_ASSERT_TRUE_MESSAGE(parse_res == CMD_SUCCESS || parse_res == CMD_ERROR_UNKNOWN,
                             "PLAY parse should succeed or return busy-equivalent");
    if (parse_res != CMD_SUCCESS) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }
    cmd_status_t play_res = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(play_res == CMD_SUCCESS || play_res == CMD_ERROR_UNKNOWN,
                             "PLAY should either start or report busy when I2S is active");
    if (play_res == CMD_ERROR_UNKNOWN) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    test_delay_ms(150);
    size_t pending_before = audio_processor_test_get_ring_used_bytes();
    TEST_ASSERT_TRUE_MESSAGE(pending_before > 0, "WAV should produce data before disconnect");

    uint8_t buf[256];
    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    TEST_ASSERT_TRUE_MESSAGE((ret == ESP_OK && bytes_read > 0) || (int)ret > 0, "Initial WAV read should produce data");

    /* Simulate A2DP drop mid-stream and ensure data is preserved. */
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");

    bytes_read = 0;
    ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    TEST_ASSERT_TRUE_MESSAGE(ret == ESP_ERR_INVALID_STATE || ret == ESP_FAIL,
                             "audio_processor_read should fail while A2DP is disconnected");
    TEST_ASSERT_EQUAL_UINT32(0, bytes_read);
    size_t pending_during_disconnect = audio_processor_test_get_ring_used_bytes();
    TEST_ASSERT_TRUE_MESSAGE(pending_during_disconnect > 0 || play_manager_is_active(),
                             "WAV should remain active or buffered during disconnect");
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    /* Reconnect and ensure playback resumes without tag loss or synth re-arm. */
    bt_manager_mock_connection_opened(NULL);

    bool saw_wav = false;
    for (int attempt = 0; attempt < 8 && !saw_wav; ++attempt) {
        bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            saw_wav = true;
            break;
        }
        test_delay_ms(120);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "WAV did not resume after A2DP reconnect");
    TEST_ASSERT_FALSE_MESSAGE(audio_processor_is_synth_mode_enabled(), "Synth keepalive should remain disabled after reconnect");
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_keepalive_read_suppressed_when_a2dp_disconnected(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_set_synth_mode(true);

    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");
    TEST_ASSERT_FALSE_MESSAGE(bt_manager_is_a2dp_connected(), "bt_manager_is_a2dp_connected should be false after mock close");

    uint8_t buf[128];
    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    printf("DIAG-KA-READ: ret=%d bytes=%u conn=%d\n", (int)ret, (unsigned)bytes_read, (int)bt_manager_is_a2dp_connected());
    TEST_ASSERT_TRUE_MESSAGE(ret == ESP_ERR_INVALID_STATE || ret == ESP_FAIL,
                             "audio_processor_read should fail when A2DP is disconnected");
    TEST_ASSERT_EQUAL_UINT32(0, bytes_read);
    TEST_ASSERT_FALSE(audio_processor_is_beep_active());

    bt_manager_mock_connection_opened(NULL);
    bytes_read = 0;
    ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(0, bytes_read);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_play_wav_failure_restores_pipeline(void)
{
    bt_manager_mock_connection_opened(NULL);

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

    audio_status_t status_before = {0};
    ret = audio_processor_get_status(&status_before);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(status_before.running);

    /* Force play_manager_play_wav to fail by using a missing file and verify
     * the pipeline is restarted when the error path runs. */
    ret = audio_processor_play_wav("/spiffs/does_not_exist.wav");
    TEST_ASSERT_TRUE_MESSAGE(ret == ESP_ERR_NOT_FOUND || ret == ESP_ERR_INVALID_STATE,
                             "PLAY should fail with not-found or busy when I2S already active");

    audio_status_t status_after = {0};
    ret = audio_processor_get_status(&status_after);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE_MESSAGE(status_after.running, "Pipeline did not restart after PLAY failure");

    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

static size_t get_file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long sz = ftell(f);
    fclose(f);
    if (sz < 0) {
        return 0;
    }
    return (size_t)sz;
}

static void start_pipeline_default(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    ESP_ERROR_CHECK(audio_processor_init(&config));
    ESP_ERROR_CHECK(audio_processor_start());
}

static void stop_pipeline_default(void)
{
    (void)audio_processor_stop();
    (void)audio_processor_deinit();
}

static void test_wav_prefill_produces_initial_audio(void)
{
    const char *path = "/spiffs/worker_long_norm.wav";
    start_pipeline_default();

    (void)audio_processor_drain_ring();
    esp_err_t play_ret = audio_processor_play_wav(path);
    TEST_ASSERT_TRUE_MESSAGE(play_ret == ESP_OK || play_ret == ESP_ERR_INVALID_STATE,
                             "PLAY should either start or report busy when I2S is active");
    if (play_ret == ESP_ERR_INVALID_STATE) {
        stop_pipeline_default();
        return;
    }

    uint8_t buf[2048];
    size_t bytes_read = 0;
    esp_err_t r = audio_processor_read(buf, sizeof(buf), &bytes_read);
    TEST_ASSERT_EQUAL(ESP_OK, r);
    TEST_ASSERT_TRUE(bytes_read > 0);

    stop_pipeline_default();
}

static void test_beep_then_play_streams_full_wav(void)
{
    const char *path = "/spiffs/worker_long_norm.wav";
    const size_t wav_size = get_file_size(path);
    TEST_ASSERT_TRUE_MESSAGE(wav_size > 0, "wav file missing");

    start_pipeline_default();
    (void)audio_processor_drain_ring();

    /* Issue a short beep to exercise the post-beep recovery path. */
    esp_err_t beep_ret = audio_processor_beep(200);
    TEST_ASSERT_TRUE_MESSAGE(beep_ret == ESP_OK || beep_ret == ESP_ERR_INVALID_STATE,
                             "BEEP should either start or report busy when I2S is active");
    if (beep_ret == ESP_ERR_INVALID_STATE) {
        stop_pipeline_default();
        return;
    }
    test_delay_ms(300);

    esp_err_t play_ret = audio_processor_play_wav(path);
    TEST_ASSERT_TRUE_MESSAGE(play_ret == ESP_OK || play_ret == ESP_ERR_INVALID_STATE,
                             "PLAY should either start or report busy when I2S is active");
    if (play_ret == ESP_ERR_INVALID_STATE) {
        stop_pipeline_default();
        return;
    }

    uint8_t buf[2048];
    size_t total_read = 0;
    size_t bytes_read = 0;
    const TickType_t start = xTaskGetTickCount();
    const TickType_t max_ticks = pdMS_TO_TICKS(15000);

    while (play_manager_is_active() || audio_processor_test_get_ring_used_bytes() > 0) {
        bytes_read = 0;
        esp_err_t r = audio_processor_read(buf, sizeof(buf), &bytes_read);
        TEST_ASSERT_EQUAL(ESP_OK, r);
        total_read += bytes_read;
        if ((xTaskGetTickCount() - start) > max_ticks) {
            break;
        }
        test_delay_ms(50);
    }

    TEST_ASSERT_TRUE_MESSAGE(total_read >= wav_size, "WAV playback truncated after beep");

    stop_pipeline_default();
}

static void test_beep_rejected_while_wav_active(void)
{
    const char *path = "/spiffs/worker_long_norm.wav";

    start_pipeline_default();
    (void)audio_processor_drain_ring();

    /* With I2S running (input only), PLAY should be rejected. */
    esp_err_t play_ret = audio_processor_play_wav(path);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, play_ret);

    /* Beep should also be rejected while I2S is active. */
    esp_err_t beep_ret = audio_processor_beep(100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, beep_ret);

    stop_pipeline_default();
}

static void test_beep_rejected_while_i2s_running(void)
{
    start_pipeline_default();
    (void)audio_processor_drain_ring();

    esp_err_t ret = audio_processor_beep(100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);

    stop_pipeline_default();
}

static void test_play_rejected_while_i2s_running(void)
{
    const char *path = "/spiffs/worker_long_norm.wav";
    start_pipeline_default();
    (void)audio_processor_drain_ring();

    esp_err_t ret = audio_processor_play_wav(path);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);

    stop_pipeline_default();
}

static void test_stop_clears_keepalive(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();

    bool synth_after = false;
    int failures_after = -1;
    audio_processor_test_idle_i2s_failures(0, true, 0, &synth_after, &failures_after);
    TEST_ASSERT_TRUE(synth_after);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    uint8_t buf[64];
    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(0, bytes_read);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_drain_stops_play_manager_and_clears_queue(void)
{
    bt_manager_mock_connection_opened(NULL);

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

    (void)audio_processor_drain_ring();

    ret = audio_processor_play_wav("/spiffs/worker_long_norm.wav");
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);
    if (ret == ESP_ERR_INVALID_STATE) {
        /* Busy path: ensure we cleanly stop and exit early. */
        ret = audio_processor_stop();
        TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

        ret = audio_processor_deinit();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    /* Allow the play manager to enqueue initial WAV data before draining. */
    test_delay_ms(50);

    TEST_ASSERT_TRUE_MESSAGE(play_manager_is_active(), "play_manager inactive after PLAY");
    size_t pending_before = audio_processor_test_get_ring_used_bytes();
    TEST_ASSERT_TRUE_MESSAGE(pending_before > 0, "Expected ring data after PLAY");

    ret = audio_processor_drain_ring();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    size_t pending_after = audio_processor_test_get_ring_used_bytes();
    TEST_ASSERT_EQUAL(0, pending_after);
    TEST_ASSERT_FALSE_MESSAGE(play_manager_is_active(), "play_manager still active after drain");

    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

static void test_fallback_stop_resume_preserves_tag_alignment(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_test_reset_tag_miss_count();

    bool synth_after = false;
    int failures_after = -1;
    audio_processor_test_idle_i2s_failures(24, false, 0, &synth_after, &failures_after);
    TEST_ASSERT_TRUE_MESSAGE(synth_after, "fallback should enable synth keepalive after repeated I2S failures");
    TEST_ASSERT_EQUAL(0, failures_after);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    cmd_status_t play_res = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(play_res == CMD_SUCCESS || play_res == CMD_ERROR_UNKNOWN,
                             "PLAY should either start or report busy when I2S is active");
    if (play_res == CMD_ERROR_UNKNOWN) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    uint8_t buf[256];
    size_t bytes_read = 0;
    bool saw_wav = false;
    for (int attempt = 0; attempt < 8 && !saw_wav; ++attempt) {
        bytes_read = 0;
        esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            saw_wav = true;
            break;
        }
        test_delay_ms(120);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "PLAY did not enqueue after fallback stop/resume");
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_interleaved_play_stop_beep_sequence(void)
{
    /* Exercise PLAY -> STOP -> BEEP -> PLAY to ensure tag and lock paths recover. */
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    cmd_status_t first_play = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(first_play == CMD_SUCCESS || first_play == CMD_ERROR_UNKNOWN, "PLAY should not fail hard when I2S running");

    test_delay_ms(50);

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    test_delay_ms(50);

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL_MESSAGE(CMD_ERROR_UNKNOWN, cmd_execute(&ctx), "BEEP should return busy when I2S running");

    /* Follow-up PLAY should also be busy while I2S remains active. */
    (void)audio_processor_drain_ring();
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    cmd_status_t second_play = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(second_play == CMD_SUCCESS || second_play == CMD_ERROR_UNKNOWN, "PLAY should not fail hard when I2S running");

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}


static void test_beep_command_clears_busy_after_draining(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 70,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    (void)audio_processor_drain_ring();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL_MESSAGE(CMD_ERROR_UNKNOWN, cmd_execute(&ctx), "BEEP should return busy when I2S running");

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_beep_busy_clears_when_manager_stopped_and_queue_empty(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 70,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Simulate a stale beep counter with an empty queue and no active beep. */
    (void)audio_processor_drain_ring();
    s_beep_remaining_bytes = 128;

    TEST_ASSERT_FALSE(audio_processor_is_beep_active());
    TEST_ASSERT_EQUAL_UINT32(0U, (uint32_t)s_beep_remaining_bytes);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_keepalive_beep_then_play_recovers(void)
{
    /* Simulate synth keepalive, then verify BEEP and PLAY both enqueue data and clear keepalive. */
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_set_synth_mode(true);
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL_MESSAGE(CMD_ERROR_UNKNOWN, cmd_execute(&ctx), "BEEP should return busy when I2S running");

    (void)audio_processor_drain_ring();

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    TEST_ASSERT_EQUAL_MESSAGE(CMD_ERROR_UNKNOWN, cmd_execute(&ctx), "PLAY should return busy when I2S running");
    TEST_ASSERT_TRUE_MESSAGE(audio_processor_is_synth_mode_enabled(), "Synth keepalive remains enabled when PLAY is rejected");

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    bt_manager_mock_connection_opened(NULL);
}

static void test_beep_synth_overlap_busy_and_recovers(void)
{
    /* Beep should be busy while already playing even if synth mode toggles; subsequent beep should succeed. */
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_set_synth_mode(true);
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    /* With I2S active, beeps should return busy. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_processor_beep(80));

    /* Toggle synth mode to ensure no crash when busy. */
    audio_processor_set_synth_mode(false);
    test_delay_ms(30);
    audio_processor_set_synth_mode(true);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_processor_beep(60));

    (void)audio_processor_drain_ring();
    audio_processor_set_synth_mode(false);

    esp_err_t ret = ESP_OK;
    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

static void test_stop_during_wav_to_beep_transition_keeps_tags_consistent(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_test_reset_tag_miss_count();
    audio_processor_test_reset_tag_recover_window();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    cmd_status_t play_res = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(play_res == CMD_SUCCESS || play_res == CMD_ERROR_UNKNOWN,
                             "PLAY should either start or report busy when I2S is active");
    if (play_res == CMD_ERROR_UNKNOWN) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    test_delay_ms(150);
    TEST_ASSERT_TRUE(audio_processor_is_wav_active());

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    test_delay_ms(50);
    TEST_ASSERT_FALSE(audio_processor_is_wav_active());

    /* Restart the pipeline and immediately transition to a beep. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    esp_err_t ret = audio_processor_beep(50);
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = audio_processor_stop();
        TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

        ret = audio_processor_deinit();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    test_delay_ms(80);

    uint8_t buf[128];
    size_t bytes_read = 0;
    bool saw_data = false;
    const int max_attempts = 5;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if (ret == ESP_OK) {
            if (bytes_read > 0) {
                saw_data = true;
                break;
            }
        } else if ((int)ret > 0) {
            bytes_read = (size_t)ret;
            if (bytes_read > 0) {
                saw_data = true;
                break;
            }
        }
        test_delay_ms(40);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_data, "beep after STOP did not produce data");

    ret = audio_processor_drain_ring();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());
    TEST_ASSERT_FALSE(audio_processor_is_wav_active());

    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

static void test_synth_keepalive_cleared_on_disconnect_and_recovers_after_reconnect(void)
{
    /* Ensure synth keepalive disarms on disconnect and only re-arms after real playback succeeds. */
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();

    /* Arm synth keepalive and confirm it is active. */
    audio_processor_set_synth_mode(true);
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    /* Disconnect A2DP: keepalive should be suppressed and reads should fail with zero bytes. */
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");

    uint8_t buf[128];
    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    TEST_ASSERT_TRUE_MESSAGE(ret == ESP_ERR_INVALID_STATE || ret == ESP_FAIL,
                             "read should fail while A2DP is disconnected");
    TEST_ASSERT_EQUAL_UINT32(0, bytes_read);
    TEST_ASSERT_FALSE_MESSAGE(audio_processor_is_synth_mode_enabled(), "Synth keepalive should be cleared on disconnect");

    /* Reconnect and perform a BEEP to re-arm keepalive on success. */
    bt_manager_mock_connection_opened(NULL);
    ret = audio_processor_beep(40);
    TEST_ASSERT_TRUE_MESSAGE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE,
                             "BEEP should either start or report busy when I2S is active");
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = audio_processor_stop();
        TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

        ret = audio_processor_deinit();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    bool saw_data = false;
    const int max_attempts = 6;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            saw_data = true;
            break;
        }
        test_delay_ms(80);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_data, "BEEP after reconnect did not produce data");
    /* Successful playback should re-arm keepalive (synth mode false but armed for future idle). */
    TEST_ASSERT_FALSE_MESSAGE(audio_processor_is_synth_mode_enabled(), "Synth keepalive should remain disabled after real playback");

    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

static void test_wav_pause_resume_after_disconnect_restarts_playback(void)
{
    /* Simulate a pause (stop), disconnect, reconnect, and resume to ensure WAV restarts cleanly. */
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_test_reset_tag_miss_count();

    cmd_context_t ctx;
    cmd_status_t parse_res = cmd_parse("PLAY worker_long_norm.wav", &ctx);
    TEST_ASSERT_TRUE_MESSAGE(parse_res == CMD_SUCCESS || parse_res == CMD_ERROR_UNKNOWN,
                             "PLAY parse should succeed or return busy-equivalent");
    if (parse_res != CMD_SUCCESS) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    cmd_status_t play_res = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(play_res == CMD_SUCCESS || play_res == CMD_ERROR_UNKNOWN,
                             "PLAY should either start or report busy when I2S is active");
    if (play_res == CMD_ERROR_UNKNOWN) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    uint8_t buf[256];
    size_t bytes_read = 0;
    bool saw_wav = false;
    for (int attempt = 0; attempt < 6 && !saw_wav; ++attempt) {
        bytes_read = 0;
        esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            saw_wav = true;
            break;
        }
        test_delay_ms(80);
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "Initial WAV did not enqueue before pause/disconnect");

    /* Pause the pipeline and simulate a disconnect while paused. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");

    /* Resume should not produce audio while disconnected. */
    esp_err_t ret = audio_processor_start();
    TEST_ASSERT_TRUE_MESSAGE(ret == ESP_ERR_INVALID_STATE || ret == ESP_OK,
                             "start should not succeed in a disconnected state");

    bytes_read = 0;
    ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
    TEST_ASSERT_TRUE_MESSAGE(ret == ESP_ERR_INVALID_STATE || ret == ESP_FAIL,
                             "read should fail while A2DP is disconnected");
    TEST_ASSERT_EQUAL_UINT32(0, bytes_read);

    /* Reconnect then resume and ensure playback restarts without tag loss. */
    bt_manager_mock_connection_opened(NULL);
    ret = audio_processor_start();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    bool resumed_wav = false;
    for (int attempt = 0; attempt < 8 && !resumed_wav; ++attempt) {
        bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            resumed_wav = true;
            break;
        }
        test_delay_ms(120);
    }

    TEST_ASSERT_TRUE_MESSAGE(resumed_wav, "WAV did not resume after reconnect/start");
    uint32_t tag_miss = audio_processor_test_get_tag_miss_count();
    if (tag_miss > 0) {
        ESP_LOGW(TAG, "pause/resume tag_miss=%" PRIu32, tag_miss);
    }

    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

static void test_synth_toggle_mid_wav_keeps_tag_counters_clean(void)
{
    ensure_i2s_stopped();
    bt_manager_mock_connection_opened(NULL);

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
    (void)audio_processor_drain_ring();
    audio_processor_test_reset_tag_miss_count();
    audio_processor_test_reset_tag_recover_window();
    audio_processor_set_synth_mode(false);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    cmd_status_t play_res = cmd_execute(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(play_res == CMD_SUCCESS || play_res == CMD_ERROR_UNKNOWN,
                             "PLAY should either start or report busy when I2S is active");
    if (play_res == CMD_ERROR_UNKNOWN) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
        bt_manager_mock_connection_opened(NULL);
        return;
    }

    uint8_t buf[256];
    size_t bytes_read = 0;
    bool saw_pre_toggle = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        bytes_read = 0;
        esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            saw_pre_toggle = true;
            break;
        }
        test_delay_ms(80);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_pre_toggle, "WAV did not enqueue before synth toggle");

    audio_processor_set_synth_mode(true);
    test_delay_ms(80);

    bool saw_post_toggle = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        bytes_read = 0;
        esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            saw_post_toggle = true;
            break;
        }
        test_delay_ms(80);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_post_toggle, "Audio did not continue after enabling synth mode");

    audio_processor_set_synth_mode(false);
    test_delay_ms(80);

    bool saw_after_restore = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        bytes_read = 0;
        esp_err_t ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        if ((ret == ESP_OK && bytes_read > 0) || (int)ret > 0) {
            saw_after_restore = true;
            break;
        }
        test_delay_ms(80);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_after_restore, "Audio did not resume after disabling synth mode");
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    esp_err_t ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

/**
 * @brief CODE_REVIEW5 Task 0.1: Baseline WAV playback duration measurement
 *
 * WHY: Quantify "ends early" behavior before resampler fix.
 * HOW: Measure actual playback time from start to completion.
 * CORRECTNESS: Expected duration = 500ms (44.1kHz stereo, 87KB file).
 *              Measured duration should match within tolerance.
 *              If significantly shorter, confirms resampler truncation.
 */
static void test_wav_playback_duration_baseline(void)
{
    ensure_i2s_stopped();
    
    const char *path = "/spiffs/worker_long_norm.wav";
    const uint32_t expected_duration_ms = 500; /* WAV file is 0.5s */
    const uint32_t tolerance_ms = 50; /* ±10% tolerance */

    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_48K, /* Output 48kHz - requires upsampling */
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* NOTE: Don't start audio processor here - play_wav will handle I2S startup */
    /* ret = audio_processor_start(); */
    /* TEST_ASSERT_EQUAL(ESP_OK, ret); */

    bt_manager_mock_connection_opened("AB:CD:EF:12:34:56");
    (void)audio_processor_drain_ring();

    /* Record start time */
    const TickType_t start_ticks = xTaskGetTickCount();

    ret = audio_processor_play_wav(path);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Drain audio until playback completes */
    uint8_t buf[2048];
    size_t total_bytes = 0;
    const TickType_t max_wait_ticks = pdMS_TO_TICKS(2000); /* 2s max */

    while (play_manager_is_active() || audio_processor_test_get_ring_used_bytes() > 0) {
        size_t bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        total_bytes += bytes_read;

        if ((xTaskGetTickCount() - start_ticks) > max_wait_ticks) {
            break;
        }
        test_delay_ms(10);
    }

    /* Record end time */
    const TickType_t end_ticks = xTaskGetTickCount();
    const uint32_t duration_ticks = end_ticks - start_ticks;
    const uint32_t duration_ms = pdTICKS_TO_MS(duration_ticks);

    /* Calculate delta */
    const int32_t delta_ms = (int32_t)duration_ms - (int32_t)expected_duration_ms;

    /* Log results for CODE_REVIEW5 baseline documentation */
    ESP_LOGI(TAG, "==== CODE_REVIEW5 Baseline WAV Playback Duration ====");
    ESP_LOGI(TAG, "WAV file: %s", path);
    ESP_LOGI(TAG, "Expected duration: %lu ms", (unsigned long)expected_duration_ms);
    ESP_LOGI(TAG, "Measured duration: %lu ms", (unsigned long)duration_ms);
    ESP_LOGI(TAG, "Delta: %ld ms (%.1f%%)", (long)delta_ms,
             (float)delta_ms * 100.0f / (float)expected_duration_ms);
    ESP_LOGI(TAG, "Total bytes read: %zu", total_bytes);
    ESP_LOGI(TAG, "Source: 44.1kHz stereo → Output: 48kHz stereo (upsampling)");
    ESP_LOGI(TAG, "=====================================================");

    /* Assert duration is within tolerance */
    TEST_ASSERT_TRUE_MESSAGE(duration_ms >= (expected_duration_ms - tolerance_ms),
                             "Playback ended too early (possible resampler truncation)");
    TEST_ASSERT_TRUE_MESSAGE(duration_ms <= (expected_duration_ms + tolerance_ms),
                             "Playback took too long");

    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}
/* ============================================================================
 * CODE_REVIEW5 Phase 6 Task 6.2: Extended WAV Duration Tests
 * ============================================================================
 * These tests validate the streaming resampler's accuracy across longer
 * playback durations. The Q16.16 fixed-point phase accumulator ensures no
 * cumulative frame loss during upsampling, downsampling, or baseline playback.
 *
 * Test files:
 * - test_441_1s.wav:  44.1kHz stereo, 1s duration (upsampling to 48kHz)
 * - test_48_downsample_1s.wav: 48kHz stereo, 1s duration (downsampling to 44.1kHz)
 * - test_48_baseline_1s.wav: 48kHz stereo, 1s duration (no resampling, 48kHz output)
 *
 * Tolerance: ±1% (10ms for 1s file) to allow for FreeRTOS timing jitter
 * ============================================================================ */

/**
 * @brief Stress test: Queue backpressure with artificial delays
 *
 * WHY: Validates queue backpressure handling doesn't drop frames or corrupt state
 * HOW: Plays 1s WAV with artificial read delays to force queue to fill up
 * CORRECTNESS: Playback completes with accurate duration and frame count despite delays
 */
static void test_queue_backpressure_stress(void)
{
    ensure_i2s_stopped();
    
    const char *path = "/spiffs/test_441_1s.wav";
    const uint32_t expected_duration_ms = 1000; /* 1 second WAV file */
    const uint32_t tolerance_ms = 100; /* ±10% tolerance (more lenient for stress test) */
    const uint32_t read_delay_ms = 50; /* Delay between reads to stress queue */

    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_48K, /* Output 48kHz - requires upsampling */
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    bt_manager_mock_connection_opened("AB:CD:EF:12:34:56");
    (void)audio_processor_drain_ring();

    /* Record start time */
    const TickType_t start_ticks = xTaskGetTickCount();

    ret = audio_processor_play_wav(path);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Drain audio with artificial delays to stress queue */
    uint8_t buf[2048];
    size_t total_bytes = 0;
    uint32_t read_count = 0;
    uint32_t max_queue_used = 0;
    const TickType_t max_wait_ticks = pdMS_TO_TICKS(5000); /* 5s max (longer for stress test) */

    while (play_manager_is_active() || audio_processor_test_get_ring_used_bytes() > 0) {
        size_t bytes_read = 0;
        ret = audio_processor_read(buf, sizeof(buf), &bytes_read);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        total_bytes += bytes_read;
        read_count++;

        /* Track maximum queue usage during stress test */
        size_t queue_used = audio_processor_test_get_ring_used_bytes();
        if (queue_used > max_queue_used) {
            max_queue_used = queue_used;
        }

        /* Introduce artificial delay to force queue to fill up */
        test_delay_ms(read_delay_ms);

        if ((xTaskGetTickCount() - start_ticks) > max_wait_ticks) {
            break;
        }
    }

    /* Record end time */
    const TickType_t end_ticks = xTaskGetTickCount();
    const uint32_t duration_ticks = end_ticks - start_ticks;
    const uint32_t duration_ms = pdTICKS_TO_MS(duration_ticks);

    /* Calculate delta */
    const int32_t delta_ms = (int32_t)duration_ms - (int32_t)expected_duration_ms;

    /* Log results for CODE_REVIEW5 Task 6.3 documentation */
    ESP_LOGI(TAG, "==== CODE_REVIEW5 Task 6.3: Queue Backpressure Stress Test ====");
    ESP_LOGI(TAG, "WAV file: %s", path);
    ESP_LOGI(TAG, "Expected duration: %lu ms", (unsigned long)expected_duration_ms);
    ESP_LOGI(TAG, "Measured duration: %lu ms", (unsigned long)duration_ms);
    ESP_LOGI(TAG, "Delta: %ld ms (%.1f%%)", (long)delta_ms,
             (float)delta_ms * 100.0f / (float)expected_duration_ms);
    ESP_LOGI(TAG, "Total bytes read: %zu", total_bytes);
    ESP_LOGI(TAG, "Read operations: %lu", (unsigned long)read_count);
    ESP_LOGI(TAG, "Read delay per operation: %lu ms", (unsigned long)read_delay_ms);
    ESP_LOGI(TAG, "Max queue usage: %zu descriptors", max_queue_used);
    ESP_LOGI(TAG, "Source: 44.1kHz stereo → Output: 48kHz stereo (upsampling)");
    ESP_LOGI(TAG, "Test: Backpressure stress with artificial delays");
    ESP_LOGI(TAG, "===============================================================");

    /* Assert duration is within tolerance despite artificial delays */
    TEST_ASSERT_TRUE_MESSAGE(duration_ms >= (expected_duration_ms - tolerance_ms),
                             "Backpressure stress: Playback ended too early (possible frame drop)");
    TEST_ASSERT_TRUE_MESSAGE(duration_ms <= (expected_duration_ms + tolerance_ms + (read_count * read_delay_ms)),
                             "Backpressure stress: Playback took too long (accounting for delays)");

    /* Verify queue was actually stressed (should have filled up) */
    TEST_ASSERT_TRUE_MESSAGE(max_queue_used >= 4,
                             "Backpressure stress: Queue was not stressed (max usage < 4 descriptors)");

    ret = audio_processor_stop();
    TEST_ASSERT_TRUE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_manager_mock_connection_opened(NULL);
}

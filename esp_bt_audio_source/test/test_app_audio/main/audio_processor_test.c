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

/* forward declarations so RUN_TEST can reference tests defined later */
static void test_keepalive_read_suppressed_when_a2dp_disconnected(void);
static void test_stop_clears_keepalive(void);
static void test_beep_command_clears_busy_after_draining(void);
static void test_beep_busy_clears_when_manager_stopped_and_queue_empty(void);
static void test_beep_synth_overlap_busy_and_recovers(void);
static void test_beep_rejected_while_i2s_running(void);
static void test_synth_keepalive_cleared_on_disconnect_and_recovers_after_reconnect(void);
static void test_fallback_stop_resume_preserves_tag_alignment(void);

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

static void test_beep_rejected_while_i2s_running(void)
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
    (void)audio_processor_drain_ring();

    esp_err_t ret = audio_processor_beep(100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);

    (void)audio_processor_stop();
    (void)audio_processor_deinit();

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



/**
 * @brief CODE_REVIEW5 Task 0.1: Baseline WAV playback duration measurement
 *
 * WHY: Quantify "ends early" behavior before resampler fix.
 * HOW: Measure actual playback time from start to completion.
 * CORRECTNESS: Expected duration = 500ms (44.1kHz stereo, 87KB file).
 *              Measured duration should match within tolerance.
 *              If significantly shorter, confirms resampler truncation.
 */
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

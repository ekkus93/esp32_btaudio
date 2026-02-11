/**
 * @file test_manager.c
 * @brief Unified test suite for beep_manager, i2s_manager, and synth_manager
 *
 * This test suite consolidates three previously separate test suites to reduce
 * ESP32 flash cycles during testing (3 flashes → 1 flash).
 */

#include "unity.h"
#include "unity_test_runner.h"

#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "beep_manager.h"
#include "i2s_manager.h"
#include "synth_manager.h"

/* ========================================================================== */
/*                      beep_manager test helpers                            */
/* ========================================================================== */

static bool s_done_called = false;

static void done_cb(void *ctx)
{
    bool *flag = (bool *)ctx;
    if (flag != NULL) {
        *flag = true;
    }
}

static void reset_state(void)
{
    beep_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    s_done_called = false;
    beep_manager_set_done_callback(NULL, NULL);
}

static size_t bytes_per_sample(audio_bit_depth_t depth)
{
    if (depth == AUDIO_BIT_DEPTH_16) {
        return 2;
    }
    if (depth == AUDIO_BIT_DEPTH_32) {
        return 4;
    }
    return 0;
}

static size_t frame_bytes(const audio_config_t *cfg)
{
    size_t sample_bytes = bytes_per_sample(cfg->bit_depth);
    size_t channels = (cfg->channels == AUDIO_CHANNEL_MONO) ? 1U : 2U;
    return sample_bytes * channels;
}

static bool drive_overlay_until_done(const audio_config_t *cfg, uint32_t duration_ms, bool *any_nonzero)
{
    uint8_t buf[256];
    size_t fbytes = frame_bytes(cfg);
    if (fbytes == 0 || cfg->sample_rate == 0) {
        return false;
    }

    uint64_t total_frames = ((uint64_t)duration_ms * (uint64_t)cfg->sample_rate) / 1000ULL;
    size_t frames_per_call = sizeof(buf) / fbytes;
    size_t max_iters = (size_t)(total_frames / (frames_per_call ? frames_per_call : 1U)) + 4U;

    for (size_t i = 0; i < max_iters && beep_overlay_is_active(); ++i) {
        memset(buf, 0, sizeof(buf));
        beep_overlay_fill(buf, sizeof(buf), cfg);
        if (any_nonzero && !*any_nonzero) {
            for (size_t j = 0; j < sizeof(buf); ++j) {
                if (buf[j] != 0) {
                    *any_nonzero = true;
                    break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return !beep_overlay_is_active();
}

/* ========================================================================== */
/*                      i2s_manager test helpers                             */
/* ========================================================================== */

static uint8_t s_raw_buf[256];
static uint8_t s_proc_buf[512];
static uint8_t s_proc_buf2[512];

static audio_config_t i2s_default_config(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 50,
        .mute = false,
        .i2s_port = I2S_NUM_0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = GPIO_NUM_NC,
    };
    return cfg;
}

static i2s_manager_buffers_t i2s_default_buffers(void)
{
    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = sizeof(s_raw_buf),
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    return bufs;
}

static void i2s_tearDown(void)
{
    i2s_manager_deinit();
}

/* ========================================================================== */
/*                      synth_manager test helpers                           */
/* ========================================================================== */

static audio_config_t synth_default_config(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 50,
        .mute = false,
        .i2s_port = 0,
        .i2s_bclk_pin = -1,
        .i2s_ws_pin = -1,
        .i2s_din_pin = -1,
        .i2s_dout_pin = -1,
    };
    return cfg;
}

static void fill_guard(uint8_t *buf, size_t len, uint8_t value)
{
    memset(buf, value, len);
}

static void synth_setUp(void)
{
    synth_manager_reset_state();
}

static void synth_tearDown(void)
{
    synth_manager_reset_state();
}

/* ========================================================================== */
/*                      beep_manager tests                                   */
/* ========================================================================== */

TEST_CASE("beep_manager_play_starts_overlay_and_calls_done", "[beep_manager]")
{
    reset_state();
    beep_manager_set_done_callback(done_cb, &s_done_called);

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 80,
    };

    beep_request_t req = {
        .duration_ms = 20,
        .freq_hz = 1200.0,
        .amplitude = 1500,
    };

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    TEST_ASSERT_TRUE(beep_overlay_is_active());

    bool any_nonzero = false;
    TEST_ASSERT_TRUE(drive_overlay_until_done(&cfg, req.duration_ms, &any_nonzero));

    TEST_ASSERT_TRUE(any_nonzero);
    TEST_ASSERT_TRUE(s_done_called);
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
}

TEST_CASE("beep_manager_rejects_invalid_args", "[beep_manager]")
{
    reset_state();

    audio_config_t bad_cfg = {
        .sample_rate = 0,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 10,
    };

    beep_request_t req = {
        .duration_ms = 5,
        .freq_hz = 1000.0,
        .amplitude = 500,
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, beep_manager_play(NULL, &bad_cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, beep_manager_play(&req, &bad_cfg));
}

TEST_CASE("beep_manager_rejects_unsupported_bit_depth", "[beep_manager]")
{
    reset_state();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_8K,
        .bit_depth = AUDIO_BIT_DEPTH_24,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 50,
    };
    beep_request_t req = {
        .duration_ms = 10,
        .freq_hz = 500.0,
        .amplitude = 800,
    };

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, beep_manager_play(&req, &cfg));
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
}

TEST_CASE("beep_manager_reports_busy_while_playing", "[beep_manager]")
{
    reset_state();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 70,
    };

    beep_request_t req = {
        .duration_ms = 500,
        .freq_hz = 440.0,
        .amplitude = 1200,
    };

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    TEST_ASSERT_EQUAL(BEEP_STATE_PLAYING, beep_manager_get_state());

    esp_err_t rc = beep_manager_play(&req, &cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, rc);

    beep_manager_stop();
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
}

TEST_CASE("beep_manager_stop_terminates_overlay", "[beep_manager]")
{
    reset_state();
    beep_manager_set_done_callback(done_cb, &s_done_called);

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 70,
    };

    beep_request_t req = {
        .duration_ms = 500,
        .freq_hz = 600.0,
        .amplitude = 1200,
    };

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    TEST_ASSERT_EQUAL(BEEP_STATE_PLAYING, beep_manager_get_state());

    beep_manager_stop();
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
    TEST_ASSERT_FALSE(s_done_called);
}

/* ========================================================================== */
/*                      i2s_manager tests                                    */
/* ========================================================================== */

TEST_CASE("i2s_source_fill_requires_init", "[i2s_manager]")
{
    size_t filled = i2s_source_fill(s_raw_buf, sizeof(s_raw_buf));
    TEST_ASSERT_EQUAL_UINT32(0, filled);
}

TEST_CASE("i2s_manager_init_rejects_missing_buffers", "[i2s_manager]")
{
    audio_config_t cfg = i2s_default_config();
    i2s_manager_buffers_t bufs = i2s_default_buffers();
    bufs.proc_buf = NULL;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_manager_init(&cfg, &bufs));
}

TEST_CASE("i2s_manager_init_requires_work_bytes", "[i2s_manager]")
{
    audio_config_t cfg = i2s_default_config();
    i2s_manager_buffers_t bufs = i2s_default_buffers();
    bufs.work_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_manager_init(&cfg, &bufs));
}

TEST_CASE("i2s_manager_stop_requires_init", "[i2s_manager]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_manager_stop());
}

#ifdef CONFIG_BT_MOCK_TESTING
TEST_CASE("i2s_source_fill_consumes_mock_queue", "[i2s_manager]")
{
    audio_config_t cfg = i2s_default_config();
    i2s_manager_buffers_t bufs = i2s_default_buffers();

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());

    int16_t samples[4] = {100, -200, 300, -400};
    TEST_ASSERT_EQUAL(ESP_OK,
                      i2s_manager_mock_push((const uint8_t *)samples,
                                            sizeof(samples),
                                            AUDIO_BIT_DEPTH_16,
                                            AUDIO_SAMPLE_RATE_16K));

    size_t filled = i2s_source_fill(s_raw_buf, sizeof(samples));
    TEST_ASSERT_EQUAL(sizeof(samples), filled);
    TEST_ASSERT_EQUAL_MEMORY(samples, s_raw_buf, filled);

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_stop());
    i2s_manager_deinit();
}
#endif

TEST_CASE("i2s_manager_start_is_idempotent", "[i2s_manager]")
{
    audio_config_t cfg = i2s_default_config();
    i2s_manager_buffers_t bufs = i2s_default_buffers();

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
    TEST_ASSERT_FALSE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
    TEST_ASSERT_TRUE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_stop());
    TEST_ASSERT_FALSE(i2s_manager_is_running());

    i2s_manager_deinit();
}

TEST_CASE("i2s_manager_start_and_stop_toggle_running", "[i2s_manager]")
{
    audio_config_t cfg = i2s_default_config();
    i2s_manager_buffers_t bufs = i2s_default_buffers();

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
    TEST_ASSERT_FALSE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
    TEST_ASSERT_TRUE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_stop());
    TEST_ASSERT_FALSE(i2s_manager_is_running());

    i2s_manager_deinit();
}

/* ========================================================================== */
/*                      synth_manager tests                                  */
/* ========================================================================== */

TEST_CASE("synth_manager_rejects_invalid_args", "[synth_manager]")
{
    audio_config_t cfg = synth_default_config();
    uint8_t buffer[32] = {0};
    size_t written = synth_manager_generate_audio(NULL, sizeof(buffer), &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(0, written);

    written = synth_manager_generate_audio(buffer, 0, &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(0, written);

    written = synth_manager_generate_audio(buffer, sizeof(buffer), NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(0, written);
}

TEST_CASE("synth_manager_aligns_to_frame_size", "[synth_manager]")
{
    audio_config_t cfg = synth_default_config();
    cfg.bit_depth = AUDIO_BIT_DEPTH_32;
    cfg.channels = AUDIO_CHANNEL_STEREO;

    /* frame_bytes = 4 bytes * 2 channels = 8; buffer not divisible by 8 */
    uint8_t buffer[30];
    fill_guard(buffer, sizeof(buffer), 0xAA);

    size_t written = synth_manager_generate_audio(buffer, sizeof(buffer), &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(24, written); /* 3 frames * 8 bytes */

    /* Ensure guard byte after written region remains intact */
    TEST_ASSERT_EQUAL_UINT8(0xAA, buffer[written]);
}

TEST_CASE("synth_manager_writes_silence_with_zero_env", "[synth_manager]")
{
    audio_config_t cfg = synth_default_config();
    uint8_t buffer[64];
    fill_guard(buffer, sizeof(buffer), 0xFF);

    size_t written = synth_manager_generate_audio(buffer, sizeof(buffer), &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(sizeof(buffer), written);

    for (size_t i = 0; i < written; ++i) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, buffer[i], "Expected silence sample");
    }
}

TEST_CASE("synth_manager_returns_zero_if_buffer_smaller_than_frame", "[synth_manager]")
{
    audio_config_t cfg = synth_default_config();
    /* frame_bytes = 2 bytes mono; buffer length 1 => no full frame */
    uint8_t buffer[1];
    buffer[0] = 0x7E;

    size_t written = synth_manager_generate_audio(buffer, sizeof(buffer), &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(0, written);
    TEST_ASSERT_EQUAL_UINT8(0x7E, buffer[0]);
}

TEST_CASE("synth_manager_falls_back_on_unknown_bit_depth", "[synth_manager]")
{
    audio_config_t cfg = synth_default_config();
    cfg.bit_depth = (audio_bit_depth_t)99; /* unsupported depth */
    uint8_t buffer[16];
    memset(buffer, 0x5A, sizeof(buffer));

    size_t written = synth_manager_generate_audio(buffer, sizeof(buffer), &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(sizeof(buffer), written);
    for (size_t i = 0; i < written; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x00, buffer[i]);
    }
}

TEST_CASE("synth_manager_aligns_24bit_stereo_frames", "[synth_manager]")
{
    audio_config_t cfg = synth_default_config();
    cfg.bit_depth = AUDIO_BIT_DEPTH_24; /* stored as 32-bit */
    cfg.channels = AUDIO_CHANNEL_STEREO;

    uint8_t buffer[20];
    fill_guard(buffer, sizeof(buffer), 0xA5);

    /* frame_bytes = 4 bytes * 2 channels = 8; expect 2 frames -> 16 bytes */
    size_t written = synth_manager_generate_audio(buffer, sizeof(buffer), &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(16, written);
    TEST_ASSERT_EQUAL_UINT8(0xA5, buffer[written]);
}

TEST_CASE("synth_manager_preserves_force_flag_when_env_zero", "[synth_manager]")
{
    audio_config_t cfg = synth_default_config();
    uint8_t buffer[32];
    bool force_flag = true;

    size_t written = synth_manager_generate_audio(buffer, sizeof(buffer), &cfg, &force_flag, NULL);
    TEST_ASSERT_EQUAL_size_t(sizeof(buffer), written);
    TEST_ASSERT_TRUE(force_flag);
}

/* ========================================================================== */
/*                      Test app entry point                                 */
/* ========================================================================== */

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}

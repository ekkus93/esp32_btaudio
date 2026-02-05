#include "unity.h"
#include "unity_test_runner.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "beep_manager.h"

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

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}

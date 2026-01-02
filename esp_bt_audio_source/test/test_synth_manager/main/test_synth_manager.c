#include "unity.h"
#include "unity_test_runner.h"

#include <string.h>
#include <math.h>

#include "synth_manager.h"

static audio_config_t default_config(void)
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

void setUp(void)
{
    synth_manager_reset_state();
}

void tearDown(void)
{
    synth_manager_reset_state();
}

TEST_CASE("synth_manager_rejects_invalid_args", "[synth_manager]")
{
    audio_config_t cfg = default_config();
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
    audio_config_t cfg = default_config();
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
    audio_config_t cfg = default_config();
    uint8_t buffer[64];
    fill_guard(buffer, sizeof(buffer), 0xFF);

    size_t written = synth_manager_generate_audio(buffer, sizeof(buffer), &cfg, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(sizeof(buffer), written);

    for (size_t i = 0; i < written; ++i) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, buffer[i], "Expected silence sample");
    }
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}

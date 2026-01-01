#include "unity.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "synth_manager.h"
#include "audio_processor.h"

static audio_config_t make_config(void)
{
    audio_config_t cfg = {0};
    cfg.sample_rate = AUDIO_SAMPLE_RATE_16K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_MONO;
    cfg.volume = 80;
    cfg.mute = false;
    cfg.i2s_port = 0;
    cfg.i2s_bclk_pin = -1;
    cfg.i2s_ws_pin = -1;
    cfg.i2s_din_pin = -1;
    cfg.i2s_dout_pin = -1;
    return cfg;
}

void setUp(void)
{
    synth_manager_reset_state();
}

void tearDown(void)
{
    synth_manager_reset_state();
}

void test_generate_should_return_zero_on_invalid_args(void)
{
    uint8_t buf[32];
    audio_config_t cfg = make_config();
    bool force = true;

    TEST_ASSERT_EQUAL_size_t(0, synth_manager_generate_audio(NULL, sizeof(buf), &cfg, &force, NULL));
    TEST_ASSERT_EQUAL_size_t(0, synth_manager_generate_audio(buf, 0, &cfg, &force, NULL));
    TEST_ASSERT_EQUAL_size_t(0, synth_manager_generate_audio(buf, sizeof(buf), NULL, &force, NULL));
}

void test_generate_should_fill_whole_frames_and_keep_default_silence(void)
{
    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    bool force = true;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    /* 16-bit mono => 2 bytes per frame, clamp to multiple of frame size */
    TEST_ASSERT_EQUAL_size_t(sizeof(buf), written);
    for (size_t i = 0; i < written; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
    }
    TEST_ASSERT_TRUE(force); /* untouched when fade is inactive */
}

void test_generate_should_clamp_to_frame_boundary(void)
{
    uint8_t buf[5];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    TEST_ASSERT_EQUAL_size_t(4, written); /* 2-byte frame => 4 bytes used */
    for (size_t i = 0; i < written; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
    }
    TEST_ASSERT_FALSE(force);
    /* The last byte beyond written should remain untouched. */
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[4]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_generate_should_return_zero_on_invalid_args);
    RUN_TEST(test_generate_should_fill_whole_frames_and_keep_default_silence);
    RUN_TEST(test_generate_should_clamp_to_frame_boundary);
    return UNITY_END();
}

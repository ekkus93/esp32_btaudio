#include "unity.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "synth_manager.h"
#include "audio_processor.h"

/* NOTE on coverage: the fade-envelope block inside synth_manager_generate_audio
 * (s_synth_fade_active / s_synth_env ramp) is currently dead in this build — no
 * linkable symbol sets s_synth_fade_active=true or starts a fade, so s_synth_env
 * stays 0 and generated output is always silence. These tests therefore lock the
 * "silent until a fade is started" contract and exercise every *reachable* branch
 * (bit-depth container widths, stereo interleave, frame clamping, nyquist guard,
 * source_fill, is_active). The fade ramp math cannot be covered from host without
 * a fade-activation API. */

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

static void assert_all_zero(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
    }
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
    assert_all_zero(buf, written);
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
    assert_all_zero(buf, written);
    TEST_ASSERT_FALSE(force);
    /* The last byte beyond written should remain untouched. */
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[4]);
}

/* --- bit-depth container widths --- */

void test_generate_32bit_uses_4byte_container_mono(void)
{
    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    cfg.bit_depth = AUDIO_BIT_DEPTH_32;
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    /* 32-bit mono => 4 bytes per frame; 64/4 = 16 frames => 64 bytes */
    TEST_ASSERT_EQUAL_size_t(64, written);
    assert_all_zero(buf, written);
}

void test_generate_24bit_uses_4byte_container(void)
{
    uint8_t buf[40];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    cfg.bit_depth = AUDIO_BIT_DEPTH_24; /* stored in 32-bit container => 4 bytes */
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    /* 24-bit stored in 4-byte container, mono => 4-byte frame; 40/4 = 10 frames */
    TEST_ASSERT_EQUAL_size_t(40, written);
    assert_all_zero(buf, written);
}

void test_generate_32bit_clamps_to_frame_boundary(void)
{
    uint8_t buf[10];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    cfg.bit_depth = AUDIO_BIT_DEPTH_32; /* 4-byte frame (mono) */
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    TEST_ASSERT_EQUAL_size_t(8, written); /* 10 -> 2 frames of 4 bytes */
    assert_all_zero(buf, written);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[8]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[9]);
}

void test_generate_invalid_bit_depth_defaults_to_2byte(void)
{
    uint8_t buf[8];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    cfg.bit_depth = (audio_bit_depth_t)99; /* unsupported => bytes_per_sample clamps to 2 */
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    /* Clamped to 2-byte samples, mono => 2-byte frame; 8/2 = 4 frames => 8 bytes */
    TEST_ASSERT_EQUAL_size_t(8, written);
    assert_all_zero(buf, written);
}

/* --- channels --- */

void test_generate_stereo_doubles_frame_bytes(void)
{
    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    cfg.channels = AUDIO_CHANNEL_STEREO; /* 16-bit stereo => 4-byte frame */
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    TEST_ASSERT_EQUAL_size_t(64, written); /* 64/4 = 16 stereo frames */
    assert_all_zero(buf, written);
}

void test_generate_stereo_clamps_odd_buffer(void)
{
    uint8_t buf[6]; /* 4-byte stereo frame => only one frame fits, 2 bytes left */
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    cfg.channels = AUDIO_CHANNEL_STEREO;
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    TEST_ASSERT_EQUAL_size_t(4, written);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[4]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[5]);
}

void test_generate_32bit_stereo_uses_8byte_frame(void)
{
    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));
    audio_config_t cfg = make_config();
    cfg.bit_depth = AUDIO_BIT_DEPTH_32;
    cfg.channels = AUDIO_CHANNEL_STEREO; /* 8-byte frame */
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    TEST_ASSERT_EQUAL_size_t(64, written); /* 64/8 = 8 frames */
    assert_all_zero(buf, written);
}

/* --- sample-rate / nyquist guard branches --- */

void test_generate_low_sample_rate_hits_nyquist_guard(void)
{
    /* 8K: nyquist_guard = 8000/2 - 1000 = 3000 < 20000 => tone clamped to 3000 */
    uint8_t buf[32];
    audio_config_t cfg = make_config();
    cfg.sample_rate = AUDIO_SAMPLE_RATE_8K;
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    TEST_ASSERT_EQUAL_size_t(32, written);
    assert_all_zero(buf, written);
}

void test_generate_high_sample_rate_keeps_full_tone(void)
{
    /* 96K: nyquist_guard = 48000 - 1000 = 47000 > 20000 => tone stays 20000 */
    uint8_t buf[32];
    audio_config_t cfg = make_config();
    cfg.sample_rate = AUDIO_SAMPLE_RATE_96K;
    bool force = false;

    size_t written = synth_manager_generate_audio(buf, sizeof(buf), &cfg, &force, NULL);

    TEST_ASSERT_EQUAL_size_t(32, written);
    assert_all_zero(buf, written);
}

/* --- source_fill / is_active --- */

void test_source_fill_returns_bytes_using_global_config(void)
{
    /* stub_audio_config.c provides s_audio_config = 48K/16-bit/stereo (4-byte frame) */
    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));

    size_t written = synth_source_fill(buf, sizeof(buf));

    TEST_ASSERT_EQUAL_size_t(64, written);
    assert_all_zero(buf, written);
}

void test_source_fill_invalid_args_returns_zero(void)
{
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_size_t(0, synth_source_fill(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, synth_source_fill(buf, 0));
}

void test_source_is_active_false_when_idle(void)
{
    synth_manager_reset_state();
    TEST_ASSERT_FALSE(synth_source_is_active());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_generate_should_return_zero_on_invalid_args);
    RUN_TEST(test_generate_should_fill_whole_frames_and_keep_default_silence);
    RUN_TEST(test_generate_should_clamp_to_frame_boundary);
    RUN_TEST(test_generate_32bit_uses_4byte_container_mono);
    RUN_TEST(test_generate_24bit_uses_4byte_container);
    RUN_TEST(test_generate_32bit_clamps_to_frame_boundary);
    RUN_TEST(test_generate_invalid_bit_depth_defaults_to_2byte);
    RUN_TEST(test_generate_stereo_doubles_frame_bytes);
    RUN_TEST(test_generate_stereo_clamps_odd_buffer);
    RUN_TEST(test_generate_32bit_stereo_uses_8byte_frame);
    RUN_TEST(test_generate_low_sample_rate_hits_nyquist_guard);
    RUN_TEST(test_generate_high_sample_rate_keeps_full_tone);
    RUN_TEST(test_source_fill_returns_bytes_using_global_config);
    RUN_TEST(test_source_fill_invalid_args_returns_zero);
    RUN_TEST(test_source_is_active_false_when_idle);
    return UNITY_END();
}

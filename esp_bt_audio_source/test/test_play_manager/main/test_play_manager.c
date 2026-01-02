#include "unity.h"
#include "unity_test_runner.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_spiffs.h"

#include "audio_queue.h"
#include "audio_util.h"
#include "play_manager.h"

static const char *k_spiffs_label = "spiffs";
static const char *k_wav_path = "/spiffs/test_play_manager.wav";

static uint8_t s_proc_buf[AUDIO_CHUNK_BLOCK_BYTES * 2];
static uint8_t s_proc_buf2[AUDIO_CHUNK_BLOCK_BYTES * 2];
static bool s_spiffs_mounted = false;

static audio_config_t test_audio_config(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 100,
        .mute = false,
        .i2s_port = 0,
        .i2s_bclk_pin = GPIO_NUM_NC,
        .i2s_ws_pin = GPIO_NUM_NC,
        .i2s_din_pin = GPIO_NUM_NC,
        .i2s_dout_pin = GPIO_NUM_NC,
    };
    return cfg;
}

static void mount_spiffs(void)
{
    if (s_spiffs_mounted) {
        return;
    }

    const esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = k_spiffs_label,
        .max_files = 4,
        .format_if_mount_failed = true,
    };

    TEST_ESP_OK(esp_vfs_spiffs_register(&conf));
    s_spiffs_mounted = true;
}

static void unmount_spiffs(void)
{
    if (!s_spiffs_mounted) {
        return;
    }
    esp_vfs_spiffs_unregister(k_spiffs_label);
    s_spiffs_mounted = false;
}

static bool write_pcm16_mono_wav(const char *path, const int16_t *samples, size_t sample_count, uint32_t sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }

    const uint32_t data_bytes = (uint32_t)(sample_count * sizeof(int16_t));
    const uint32_t riff_size = 36u + data_bytes;
    const uint32_t fmt_chunk_size = 16u;
    const uint16_t audio_format = 1u; /* PCM */
    const uint16_t num_channels = 1u;
    const uint32_t byte_rate = sample_rate * num_channels * (uint32_t)sizeof(int16_t);
    const uint16_t block_align = num_channels * (uint16_t)sizeof(int16_t);
    const uint16_t bits_per_sample = 16u;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_chunk_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
    fwrite(samples, sizeof(int16_t), sample_count, f);

    fclose(f);
    return true;
}

static void cleanup_env(void)
{
    play_manager_deinit();
    audio_chunk_pool_deinit();
    remove(k_wav_path);
    unmount_spiffs();
}

static bool write_custom_wav(const char *path, uint16_t audio_format, uint16_t bits_per_sample, bool short_fmt, bool omit_data)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }

    const uint32_t riff_size = 36u;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    uint32_t fmt_size = short_fmt ? 8u : 16u;
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    if (!short_fmt) {
        uint16_t channels = 1;
        uint32_t sample_rate = 44100;
        uint32_t byte_rate = sample_rate * 2u;
        uint16_t block_align = 2;
        fwrite(&channels, 2, 1, f);
        fwrite(&sample_rate, 4, 1, f);
        fwrite(&byte_rate, 4, 1, f);
        fwrite(&block_align, 2, 1, f);
        fwrite(&bits_per_sample, 2, 1, f);
    }

    if (!omit_data) {
        uint32_t data_size = 4u;
        uint32_t zero = 0;
        fwrite("data", 1, 4, f);
        fwrite(&data_size, 4, 1, f);
        fwrite(&zero, 1, sizeof(zero), f);
    }

    fclose(f);
    return true;
}

TEST_CASE("play_manager_init_rejects_null_args", "[play_manager]")
{
    play_manager_buffers_t bufs = {
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    audio_config_t cfg = test_audio_config();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, play_manager_init(NULL, &bufs));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, play_manager_init(&cfg, NULL));

    cleanup_env();
}

TEST_CASE("play_manager_requires_init_and_valid_path", "[play_manager]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, play_manager_play_wav(k_wav_path));

    play_manager_buffers_t bufs = {
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    audio_config_t cfg = test_audio_config();

    TEST_ESP_OK(play_manager_init(&cfg, &bufs));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, play_manager_play_wav(k_wav_path));

    cleanup_env();
}

TEST_CASE("play_manager_streams_and_drains_wav", "[play_manager]")
{
    mount_spiffs();
    TEST_ASSERT_TRUE(audio_chunk_pool_init());

    const int16_t samples[] = {0, 1000, -1000, 500};
    TEST_ASSERT_TRUE(write_pcm16_mono_wav(k_wav_path, samples, sizeof(samples) / sizeof(samples[0]), (uint32_t)AUDIO_SAMPLE_RATE_16K));

    play_manager_buffers_t bufs = {
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    audio_config_t cfg = test_audio_config();
    TEST_ESP_OK(play_manager_init(&cfg, &bufs));

    TEST_ESP_OK(play_manager_play_wav(k_wav_path));
    TEST_ASSERT_TRUE(play_manager_is_active());

    /* Drain queued audio and notify consumption. */
    size_t consumed = 0;
    bool drained = false;
    for (int i = 0; i < 8 && !drained; ++i) {
        audio_chunk_t chunk = {0};
        if (!audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(50))) {
            /* Try to enqueue more if the queue ran dry. */
            TEST_ESP_OK(play_manager_fill());
            continue;
        }
        consumed += chunk.len;
        drained = play_manager_consume(chunk.len);
        audio_chunk_release_block(chunk.data);
        if (!drained) {
            TEST_ESP_OK(play_manager_fill());
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(drained, "play_manager_consume should report drained");
    TEST_ASSERT_FALSE(play_manager_is_active());
    TEST_ASSERT_EQUAL(0, play_manager_pending_bytes());
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)consumed);

    cleanup_env();
}

TEST_CASE("play_manager_abort_clears_state", "[play_manager]")
{
    mount_spiffs();
    TEST_ASSERT_TRUE(audio_chunk_pool_init());

    const int16_t samples[] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(write_pcm16_mono_wav(k_wav_path, samples, sizeof(samples) / sizeof(samples[0]), (uint32_t)AUDIO_SAMPLE_RATE_16K));

    play_manager_buffers_t bufs = {
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    audio_config_t cfg = test_audio_config();
    TEST_ESP_OK(play_manager_init(&cfg, &bufs));

    TEST_ESP_OK(play_manager_play_wav(k_wav_path));
    TEST_ASSERT_TRUE(play_manager_is_active());

    play_manager_abort(false);

    TEST_ASSERT_FALSE(play_manager_is_active());
    TEST_ASSERT_EQUAL(0, play_manager_pending_bytes());

    /* Clear any queued blocks from the pre-abort fill. */
    audio_chunk_clear();

    cleanup_env();
}

TEST_CASE("play_manager_rejects_short_fmt_chunk", "[play_manager]")
{
    mount_spiffs();
    TEST_ASSERT_TRUE(audio_chunk_pool_init());

    play_manager_buffers_t bufs = {
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    audio_config_t cfg = test_audio_config();

    TEST_ESP_OK(play_manager_init(&cfg, &bufs));
    TEST_ASSERT_TRUE(write_custom_wav(k_wav_path, 1, 16, true, false));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, play_manager_play_wav(k_wav_path));
    TEST_ASSERT_FALSE(play_manager_is_active());
    TEST_ASSERT_EQUAL(0, play_manager_pending_bytes());

    cleanup_env();
}

TEST_CASE("play_manager_rejects_non_pcm", "[play_manager]")
{
    mount_spiffs();
    TEST_ASSERT_TRUE(audio_chunk_pool_init());

    play_manager_buffers_t bufs = {
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    audio_config_t cfg = test_audio_config();

    TEST_ESP_OK(play_manager_init(&cfg, &bufs));
    TEST_ASSERT_TRUE(write_custom_wav(k_wav_path, 3 /* IEEE float */, 16, false, false));

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, play_manager_play_wav(k_wav_path));
    TEST_ASSERT_FALSE(play_manager_is_active());
    TEST_ASSERT_EQUAL(0, play_manager_pending_bytes());

    cleanup_env();
}

TEST_CASE("play_manager_rejects_missing_data_chunk", "[play_manager]")
{
    mount_spiffs();
    TEST_ASSERT_TRUE(audio_chunk_pool_init());

    play_manager_buffers_t bufs = {
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    audio_config_t cfg = test_audio_config();

    TEST_ESP_OK(play_manager_init(&cfg, &bufs));
    TEST_ASSERT_TRUE(write_custom_wav(k_wav_path, 1, 16, false, true));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, play_manager_play_wav(k_wav_path));
    TEST_ASSERT_FALSE(play_manager_is_active());
    TEST_ASSERT_EQUAL(0, play_manager_pending_bytes());

    cleanup_env();
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}

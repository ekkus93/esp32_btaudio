#include "unity.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "audio_queue.h"
#include "play_manager.h"
#include "audio_processor.h"

static const char *k_test_wav_path = "/tmp/play_manager_host.wav";

static void write_test_wav(void)
{
    /* Simple 44.1 kHz stereo, 16-bit PCM with 4 frames. */
    const uint16_t samples[] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888};
    const uint32_t data_bytes = sizeof(samples);
    const uint32_t riff_size = 36 + data_bytes;
    const uint16_t audio_format = 1;
    const uint16_t num_channels = 2;
    const uint32_t sample_rate = 44100;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    const uint16_t block_align = num_channels * (bits_per_sample / 8);

    FILE *f = fopen(k_test_wav_path, "wb");
    TEST_ASSERT_NOT_NULL(f);

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    const uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
    fwrite(samples, sizeof(samples), 1, f);

    fclose(f);
}

void setUp(void)
{
    audio_chunk_pool_deinit();
    play_manager_deinit();
    remove(k_test_wav_path);
}

void tearDown(void)
{
    audio_chunk_pool_deinit();
    play_manager_deinit();
    remove(k_test_wav_path);
}

void test_play_manager_queues_wav_blocks_without_i2s_buffers(void)
{
    write_test_wav();
    TEST_ASSERT_TRUE(audio_chunk_pool_init());

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80,
        .mute = false,
        .i2s_port = 0,
    };
    play_manager_buffers_t bufs = {
        .work_bytes = 4096,
    };

    TEST_ASSERT_EQUAL(ESP_OK, play_manager_init(&cfg, &bufs));
    TEST_ASSERT_EQUAL(ESP_OK, play_manager_play_wav(k_test_wav_path));

    /* Fill should allocate queue-owned blocks and enqueue WAV-tagged data. */
    TEST_ASSERT_EQUAL(ESP_OK, play_manager_fill());

    audio_chunk_t chunk = {0};
    TEST_ASSERT_TRUE(audio_chunk_dequeue(&chunk, 0));
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_WAV, chunk.tag);
    TEST_ASSERT_NOT_NULL(chunk.data);
    TEST_ASSERT_TRUE(chunk.len > 0 && chunk.len <= AUDIO_CHUNK_BLOCK_BYTES);

    audio_chunk_release_block(chunk.data);
    play_manager_deinit();
    audio_chunk_pool_deinit();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_play_manager_queues_wav_blocks_without_i2s_buffers);
    return UNITY_END();
}

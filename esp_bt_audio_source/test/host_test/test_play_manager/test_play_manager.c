#include "unity.h"

#include "play_manager.h"
#include "audio_queue.h"
#include "esp_err.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Stubs for conversion/resample helpers (passthrough) ----------------- */
esp_err_t convert_audio_format(const audio_convert_args_t *args)
{
    if (args == NULL || args->dst == NULL || args->dst_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (args->src_size > 0 && args->src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (args->src_size > 0) {
        memmove(args->dst, args->src, args->src_size);
    }
    *args->dst_size = args->src_size;
    return ESP_OK;
}

esp_err_t resample_audio(const audio_resample_args_t *args)
{
    if (args == NULL || args->dst == NULL || args->dst_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (args->src_size > 0 && args->src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (args->src_size > 0) {
        memmove(args->dst, args->src, args->src_size);
    }
    *args->dst_size = args->src_size;
    return ESP_OK;
}

/* ---- Test helpers -------------------------------------------------------- */
static audio_config_t make_config(void)
{
    audio_config_t cfg = {0};
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 80;
    cfg.mute = false;
    cfg.i2s_port = 0;
    cfg.i2s_bclk_pin = -1;
    cfg.i2s_ws_pin = -1;
    cfg.i2s_din_pin = -1;
    cfg.i2s_dout_pin = -1;
    return cfg;
}

static char *write_temp_wav(const uint8_t *payload, size_t payload_len)
{
    char tmpl[] = "/tmp/pm_wavXXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return NULL;
    }
    FILE *f = fdopen(fd, "wb");
    if (f == NULL) {
        close(fd);
        unlink(tmpl);
        return NULL;
    }

    const uint32_t riff_size = 36U + (uint32_t)payload_len;
    const uint16_t audio_format = 1;
    const uint16_t channels = 2;
    const uint32_t sample_rate = 44100;
    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    const uint32_t byte_rate = sample_rate * block_align;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    uint32_t fmt_size = 16;
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    uint32_t data_size = (uint32_t)payload_len;
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    if (payload_len > 0 && payload != NULL) {
        fwrite(payload, 1, payload_len, f);
    }

    fflush(f);
    fclose(f);
    return strdup(tmpl);
}

static char *write_invalid_wav(void)
{
    char tmpl[] = "/tmp/pm_wav_badXXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return NULL;
    }
    FILE *f = fdopen(fd, "wb");
    if (f == NULL) {
        close(fd);
        unlink(tmpl);
        return NULL;
    }
    /* Deliberately malformed header */
    fwrite("BADA", 1, 4, f);
    fflush(f);
    fclose(f);
    return strdup(tmpl);
}

static void remove_file(char *path)
{
    if (path != NULL) {
        unlink(path);
        free(path);
    }
}

/* ---- Unity fixtures ------------------------------------------------------ */
static play_manager_buffers_t make_buffers(void)
{
    static uint8_t proc_buf[2048];
    static uint8_t proc_buf2[2048];
    play_manager_buffers_t bufs = {
        .proc_buf = proc_buf,
        .proc_buf2 = proc_buf2,
        .work_bytes = sizeof(proc_buf),
    };
    return bufs;
}

void setUp(void)
{
    audio_chunk_clear();
    audio_chunk_pool_deinit();
    TEST_ASSERT_TRUE_MESSAGE(audio_chunk_pool_init(), "audio_chunk_pool_init failed");
    play_manager_deinit();
}

void tearDown(void)
{
    play_manager_deinit();
    audio_chunk_clear();
    audio_chunk_pool_deinit();
}

/* ---- Tests --------------------------------------------------------------- */
void test_init_should_reject_invalid_args(void)
{
    play_manager_buffers_t bufs = {0};
    audio_config_t cfg = make_config();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, play_manager_init(NULL, &bufs));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, play_manager_init(&cfg, NULL));
}

void test_init_and_deinit_should_succeed_twice(void)
{
    audio_config_t cfg = make_config();
    play_manager_buffers_t bufs = make_buffers();

    TEST_ASSERT_EQUAL(ESP_OK, play_manager_init(&cfg, &bufs));
    TEST_ASSERT_EQUAL(ESP_OK, play_manager_init(&cfg, &bufs));
    play_manager_deinit();
}

void test_play_wav_should_stream_and_drain(void)
{
    audio_config_t cfg = make_config();
    play_manager_buffers_t bufs = make_buffers();

    uint8_t payload[64];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i + 1);
    }

    char *path = write_temp_wav(payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL_MESSAGE(path, "failed to create temp wav");

    TEST_ASSERT_EQUAL(ESP_OK, play_manager_init(&cfg, &bufs));
    TEST_ASSERT_EQUAL(ESP_OK, play_manager_play_wav(path));
    TEST_ASSERT_TRUE(play_manager_is_active());

    size_t pending = play_manager_pending_bytes();
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)pending);

    while (play_manager_pending_bytes() > 0) {
        size_t to_consume = play_manager_pending_bytes();
        bool drained = play_manager_consume(to_consume);
        if (drained) {
            break;
        }
    }

    TEST_ASSERT_FALSE(play_manager_is_active());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)play_manager_pending_bytes());

    remove_file(path);
}

void test_play_wav_invalid_header_should_fail(void)
{
    audio_config_t cfg = make_config();
    play_manager_buffers_t bufs = make_buffers();

    char *path = write_invalid_wav();
    TEST_ASSERT_NOT_NULL_MESSAGE(path, "failed to create invalid wav");

    TEST_ASSERT_EQUAL(ESP_OK, play_manager_init(&cfg, &bufs));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, play_manager_play_wav(path));

    remove_file(path);
}

void test_abort_should_stop_active_stream(void)
{
    audio_config_t cfg = make_config();
    play_manager_buffers_t bufs = make_buffers();

    uint8_t payload[32] = {0};
    char *path = write_temp_wav(payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL_MESSAGE(path, "failed to create temp wav");

    TEST_ASSERT_EQUAL(ESP_OK, play_manager_init(&cfg, &bufs));
    TEST_ASSERT_EQUAL(ESP_OK, play_manager_play_wav(path));
    TEST_ASSERT_TRUE(play_manager_is_active());

    play_manager_abort(false);

    TEST_ASSERT_FALSE(play_manager_is_active());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)play_manager_pending_bytes());

    remove_file(path);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_should_reject_invalid_args);
    RUN_TEST(test_init_and_deinit_should_succeed_twice);
    RUN_TEST(test_play_wav_should_stream_and_drain);
    RUN_TEST(test_play_wav_invalid_header_should_fail);
    RUN_TEST(test_abort_should_stop_active_stream);
    return UNITY_END();
}

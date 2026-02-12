#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"
#include "audio_ringbuffer.h"

#include <string.h>

static void init_defaults(void)
{
    s_is_initialized = true;
    s_drop_ring_audio = false;
    s_beep_remaining_bytes = 0;

    memset(&s_audio_stats, 0, sizeof(s_audio_stats));
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
}

void setUp(void)
{
    if (s_audio_ring != NULL) {
        audio_rb_deinit(s_audio_ring);
        s_audio_ring = NULL;
    }

    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&s_audio_ring, 256, false));
    init_defaults();
}

void tearDown(void)
{
    if (s_audio_ring != NULL) {
        audio_rb_deinit(s_audio_ring);
        s_audio_ring = NULL;
    }
}

void test_audio_processor_read_should_drain_ring_and_return_silence_when_drop_flag_set(void)
{
    uint8_t input[32];
    uint8_t output[16];
    size_t bytes_read = 0;

    memset(input, 0xA5, sizeof(input));
    memset(output, 0xCC, sizeof(output));

    TEST_ASSERT_EQUAL_UINT(sizeof(input), audio_rb_write(s_audio_ring, input, sizeof(input)));
    s_drop_ring_audio = true;

    esp_err_t err = audio_processor_read(output, sizeof(output), &bytes_read);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(output), bytes_read);
    TEST_ASSERT_FALSE(s_drop_ring_audio);
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(s_audio_ring));
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00, output, sizeof(output));
}

void test_audio_processor_read_should_zero_fill_underrun_during_beep(void)
{
    uint8_t input[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
    uint8_t output[16];
    size_t bytes_read = 0;

    memset(output, 0xEE, sizeof(output));
    TEST_ASSERT_EQUAL_UINT(sizeof(input), audio_rb_write(s_audio_ring, input, sizeof(input)));

    s_beep_remaining_bytes = 64;

    esp_err_t err = audio_processor_read(output, sizeof(output), &bytes_read);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(output), bytes_read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, output, sizeof(input));
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00, output + sizeof(input), sizeof(output) - sizeof(input));
    TEST_ASSERT_EQUAL_UINT(64, s_beep_remaining_bytes);

    TEST_ASSERT_EQUAL_UINT32(1, s_audio_stats.buffer_underruns);
    TEST_ASSERT_EQUAL_UINT64(sizeof(output) - sizeof(input), s_audio_stats.underrun_bytes);
    TEST_ASSERT_EQUAL_UINT64(sizeof(output), s_audio_stats.bytes_read);
}

void test_audio_processor_read_should_reject_null_bytes_read_pointer(void)
{
    uint8_t output[16] = {0};

    esp_err_t err = audio_processor_read(output, sizeof(output), NULL);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_processor_read_should_drain_ring_and_return_silence_when_drop_flag_set);
    RUN_TEST(test_audio_processor_read_should_zero_fill_underrun_during_beep);
    RUN_TEST(test_audio_processor_read_should_reject_null_bytes_read_pointer);
    return UNITY_END();
}

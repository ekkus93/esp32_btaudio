#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"
#include "audio_ringbuffer.h"

#include <stdlib.h>
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

void test_ring_buffer_capacity_meets_minimum_floor(void)
{
    /* The ring buffer created by setUp() must be at least AUDIO_MIN_RB_CAPACITY_BYTES.
     * Production default is 32 KiB (CONFIG_AUDIO_RB_CAPACITY_KB=32). setUp() creates
     * only 256 bytes for speed; this test uses a dedicated buffer instead. */
    audio_rb_t *rb = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, AUDIO_MIN_RB_CAPACITY_BYTES, false));
    TEST_ASSERT_NOT_NULL(rb);
    TEST_ASSERT_GREATER_OR_EQUAL(AUDIO_MIN_RB_CAPACITY_BYTES, audio_rb_capacity(rb));
    audio_rb_deinit(rb);
}

void test_audio_buffer_fill_and_drain_round_trip(void)
{
    /* setUp() creates a 256-byte ring buffer; inject half, drain, verify free bytes restore. */
    size_t initial_free = audio_rb_available_to_write(s_audio_ring);
    TEST_ASSERT_GREATER_THAN_UINT(0, initial_free);

    const size_t inject_bytes = 64;
    uint8_t data[64];
    memset(data, 0x5A, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(inject_bytes, audio_rb_write(s_audio_ring, data, inject_bytes));

    size_t after_inject_free = audio_rb_available_to_write(s_audio_ring);
    TEST_ASSERT_EQUAL_UINT(initial_free - inject_bytes, after_inject_free);

    uint8_t out[64];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(out, sizeof(out), &bytes_read));
    TEST_ASSERT_EQUAL_UINT(sizeof(out), bytes_read);

    size_t after_read_free = audio_rb_available_to_write(s_audio_ring);
    TEST_ASSERT_EQUAL_UINT(initial_free, after_read_free);
}

void test_audio_buffer_full_rejects_write_no_state_mutation(void)
{
    /* Fill the ring buffer completely, then assert the next write is rejected. */
    size_t cap = audio_rb_capacity(s_audio_ring);
    uint8_t *filler = (uint8_t *)malloc(cap);
    TEST_ASSERT_NOT_NULL(filler);
    memset(filler, 0, cap);
    TEST_ASSERT_EQUAL_UINT(cap, audio_rb_write(s_audio_ring, filler, cap));
    free(filler);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_rb_available_to_write(s_audio_ring));

    /* Ring is full; another write must not corrupt state. */
    uint8_t extra[8] = {0xFF};
    size_t written = audio_rb_write(s_audio_ring, extra, sizeof(extra));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)written);

    /* State unchanged: ring still full. */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_rb_available_to_write(s_audio_ring));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_processor_read_should_drain_ring_and_return_silence_when_drop_flag_set);
    RUN_TEST(test_audio_processor_read_should_zero_fill_underrun_during_beep);
    RUN_TEST(test_audio_processor_read_should_reject_null_bytes_read_pointer);
    /* TEST-2: ring buffer capacity floor */
    RUN_TEST(test_ring_buffer_capacity_meets_minimum_floor);
    /* TEST-1a: buffer fill / drain round-trip */
    RUN_TEST(test_audio_buffer_fill_and_drain_round_trip);
    /* TEST-1c: full buffer rejects write without state mutation */
    RUN_TEST(test_audio_buffer_full_rejects_write_no_state_mutation);
    return UNITY_END();
}

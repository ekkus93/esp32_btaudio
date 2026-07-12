#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"
#include "audio_ringbuffer.h"

#include <stdlib.h>
#include <string.h>

/* audio_processor_diag.c (linked for apply_volume) references this symbol from
 * its diag-summary path, which this test never exercises. Stub it to avoid
 * pulling in the whole audio_processor.c. */
uint32_t audio_processor_test_get_tag_miss_count(void) { return 0; }

static void init_defaults(void)
{
    s_is_initialized = true;
    s_drop_ring_audio = false;
    s_beep_remaining_bytes = 0;
    s_volume_gain = 100;   /* unity by default; volume test overrides */

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

/* Regression: audio_processor_read() must scale the output by s_volume_gain.
 * This is the test that was missing when apply_volume() sat as dead code and
 * the VOLUME command silently did nothing. */
void test_audio_processor_read_applies_volume_gain(void)
{
    int16_t input[16];
    for (int i = 0; i < 16; i++) {
        input[i] = 1000;
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(input),
                           audio_rb_write(s_audio_ring, (uint8_t *)input, sizeof(input)));

    s_volume_gain = 50;   /* 50% -> samples halve (1000 * 50 / 100 = 500) */

    int16_t output[16];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      audio_processor_read((uint8_t *)output, sizeof(output), &bytes_read));
    TEST_ASSERT_EQUAL_UINT(sizeof(output), bytes_read);
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(500, output[i]);
    }
}

/* Regression for the "loud static" bug: the A2DP buffer is 16-bit even when the
 * I2S input bit_depth is 24/32 (16-in-32 slots). The volume path must scale the
 * buffer as 16-bit regardless of s_audio_config.bit_depth. Before the fix this
 * took apply_volume()'s 32-bit branch and scrambled the samples. */
void test_audio_processor_read_volume_is_s16_even_when_bit_depth_32(void)
{
    int16_t input[16];
    for (int i = 0; i < 16; i++) {
        input[i] = 1000;
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(input),
                           audio_rb_write(s_audio_ring, (uint8_t *)input, sizeof(input)));

    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_32;   /* like the hardware */
    s_volume_gain = 50;

    int16_t output[16];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      audio_processor_read((uint8_t *)output, sizeof(output), &bytes_read));
    /* Each 16-bit sample must still be cleanly halved, not scrambled. */
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(500, output[i]);
    }
}

/* At unity (100) the samples must pass through unchanged (apply_volume no-op). */
void test_audio_processor_read_volume_unity_is_passthrough(void)
{
    int16_t input[8];
    for (int i = 0; i < 8; i++) {
        input[i] = (int16_t)(2000 + i);
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(input),
                           audio_rb_write(s_audio_ring, (uint8_t *)input, sizeof(input)));
    s_volume_gain = 100;

    int16_t output[8];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      audio_processor_read((uint8_t *)output, sizeof(output), &bytes_read));
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_INT16((int16_t)(2000 + i), output[i]);
    }
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

/* ── A2DP pull-rate tracking (UARTAUDIO diagnostics) ────────────────────
 * Time is injected via the UNIT_TEST note hook so tests are deterministic;
 * one test exercises the real audio_processor_read() feed path. */

void test_read_rate_no_activity_reports_zeros(void)
{
    audio_processor_test_read_rate_reset();
    audio_read_rate_t rr;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_read_rate(&rr));
    TEST_ASSERT_EQUAL_UINT32(0, rr.calls);
    TEST_ASSERT_EQUAL_UINT32(0, rr.bytes_requested);
    TEST_ASSERT_EQUAL_UINT32(0, rr.window_ms);
    TEST_ASSERT_EQUAL_UINT32(0, rr.rate_bps);
}

void test_read_rate_computes_bytes_per_second(void)
{
    audio_processor_test_read_rate_reset();
    /* 21 reads of 512 B, 10 ms apart -> 10752 B over a 200 ms window */
    for (int i = 0; i <= 20; i++) {
        audio_processor_test_read_rate_note(1000 + (uint32_t)i * 10, 512);
    }

    audio_read_rate_t rr;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_read_rate(&rr));
    TEST_ASSERT_EQUAL_UINT32(21, rr.calls);
    TEST_ASSERT_EQUAL_UINT32(21 * 512, rr.bytes_requested);
    TEST_ASSERT_EQUAL_UINT32(200, rr.window_ms);
    TEST_ASSERT_EQUAL_UINT32(21U * 512U * 1000U / 200U, rr.rate_bps);
}

void test_read_rate_short_window_reports_zero_rate(void)
{
    audio_processor_test_read_rate_reset();
    audio_processor_test_read_rate_note(1000, 512);
    audio_processor_test_read_rate_note(1050, 512); /* 50 ms < 100 ms floor */

    audio_read_rate_t rr;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_read_rate(&rr));
    TEST_ASSERT_EQUAL_UINT32(2, rr.calls);
    TEST_ASSERT_EQUAL_UINT32(0, rr.rate_bps); /* too little data to trust */
}

void test_read_rate_gap_starts_new_burst(void)
{
    audio_processor_test_read_rate_reset();
    for (int i = 0; i <= 20; i++) {
        audio_processor_test_read_rate_note(1000 + (uint32_t)i * 10, 512);
    }
    /* >1 s of silence (stream stopped), then activity resumes */
    audio_processor_test_read_rate_note(5000, 256);

    audio_read_rate_t rr;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_read_rate(&rr));
    TEST_ASSERT_EQUAL_UINT32(1, rr.calls);
    TEST_ASSERT_EQUAL_UINT32(256, rr.bytes_requested);
    TEST_ASSERT_EQUAL_UINT32(0, rr.window_ms);
    TEST_ASSERT_EQUAL_UINT32(0, rr.rate_bps);
}

void test_read_rate_goes_idle_after_gap(void)
{
    audio_processor_test_read_rate_reset();
    for (int i = 0; i <= 20; i++) {
        audio_processor_test_read_rate_note(1000 + (uint32_t)i * 10, 512);
    }
    /* stream stopped >1 s ago: the getter must report idle, not the
     * frozen last-burst rate */
    audio_processor_test_read_rate_set_now(3000);

    audio_read_rate_t rr;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_read_rate(&rr));
    TEST_ASSERT_EQUAL_UINT32(0, rr.calls);
    TEST_ASSERT_EQUAL_UINT32(0, rr.rate_bps);
}

void test_read_rate_null_arg_rejected(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_processor_get_read_rate(NULL));
}

void test_audio_processor_read_feeds_rate_tracker(void)
{
    audio_processor_test_read_rate_reset();
    uint8_t out[16];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(out, sizeof(out), &bytes_read));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(out, sizeof(out), &bytes_read));

    audio_read_rate_t rr;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_get_read_rate(&rr));
    TEST_ASSERT_EQUAL_UINT32(2, rr.calls);
    TEST_ASSERT_EQUAL_UINT32(32, rr.bytes_requested);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_processor_read_should_drain_ring_and_return_silence_when_drop_flag_set);
    RUN_TEST(test_audio_processor_read_should_zero_fill_underrun_during_beep);
    RUN_TEST(test_audio_processor_read_applies_volume_gain);
    RUN_TEST(test_audio_processor_read_volume_is_s16_even_when_bit_depth_32);
    RUN_TEST(test_audio_processor_read_volume_unity_is_passthrough);
    RUN_TEST(test_audio_processor_read_should_reject_null_bytes_read_pointer);
    /* TEST-2: ring buffer capacity floor */
    RUN_TEST(test_ring_buffer_capacity_meets_minimum_floor);
    /* TEST-1a: buffer fill / drain round-trip */
    RUN_TEST(test_audio_buffer_fill_and_drain_round_trip);
    /* TEST-1c: full buffer rejects write without state mutation */
    RUN_TEST(test_audio_buffer_full_rejects_write_no_state_mutation);
    RUN_TEST(test_read_rate_no_activity_reports_zeros);
    RUN_TEST(test_read_rate_computes_bytes_per_second);
    RUN_TEST(test_read_rate_short_window_reports_zero_rate);
    RUN_TEST(test_read_rate_gap_starts_new_burst);
    RUN_TEST(test_read_rate_goes_idle_after_gap);
    RUN_TEST(test_read_rate_null_arg_rejected);
    RUN_TEST(test_audio_processor_read_feeds_rate_tracker);
    return UNITY_END();
}

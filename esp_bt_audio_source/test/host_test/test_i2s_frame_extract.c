/**
 * test_i2s_frame_extract.c — unit tests for the S3->WROOM32 I2S payload
 * phase detector + extractor (i2s_frame_extract.c).
 *
 * The link carries 16 significant bits per 32-bit slot; the ESP32-classic
 * capture lands the two payload halves of each 64-bit frame at a phase that
 * shifts per enable session. All phase patterns tested here were observed on
 * real hardware during the 2026-07-11 bring-up:
 *   - {1,3} payload in the HIGH half of both words
 *   - {0,2} payload in the LOW half of both words
 *   - {2,3} both payloads packed into word 1 (word 0 all zeros)
 *   - {0,1} both payloads packed into word 0 (word 1 all zeros)
 *
 * Also pins:
 *   - wire ordering (word first, high half before low: MSB first on wire)
 *   - sign preservation
 *   - IN-PLACE extraction safety (out aliasing halves) — including the
 *     word-0-packed phase where a naive write-then-read clobbers h[0]
 *   - detect() returning PHASE_NONE for silent/short blocks
 */

#include <string.h>
#include "unity.h"
#include "i2s_frame_extract.h"

void setUp(void) {}
void tearDown(void) {}

#define NFRAMES 32
#define NHALVES (NFRAMES * 4)

/* Distinct, sign-mixed L/R ramps so ordering and sign bugs can't hide. */
static int16_t sample_l(int f) { return (int16_t)(100 * f - 1000); }
static int16_t sample_r(int f) { return (int16_t)(-200 * f + 500); }

/* Build frames with the L payload at half-offset offL and R at offR;
 * all other halves zero. */
static void build_frames(uint16_t *h, int off_l, int off_r)
{
    memset(h, 0, NHALVES * sizeof(uint16_t));
    for (int f = 0; f < NFRAMES; f++) {
        h[4 * f + off_l] = (uint16_t)sample_l(f);
        h[4 * f + off_r] = (uint16_t)sample_r(f);
    }
}

static void assert_extracted_lr(const int16_t *out)
{
    for (int f = 0; f < NFRAMES; f++) {
        TEST_ASSERT_EQUAL_INT16(sample_l(f), out[2 * f]);
        TEST_ASSERT_EQUAL_INT16(sample_r(f), out[2 * f + 1]);
    }
}

/* --- detection ---------------------------------------------------------- */

void test_detect_both_high_halves(void)
{
    uint16_t h[NHALVES];
    build_frames(h, 1, 3);  /* payload high half of word0 and word1 */
    /* wire order: word0-high (1) before word1-high (3) */
    TEST_ASSERT_EQUAL_INT(0x13, i2s_frame_extract_detect(h, NHALVES));
}

void test_detect_both_low_halves(void)
{
    uint16_t h[NHALVES];
    build_frames(h, 0, 2);  /* payload low half of word0 and word1 */
    TEST_ASSERT_EQUAL_INT(0x02, i2s_frame_extract_detect(h, NHALVES));
}

void test_detect_packed_in_word1(void)
{
    uint16_t h[NHALVES];
    /* word0 = zeros; word1 = [L(high)|R(low)] — L is first on the wire */
    build_frames(h, 3, 2);
    TEST_ASSERT_EQUAL_INT(0x32, i2s_frame_extract_detect(h, NHALVES));
}

void test_detect_packed_in_word0(void)
{
    uint16_t h[NHALVES];
    build_frames(h, 1, 0);  /* word0 = [L(high)|R(low)]; word1 = zeros */
    TEST_ASSERT_EQUAL_INT(0x10, i2s_frame_extract_detect(h, NHALVES));
}

void test_detect_silence_returns_none(void)
{
    uint16_t h[NHALVES];
    memset(h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(I2S_FRAME_PHASE_NONE,
                          i2s_frame_extract_detect(h, NHALVES));
}

void test_detect_short_block_returns_none(void)
{
    uint16_t h[NHALVES];
    build_frames(h, 1, 3);
    /* fewer than 16 frames of data is not enough to trust */
    TEST_ASSERT_EQUAL_INT(I2S_FRAME_PHASE_NONE,
                          i2s_frame_extract_detect(h, 15 * 4));
}

void test_detect_null_returns_none(void)
{
    TEST_ASSERT_EQUAL_INT(I2S_FRAME_PHASE_NONE,
                          i2s_frame_extract_detect(NULL, NHALVES));
}

/* --- extraction --------------------------------------------------------- */

void test_extract_both_high_halves(void)
{
    uint16_t h[NHALVES];
    int16_t out[NFRAMES * 2];
    build_frames(h, 1, 3);
    size_t n = i2s_frame_extract(h, NHALVES, 0x13, out);
    TEST_ASSERT_EQUAL_size_t(NFRAMES * 2, n);
    assert_extracted_lr(out);
}

void test_extract_both_low_halves(void)
{
    uint16_t h[NHALVES];
    int16_t out[NFRAMES * 2];
    build_frames(h, 0, 2);
    size_t n = i2s_frame_extract(h, NHALVES, 0x02, out);
    TEST_ASSERT_EQUAL_size_t(NFRAMES * 2, n);
    assert_extracted_lr(out);
}

void test_extract_packed_word1(void)
{
    uint16_t h[NHALVES];
    int16_t out[NFRAMES * 2];
    build_frames(h, 3, 2);
    size_t n = i2s_frame_extract(h, NHALVES, 0x32, out);
    TEST_ASSERT_EQUAL_size_t(NFRAMES * 2, n);
    assert_extracted_lr(out);
}

void test_extract_in_place_both_high(void)
{
    uint16_t h[NHALVES];
    build_frames(h, 1, 3);
    size_t n = i2s_frame_extract(h, NHALVES, 0x13, (int16_t *)h);
    TEST_ASSERT_EQUAL_size_t(NFRAMES * 2, n);
    assert_extracted_lr((const int16_t *)h);
}

void test_extract_in_place_packed_word0(void)
{
    /* Regression: phase {1,0} — L in word0-high, R in word0-LOW. A naive
     * in-place loop writes out[0] into h[0] BEFORE reading h[0] for out[1],
     * corrupting the very first R sample. The extractor must read both
     * halves into temporaries first. */
    uint16_t h[NHALVES];
    build_frames(h, 1, 0);
    size_t n = i2s_frame_extract(h, NHALVES, 0x10, (int16_t *)h);
    TEST_ASSERT_EQUAL_size_t(NFRAMES * 2, n);
    assert_extracted_lr((const int16_t *)h);
}

void test_extract_in_place_matches_out_of_place(void)
{
    uint16_t h1[NHALVES], h2[NHALVES];
    int16_t out[NFRAMES * 2];
    build_frames(h1, 1, 0);
    memcpy(h2, h1, sizeof(h1));
    i2s_frame_extract(h1, NHALVES, 0x10, out);            /* out-of-place */
    i2s_frame_extract(h2, NHALVES, 0x10, (int16_t *)h2);  /* in-place */
    TEST_ASSERT_EQUAL_INT16_ARRAY(out, (const int16_t *)h2, NFRAMES * 2);
}

void test_extract_preserves_negative_extremes(void)
{
    uint16_t h[8];
    int16_t out[4];
    memset(h, 0, sizeof(h));
    h[1] = (uint16_t)-32768;  /* INT16_MIN, word0 high */
    h[3] = (uint16_t)32767;   /* INT16_MAX, word1 high */
    h[5] = (uint16_t)-1;
    h[7] = (uint16_t)1;
    size_t n = i2s_frame_extract(h, 8, 0x13, out);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_INT16(-32768, out[0]);
    TEST_ASSERT_EQUAL_INT16(32767, out[1]);
    TEST_ASSERT_EQUAL_INT16(-1, out[2]);
    TEST_ASSERT_EQUAL_INT16(1, out[3]);
}

void test_extract_rejects_bad_phase(void)
{
    uint16_t h[NHALVES];
    int16_t out[NFRAMES * 2];
    build_frames(h, 1, 3);
    TEST_ASSERT_EQUAL_size_t(0, i2s_frame_extract(h, NHALVES,
                                                  I2S_FRAME_PHASE_NONE, out));
    TEST_ASSERT_EQUAL_size_t(0, i2s_frame_extract(h, NHALVES, 0x11, out));
    TEST_ASSERT_EQUAL_size_t(0, i2s_frame_extract(h, NHALVES, 0x14, out));
    TEST_ASSERT_EQUAL_size_t(0, i2s_frame_extract(NULL, NHALVES, 0x13, out));
    TEST_ASSERT_EQUAL_size_t(0, i2s_frame_extract(h, NHALVES, 0x13, NULL));
}

void test_detect_then_extract_round_trip_all_phases(void)
{
    /* The pairs the hardware has produced, plus the remaining legal ones.
     * L always goes at the wire-earlier offset (wire order: word first,
     * high half before low; keys 1 < 0 < 3 < 2). */
    static const int offs[][2] = {
        {1, 3}, {0, 2}, {3, 2}, {1, 0}, {1, 2}, {0, 3},
    };
    for (size_t i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) {
        uint16_t h[NHALVES];
        int16_t out[NFRAMES * 2];
        build_frames(h, offs[i][0], offs[i][1]);
        int phase = i2s_frame_extract_detect(h, NHALVES);
        TEST_ASSERT_NOT_EQUAL(I2S_FRAME_PHASE_NONE, phase);
        size_t n = i2s_frame_extract(h, NHALVES, phase, out);
        TEST_ASSERT_EQUAL_size_t(NFRAMES * 2, n);
        assert_extracted_lr(out);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_detect_both_high_halves);
    RUN_TEST(test_detect_both_low_halves);
    RUN_TEST(test_detect_packed_in_word1);
    RUN_TEST(test_detect_packed_in_word0);
    RUN_TEST(test_detect_silence_returns_none);
    RUN_TEST(test_detect_short_block_returns_none);
    RUN_TEST(test_detect_null_returns_none);
    RUN_TEST(test_extract_both_high_halves);
    RUN_TEST(test_extract_both_low_halves);
    RUN_TEST(test_extract_packed_word1);
    RUN_TEST(test_extract_in_place_both_high);
    RUN_TEST(test_extract_in_place_packed_word0);
    RUN_TEST(test_extract_in_place_matches_out_of_place);
    RUN_TEST(test_extract_preserves_negative_extremes);
    RUN_TEST(test_extract_rejects_bad_phase);
    RUN_TEST(test_detect_then_extract_round_trip_all_phases);
    return UNITY_END();
}

/* RH-S3-06: verify that pack_s16_msb() produces the correct 32-bit top-half
 * packed representation without invoking signed left-shift UB.
 *
 * Expected bit-patterns:
 *   INT16_MIN (-32768) -> 0x80000000
 *   -1              -> 0xFFFF0000
 *   0               -> 0x00000000
 *   1               -> 0x00010000
 *   INT16_MAX (32767) -> 0x7FFF0000
 */
#include <stdint.h>
#include "unity.h"

/* Inline helper matching the reference pattern from RH-S3-06 */
static inline int32_t pack_s16_msb(int16_t sample)
{
    return (int32_t)sample * INT32_C(65536);
}

void setUp(void) {}
void tearDown(void) {}

static void test_pack_s16_msb_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x00000000U, (uint32_t)pack_s16_msb(0));
}

static void test_pack_s16_msb_one(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x00010000U, (uint32_t)pack_s16_msb(1));
}

static void test_pack_s16_msb_minus_one(void)
{
    TEST_ASSERT_EQUAL_UINT32(0xFFFF0000U, (uint32_t)pack_s16_msb(-1));
}

static void test_pack_s16_msb_int16_max(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x7FFF0000U, (uint32_t)pack_s16_msb(INT16_MAX));
}

static void test_pack_s16_msb_int16_min(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x80000000U, (uint32_t)pack_s16_msb(INT16_MIN));
}

static void test_pack_s16_msb_roundtrip(void)
{
    /* Verify that packing + shifting back recovers the original sample. */
    for (int16_t v = -128; v <= 127; v++) {
        int32_t packed = pack_s16_msb(v);
        int32_t recovered = packed >> 16;
        TEST_ASSERT_EQUAL_INT(v, recovered);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pack_s16_msb_zero);
    RUN_TEST(test_pack_s16_msb_one);
    RUN_TEST(test_pack_s16_msb_minus_one);
    RUN_TEST(test_pack_s16_msb_int16_max);
    RUN_TEST(test_pack_s16_msb_int16_min);
    RUN_TEST(test_pack_s16_msb_roundtrip);
    return UNITY_END();
}

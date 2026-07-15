/* S3 pre-I2S volume: host tests for the pure i2s_out_apply_gain(). */
#include "unity.h"
#include "i2s_out.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void fill(int16_t *b, size_t n, int16_t v)
{
    for (size_t i = 0; i < n; i++) b[i] = v;
}

static void test_unity_is_passthrough(void)
{
    int16_t s[8]; fill(s, 8, 1000);
    i2s_out_apply_gain(s, 8, 100);
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_INT16(1000, s[i]);
}

static void test_over_100_is_noop(void)
{
    int16_t s[4]; fill(s, 4, 1234);
    i2s_out_apply_gain(s, 4, 150);              /* clamped/no-op, never amplifies */
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT16(1234, s[i]);
}

static void test_half(void)
{
    int16_t s[4] = { 1000, -1000, 20000, -20000 };
    i2s_out_apply_gain(s, 4, 50);
    TEST_ASSERT_EQUAL_INT16(500, s[0]);
    TEST_ASSERT_EQUAL_INT16(-500, s[1]);
    TEST_ASSERT_EQUAL_INT16(10000, s[2]);
    TEST_ASSERT_EQUAL_INT16(-10000, s[3]);
}

static void test_tenth(void)
{
    int16_t s[2] = { 10000, -30000 };
    i2s_out_apply_gain(s, 2, 10);
    TEST_ASSERT_EQUAL_INT16(1000, s[0]);
    TEST_ASSERT_EQUAL_INT16(-3000, s[1]);
}

static void test_zero_and_negative_mute(void)
{
    int16_t s[4]; fill(s, 4, 12345);
    i2s_out_apply_gain(s, 4, 0);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT16(0, s[i]);
    fill(s, 4, -9999);
    i2s_out_apply_gain(s, 4, -5);               /* negative also mutes */
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT16(0, s[i]);
}

static void test_no_clipping_or_overflow(void)
{
    int16_t s[2] = { 32767, -32768 };
    i2s_out_apply_gain(s, 2, 50);               /* must not overflow int16 math */
    TEST_ASSERT_EQUAL_INT16(16383, s[0]);       /* 32767*50/100 = 16383.5 -> 16383 */
    TEST_ASSERT_EQUAL_INT16(-16384, s[1]);      /* -32768*50/100 = -16384 */
}

static void test_null_and_zero_count_safe(void)
{
    i2s_out_apply_gain(NULL, 8, 50);            /* no crash */
    int16_t s[2] = { 7, 8 };
    i2s_out_apply_gain(s, 0, 50);
    TEST_ASSERT_EQUAL_INT16(7, s[0]);           /* untouched */
}

static void test_gain_one_percent(void)
{
    int16_t s[2] = { 10000, -10000 };
    i2s_out_apply_gain(s, 2, 1);
    TEST_ASSERT_EQUAL_INT16(100, s[0]);         /* 10000 * 1/100 = 100 */
    TEST_ASSERT_EQUAL_INT16(-100, s[1]);
}

static void test_gain_ninety_nine_percent(void)
{
    int16_t s[2] = { 10000, -10000 };
    i2s_out_apply_gain(s, 2, 99);
    TEST_ASSERT_EQUAL_INT16(9900, s[0]);        /* 10000 * 99/100 = 9900 */
    TEST_ASSERT_EQUAL_INT16(-9900, s[1]);
}

static void test_gain_intermediate_values(void)
{
    int16_t s[6] = { 1000, 1000, 1000, 1000, 1000, 1000 };

    /* 25% */
    i2s_out_apply_gain(s, 2, 25);
    TEST_ASSERT_EQUAL_INT16(250, s[0]);         /* 1000 * 25/100 = 250 */
    TEST_ASSERT_EQUAL_INT16(250, s[1]);

    /* 33% */
    s[2] = 1000; s[3] = 1000;
    i2s_out_apply_gain(s + 2, 2, 33);
    TEST_ASSERT_EQUAL_INT16(330, s[2]);         /* 1000 * 33/100 = 330 */
    TEST_ASSERT_EQUAL_INT16(330, s[3]);

    /* 75% */
    s[4] = 1000; s[5] = 1000;
    i2s_out_apply_gain(s + 4, 2, 75);
    TEST_ASSERT_EQUAL_INT16(750, s[4]);         /* 1000 * 75/100 = 750 */
    TEST_ASSERT_EQUAL_INT16(750, s[5]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unity_is_passthrough);
    RUN_TEST(test_over_100_is_noop);
    RUN_TEST(test_half);
    RUN_TEST(test_tenth);
    RUN_TEST(test_zero_and_negative_mute);
    RUN_TEST(test_no_clipping_or_overflow);
    RUN_TEST(test_null_and_zero_count_safe);
    RUN_TEST(test_gain_one_percent);
    RUN_TEST(test_gain_ninety_nine_percent);
    RUN_TEST(test_gain_intermediate_values);
    return UNITY_END();
}

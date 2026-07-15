/*
 * test_i2s_out_pump — pure pending-block arithmetic (TODO 3.4/3.5):
 * i2s_pending_advance() computes how many REAL (non-zero-fill) bytes of a
 * partially/fully accepted write should be consumed from the ring, and
 * shifts the remaining pending buffer to the front.
 */
#include "unity.h"
#include "i2s_out.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void fill_pending(uint8_t *buf, size_t real, size_t total, uint8_t real_base)
{
    for (size_t i = 0; i < real; i++) buf[i] = (uint8_t)(real_base + i);
    if (total > real) memset(buf + real, 0, total - real);
}

static void test_full_accept_no_zero_fill(void)
{
    uint8_t pending[64];
    fill_pending(pending, 64, 64, 1);
    size_t len = 64, real = 64;

    size_t real_accepted = i2s_pending_advance(pending, &len, &real, 64);

    TEST_ASSERT_EQUAL_UINT(64, real_accepted);
    TEST_ASSERT_EQUAL_UINT(0, len);
    TEST_ASSERT_EQUAL_UINT(0, real);
}

static void test_full_accept_with_zero_fill_tail(void)
{
    uint8_t pending[64];
    fill_pending(pending, 20, 64, 0x40);
    size_t len = 64, real = 20;

    /* Driver accepted the whole 64-byte block (20 real + 44 zero-fill). */
    size_t real_accepted = i2s_pending_advance(pending, &len, &real, 64);

    TEST_ASSERT_EQUAL_UINT(20, real_accepted);  /* only the real bytes count */
    TEST_ASSERT_EQUAL_UINT(0, len);
    TEST_ASSERT_EQUAL_UINT(0, real);
}

static void test_zero_write_leaves_pending_untouched(void)
{
    uint8_t pending[16];
    fill_pending(pending, 16, 16, 0x10);
    uint8_t before[16];
    memcpy(before, pending, 16);
    size_t len = 16, real = 16;

    size_t real_accepted = i2s_pending_advance(pending, &len, &real, 0);

    TEST_ASSERT_EQUAL_UINT(0, real_accepted);
    TEST_ASSERT_EQUAL_UINT(16, len);
    TEST_ASSERT_EQUAL_UINT(16, real);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, pending, 16);
}

static void test_partial_write_within_real_data(void)
{
    uint8_t pending[64];
    fill_pending(pending, 64, 64, 1);   /* all real, values 1..64 */
    size_t len = 64, real = 64;

    size_t real_accepted = i2s_pending_advance(pending, &len, &real, 10);

    TEST_ASSERT_EQUAL_UINT(10, real_accepted);
    TEST_ASSERT_EQUAL_UINT(54, len);
    TEST_ASSERT_EQUAL_UINT(54, real);
    /* Remaining buffer is the old [10..63] shifted to the front. */
    TEST_ASSERT_EQUAL_UINT8(11, pending[0]);
    TEST_ASSERT_EQUAL_UINT8(64, pending[53]);
}

static void test_partial_write_only_touches_zero_fill_tail(void)
{
    uint8_t pending[64];
    fill_pending(pending, 20, 64, 0x40);
    size_t len = 64, real = 20;

    /* Driver accepted 30 bytes: all 20 real bytes + 10 of the zero-fill tail. */
    size_t real_accepted = i2s_pending_advance(pending, &len, &real, 30);

    TEST_ASSERT_EQUAL_UINT(20, real_accepted);  /* every real byte, no more */
    TEST_ASSERT_EQUAL_UINT(34, len);            /* 64 - 30 zero-fill bytes remain */
    TEST_ASSERT_EQUAL_UINT(0, real);            /* no real bytes left pending */
}

static void test_written_clamped_to_pending_len(void)
{
    uint8_t pending[8];
    fill_pending(pending, 8, 8, 1);
    size_t len = 8, real = 8;

    /* Driver claims to have written more than was ever requested — must be
     * clamped, not trusted, to avoid corrupting the state machine. */
    size_t real_accepted = i2s_pending_advance(pending, &len, &real, 999);

    TEST_ASSERT_EQUAL_UINT(8, real_accepted);
    TEST_ASSERT_EQUAL_UINT(0, len);
    TEST_ASSERT_EQUAL_UINT(0, real);
}

static void test_sequence_of_partial_writes_drains_correctly(void)
{
    uint8_t pending[32];
    fill_pending(pending, 32, 32, 1);
    size_t len = 32, real = 32;
    size_t total_real_accepted = 0;

    /* Simulate a driver that only ever accepts 5 bytes per call. */
    while (len > 0) {
        size_t accepted = i2s_pending_advance(pending, &len, &real, 5);
        total_real_accepted += accepted;
    }

    TEST_ASSERT_EQUAL_UINT(32, total_real_accepted);
    TEST_ASSERT_EQUAL_UINT(0, len);
    TEST_ASSERT_EQUAL_UINT(0, real);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_accept_no_zero_fill);
    RUN_TEST(test_full_accept_with_zero_fill_tail);
    RUN_TEST(test_zero_write_leaves_pending_untouched);
    RUN_TEST(test_partial_write_within_real_data);
    RUN_TEST(test_partial_write_only_touches_zero_fill_tail);
    RUN_TEST(test_written_clamped_to_pending_len);
    RUN_TEST(test_sequence_of_partial_writes_drains_correctly);
    return UNITY_END();
}

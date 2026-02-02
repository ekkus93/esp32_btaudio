#include "unity.h"

#include <string.h>
#include <stdint.h>

#include "util_safe.h"

/* Convenience aliases to maintain test names */
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset

void setUp(void) {}
void tearDown(void) {}

void test_safe_memcpy_should_handle_null_and_zero(void)
{
    uint8_t dst[8] = {0};
    uint8_t src[8] = {1,2,3,4,5,6,7,8};

    TEST_ASSERT_EQUAL_size_t(0, safe_memcpy(NULL, sizeof(dst), src, sizeof(src)));
    TEST_ASSERT_EQUAL_size_t(0, safe_memcpy(dst, sizeof(dst), NULL, sizeof(src)));
    TEST_ASSERT_EQUAL_size_t(0, safe_memcpy(dst, 0, src, sizeof(src)));
    TEST_ASSERT_EQUAL_size_t(0, safe_memcpy(dst, sizeof(dst), src, 0));
}

void test_safe_memcpy_should_clip_to_dst_size(void)
{
    uint8_t dst[4] = {0};
    const uint8_t src[8] = {10,11,12,13,14,15,16,17};

    size_t copied = safe_memcpy(dst, sizeof(dst), src, sizeof(src));

    TEST_ASSERT_EQUAL_size_t(sizeof(dst), copied);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, sizeof(dst));
}

void test_safe_memset_should_clip_and_preserve_tail(void)
{
    uint8_t dst[6] = {0};
    memset(dst, 0xA5, sizeof(dst));

    safe_memset(dst, sizeof(dst), 0x11, 4);

    TEST_ASSERT_EQUAL_HEX8(0x11, dst[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11, dst[1]);
    TEST_ASSERT_EQUAL_HEX8(0x11, dst[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11, dst[3]);
    /* tail remains unchanged */
    TEST_ASSERT_EQUAL_HEX8(0xA5, dst[4]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, dst[5]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_safe_memcpy_should_handle_null_and_zero);
    RUN_TEST(test_safe_memcpy_should_clip_to_dst_size);
    RUN_TEST(test_safe_memset_should_clip_and_preserve_tail);
    return UNITY_END();
}

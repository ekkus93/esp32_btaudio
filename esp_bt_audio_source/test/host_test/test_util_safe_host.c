#include "unity.h"
#include "util_safe.h"
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_safe_memcpy_truncates(void)
{
    char dst[5] = {'x','x','x','x','!'};
    const char *src = "ABCDE";
    util_safe_memcpy(dst, 4, src, strlen(src));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("ABCD", dst, 4);
    TEST_ASSERT_EQUAL_CHAR('!', dst[4]);
}

static void test_safe_memcpy_zero_len_no_change(void)
{
    char dst[4] = {'a','b','c','d'};
    const char *src = "ZZ";
    util_safe_memcpy(dst, sizeof(dst), src, 0);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("abcd", dst, sizeof(dst));
}

static void test_safe_memcpy_zero_dst_size_no_change(void)
{
    char dst[4] = {'a','b','c','d'};
    const char *src = "ZZ";
    util_safe_memcpy(dst, 0, src, strlen(src));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("abcd", dst, sizeof(dst));
}

static void test_safe_memcpy_exact_fit(void)
{
    char dst[4] = {'x','x','x','x'};
    const char *src = "WXYZ";
    util_safe_memcpy(dst, sizeof(dst), src, strlen(src));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("WXYZ", dst, sizeof(dst));
}

static void test_safe_memcpy_overlong_preserves_tail(void)
{
    char dst[6] = {'0','1','2','3','4','5'};
    const char *src = "ABCDEF";
    util_safe_memcpy(dst, 3, src, strlen(src));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("ABC345", dst, sizeof(dst));
}

static void test_safe_copy_str_handles_null(void)
{
    char dst[5] = {'x','x','x','x','x'};
    util_safe_copy_str(dst, sizeof(dst), NULL);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

static void test_safe_copy_str_dst_size_one(void)
{
    char dst[1] = {'x'};
    util_safe_copy_str(dst, sizeof(dst), "hi");
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

static void test_safe_copy_str_truncates_and_terminates(void)
{
    char dst[5] = {'x','x','x','x','!'};
    util_safe_copy_str(dst, sizeof(dst), "ABCDEFG");
    TEST_ASSERT_EQUAL_STRING_LEN("ABCD", dst, 4);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[4]);
}

static void test_parse_mac_accepts_colon_format(void)
{
    uint8_t mac[6] = {0};
    TEST_ASSERT_TRUE(util_parse_mac("AA:BB:CC:DD:EE:FF", mac));
    const uint8_t expected[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, mac, 6);
}

static void test_parse_mac_rejects_bad_format(void)
{
    uint8_t mac[6] = {0};
    TEST_ASSERT_FALSE(util_parse_mac("AA:BB:CC:DD:EE", mac));
    TEST_ASSERT_FALSE(util_parse_mac("GG:00:00:00:00:00", mac));
}

static void test_format_mac_produces_upper_hex(void)
{
    const uint8_t mac[6] = {0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
    char buf[32] = {0};
    util_format_mac(mac, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0A:1B:2C:3D:4E:5F", buf);
}

static void test_safe_vsnprintf_truncates_with_null_term(void)
{
    char buf[5] = {'x','x','x','x','x'};
    util_safe_snprintf(buf, sizeof(buf), "%s", "123456");
    TEST_ASSERT_EQUAL_CHAR_ARRAY("1234", buf, 4);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[4]);
}

static void test_safe_vsnprintf_zero_size_guard(void)
{
    char buf[3] = {'a','b','c'};
    util_safe_snprintf(buf, 0, "%s", "zz");
    TEST_ASSERT_EQUAL_CHAR_ARRAY("abc", buf, sizeof(buf));
}

static void test_safe_memset_zero_len_no_change(void)
{
    char buf[3] = {'a','b','c'};
    util_safe_memset(buf, sizeof(buf), 'z', 0);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("abc", buf, sizeof(buf));
}

static void test_safe_memset_basic_fill(void)
{
    char buf[4] = {0};
    util_safe_memset(buf, sizeof(buf), 'q', 3);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("qqq\0", buf, sizeof(buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_safe_memcpy_truncates);
    RUN_TEST(test_safe_memcpy_zero_len_no_change);
    RUN_TEST(test_safe_memcpy_zero_dst_size_no_change);
    RUN_TEST(test_safe_memcpy_exact_fit);
    RUN_TEST(test_safe_memcpy_overlong_preserves_tail);
    RUN_TEST(test_safe_copy_str_handles_null);
    RUN_TEST(test_safe_copy_str_dst_size_one);
    RUN_TEST(test_safe_copy_str_truncates_and_terminates);
    RUN_TEST(test_parse_mac_accepts_colon_format);
    RUN_TEST(test_parse_mac_rejects_bad_format);
    RUN_TEST(test_format_mac_produces_upper_hex);
    RUN_TEST(test_safe_vsnprintf_truncates_with_null_term);
    RUN_TEST(test_safe_vsnprintf_zero_size_guard);
    RUN_TEST(test_safe_memset_zero_len_no_change);
    RUN_TEST(test_safe_memset_basic_fill);
    return UNITY_END();
}

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

static void test_safe_copy_str_handles_null(void)
{
    char dst[5] = {'x','x','x','x','x'};
    util_safe_copy_str(dst, sizeof(dst), NULL);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_safe_memcpy_truncates);
    RUN_TEST(test_safe_copy_str_handles_null);
    RUN_TEST(test_parse_mac_accepts_colon_format);
    RUN_TEST(test_parse_mac_rejects_bad_format);
    RUN_TEST(test_format_mac_produces_upper_hex);
    return UNITY_END();
}

#include "unity.h"
#include "util_safe.h"
#include <string.h>

TEST_CASE("safe_memcpy_truncates", "[util_safe]") {
    char dst[4] = {0};
    const char *src = "ABCDE";
    util_safe_memcpy(dst, sizeof(dst), src, strlen(src));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("ABC", dst, 3);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[3]);
}

TEST_CASE("safe_copy_str_handles_null", "[util_safe]") {
    char dst[5] = {'x','x','x','x','x'};
    util_safe_copy_str(dst, sizeof(dst), NULL);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

TEST_CASE("parse_mac_accepts_colon_format", "[util_safe]") {
    uint8_t mac[6] = {0};
    TEST_ASSERT_TRUE(util_parse_mac("AA:BB:CC:DD:EE:FF", mac));
    const uint8_t expected[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, mac, 6);
}

TEST_CASE("parse_mac_rejects_bad_format", "[util_safe]") {
    uint8_t mac[6] = {0};
    TEST_ASSERT_FALSE(util_parse_mac("AA:BB:CC:DD:EE", mac));
    TEST_ASSERT_FALSE(util_parse_mac("GG:00:00:00:00:00", mac));
}

TEST_CASE("format_mac_produces_upper_hex", "[util_safe]") {
    const uint8_t mac[6] = {0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
    char buf[32] = {0};
    util_format_mac(mac, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0A:1B:2C:3D:4E:5F", buf);
}

/*
 * test_url_policy — FIX3 Phase 5B: pure IPv4/IPv6 destination policy,
 * literal-address parsing, and host extraction.
 *
 * Built as two targets from the same source (see CMakeLists.txt):
 *   test_url_policy                     — default (strict) policy
 *   test_url_policy_local_streams_allowed — CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS defined
 * RUN_LOCAL_STREAMS_ALLOWED_VARIANT selects which set of expectations
 * main() runs, since the compile-time Kconfig branch can't be flipped at
 * runtime in a single binary.
 */
#include "unity.h"
#include "url_policy.h"

#include <arpa/inet.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static uint32_t v4(const char *s)
{
    struct in_addr a;
    TEST_ASSERT_EQUAL(1, inet_pton(AF_INET, s, &a));
    return a.s_addr;
}

static void v6(const char *s, uint8_t out[16])
{
    struct in6_addr a;
    TEST_ASSERT_EQUAL_MESSAGE(1, inet_pton(AF_INET6, s, &a), s);
    memcpy(out, &a, 16);
}

/* ---- IPv4 blocked ranges (boundaries) ---- */

void test_ipv4_unspecified_blocked(void)  { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("0.0.0.0"))); }
void test_ipv4_unspecified_net_blocked(void) { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("0.255.255.255"))); }
void test_ipv4_loopback_blocked(void)     { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("127.0.0.1"))); }
void test_ipv4_loopback_edge_blocked(void){ TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("127.255.255.255"))); }
void test_ipv4_private_10_blocked(void)   { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("10.0.0.1"))); }
void test_ipv4_link_local_blocked(void)   { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("169.254.1.1"))); }
void test_ipv4_private_172_16_blocked(void)  { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("172.16.0.1"))); }
void test_ipv4_private_172_31_blocked(void)  { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("172.31.255.255"))); }
void test_ipv4_private_192_168_blocked(void) { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("192.168.1.1"))); }
void test_ipv4_multicast_blocked(void)    { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("224.0.0.1"))); }
void test_ipv4_broadcast_blocked(void)    { TEST_ASSERT_FALSE(url_policy_ipv4_allowed(v4("255.255.255.255"))); }

/* Just outside each blocked range must be allowed. */
void test_ipv4_just_below_private_172_16_allowed(void) { TEST_ASSERT_TRUE(url_policy_ipv4_allowed(v4("172.15.255.255"))); }
void test_ipv4_just_above_private_172_31_allowed(void) { TEST_ASSERT_TRUE(url_policy_ipv4_allowed(v4("172.32.0.0"))); }
void test_ipv4_just_below_multicast_allowed(void)      { TEST_ASSERT_TRUE(url_policy_ipv4_allowed(v4("223.255.255.255"))); }
void test_ipv4_public_allowed(void)                    { TEST_ASSERT_TRUE(url_policy_ipv4_allowed(v4("8.8.8.8"))); }
void test_ipv4_public_allowed2(void)                   { TEST_ASSERT_TRUE(url_policy_ipv4_allowed(v4("93.184.216.34"))); }

/* ---- IPv6 blocked ranges ---- */

void test_ipv6_unspecified_blocked(void)
{
    uint8_t a[16]; v6("::", a);
    TEST_ASSERT_FALSE(url_policy_ipv6_allowed(a));
}

void test_ipv6_loopback_blocked(void)
{
    uint8_t a[16]; v6("::1", a);
    TEST_ASSERT_FALSE(url_policy_ipv6_allowed(a));
}

void test_ipv6_link_local_blocked(void)
{
    uint8_t a[16]; v6("fe80::1", a);
    TEST_ASSERT_FALSE(url_policy_ipv6_allowed(a));
}

void test_ipv6_unique_local_blocked(void)
{
    uint8_t a[16]; v6("fc00::1", a);
    TEST_ASSERT_FALSE(url_policy_ipv6_allowed(a));
    uint8_t b[16]; v6("fd00::1", b);
    TEST_ASSERT_FALSE(url_policy_ipv6_allowed(b));
}

void test_ipv6_multicast_blocked(void)
{
    uint8_t a[16]; v6("ff02::1", a);
    TEST_ASSERT_FALSE(url_policy_ipv6_allowed(a));
}

void test_ipv6_v4_mapped_blocked_address_blocked(void)
{
    uint8_t a[16]; v6("::ffff:127.0.0.1", a);
    TEST_ASSERT_FALSE(url_policy_ipv6_allowed(a));
}

void test_ipv6_v4_mapped_public_address_allowed(void)
{
    uint8_t a[16]; v6("::ffff:8.8.8.8", a);
    TEST_ASSERT_TRUE(url_policy_ipv6_allowed(a));
}

void test_ipv6_public_allowed(void)
{
    uint8_t a[16]; v6("2001:4860:4860::8888", a);
    TEST_ASSERT_TRUE(url_policy_ipv6_allowed(a));
}

/* ---- literal parsing ---- */

void test_parse_literal_ipv4_valid(void)
{
    uint32_t out;
    TEST_ASSERT_TRUE(url_policy_parse_literal_ipv4("1.2.3.4", &out));
}

void test_parse_literal_ipv4_rejects_hostname(void)
{
    uint32_t out;
    TEST_ASSERT_FALSE(url_policy_parse_literal_ipv4("example.com", &out));
}

void test_parse_literal_ipv6_valid(void)
{
    uint8_t out[16];
    TEST_ASSERT_TRUE(url_policy_parse_literal_ipv6("::1", out));
}

void test_parse_literal_ipv6_rejects_hostname(void)
{
    uint8_t out[16];
    TEST_ASSERT_FALSE(url_policy_parse_literal_ipv6("example.com", out));
}

/* ---- host extraction ---- */

void test_extract_host_basic(void)
{
    char host[64];
    TEST_ASSERT_TRUE(url_policy_extract_host("http://example.com/stream.mp3", host, sizeof(host)));
    TEST_ASSERT_EQUAL_STRING("example.com", host);
}

void test_extract_host_with_port(void)
{
    char host[64];
    TEST_ASSERT_TRUE(url_policy_extract_host("http://example.com:8080/stream.mp3", host, sizeof(host)));
    TEST_ASSERT_EQUAL_STRING("example.com", host);
}

void test_extract_host_ipv6_bracketed(void)
{
    char host[64];
    TEST_ASSERT_TRUE(url_policy_extract_host("http://[::1]:8080/stream.mp3", host, sizeof(host)));
    TEST_ASSERT_EQUAL_STRING("::1", host);
}

void test_extract_host_no_scheme_fails(void)
{
    char host[64];
    TEST_ASSERT_FALSE(url_policy_extract_host("example.com/stream.mp3", host, sizeof(host)));
}

/* ---- url_policy_check_literal (end-to-end for a URL string) ---- */

void test_check_literal_hostname_passes(void)
{
    TEST_ASSERT_TRUE(url_policy_check_literal("http://example.com/stream.mp3"));
}

void test_check_literal_public_ip_passes(void)
{
    TEST_ASSERT_TRUE(url_policy_check_literal("http://8.8.8.8/stream.mp3"));
}

void test_check_literal_private_ip_blocked(void)
{
    TEST_ASSERT_FALSE(url_policy_check_literal("http://192.168.1.1/stream.mp3"));
}

void test_check_literal_loopback_blocked(void)
{
    TEST_ASSERT_FALSE(url_policy_check_literal("http://127.0.0.1:8000/stream.mp3"));
}

#ifdef RUN_LOCAL_STREAMS_ALLOWED_VARIANT
void test_local_streams_override_allows_loopback(void)
{
    TEST_ASSERT_TRUE(url_policy_ipv4_allowed(v4("127.0.0.1")));
    TEST_ASSERT_TRUE(url_policy_check_literal("http://127.0.0.1:8000/stream.mp3"));
}
#endif

int main(void)
{
    UNITY_BEGIN();
#ifdef RUN_LOCAL_STREAMS_ALLOWED_VARIANT
    RUN_TEST(test_local_streams_override_allows_loopback);
#else
    RUN_TEST(test_ipv4_unspecified_blocked);
    RUN_TEST(test_ipv4_unspecified_net_blocked);
    RUN_TEST(test_ipv4_loopback_blocked);
    RUN_TEST(test_ipv4_loopback_edge_blocked);
    RUN_TEST(test_ipv4_private_10_blocked);
    RUN_TEST(test_ipv4_link_local_blocked);
    RUN_TEST(test_ipv4_private_172_16_blocked);
    RUN_TEST(test_ipv4_private_172_31_blocked);
    RUN_TEST(test_ipv4_private_192_168_blocked);
    RUN_TEST(test_ipv4_multicast_blocked);
    RUN_TEST(test_ipv4_broadcast_blocked);
    RUN_TEST(test_ipv4_just_below_private_172_16_allowed);
    RUN_TEST(test_ipv4_just_above_private_172_31_allowed);
    RUN_TEST(test_ipv4_just_below_multicast_allowed);
    RUN_TEST(test_ipv4_public_allowed);
    RUN_TEST(test_ipv4_public_allowed2);
    RUN_TEST(test_ipv6_unspecified_blocked);
    RUN_TEST(test_ipv6_loopback_blocked);
    RUN_TEST(test_ipv6_link_local_blocked);
    RUN_TEST(test_ipv6_unique_local_blocked);
    RUN_TEST(test_ipv6_multicast_blocked);
    RUN_TEST(test_ipv6_v4_mapped_blocked_address_blocked);
    RUN_TEST(test_ipv6_v4_mapped_public_address_allowed);
    RUN_TEST(test_ipv6_public_allowed);
    RUN_TEST(test_parse_literal_ipv4_valid);
    RUN_TEST(test_parse_literal_ipv4_rejects_hostname);
    RUN_TEST(test_parse_literal_ipv6_valid);
    RUN_TEST(test_parse_literal_ipv6_rejects_hostname);
    RUN_TEST(test_extract_host_basic);
    RUN_TEST(test_extract_host_with_port);
    RUN_TEST(test_extract_host_ipv6_bracketed);
    RUN_TEST(test_extract_host_no_scheme_fails);
    RUN_TEST(test_check_literal_hostname_passes);
    RUN_TEST(test_check_literal_public_ip_passes);
    RUN_TEST(test_check_literal_private_ip_blocked);
    RUN_TEST(test_check_literal_loopback_blocked);
#endif
    return UNITY_END();
}

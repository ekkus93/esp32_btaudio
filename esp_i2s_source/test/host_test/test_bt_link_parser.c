/*
 * test_bt_link_parser — LINK-1a: field splitting, status classification,
 * terminal-vs-async, and the partial-read line assembler.
 */
#include "unity.h"
#include "bt_link_parser.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* --- parse_line --- */
static void test_parse_ok_full(void)
{
    char line[] = "OK|VERSION|0.2.0|";
    bt_link_msg_t m;
    TEST_ASSERT_TRUE(bt_link_parse_line(line, &m));
    TEST_ASSERT_EQUAL_INT(BT_LINK_OK, m.status);
    TEST_ASSERT_EQUAL_STRING("VERSION", m.command);
    TEST_ASSERT_EQUAL_STRING("0.2.0", m.result);
    TEST_ASSERT_EQUAL_STRING("", m.data);
}

static void test_parse_data_present(void)
{
    char line[] = "OK|STATUS|CONNECTED|mac=30ED";
    bt_link_msg_t m;
    bt_link_parse_line(line, &m);
    TEST_ASSERT_EQUAL_INT(BT_LINK_OK, m.status);
    TEST_ASSERT_EQUAL_STRING("CONNECTED", m.result);
    TEST_ASSERT_EQUAL_STRING("mac=30ED", m.data);
}

static void test_parse_err_and_event_and_info(void)
{
    char e[] = "ERR|CONNECT|TIMEOUT|";
    char v[] = "EVENT|PAIR|CONFIRM|123456";
    char i[] = "INFO|SCAN|ITEM|name=Buds";
    bt_link_msg_t m;

    bt_link_parse_line(e, &m);
    TEST_ASSERT_EQUAL_INT(BT_LINK_ERR, m.status);
    TEST_ASSERT_EQUAL_STRING("TIMEOUT", m.result);

    bt_link_parse_line(v, &m);
    TEST_ASSERT_EQUAL_INT(BT_LINK_EVENT, m.status);
    TEST_ASSERT_EQUAL_STRING("PAIR", m.command);
    TEST_ASSERT_EQUAL_STRING("123456", m.data);

    bt_link_parse_line(i, &m);
    TEST_ASSERT_EQUAL_INT(BT_LINK_INFO, m.status);
}

static void test_parse_data_keeps_embedded_pipes(void)
{
    char line[] = "EVENT|X|Y|a|b|c";
    bt_link_msg_t m;
    bt_link_parse_line(line, &m);
    TEST_ASSERT_EQUAL_STRING("Y", m.result);
    TEST_ASSERT_EQUAL_STRING("a|b|c", m.data);
}

static void test_parse_missing_trailing_fields(void)
{
    char line[] = "OK|PING";
    bt_link_msg_t m;
    bt_link_parse_line(line, &m);
    TEST_ASSERT_EQUAL_STRING("PING", m.command);
    TEST_ASSERT_EQUAL_STRING("", m.result);
    TEST_ASSERT_EQUAL_STRING("", m.data);
}

static void test_parse_status_only_and_unknown(void)
{
    char a[] = "OK";
    char b[] = "FOO|bar|baz|";
    bt_link_msg_t m;
    bt_link_parse_line(a, &m);
    TEST_ASSERT_EQUAL_INT(BT_LINK_OK, m.status);
    TEST_ASSERT_EQUAL_STRING("", m.command);
    bt_link_parse_line(b, &m);
    TEST_ASSERT_EQUAL_INT(BT_LINK_UNKNOWN, m.status);
    TEST_ASSERT_EQUAL_STRING("FOO", m.status_str);
}

static void test_parse_empty_line_rejected(void)
{
    char line[] = "";
    bt_link_msg_t m;
    TEST_ASSERT_FALSE(bt_link_parse_line(line, &m));
}

static void test_is_terminal(void)
{
    TEST_ASSERT_TRUE(bt_link_is_terminal(BT_LINK_OK));
    TEST_ASSERT_TRUE(bt_link_is_terminal(BT_LINK_ERR));
    TEST_ASSERT_FALSE(bt_link_is_terminal(BT_LINK_INFO));
    TEST_ASSERT_FALSE(bt_link_is_terminal(BT_LINK_EVENT));
    TEST_ASSERT_FALSE(bt_link_is_terminal(BT_LINK_UNKNOWN));
}

/* --- line assembler --- */
#define MAX_CAP 16
typedef struct { char lines[MAX_CAP][BT_LINK_LINE_MAX]; int count; } cap_t;
static void cap_cb(void *ctx, char *line)
{
    cap_t *c = (cap_t *)ctx;
    if (c->count < MAX_CAP) strcpy(c->lines[c->count++], line);
}

static void test_linebuf_single_crlf(void)
{
    bt_link_linebuf_t lb; bt_link_linebuf_init(&lb);
    cap_t c = {0};
    const char *s = "OK|VERSION|1|\r\n";
    bt_link_linebuf_feed(&lb, (const uint8_t *)s, strlen(s), cap_cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.count);
    TEST_ASSERT_EQUAL_STRING("OK|VERSION|1|", c.lines[0]);
}

static void test_linebuf_partial_reads(void)
{
    bt_link_linebuf_t lb; bt_link_linebuf_init(&lb);
    cap_t c = {0};
    const char *a = "OK|VER";
    const char *b = "SION|1|\r\n";
    bt_link_linebuf_feed(&lb, (const uint8_t *)a, strlen(a), cap_cb, &c);
    TEST_ASSERT_EQUAL_INT(0, c.count);  /* no newline yet */
    bt_link_linebuf_feed(&lb, (const uint8_t *)b, strlen(b), cap_cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.count);
    TEST_ASSERT_EQUAL_STRING("OK|VERSION|1|", c.lines[0]);
}

static void test_linebuf_multiple_and_interleaved(void)
{
    bt_link_linebuf_t lb; bt_link_linebuf_init(&lb);
    cap_t c = {0};
    /* an event interleaved before the terminal OK */
    const char *s = "EVENT|A|B|\r\nOK|CONNECT|DONE|\r\n";
    bt_link_linebuf_feed(&lb, (const uint8_t *)s, strlen(s), cap_cb, &c);
    TEST_ASSERT_EQUAL_INT(2, c.count);
    TEST_ASSERT_EQUAL_STRING("EVENT|A|B|", c.lines[0]);
    TEST_ASSERT_EQUAL_STRING("OK|CONNECT|DONE|", c.lines[1]);
}

static void test_linebuf_skips_empty_lines(void)
{
    bt_link_linebuf_t lb; bt_link_linebuf_init(&lb);
    cap_t c = {0};
    const char *s = "\r\n\r\nOK|X|Y|\r\n";
    bt_link_linebuf_feed(&lb, (const uint8_t *)s, strlen(s), cap_cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.count);
    TEST_ASSERT_EQUAL_STRING("OK|X|Y|", c.lines[0]);
}

static void test_linebuf_overflow_recovery(void)
{
    bt_link_linebuf_t lb; bt_link_linebuf_init(&lb);
    cap_t c = {0};
    /* 600-char run with no newline overflows the 512 buffer, is dropped, then
     * a normal line after the newline must still parse. */
    char big[640];
    memset(big, 'x', 600);
    big[600] = '\n';
    strcpy(big + 601, "OK|PING|OK|\r\n");
    bt_link_linebuf_feed(&lb, (const uint8_t *)big, strlen(big), cap_cb, &c);
    TEST_ASSERT_TRUE(lb.overflow_count > 0);
    TEST_ASSERT_EQUAL_INT(1, c.count);
    TEST_ASSERT_EQUAL_STRING("OK|PING|OK|", c.lines[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_ok_full);
    RUN_TEST(test_parse_data_present);
    RUN_TEST(test_parse_err_and_event_and_info);
    RUN_TEST(test_parse_data_keeps_embedded_pipes);
    RUN_TEST(test_parse_missing_trailing_fields);
    RUN_TEST(test_parse_status_only_and_unknown);
    RUN_TEST(test_parse_empty_line_rejected);
    RUN_TEST(test_is_terminal);
    RUN_TEST(test_linebuf_single_crlf);
    RUN_TEST(test_linebuf_partial_reads);
    RUN_TEST(test_linebuf_multiple_and_interleaved);
    RUN_TEST(test_linebuf_skips_empty_lines);
    RUN_TEST(test_linebuf_overflow_recovery);
    return UNITY_END();
}

/* RADIO-1a: host tests for the pure playlist + ICY metadata parsers. */
#include "unity.h"
#include "radio_parse.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---------- playlist resolution ---------- */

void test_pls_basic(void)
{
    const char *pls =
        "[playlist]\r\n"
        "NumberOfEntries=1\r\n"
        "File1=http://stream.example.com:8000/live\r\n"
        "Title1=Cool Radio\r\n"
        "Length1=-1\r\n";
    char out[128];
    TEST_ASSERT_TRUE(radio_playlist_first_url(pls, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://stream.example.com:8000/live", out);
}

void test_pls_https_and_first_of_many(void)
{
    const char *pls =
        "[playlist]\n"
        "File1=https://a.example:1079/listen?sid=1\n"
        "File2=https://b.example:1079/listen?sid=2\n"
        "NumberOfEntries=2\n";
    char out[128];
    TEST_ASSERT_TRUE(radio_playlist_first_url(pls, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://a.example:1079/listen?sid=1", out);
}

void test_pls_case_insensitive_key(void)
{
    const char *pls = "[Playlist]\nfile1 = http://x.example/s\n";
    char out[64];
    TEST_ASSERT_TRUE(radio_playlist_first_url(pls, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://x.example/s", out);
}

void test_m3u_extended_skips_comments(void)
{
    const char *m3u =
        "#EXTM3U\n"
        "#EXTINF:-1,Some Station\n"
        "http://stream.example/aac\n";
    char out[64];
    TEST_ASSERT_TRUE(radio_playlist_first_url(m3u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://stream.example/aac", out);
}

void test_bare_url(void)
{
    char out[64];
    TEST_ASSERT_TRUE(radio_playlist_first_url("https://direct.example/stream", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://direct.example/stream", out);
}

void test_no_url_returns_false(void)
{
    char out[64];
    TEST_ASSERT_FALSE(radio_playlist_first_url("[playlist]\nNumberOfEntries=0\n", out, sizeof(out)));
    TEST_ASSERT_FALSE(radio_playlist_first_url("#EXTM3U\n#EXTINF:-1,x\n", out, sizeof(out)));
    TEST_ASSERT_FALSE(radio_playlist_first_url("", out, sizeof(out)));
}

void test_url_too_long_returns_false(void)
{
    const char *pls = "[playlist]\nFile1=http://this-url-is-longer-than-the-buffer/x\n";
    char out[16];
    TEST_ASSERT_FALSE(radio_playlist_first_url(pls, out, sizeof(out)));
}

/* ---------- ICY metadata ---------- */

void test_icy_stream_title(void)
{
    char t[64];
    TEST_ASSERT_TRUE(radio_icy_stream_title("StreamTitle='Artist - Song';StreamUrl='http://x';", t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("Artist - Song", t);
}

void test_icy_title_only(void)
{
    char t[64];
    TEST_ASSERT_TRUE(radio_icy_stream_title("StreamTitle='Just A Title';", t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("Just A Title", t);
}

void test_icy_empty_title(void)
{
    char t[64];
    TEST_ASSERT_TRUE(radio_icy_stream_title("StreamTitle='';StreamUrl='';", t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("", t);
}

void test_icy_no_title_field(void)
{
    char t[64];
    TEST_ASSERT_FALSE(radio_icy_stream_title("StreamUrl='http://x';", t, sizeof(t)));
}

void test_icy_null_padded_block(void)
{
    /* Real ICY blocks are padded with NULs to a 16-byte multiple. */
    char block[48];
    memset(block, 0, sizeof(block));
    memcpy(block, "StreamTitle='Padded';", 21);
    char t[64];
    TEST_ASSERT_TRUE(radio_icy_stream_title(block, t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("Padded", t);
}

void test_icy_title_truncated_to_buffer(void)
{
    char t[8];
    TEST_ASSERT_TRUE(radio_icy_stream_title("StreamTitle='0123456789';", t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("0123456", t);  /* 7 chars + NUL */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pls_basic);
    RUN_TEST(test_pls_https_and_first_of_many);
    RUN_TEST(test_pls_case_insensitive_key);
    RUN_TEST(test_m3u_extended_skips_comments);
    RUN_TEST(test_bare_url);
    RUN_TEST(test_no_url_returns_false);
    RUN_TEST(test_url_too_long_returns_false);
    RUN_TEST(test_icy_stream_title);
    RUN_TEST(test_icy_title_only);
    RUN_TEST(test_icy_empty_title);
    RUN_TEST(test_icy_no_title_field);
    RUN_TEST(test_icy_null_padded_block);
    RUN_TEST(test_icy_title_truncated_to_buffer);
    return UNITY_END();
}

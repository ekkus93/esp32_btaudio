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

/* ---------- ICY demuxer ---------- */

typedef struct {
    unsigned char audio[512];
    int alen;
    char title[128];
    int titles;
} demux_cap_t;

static void cap_audio(void *ctx, const unsigned char *d, size_t n)
{
    demux_cap_t *c = ctx;
    memcpy(c->audio + c->alen, d, n);
    c->alen += (int)n;
}
static void cap_title(void *ctx, const char *t)
{
    demux_cap_t *c = ctx;
    strncpy(c->title, t, sizeof(c->title) - 1);
    c->titles++;
}

/* Build an ICY stream: [metaint audio][len byte][len*16 meta][metaint audio]. */
static int build_icy(unsigned char *s, const char *a1, const char *meta, const char *a2)
{
    int p = 0;
    int al1 = (int)strlen(a1);
    memcpy(s + p, a1, (size_t)al1); p += al1;
    if (meta) {
        int mlen = (int)strlen(meta);
        int units = (mlen + 15) / 16;
        s[p++] = (unsigned char)units;
        memcpy(s + p, meta, (size_t)mlen); p += mlen;
        int pad = units * 16 - mlen;
        memset(s + p, 0, (size_t)pad); p += pad;
    } else {
        s[p++] = 0;  /* empty metadata block */
    }
    memcpy(s + p, a2, strlen(a2)); p += (int)strlen(a2);
    return p;
}

void test_demux_metaint_zero_is_all_audio(void)
{
    radio_icy_demux_t d;
    radio_icy_demux_init(&d, 0);
    demux_cap_t c = {0};
    radio_icy_demux_feed(&d, (const unsigned char *)"hello world", 11, cap_audio, cap_title, &c);
    TEST_ASSERT_EQUAL_INT(11, c.alen);
    TEST_ASSERT_EQUAL_INT(0, c.titles);
}

void test_demux_splits_audio_and_title(void)
{
    unsigned char s[256];
    int n = build_icy(s, "AAAAAAAA", "StreamTitle='Hi';", "BBBBBBBB");  /* metaint=8 */
    radio_icy_demux_t d;
    radio_icy_demux_init(&d, 8);
    demux_cap_t c = {0};
    radio_icy_demux_feed(&d, s, (size_t)n, cap_audio, cap_title, &c);
    TEST_ASSERT_EQUAL_INT(16, c.alen);
    TEST_ASSERT_EQUAL_MEMORY("AAAAAAAABBBBBBBB", c.audio, 16);
    TEST_ASSERT_EQUAL_INT(1, c.titles);
    TEST_ASSERT_EQUAL_STRING("Hi", c.title);
}

void test_demux_byte_at_a_time_same_result(void)
{
    unsigned char s[256];
    int n = build_icy(s, "AAAAAAAA", "StreamTitle='Song';", "BBBBBBBB");
    radio_icy_demux_t d;
    radio_icy_demux_init(&d, 8);
    demux_cap_t c = {0};
    for (int i = 0; i < n; i++) {
        radio_icy_demux_feed(&d, s + i, 1, cap_audio, cap_title, &c);  /* feed-boundary agnostic */
    }
    TEST_ASSERT_EQUAL_INT(16, c.alen);
    TEST_ASSERT_EQUAL_MEMORY("AAAAAAAABBBBBBBB", c.audio, 16);
    TEST_ASSERT_EQUAL_STRING("Song", c.title);
}

void test_demux_empty_metadata_block(void)
{
    unsigned char s[256];
    int n = build_icy(s, "AAAAAAAA", NULL, "BBBBBBBB");  /* length byte 0 */
    radio_icy_demux_t d;
    radio_icy_demux_init(&d, 8);
    demux_cap_t c = {0};
    radio_icy_demux_feed(&d, s, (size_t)n, cap_audio, cap_title, &c);
    TEST_ASSERT_EQUAL_INT(16, c.alen);
    TEST_ASSERT_EQUAL_INT(0, c.titles);  /* no title on empty block */
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
    RUN_TEST(test_demux_metaint_zero_is_all_audio);
    RUN_TEST(test_demux_splits_audio_and_title);
    RUN_TEST(test_demux_byte_at_a_time_same_result);
    RUN_TEST(test_demux_empty_metadata_block);
    return UNITY_END();
}

/*
 * test_radio_stream_redirects.c — I2S-1 (docs/UNIT_TESTS2_TODO.md):
 * resolve_redirect_location / redirect_target_allowed / connect_with_redirects
 * / codec_from_ct / ci_contains / reconnect_delay_ms, de-static'd from
 * radio_stream.c via radio_internal.h specifically for these tests.
 *
 * Uses mocks/fake_http_client.c, a controllable fake esp_http_client that
 * lets a test script an entire multi-hop HTTP response sequence (status +
 * headers per hop), rather than the single fixed 200/no-headers response
 * test_radio_lifecycle.c's local stub returns.
 *
 * This target links ONLY radio_stream.c + radio_parse.c (real, pure ICY/
 * playlist parsing) + url_policy.c (real). radio_stream.c's stream_task
 * (never called by these tests — it's a FreeRTOS task loop, out of scope
 * per docs/UNIT_TESTS2_TODO.md) still references a handful of symbols owned
 * by radio.c core / radio_ring.c for LINK completeness only; rather than
 * pull in that whole subsystem (NVS, PSRAM heap_caps, the audio decoder —
 * see test_radio_lifecycle.c for what that drags in), those few symbols get
 * minimal link-only stubs below. None of them are exercised by any test
 * here — they exist purely so the linker resolves stream_task's body.
 */
#include "unity.h"
#include "radio_internal.h"
#include "fake_http_client.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- link-only stubs: satisfy stream_task's/on_audio's/on_title's/
 * http_evt's references without pulling in radio.c core / radio_ring.c.
 * Never exercised by a test in this file (stream_task is never called). ---- */
SemaphoreHandle_t g_radio_ring_mtx;
radio_err_t       g_radio_last_error;
char              g_radio_last_error_detail[64];
char              g_radio_station[RADIO_NAME_MAX];
char              g_radio_title[RADIO_TITLE_MAX];
radio_codec_t     g_radio_codec;
int               g_radio_bitrate;
int               g_radio_http_status;
uint64_t          g_radio_bytes_in;
uint32_t          g_radio_reconnects;
uint32_t          g_radio_overflow;
size_t            g_radio_ring_cap;
size_t            g_radio_ring_count;

size_t ring_write(const uint8_t *d, size_t n) { (void)d; return n; }
void radio_session_fault(radio_session_t *s, radio_err_t err, const char *detail)
{
    (void)s; (void)err; (void)detail;
}
void radio_try_publish_running(radio_session_t *s) { (void)s; }
const char *radio_codec_str(radio_codec_t c) { (void)c; return "test"; }

void setUp(void)
{
    fake_http_client_reset();
    g_radio_ring_mtx = xSemaphoreCreateMutex();
    g_radio_last_error = RADIO_ERR_NONE;
    g_radio_last_error_detail[0] = '\0';
}

void tearDown(void) {}

/* ---- infra smoke test: prove the fake HTTP client + de-static'd
 * connect_with_redirects wiring actually works end-to-end before adding the
 * detailed per-function edge-case matrices. ---- */
void test_infra_single_hop_200_returns_open_client(void)
{
    fake_http_client_queue_hop(200, NULL, 0);

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://example.invalid/stream", &permanent);

    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_FALSE(permanent);
    TEST_ASSERT_EQUAL_INT(200, esp_http_client_get_status_code(h));
    TEST_ASSERT_EQUAL_INT(1, fake_http_client_hops_consumed());
}

/* ---- resolve_redirect_location: pure string logic, no HTTP mocking. ---- */

void test_resolve_redirect_absolute_location_copied_verbatim(void)
{
    char out[64];
    bool ok = resolve_redirect_location("http://old.example/a", "http://new.example/b", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("http://new.example/b", out);
}

void test_resolve_redirect_root_relative_splices_base_scheme_and_host(void)
{
    char out[64];
    bool ok = resolve_redirect_location("http://old.example/dir/a", "/new/path", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("http://old.example/new/path", out);
}

void test_resolve_redirect_root_relative_stops_at_query_and_fragment(void)
{
    char out[64];
    /* host_end must stop at '?' / '#', not run to the end of base_url's path. */
    bool ok = resolve_redirect_location("http://old.example/dir?x=1#frag", "/new", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("http://old.example/new", out);
}

void test_resolve_redirect_other_relative_form_rejected(void)
{
    char out[64];
    TEST_ASSERT_FALSE(resolve_redirect_location("http://old.example/a", "relative/path", out, sizeof(out)));
    TEST_ASSERT_FALSE(resolve_redirect_location("http://old.example/a", "path", out, sizeof(out)));
}

void test_resolve_redirect_null_or_empty_location_rejected(void)
{
    char out[64];
    TEST_ASSERT_FALSE(resolve_redirect_location("http://old.example/a", NULL, out, sizeof(out)));
    TEST_ASSERT_FALSE(resolve_redirect_location("http://old.example/a", "", out, sizeof(out)));
}

void test_resolve_redirect_absolute_location_too_big_rejected_not_truncated(void)
{
    char out[8];
    bool ok = resolve_redirect_location("http://old.example/a", "http://new.example/longpath", out, sizeof(out));
    TEST_ASSERT_FALSE(ok);
}

void test_resolve_redirect_base_url_missing_scheme_with_root_relative_rejected(void)
{
    char out[64];
    /* base_url has no "://" — malformed; root-relative location can't be spliced. */
    TEST_ASSERT_FALSE(resolve_redirect_location("old.example/a", "/new", out, sizeof(out)));
}

void test_resolve_redirect_absolute_location_exact_boundary(void)
{
    /* "http://a" is 8 chars; strnlen(location, out_sz) with out_sz==9 means
     * len==8 < out_sz==9 -> succeeds (needs len+1==9 bytes, exactly out_sz). */
    char out9[9];
    TEST_ASSERT_TRUE(resolve_redirect_location("http://old.example/a", "http://a", out9, sizeof(out9)));
    TEST_ASSERT_EQUAL_STRING("http://a", out9);

    /* out_sz==8: len==8 is NOT < out_sz==8 -> rejected (off-by-one guard). */
    char out8[8];
    TEST_ASSERT_FALSE(resolve_redirect_location("http://old.example/a", "http://a", out8, sizeof(out8)));
}

/* ---- redirect_target_allowed: on host, ESP_PLATFORM is undefined so this
 * reduces to exactly url_policy_check_literal(url) — the DNS re-check block
 * compiles out. These confirm the wrapper calls through correctly; the
 * policy matrix itself is url_policy.c's own test responsibility. ---- */

void test_redirect_target_allowed_public_ip_literal(void)
{
    TEST_ASSERT_TRUE(redirect_target_allowed("http://8.8.8.8/stream"));
}

void test_redirect_target_allowed_blocks_loopback(void)
{
    TEST_ASSERT_FALSE(redirect_target_allowed("http://127.0.0.1/x"));
}

void test_redirect_target_allowed_blocks_private(void)
{
    TEST_ASSERT_FALSE(redirect_target_allowed("http://192.168.1.1/x"));
}

void test_redirect_target_allowed_blocks_link_local(void)
{
    TEST_ASSERT_FALSE(redirect_target_allowed("http://169.254.1.1/x"));
}

void test_redirect_target_allowed_hostname_allowed_on_host(void)
{
    /* Non-literal hostname: DNS resolution happens device-side only
     * (ESP_PLATFORM-gated); on host this passes through as allowed. */
    TEST_ASSERT_TRUE(redirect_target_allowed("http://stream.example.com/x"));
}

/* ---- connect_with_redirects: the full hop-following loop. ---- */

void test_connect_single_hop_200_terminal(void)
{
    fake_http_client_queue_hop(200, NULL, 0);

    bool permanent = true; /* pre-set to a wrong value to confirm it's reset */
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_FALSE(permanent);
    TEST_ASSERT_EQUAL_INT(200, esp_http_client_get_status_code(h));
    TEST_ASSERT_EQUAL_INT(1, fake_http_client_hops_consumed());
}

void test_connect_one_valid_redirect_follows_to_hop2(void)
{
    fake_http_header_t hop0_headers[] = { { "Location", "http://good.example/next" } };
    fake_http_client_queue_hop(302, hop0_headers, 1);
    fake_http_client_queue_hop(200, NULL, 0);

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_FALSE(permanent);
    TEST_ASSERT_EQUAL_INT(200, esp_http_client_get_status_code(h));
    TEST_ASSERT_EQUAL_INT(2, fake_http_client_hops_consumed());
}

void test_connect_exactly_max_redirects_succeeds_on_terminal(void)
{
    /* MAX_REDIRECTS == 5: 5 redirects (hops 0-4, each 3xx) followed by a
     * terminal response on the 6th connection (hop index 5). */
    for (int i = 0; i < 5; i++) {
        char loc[64];
        snprintf(loc, sizeof(loc), "http://good.example/hop%d", i + 1);
        fake_http_header_t headers[] = { { "Location", loc } };
        fake_http_client_queue_hop(302, headers, 1);
    }
    fake_http_client_queue_hop(200, NULL, 0);

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_FALSE(permanent);
    TEST_ASSERT_EQUAL_INT(200, esp_http_client_get_status_code(h));
    TEST_ASSERT_EQUAL_INT(6, fake_http_client_hops_consumed());
}

void test_connect_max_redirects_plus_one_fails_permanent(void)
{
    /* 6 consecutive 3xx responses (hops 0-5): the 6th is still 3xx while
     * hop==MAX_REDIRECTS(5) -> redirect_limit, permanent fault. */
    for (int i = 0; i < 6; i++) {
        char loc[64];
        snprintf(loc, sizeof(loc), "http://good.example/hop%d", i + 1);
        fake_http_header_t headers[] = { { "Location", loc } };
        fake_http_client_queue_hop(302, headers, 1);
    }

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NULL(h);
    TEST_ASSERT_TRUE(permanent);
    TEST_ASSERT_EQUAL_INT(RADIO_ERR_HTTP_STATUS, g_radio_last_error);
    TEST_ASSERT_EQUAL_STRING("redirect_limit", g_radio_last_error_detail);
}

void test_connect_missing_location_header_fails_malformed(void)
{
    fake_http_client_queue_hop(302, NULL, 0); /* 3xx, no Location at all */

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NULL(h);
    TEST_ASSERT_TRUE(permanent);
    TEST_ASSERT_EQUAL_INT(RADIO_ERR_HTTP_STATUS, g_radio_last_error);
    TEST_ASSERT_EQUAL_STRING("redirect_malformed", g_radio_last_error_detail);
}

void test_connect_empty_location_header_fails_malformed(void)
{
    fake_http_header_t headers[] = { { "Location", "" } };
    fake_http_client_queue_hop(302, headers, 1);

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NULL(h);
    TEST_ASSERT_TRUE(permanent);
    TEST_ASSERT_EQUAL_STRING("redirect_malformed", g_radio_last_error_detail);
}

void test_connect_redirect_to_blocked_target_fails_ssrf(void)
{
    /* THE assertion that matters most in this file: a redirect to a private
     * address must be rejected, not followed. */
    fake_http_header_t headers[] = { { "Location", "http://192.168.1.5/evil" } };
    fake_http_client_queue_hop(302, headers, 1);

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NULL(h);
    TEST_ASSERT_TRUE(permanent);
    TEST_ASSERT_EQUAL_INT(RADIO_ERR_HTTP_STATUS, g_radio_last_error);
    TEST_ASSERT_EQUAL_STRING("redirect_url_blocked", g_radio_last_error_detail);
    /* Only the blocked hop's client was ever opened — the SSRF target was
     * never connected to. */
    TEST_ASSERT_EQUAL_INT(1, fake_http_client_hops_consumed());
}

void test_connect_init_alloc_failure(void)
{
    fake_http_client_force_init_null();

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NULL(h);
    TEST_ASSERT_FALSE(permanent); /* transient, not a policy/limit violation */
    TEST_ASSERT_EQUAL_INT(RADIO_ERR_HTTP_CLIENT_ALLOC, g_radio_last_error);
}

void test_connect_open_failure_is_transient(void)
{
    fake_http_client_queue_hop(200, NULL, 0); /* never reached */
    fake_http_client_force_open_err(ESP_FAIL);

    bool permanent = false;
    esp_http_client_handle_t h = connect_with_redirects("http://good.example/a", &permanent);

    TEST_ASSERT_NULL(h);
    TEST_ASSERT_FALSE(permanent);
}

void test_connect_4xx_5xx_returned_as_terminal(void)
{
    fake_http_client_queue_hop(404, NULL, 0);
    bool permanent404 = false;
    esp_http_client_handle_t h404 = connect_with_redirects("http://good.example/a", &permanent404);
    TEST_ASSERT_NOT_NULL(h404);
    TEST_ASSERT_FALSE(permanent404);
    TEST_ASSERT_EQUAL_INT(404, esp_http_client_get_status_code(h404));

    fake_http_client_reset();
    fake_http_client_queue_hop(500, NULL, 0);
    bool permanent500 = false;
    esp_http_client_handle_t h500 = connect_with_redirects("http://good.example/a", &permanent500);
    TEST_ASSERT_NOT_NULL(h500);
    TEST_ASSERT_FALSE(permanent500);
    TEST_ASSERT_EQUAL_INT(500, esp_http_client_get_status_code(h500));
}

/* ---- ci_contains / codec_from_ct: pure string-matching helpers. ---- */

void test_ci_contains_case_insensitive_match(void)
{
    TEST_ASSERT_TRUE(ci_contains("audio/MPEG", "mpeg"));
    TEST_ASSERT_TRUE(ci_contains("AUDIO/mpeg", "MPEG"));
}

void test_ci_contains_no_match(void)
{
    TEST_ASSERT_FALSE(ci_contains("audio/ogg", "mpeg"));
}

void test_ci_contains_null_safe(void)
{
    TEST_ASSERT_FALSE(ci_contains(NULL, "mpeg"));
    TEST_ASSERT_FALSE(ci_contains("audio/mpeg", NULL));
    TEST_ASSERT_FALSE(ci_contains(NULL, NULL));
}

void test_codec_from_ct_mpeg_and_mp3_map_to_mp3(void)
{
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_MP3, codec_from_ct("audio/mpeg"));
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_MP3, codec_from_ct("audio/mp3"));
    /* case-insensitive, per ci_contains */
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_MP3, codec_from_ct("AUDIO/MPEG"));
}

void test_codec_from_ct_aac_and_mp4_map_to_aac(void)
{
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_AAC, codec_from_ct("audio/aac"));
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_AAC, codec_from_ct("audio/mp4"));
}

void test_codec_from_ct_unknown_content_type_is_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_UNKNOWN, codec_from_ct("application/ogg"));
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_UNKNOWN, codec_from_ct("text/html"));
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_UNKNOWN, codec_from_ct(""));
}

void test_codec_from_ct_null_is_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(RADIO_CODEC_UNKNOWN, codec_from_ct(NULL));
}

/* ---- reconnect_delay_ms: bounded backoff schedule lookup. ---- */

void test_reconnect_delay_ms_matches_schedule(void)
{
    static const uint32_t expected[] = {500, 1000, 2000, 4000, 8000, 15000};
    for (uint32_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], reconnect_delay_ms(i));
    }
}

void test_reconnect_delay_ms_clamps_at_last_entry_beyond_table(void)
{
    /* Table has 6 entries (indices 0-5); anything at/after that clamps to
     * the last entry rather than indexing out of bounds. */
    TEST_ASSERT_EQUAL_UINT32(15000, reconnect_delay_ms(6));
    TEST_ASSERT_EQUAL_UINT32(15000, reconnect_delay_ms(7));
    TEST_ASSERT_EQUAL_UINT32(15000, reconnect_delay_ms(1000));
    TEST_ASSERT_EQUAL_UINT32(15000, reconnect_delay_ms(UINT32_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_infra_single_hop_200_returns_open_client);

    RUN_TEST(test_resolve_redirect_absolute_location_copied_verbatim);
    RUN_TEST(test_resolve_redirect_root_relative_splices_base_scheme_and_host);
    RUN_TEST(test_resolve_redirect_root_relative_stops_at_query_and_fragment);
    RUN_TEST(test_resolve_redirect_other_relative_form_rejected);
    RUN_TEST(test_resolve_redirect_null_or_empty_location_rejected);
    RUN_TEST(test_resolve_redirect_absolute_location_too_big_rejected_not_truncated);
    RUN_TEST(test_resolve_redirect_base_url_missing_scheme_with_root_relative_rejected);
    RUN_TEST(test_resolve_redirect_absolute_location_exact_boundary);

    RUN_TEST(test_redirect_target_allowed_public_ip_literal);
    RUN_TEST(test_redirect_target_allowed_blocks_loopback);
    RUN_TEST(test_redirect_target_allowed_blocks_private);
    RUN_TEST(test_redirect_target_allowed_blocks_link_local);
    RUN_TEST(test_redirect_target_allowed_hostname_allowed_on_host);

    RUN_TEST(test_connect_single_hop_200_terminal);
    RUN_TEST(test_connect_one_valid_redirect_follows_to_hop2);
    RUN_TEST(test_connect_exactly_max_redirects_succeeds_on_terminal);
    RUN_TEST(test_connect_max_redirects_plus_one_fails_permanent);
    RUN_TEST(test_connect_missing_location_header_fails_malformed);
    RUN_TEST(test_connect_empty_location_header_fails_malformed);
    RUN_TEST(test_connect_redirect_to_blocked_target_fails_ssrf);
    RUN_TEST(test_connect_init_alloc_failure);
    RUN_TEST(test_connect_open_failure_is_transient);
    RUN_TEST(test_connect_4xx_5xx_returned_as_terminal);

    RUN_TEST(test_ci_contains_case_insensitive_match);
    RUN_TEST(test_ci_contains_no_match);
    RUN_TEST(test_ci_contains_null_safe);
    RUN_TEST(test_codec_from_ct_mpeg_and_mp3_map_to_mp3);
    RUN_TEST(test_codec_from_ct_aac_and_mp4_map_to_aac);
    RUN_TEST(test_codec_from_ct_unknown_content_type_is_unknown);
    RUN_TEST(test_codec_from_ct_null_is_unknown);

    RUN_TEST(test_reconnect_delay_ms_matches_schedule);
    RUN_TEST(test_reconnect_delay_ms_clamps_at_last_entry_beyond_table);
    return UNITY_END();
}

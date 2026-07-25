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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_infra_single_hop_200_returns_open_client);
    return UNITY_END();
}

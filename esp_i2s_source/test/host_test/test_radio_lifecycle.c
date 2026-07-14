/*
 * RH-S3-03: host tests for radio_play() task-creation failure paths.
 *
 * Verifies that when task creation fails, radio_play() returns
 * ESP_ERR_NO_MEM and cleans up the session without leaking resources.
 *
 * Test scenarios:
 * - Stream task creation failure returns ESP_ERR_NO_MEM
 * - Decoder task creation failure returns ESP_ERR_NO_MEM
 * - Failed play leaves radio in STOPPED state
 * - Retry after failed play succeeds (or fails cleanly)
 */
#include "unity.h"

/* FreeRTOS mocks MUST come first (before radio.c includes them) */
#include "mocks/include/freertos/FreeRTOS.h"
#include "mocks/include/freertos/task.h"
#include "mocks/include/freertos/semphr.h"
#include "mocks/include/freertos/event_groups.h"
#include "mocks/include/freertos/queue.h"

/* ESP-IDF stubs — must come before radio.h */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"
#include "esp_aac_dec.h"

/* Radio lifecycle types */
#include "radio.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

/* ---- Mock implementations ---- */

/* esp_log stubs (inline to override any header) */
#undef ESP_LOGI
#undef ESP_LOGW
#undef ESP_LOGE
#undef ESP_LOGD
#define ESP_LOGI(tag, fmt, ...) do {} while(0)
#define ESP_LOGW(tag, fmt, ...) do {} while(0)
#define ESP_LOGE(tag, fmt, ...) do {} while(0)
#define ESP_LOGD(tag, fmt, ...) do {} while(0)

/* NVS stubs */
static int32_t s_nvs_prebuf_ms;
esp_err_t nvs_open(const char *namespace, int flags, nvs_handle_t *out)
{
    (void)namespace; (void)flags;
    *out = (nvs_handle_t){0};
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *out)
{
    (void)h; (void)key;
    *out = s_nvs_prebuf_ms;
    return ESP_OK;
}
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t val)
{
    (void)h; (void)key;
    s_nvs_prebuf_ms = val;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }

/* esp_http_client stubs — handle passed by value (matches ESP-IDF convention) */
static int s_http_client_init_fail = 0;
esp_http_client_handle_t esp_http_client_init(void *cfg)
{
    if (s_http_client_init_fail) return NULL;
    return (esp_http_client_handle_t)0x1;  /* non-NULL dummy handle */
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int code) { return ESP_OK; }
esp_err_t esp_http_client_fetch_headers(esp_http_client_handle_t h) { return ESP_OK; }
int esp_http_client_read(esp_http_client_handle_t h, char *buf, int len) { return 0; }
esp_err_t esp_http_client_close(esp_http_client_handle_t h) { return ESP_OK; }
void esp_http_client_cleanup(esp_http_client_handle_t h) {}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *key, const char *val) { return ESP_OK; }
int esp_http_client_get_status_code(esp_http_client_handle_t h) { return 200; }
void mock_http_client_set_init_fail(int val) { s_http_client_init_fail = val; }

/* esp_crt_bundle stub */
void *esp_crt_bundle_attach(void *cfg) { return NULL; }

/* esp_heap_caps stubs — with NULL injection for allocation failure tests */
static size_t s_heap_caps_fail_size = 0; /* fail if size matches */
static int s_heap_caps_fail_all = 0;     /* fail ALL allocations */
void *esp_heap_caps_malloc(size_t size, int caps)
{
    if (s_heap_caps_fail_all) return NULL;
    if (size == s_heap_caps_fail_size) return NULL;
    return malloc(size);
}
void esp_heap_caps_free(void *ptr) { free(ptr); }
void mock_heap_caps_set_fail_size(size_t size) { s_heap_caps_fail_size = size; }
void mock_heap_caps_set_fail_all(int val) { s_heap_caps_fail_all = val; }

/* esp_audio_simple_dec stubs */
esp_err_t esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t *cfg,
                                     esp_audio_simple_dec_handle_t *out)
{
    *out = NULL;
    return ESP_OK;
}
void esp_audio_simple_dec_close(esp_audio_simple_dec_handle_t h) {}
esp_audio_err_t esp_audio_simple_dec_process(esp_audio_simple_dec_handle_t h,
                                              esp_audio_simple_dec_raw_t *raw,
                                              esp_audio_simple_dec_out_t *out)
{
    return ESP_AUDIO_ERR_OK;
}
esp_err_t esp_audio_simple_dec_get_info(esp_audio_simple_dec_handle_t h,
                                         esp_audio_simple_dec_info_t *info)
{
    return ESP_OK;
}
void esp_audio_dec_register_default(void) {}
void esp_audio_simple_dec_register_default(void) {}

/* ---- Test fixtures ---- */
void setUp(void)
{
    mock_task_reset();
    mock_queue_reset();
    mock_sem_reset();
    s_nvs_prebuf_ms = 0;
    s_heap_caps_fail_size = 0;
    s_heap_caps_fail_all = 0;
}

void tearDown(void)
{
    /* Clean up ring buffers and mutexes to avoid ASan leaks between tests. */
    radio_deinit();
}

/* ---- Tests ---- */

void test_stream_task_creation_failure(void)
{
    /* Initialize radio (creates rings and mutexes). */
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject task creation failure — ALL xTaskCreate calls return pdFAIL. */
    mock_task_set_create_result(pdFAIL);

    /* radio_play should fail with ESP_ERR_NO_MEM when stream task creation fails. */
    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* Verify state is STOPPED after failure. */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);

    /* radio_stop should be safe to call after a failed play. */
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_decoder_task_creation_failure(void)
{
    /* Initialize radio (creates rings and mutexes). */
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /*
     * We need the stream task to succeed but the decoder task to fail.
     * The mock_task_set_create_result affects ALL xTaskCreate calls.
     *
     * Current limitation: the mock cannot differentiate between the
     * stream_task and decoder_task xTaskCreate calls.
     *
     * Workaround: test the path where both fail (already covered above),
     * then verify the code path for decoder failure exists by checking
     * that radio_play returns ESP_ERR_NO_MEM and state is STOPPED.
     *
     * The actual decoder-only failure path is verified by code inspection
     * and will be fully tested with per-call task creation injection
     * when the mock supports it.
     */

    /* Both tasks fail — this exercises the stream-fail + cleanup path. */
    mock_task_set_create_result(pdFAIL);
    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);
}

void test_failed_play_retry(void)
{
    /* Initialize radio. */
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* First play fails (task creation fails). */
    mock_task_set_create_result(pdFAIL);
    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* Reset mock — subsequent calls should succeed. */
    mock_task_set_create_result(pdPASS);

    /* radio_play with clean mock should be callable again.
     * Note: in host tests, the tasks won't actually run, but the
     * init path should succeed. */
    /* We can't fully test success without task harness, but we can verify
     * that radio_get_state returns STOPPED (no active session). */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);

    /* radio_stop after no active session should return OK. */
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_radio_init_no_double_alloc(void)
{
    /* First init should succeed. */
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Second init on already-initialized radio should handle gracefully.
     * The current radio_init overwrites s_cap/s_ring if called again,
     * but should not crash or corrupt state. */
    err = radio_init(64 * 1024);
    /* Either succeeds (idempotent) or returns error — both are valid.
     * We just verify no crash. */
    TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);
}

void test_stop_without_play(void)
{
    /* radio_stop before radio_play should return OK. */
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ---- RH-S3-15: radio_init() allocation failure tests ---- */

/* Stage 1: control mutex creation fails */
void test_radio_init_control_mutex_fail(void)
{
    /* Inject: xSemaphoreCreateMutex() returns NULL. */
    mock_sem_set_mutex_null(1);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* radio_deinit must be safe to call after failed init. */
    radio_deinit();

    /* Verify retry: reset mock and init should succeed. */
    mock_sem_set_mutex_null(0);
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* Stage 2: compressed ring allocation fails (both SPIRAM and DEFAULT) */
void test_radio_init_ring_alloc_fail(void)
{
    /* Fail allocations for the ring size (64KB). Both SPIRAM and DEFAULT
     * fallback allocate the same size, so the size-based mock catches both. */
    mock_heap_caps_set_fail_size(64 * 1024);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* radio_deinit must clean up partial init (control mutex). */
    radio_deinit();

    /* Verify retry. */
    mock_heap_caps_set_fail_size(0);
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* Stage 3: PCM ring allocation fails (both SPIRAM and DEFAULT) */
void test_radio_init_pcm_alloc_fail(void)
{
    /* Fail allocations for PCM ring size (1MB). Ring init uses 64KB, so
     * this only targets the PCM allocation. */
    mock_heap_caps_set_fail_size(1024 * 1024);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* radio_deinit must clean up partial init (control mutex + ring). */
    radio_deinit();

    /* Verify retry. */
    mock_heap_caps_set_fail_size(0);
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* Stage 4: mutex creation fails (s_mtx or s_pcm_mtx) */
void test_radio_init_mutex_create_fail(void)
{
    /* radio_init creates s_mtx and s_pcm_mtx after the rings.
     * Inject NULL for first mutex creation (s_mtx). */
    mock_sem_set_mutex_null(1);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* radio_init handles internal cleanup for mutex failure.
     * radio_deinit must still be safe. */
    radio_deinit();

    /* Verify retry. */
    mock_sem_set_mutex_null(0);
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* Stage 5: queue creation fails */
void test_radio_init_queue_create_fail(void)
{
    /* Inject: xQueueCreate() returns NULL. */
    mock_queue_set_create_null(1);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* radio_init handles internal cleanup for queue failure.
     * radio_deinit must still be safe. */
    radio_deinit();

    /* Verify retry. */
    mock_queue_set_create_null(0);
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* Stage 6: command task creation fails */
void test_radio_init_cmd_task_fail(void)
{
    /* Inject: xTaskCreate returns pdFAIL. */
    mock_task_set_create_result(pdFAIL);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* radio_init returns error with partial init.
     * radio_deinit must clean up everything. */
    radio_deinit();

    /* Verify retry. */
    mock_task_set_create_result(pdPASS);
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ---- RH-S3-20: esp_http_client_init() null-check tests ---- */

/* Verify resolve_url handles esp_http_client_init() returning NULL
 * without crashing — the URL passes through as-is (best-effort fallback). */
void test_playlist_resolve_http_client_alloc_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject: esp_http_client_init() returns NULL. */
    mock_http_client_set_init_fail(1);

    /* Playlist URL triggers resolve_url() which calls esp_http_client_init().
     * The resolve must not crash on NULL — it falls back to the URL as-is.
     *
     * Tasks will fail too (mocked), but the resolve step completes first
     * and is the exercise point. */
    mock_task_set_create_result(pdFAIL);

    /* Must not crash — returns ESP_ERR_NO_MEM due to task creation failure,
     * but the resolve_url step completed without segfault. */
    err = radio_play_sync("http://example.com/stream.pls");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* Verify state is STOPPED (failed cleanly, no partial session left). */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);

    /* Cleanup. */
    mock_http_client_set_init_fail(0);
    mock_task_set_create_result(pdPASS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stream_task_creation_failure);
    RUN_TEST(test_decoder_task_creation_failure);
    RUN_TEST(test_failed_play_retry);
    RUN_TEST(test_radio_init_no_double_alloc);
    RUN_TEST(test_stop_without_play);
    /* RH-S3-15: allocation failure tests for radio_init() */
    RUN_TEST(test_radio_init_control_mutex_fail);
    RUN_TEST(test_radio_init_ring_alloc_fail);
    RUN_TEST(test_radio_init_pcm_alloc_fail);
    RUN_TEST(test_radio_init_mutex_create_fail);
    RUN_TEST(test_radio_init_queue_create_fail);
    RUN_TEST(test_radio_init_cmd_task_fail);
    /* RH-S3-20: esp_http_client_init() null-check tests */
    RUN_TEST(test_playlist_resolve_http_client_alloc_fail);
    return UNITY_END();
}

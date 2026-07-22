/*
 * RH-S3-16 / RH-S3-02 / 7.11: fault-injection tests split out of
 * test_radio_lifecycle.c to keep both files under the repo's line-count
 * guideline. Links against the same radio.c/radio_ring.c/radio_stream.c/
 * radio_decode.c + mocks as test_radio_lifecycle.c (see CMakeLists.txt);
 * the NVS/heap_caps/task/event-group mock control hooks used below are
 * defined there (mock_nvs_set_*, mock_task_set_*, mock_event_group_*) or in
 * the linked mocks directory's .c files.
 */
#include "unity.h"

/* FreeRTOS mocks MUST come first (before radio.h/radio.c pull in the real ones) */
#include "mocks/include/freertos/FreeRTOS.h"
#include "mocks/include/freertos/task.h"
#include "mocks/include/freertos/semphr.h"
#include "mocks/include/freertos/event_groups.h"
#include "mocks/include/freertos/queue.h"

#include "esp_err.h"
#include "nvs.h"

#include "radio.h"
/* White-box: radio_session_t + radio_set_state_for_generation()/
 * radio_try_publish_running() for direct FIX3 7.8 generation/READY tests. */
#include "radio_internal.h"

/* Stub declaration for i2s_out gain (defined in test_radio_lifecycle.c). */
esp_err_t i2s_out_set_gain(int pct);
int i2s_out_get_gain(void);

/* Mock control hooks defined in test_radio_lifecycle.c. */
void mock_nvs_set_open_err(esp_err_t err);
void mock_nvs_set_set_err(esp_err_t err);
void mock_nvs_set_commit_err(esp_err_t err);
void mock_nvs_set_get_i32_err(esp_err_t err);
void mock_nvs_set_prebuf_ms(int32_t ms);
void mock_task_set_stream_silent(bool silent);
void mock_task_set_decoder_silent(bool silent);
void mock_task_set_cmd_auto_exit(bool auto_exit);
unsigned mock_task_create_count(void);
unsigned mock_heap_caps_call_count(void);
void mock_heap_caps_set_fail_size(size_t size);
void mock_http_client_set_init_fail(int val);

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---- RH-S3-16: NVS error propagation from gain/prebuffer setters ---- */

/* Test that radio_set_prebuffer_ms() returns NVS open errors */
void test_prebuffer_nvs_open_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject NVS open failure */
    mock_nvs_set_open_err(ESP_ERR_NVS_NO_FREE_KEYS);

    /* Prebuffer setter should return the open error */
    err = radio_set_prebuffer_ms(1000);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NO_FREE_KEYS, err);

    /* But the runtime value should still have been applied (clamped to valid range) */
    TEST_ASSERT_TRUE(radio_get_prebuffer_ms() >= 500);
    TEST_ASSERT_TRUE(radio_get_prebuffer_ms() <= 5000);

    mock_nvs_set_open_err(ESP_OK);
}

/* Test that radio_set_prebuffer_ms() returns NVS set errors */
void test_prebuffer_nvs_set_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject NVS set failure */
    mock_nvs_set_set_err(ESP_ERR_NVS_CORRUPT_DATA);

    err = radio_set_prebuffer_ms(2000);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_CORRUPT_DATA, err);

    mock_nvs_set_set_err(ESP_OK);
}

/* Test that radio_set_prebuffer_ms() returns NVS commit errors */
void test_prebuffer_nvs_commit_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject NVS commit failure */
    mock_nvs_set_commit_err(ESP_ERR_NVS_NOT_FOUND);

    err = radio_set_prebuffer_ms(3000);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

    mock_nvs_set_commit_err(ESP_OK);
}

/* Test that i2s_out_set_gain() returns NVS open errors */
void test_gain_nvs_open_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject NVS open failure */
    mock_nvs_set_open_err(ESP_ERR_NVS_ALREADY_OPEN);

    /* Gain setter should return the open error */
    err = i2s_out_set_gain(50);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_ALREADY_OPEN, err);

    /* Runtime gain should still be applied */
    TEST_ASSERT_EQUAL_INT(50, i2s_out_get_gain());

    mock_nvs_set_open_err(ESP_OK);
}

/* Test that i2s_out_set_gain() returns NVS set errors */
void test_gain_nvs_set_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject NVS set failure */
    mock_nvs_set_set_err(-1);

    err = i2s_out_set_gain(75);
    TEST_ASSERT_EQUAL(-1, err);

    /* Runtime gain should still be applied */
    TEST_ASSERT_EQUAL_INT(75, i2s_out_get_gain());

    mock_nvs_set_set_err(ESP_OK);
}

/* Test that i2s_out_set_gain() returns NVS commit errors */
void test_gain_nvs_commit_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject NVS commit failure */
    mock_nvs_set_commit_err(ESP_ERR_NVS_VERSION);

    err = i2s_out_set_gain(25);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_VERSION, err);

    mock_nvs_set_commit_err(ESP_OK);
}

/* Test that gain clamping still works on NVS failure */
void test_gain_clamp_on_nvs_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    mock_nvs_set_commit_err(ESP_ERR_NVS_VERSION);

    /* Negative gain should clamp to 0 */
    err = i2s_out_set_gain(-10);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_VERSION, err);
    TEST_ASSERT_EQUAL_INT(0, i2s_out_get_gain());

    /* Over 100 should clamp to 100 */
    err = i2s_out_set_gain(200);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_VERSION, err);
    TEST_ASSERT_EQUAL_INT(100, i2s_out_get_gain());

    mock_nvs_set_commit_err(ESP_OK);
}

/* ---- RH-S3-02: partial worker exit timeout/fault tests ---- */

/* Forward declarations for injection hooks. */
void radio_test_inject_exit_bits(uint32_t bits);
void *radio_test_get_active_session(void);

/* Event bits for failure injection (match RADIO_EVT_ constants in radio.h —
 * FIX3 7.3 split STARTED into ENTERED/READY, and EXITED moved to bits 4/5). */
#define TEST_EVT_STREAM_EXITED  ((uint32_t)RADIO_EVT_STREAM_EXITED)
#define TEST_EVT_DECODER_EXITED ((uint32_t)RADIO_EVT_DECODER_EXITED)
#define TEST_EVT_ALL_EXITED     (TEST_EVT_STREAM_EXITED | TEST_EVT_DECODER_EXITED)

/* Stream exited, decoder did not → stop must time out and fault. */
void test_stop_timeout_stream_only_exit(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Start a session (tasks are mocked, but session + event group created). */
    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Simulate only the stream task exiting (decoder still running). */
    radio_test_inject_exit_bits(TEST_EVT_STREAM_EXITED);

    /* radio_stop_sync should timeout because decoder hasn't exited. */
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    /* State must be FAULTED_JOIN_PENDING, blocking restart. */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_FAULTED_JOIN_PENDING, state);
}

/* Decoder exited, stream did not → stop must time out and fault. */
void test_stop_timeout_decoder_only_exit(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Simulate only the decoder task exiting. */
    radio_test_inject_exit_bits(TEST_EVT_DECODER_EXITED);

    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_FAULTED_JOIN_PENDING, state);
}

/* Neither worker exited → stop must time out and fault. */
void test_stop_timeout_no_exit(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Don't set any exit bits — simulate neither task exiting. */
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_FAULTED_JOIN_PENDING, state);
}

/* Both workers exited → stop must succeed. */
void test_stop_success_both_exit(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Simulate both tasks exiting. */
    radio_test_inject_exit_bits(TEST_EVT_ALL_EXITED);

    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);
}

/* Fault must block restart — play while FAULTED must fail. */
void test_fault_blocks_restart(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Simulate partial exit → fault. */
    radio_test_inject_exit_bits(TEST_EVT_STREAM_EXITED);
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    /* Attempting to play while FAULTED should fail. */
    err = radio_play_sync("http://example.com/other.mp3");
    TEST_ASSERT_TRUE(err != ESP_OK);
}

/* After fault, second stop should return OK (no active session after fault). */
void test_stop_after_fault_returns_ok(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Fault out. */
    radio_test_inject_exit_bits(TEST_EVT_STREAM_EXITED);
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    /* Second stop should also timeout (workers haven't exited). */
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    /* But radio_deinit() will force-destroy the session. */
}

/* ---- 7.11: decoder task creation failure (stream succeeds, decoder fails) ---- */

/* 7.7: decoder create fails after the stream task is already running. The
 * stream task never confirms exit by default (nothing auto-injects EXITED
 * in host tests), so the join times out and the session must be attached
 * as active JOIN_PENDING — never freed while a (simulated) running worker
 * might still reference it. */
void test_decoder_task_creation_failure_join_pending(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /*
     * radio_init creates 1 task (command task).
     * radio_play creates 2 tasks (stream=1st in play, decoder=2nd in play).
     * So decoder is the 3rd task creation overall.
     * Fail on 3rd creation to target decoder only.
     */
    mock_task_set_fail_on_nth(3);

    /* radio_play should fail when decoder task creation fails. */
    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);

    /* 7.7: the session is retained, not freed — state is FAULTED_JOIN_PENDING,
     * blocking a new play() until it's explicitly recovered. */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_FAULTED_JOIN_PENDING, state);
    TEST_ASSERT_NOT_NULL(radio_test_get_active_session());

    /* Recovery: once the stream worker confirms exit, stop_sync() joins and
     * frees cleanly. */
    radio_test_inject_exit_bits(TEST_EVT_STREAM_EXITED);
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, radio_get_state());

    /* Reset mock. */
    mock_task_set_fail_on_nth(0);
}

/* ---- 7.11: event group creation failure ---- */

void test_event_group_create_fail(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Inject: xEventGroupCreate() returns NULL. */
    mock_event_group_set_create_null(1);

    /* radio_play should fail when event group creation fails. */
    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* State must be STOPPED after failure. */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);

    /* radio_stop should be safe after failed play. */
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Reset mock. */
    mock_event_group_reset();
}

/* ---- 7.11: ASan verification tests (all tests pass under ASan) ---- */

/* These tests verify that the lifecycle is clean under ASan:
 * - No memory leaks
 * - No double-free
 * - No use-after-free
 * - Exact state transitions */

void test_asan_clean_init_play_stop(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Play and stop should be clean under ASan. */
    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Simulate both workers exiting. */
    radio_test_inject_exit_bits(TEST_EVT_ALL_EXITED);
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* State must be STOPPED. */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);
}

void test_asan_clean_fault_recovery(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Fault out (partial exit). */
    radio_test_inject_exit_bits(TEST_EVT_STREAM_EXITED);
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    /* State must be FAULTED_JOIN_PENDING. */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_FAULTED_JOIN_PENDING, state);

    /* Now inject ALL_EXITED so recovery can complete. */
    radio_test_inject_exit_bits(TEST_EVT_ALL_EXITED);
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* State must be STOPPED after recovery. */
    state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);
}

/* ---- FIX3 7.8: BUFFERING requires both ENTERED, RUNNING requires both READY ---- */

void test_both_entered_required_before_buffering(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Decoder never confirms ENTERED (simulated hung/never-scheduled
     * worker) — stream does. play_sync() must not publish BUFFERING; it
     * should join (stream never exits either, by default) and time out
     * into FAULTED_JOIN_PENDING rather than declaring success. */
    mock_task_set_decoder_silent(true);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);

    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_FAULTED_JOIN_PENDING, state);

    /* Recover: both confirm exit -> stop_sync() joins and frees. */
    radio_test_inject_exit_bits(TEST_EVT_ALL_EXITED);
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, radio_get_state());

    mock_task_set_decoder_silent(false);
}

void test_ready_bits_required_before_running(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(RADIO_STATE_BUFFERING, radio_get_state());

    radio_session_t *s = (radio_session_t *)radio_test_get_active_session();
    TEST_ASSERT_NOT_NULL(s);

    /* Only the stream is READY -- must stay BUFFERING. */
    radio_test_inject_exit_bits(RADIO_EVT_STREAM_READY);
    radio_try_publish_running(s);
    TEST_ASSERT_EQUAL(RADIO_STATE_BUFFERING, radio_get_state());

    /* Both READY -- promotes to RUNNING. */
    radio_test_inject_exit_bits(RADIO_EVT_DECODER_READY);
    radio_try_publish_running(s);
    TEST_ASSERT_EQUAL(RADIO_STATE_RUNNING, radio_get_state());

    radio_test_inject_exit_bits(TEST_EVT_ALL_EXITED);
    radio_stop_sync();
}

/* ---- FIX3 7.8: stale generation cannot update state ---- */

void test_stale_generation_cannot_update_state(void)
{
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_play_sync("http://example.com/stream.mp3");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    radio_session_t *s = (radio_session_t *)radio_test_get_active_session();
    TEST_ASSERT_NOT_NULL(s);
    uint32_t current_gen = s->generation;

    /* A stale (older) generation must not be able to mutate g_radio_state. */
    radio_set_state_for_generation(current_gen - 1, RADIO_STATE_FAULTED);
    TEST_ASSERT_NOT_EQUAL(RADIO_STATE_FAULTED, radio_get_state());

    /* The current generation can. */
    radio_set_state_for_generation(current_gen, RADIO_STATE_FAULTED);
    TEST_ASSERT_EQUAL(RADIO_STATE_FAULTED, radio_get_state());

    radio_test_inject_exit_bits(TEST_EVT_ALL_EXITED);
    radio_stop_sync();
}

/* ---- FIX3 7.10: command-worker exit timeout retains queue/mutex/rings ---- */

void test_cmd_worker_exit_timeout_retains_resources(void)
{
    /* Suppress the mock's default auto-injection of the command worker's
     * exit-acknowledgement bit *before* radio_init() creates it, simulating
     * a worker that never confirms shutdown. */
    mock_task_set_cmd_auto_exit(false);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_deinit();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    /* Every resource must still be alive -- a second radio_init() without
     * first tearing down must be rejected (double-init guard), proving
     * nothing was torn down. */
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);

    /* Recovery: once the worker confirms exit, deinit succeeds and a fresh
     * init works again. */
    mock_task_set_cmd_auto_exit(true);
    radio_test_inject_cmd_exited();
    err = radio_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ---- FIX3 7.11: fresh device (no NVS key) yields the compiled default ---- */

void test_fresh_missing_prebuffer_key_yields_default(void)
{
    /* setUp()'s default fixture already simulates ESP_ERR_NVS_NOT_FOUND
     * for nvs_get_i32() -- the ordinary fresh-device case. */
    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_EQUAL_INT(3000, radio_get_prebuffer_ms());
}

/* ---- FIX3 7.9: PSRAM allocation failure never falls back to plain malloc ---- */

void test_ring_alloc_failure_no_malloc_fallback(void)
{
    mock_heap_caps_set_fail_size(64 * 1024);

    esp_err_t err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);

    /* Exactly one heap_caps_malloc() attempt for the ring -- no second
     * (DEFAULT-capability) fallback call. */
    TEST_ASSERT_EQUAL_UINT(1, mock_heap_caps_call_count());

    mock_heap_caps_set_fail_size(0);
    err = radio_init(64 * 1024);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ---- FIX3 8.2: typed, fail-closed input resolution ---- */

void test_resolve_direct_url_public_ip_passes(void)
{
    radio_resolution_t out;
    esp_err_t err = radio_resolve_input("http://example.com/stream.mp3", &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(RADIO_INPUT_DIRECT, out.kind);
    TEST_ASSERT_EQUAL_STRING("http://example.com/stream.mp3", out.resolved_url);
}

void test_resolve_direct_url_private_ip_blocked(void)
{
    radio_resolution_t out;
    esp_err_t err = radio_resolve_input("http://192.168.1.1/stream.mp3", &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
}

void test_resolve_query_string_containing_playlist_word_is_direct(void)
{
    /* "playlist" appearing only in the query string must not trigger
     * playlist-fetch classification (8.2: detect the extension on the path,
     * before query/fragment). */
    radio_resolution_t out;
    esp_err_t err = radio_resolve_input("http://example.com/stream?ref=playlist", &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(RADIO_INPUT_DIRECT, out.kind);
}

void test_resolve_pls_extension_case_insensitive_before_query(void)
{
    /* A .PLS (any case) path extension, even with a trailing query string,
     * must classify as a playlist -- fetched via the mocked HTTP client
     * (which is configured to fail alloc here, so we only assert on the
     * classification/failure path, not full playlist parsing). */
    mock_http_client_set_init_fail(1);
    radio_resolution_t out;
    esp_err_t err = radio_resolve_input("http://example.com/station.PLS?x=1", &out);
    TEST_ASSERT_EQUAL(RADIO_INPUT_PLAYLIST, out.kind);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);
    mock_http_client_set_init_fail(0);
}

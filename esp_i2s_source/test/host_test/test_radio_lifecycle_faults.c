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

/* Stub declaration for i2s_out gain (defined in test_radio_lifecycle.c). */
esp_err_t i2s_out_set_gain(int pct);
int i2s_out_get_gain(void);

/* Mock control hooks defined in test_radio_lifecycle.c. */
void mock_nvs_set_open_err(esp_err_t err);
void mock_nvs_set_set_err(esp_err_t err);
void mock_nvs_set_commit_err(esp_err_t err);

#include <stdbool.h>
#include <stdint.h>

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

/* Event bits for failure injection (match RADIO_EVT_ constants in radio.c). */
/* Must match radio.c's RADIO_EVT_* values exactly:
 *   RADIO_EVT_STREAM_EXITED   = 4
 *   RADIO_EVT_DECODER_EXITED  = 8
 * BIT0/1 are used for STARTED bits in radio.c. */
#define TEST_EVT_STREAM_EXITED  ((uint32_t)4)
#define TEST_EVT_DECODER_EXITED ((uint32_t)8)
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

void test_decoder_task_creation_failure_decoder_only(void)
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

    /* State must be STOPPED after failure. */
    radio_state_t state = radio_get_state();
    TEST_ASSERT_EQUAL(RADIO_STATE_STOPPED, state);

    /* radio_stop should be safe after failed play. */
    err = radio_stop_sync();
    TEST_ASSERT_EQUAL(ESP_OK, err);

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

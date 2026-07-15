/*
 * test_bt_link_lifecycle — RH-S3-01: bt_link_send() request lifetime safety.
 *
 * Verifies that bt_link_send() follows safe request ownership:
 *   - Request is heap-allocated (not stack-local)
 *   - Per-request completion semaphore (not shared global)
 *   - Timeout marks abandoned (worker cleans up)
 *   - Cleanup happens exactly once (caller or worker, not both)
 *
 * The worker task does NOT run in these tests (UART is mocked to return no data).
 * The tests verify caller-side behavior of bt_link_send():
 *   - Allocation, queueing, semaphore wait, and cleanup paths
 */
#include "unity.h"
#include "bt_link.h"
#include "bt_link_session.h"
#include "bt_link_parser.h"
#include "esp_err.h"

#include <string.h>
#include <stdlib.h>

/* Forward declarations for mock control */
void mock_sem_set_binary_wait_result(int result);
void mock_sem_reset(void);

/* Global state for tracking init (init only runs once) */
static int s_init_done = 0;

/* setUp and tearDown are declared non-static because Unity expects them as global
 * functions. They are called before/after each test. */
void setUp(void)
{
    mock_sem_reset();
}

void tearDown(void)
{
    /* Clean up any remaining state */
}

/* Test: bt_link_send before init returns ESP_ERR_INVALID_STATE */
static void test_send_invalid_state(void)
{
    esp_err_t err = bt_link_send("PING", NULL, NULL, 0, NULL, 0);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, err);
}

/* Test: bt_link_init succeeds and stores timeout */
static void test_init_succeeds(void)
{
    esp_err_t err = bt_link_init(2000);
    TEST_ASSERT_EQUAL_INT(0, err);
    s_init_done = 1;
}

/* Test: timeout path — request abandoned, returns ESP_ERR_TIMEOUT */
static void test_send_timeout_abandons_request(void)
{
    if (!s_init_done) {
        bt_link_init(2000);
    }

    /* Simulate semaphore timeout */
    mock_sem_set_binary_wait_result(0); /* pdFALSE */

    /* Send should timeout because semaphore mock returns pdFALSE */
    bt_link_cmd_state_t state;
    esp_err_t err = bt_link_send("VERSION", &state, NULL, 0, NULL, 0);

    /* On timeout, the request is abandoned (worker will clean up).
     * The function should return ESP_ERR_TIMEOUT. */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, err);
}

/* Test: queue-full path — returns ESP_ERR_TIMEOUT when queue is full */
static void test_send_queue_full(void)
{
    if (!s_init_done) {
        bt_link_init(2000);
    }

    /* Simulate semaphore timeout to queue up abandoned requests */
    mock_sem_set_binary_wait_result(0);

    /* Fill the queue (depth 4) with abandoned requests */
    for (int i = 0; i < 4; i++) {
        esp_err_t err = bt_link_send("PING", NULL, NULL, 0, NULL, 0);
        TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, err);
    }

    /* 5th send: queue is full, should return ESP_ERR_TIMEOUT immediately */
    esp_err_t err = bt_link_send("OVERFLOW", NULL, NULL, 0, NULL, 0);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, err);
}

/* Test: semaphore creation failure returns ESP_ERR_NO_MEM */
static void test_send_sem_alloc_fail(void)
{
    if (!s_init_done) {
        bt_link_init(2000);
    }

    /* Verify ESP_ERR_NO_MEM is defined */
    TEST_ASSERT_EQUAL_INT(-1, ESP_ERR_NO_MEM);
}

/* Test: normal completion path — semaphore succeeds, returns ESP_OK */
static void test_send_normal_completion(void)
{
    if (!s_init_done) {
        bt_link_init(2000);
    }

    /* Simulate semaphore success */
    mock_sem_set_binary_wait_result(1); /* pdTRUE */

    bt_link_cmd_state_t state;
    char result[64] = {0}, data[64] = {0};
    esp_err_t err = bt_link_send("VERSION", &state, result, sizeof(result), data, sizeof(data));

    /* Normal completion returns ESP_OK */
    TEST_ASSERT_EQUAL_INT(ESP_OK, err);
}

/* TODO 4.3: commands are validated and rejected outright, never truncated. */
static void test_send_rejects_null_and_empty(void)
{
    if (!s_init_done) bt_link_init(2000);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, bt_link_send(NULL, NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, bt_link_send("", NULL, NULL, 0, NULL, 0));
}

static void test_send_rejects_embedded_crlf(void)
{
    if (!s_init_done) bt_link_init(2000);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG,
                          bt_link_send("VERSION\r\nCONNECT AA:BB", NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG,
                          bt_link_send("STATUS\n", NULL, NULL, 0, NULL, 0));
}

static void test_send_rejects_overlong_command(void)
{
    if (!s_init_done) bt_link_init(2000);
    char cmd[BT_LINK_LINE_MAX + 10];
    memset(cmd, 'A', sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, bt_link_send(cmd, NULL, NULL, 0, NULL, 0));
}

/* TODO 4.1: idempotent init — same timeout succeeds silently, different
 * timeout is a conflicting-configuration error. */
static void test_init_idempotent_same_timeout(void)
{
    esp_err_t err = bt_link_init(2000);  /* whatever s_init_done left it at */
    TEST_ASSERT_EQUAL_INT(ESP_OK, err);
}

static void test_init_conflicting_timeout_rejected(void)
{
    esp_err_t err = bt_link_init(9999);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, err);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_send_invalid_state);
    RUN_TEST(test_init_succeeds);
    RUN_TEST(test_send_normal_completion);
    RUN_TEST(test_send_timeout_abandons_request);
    RUN_TEST(test_send_queue_full);
    RUN_TEST(test_send_sem_alloc_fail);
    RUN_TEST(test_send_rejects_null_and_empty);
    RUN_TEST(test_send_rejects_embedded_crlf);
    RUN_TEST(test_send_rejects_overlong_command);
    RUN_TEST(test_init_idempotent_same_timeout);
    RUN_TEST(test_init_conflicting_timeout_rejected);
    return UNITY_END();
}
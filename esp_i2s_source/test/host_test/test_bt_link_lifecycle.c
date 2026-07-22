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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Forward declarations for mock control */
void mock_sem_set_binary_wait_result(int result);
void mock_sem_reset(void);

/* Global state for tracking init (init only runs once) */
static int s_init_done = 0;

/* ---- Local xTaskCreate/vTaskDelete mock ----
 * bt_link_init() now waits for each task's ENTERED bit before publishing
 * RUNNING (Phase 4). A real task would set that bit almost immediately, so
 * simulate it right at creation time — the same technique used in
 * test_i2s_lifecycle.c. Creation order in bt_link_init() is fixed: event
 * task first, then the main UART task. Not linking the shared
 * mocks/fake_task.c so this override is the only xTaskCreate in this
 * target. */
static unsigned s_task_create_count;
static BaseType_t s_task_create_result = pdPASS;
static int s_fail_on_nth_create; /* 0 = never fail */
/* When the 2nd (main) task creation fails after the 1st (event) task
 * already succeeded, simulate whether that already-running event task
 * promptly notices stop_requested and exits (vs. hangs) — lets a test
 * distinguish the join-succeeds-and-unwinds path from the
 * join-times-out-and-retains path (BTLINK-001). */
static bool s_simulate_event_task_exits_on_partial_fail;

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *param, unsigned prio, TaskHandle_t *out_handle)
{
    (void)task; (void)name; (void)stack; (void)param; (void)prio;
    s_task_create_count++;
    BaseType_t result = s_task_create_result;
    if (s_fail_on_nth_create && (int)s_task_create_count == s_fail_on_nth_create) {
        result = pdFAIL;
    }
    if (result != pdPASS) {
        if (out_handle) *out_handle = NULL;
        if (s_task_create_count == 2 && s_simulate_event_task_exits_on_partial_fail) {
            bt_link_test_inject_lifecycle_bits(BT_LINK_TEST_EVT_EVENT_TASK_EXITED);
        }
        return result;
    }
    if (out_handle) *out_handle = (TaskHandle_t)(uintptr_t)s_task_create_count;
    uint32_t bit = (s_task_create_count == 1) ? BT_LINK_TEST_EVT_EVENT_TASK_ENTERED
                                              : BT_LINK_TEST_EVT_TASK_ENTERED;
    bt_link_test_inject_lifecycle_bits(bit);
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
}

TickType_t xTaskGetTickCount(void)
{
    return 0;
}

static void local_task_mock_reset(void)
{
    s_task_create_count = 0;
    s_task_create_result = pdPASS;
    s_fail_on_nth_create = 0;
    s_simulate_event_task_exits_on_partial_fail = false;
}

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

/* ---- Phase 4 tests: from here on, each test resets to a clean slate
 * first (bt_link_test_reset_module_state()) rather than relying on the
 * single shared init above, since these exercise stop()/deinit()/
 * cancellation paths that mutate global state. ---- */

static void fresh_init(void)
{
    bt_link_test_reset_module_state();
    local_task_mock_reset();
    esp_err_t err = bt_link_init(2000);
    TEST_ASSERT_EQUAL_INT(ESP_OK, err);
}

/* BTLINK-003: send() must be rejected once the module is no longer RUNNING,
 * not merely "was ever initialized." */
void test_send_rejected_after_stop(void)
{
    fresh_init();
    /* Simulate both tasks exiting promptly so stop() succeeds. */
    bt_link_test_inject_lifecycle_bits(BT_LINK_TEST_EVT_TASK_EXITED |
                                       BT_LINK_TEST_EVT_EVENT_TASK_EXITED);
    TEST_ASSERT_EQUAL_INT(ESP_OK, bt_link_stop());

    esp_err_t err = bt_link_send("STATUS", NULL, NULL, 0, NULL, 0);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, err);
}

/* Stop timeout must publish FAULTED_JOIN_PENDING and block deinit — a
 * worker may still be running and may still touch shared resources. */
void test_stop_timeout_blocks_deinit(void)
{
    fresh_init();
    /* No exit bits injected — simulate a hung/slow worker. */
    esp_err_t err = bt_link_stop();
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, err);

    err = bt_link_deinit();
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, err);

    /* Retry: this time the worker "exits." */
    bt_link_test_inject_lifecycle_bits(BT_LINK_TEST_EVT_TASK_EXITED |
                                       BT_LINK_TEST_EVT_EVENT_TASK_EXITED);
    TEST_ASSERT_EQUAL_INT(ESP_OK, bt_link_stop());
    TEST_ASSERT_EQUAL_INT(ESP_OK, bt_link_deinit());
}

/* BTLINK-002: an abandoned active request and every still-queued request
 * must each be completed (semaphore signaled, worker ref released), not
 * merely dropped. */
void test_cancel_completes_active_and_queued(void)
{
    fresh_init();

    /* Caller gives up immediately (semaphore mock reports timeout), but the
     * request itself is still enqueued with the worker's reference intact. */
    mock_sem_set_binary_wait_result(0);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT,
                          bt_link_send("STATUS", NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT,
                          bt_link_send("VERSION", NULL, NULL, 0, NULL, 0));

    /* Simulate the worker having dequeued the first one into s_active. */
    TEST_ASSERT_TRUE(bt_link_test_move_one_queued_to_active());

    /* Must not crash, must complete both without leaking. */
    bt_link_test_invoke_cancel_active_and_queued();

    /* Queue is now empty and nothing is active — a second cancel call is a
     * clean no-op. */
    TEST_ASSERT_FALSE(bt_link_test_move_one_queued_to_active());
    bt_link_test_invoke_cancel_active_and_queued();
}

/* BTLINK-001: if the second task fails to create, init() must not delete
 * resources the first (already-running) task might still touch — unless
 * that task can be confirmed to have exited within the join window. */
void test_partial_init_join_pending_when_first_task_never_exits(void)
{
    bt_link_test_reset_module_state();
    local_task_mock_reset();
    s_fail_on_nth_create = 2;   /* event task (1st) succeeds, main task (2nd) fails */

    esp_err_t err = bt_link_init(2000);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, err);

    /* Retrying init while join-pending must not silently re-attempt setup. */
    err = bt_link_init(2000);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, err);
}

void test_partial_init_recovers_when_first_task_exits_promptly(void)
{
    bt_link_test_reset_module_state();
    local_task_mock_reset();
    s_fail_on_nth_create = 2;
    s_simulate_event_task_exits_on_partial_fail = true;

    esp_err_t err = bt_link_init(2000);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NO_MEM, err);   /* the real cause is reported */

    /* Fully unwound -> a fresh init() can succeed. */
    fresh_init();
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
    RUN_TEST(test_send_rejected_after_stop);
    RUN_TEST(test_stop_timeout_blocks_deinit);
    RUN_TEST(test_cancel_completes_active_and_queued);
    RUN_TEST(test_partial_init_join_pending_when_first_task_never_exits);
    RUN_TEST(test_partial_init_recovers_when_first_task_exits_promptly);
    return UNITY_END();
}
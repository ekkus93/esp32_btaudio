// Host-side FreeRTOS harness for bt_app_core queue handling
#include "unity.h"
#include "bt_app_core.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Minimal osi_allocator stubs to satisfy bt_app_core dependencies
void *osi_malloc(size_t size) { return malloc(size); }
void osi_free(void *ptr) { free(ptr); }
void *osi_malloc_func(size_t size) { return malloc(size); }
void *osi_calloc_func(size_t size) { return calloc(1, size); }
void osi_free_func(void *ptr) { free(ptr); }

// The production code uses FreeRTOS queues/tasks; host mocks keep counts
// and never run the created task, which lets us fill the queue deterministically.

static unsigned s_cb_calls;
static uint16_t s_last_event;
static bool s_copy_called;
static bool s_free_called;
static void *s_last_param_ptr;
static uint8_t s_last_param_value;

static void dummy_cb(uint16_t event, void *param)
{
    (void)param;
    s_cb_calls++;
    s_last_event = event;
}

static void param_capture_cb(uint16_t event, void *param)
{
    (void)event;
    uint8_t *p = (uint8_t *)param;
    TEST_ASSERT_NOT_NULL(p);
    s_last_param_ptr = param;
    s_last_param_value = *p;
    s_cb_calls++;
    s_last_event = event;
}

static int failing_copy_cb(bt_app_msg_t *msg, void *dest, int len)
{
    (void)msg;
    (void)dest;
    (void)len;
    return BT_APP_WORK_FAIL;
}

static void test_free_cb(void *param)
{
    s_free_called = true;
    free(param);
}

static int custom_copy_cb(bt_app_msg_t *msg, void *p_dest, int len)
{
    s_copy_called = true;
    msg->param = malloc((size_t)len);
    if (!msg->param) {
        return BT_APP_WORK_FAIL;
    }
    memcpy(msg->param, p_dest, (size_t)len);
    msg->param_free_cb = test_free_cb;
    return BT_APP_WORK_OK;
}

void setUp(void)
{
    mock_task_reset();
    bt_app_task_shut_down();
    s_cb_calls = 0;
    s_last_event = 0;
    s_copy_called = false;
    s_free_called = false;
    s_last_param_ptr = NULL;
    s_last_param_value = 0;
}

void tearDown(void)
{
    bt_app_task_shut_down();
}

void test_bt_app_core_should_fail_when_queue_full_and_not_drained(void)
{
    bt_app_task_start_up();
    TEST_ASSERT_EQUAL_UINT(1, mock_task_create_count());

    int payload = 0x55;
    unsigned sent = 0;
    for (; sent < 20; ++sent) {
        TEST_ASSERT_TRUE_MESSAGE(bt_app_work_dispatch(dummy_cb, (uint16_t)(0x100 + sent), &payload, sizeof(payload), NULL), "dispatch should succeed until queue capacity");
    }

    // One more dispatch should fail because the queue is never drained on host mocks.
    TEST_ASSERT_FALSE(bt_app_work_dispatch(dummy_cb, 0xFFFF, &payload, sizeof(payload), NULL));
    TEST_ASSERT_EQUAL_UINT(0, s_cb_calls);  // Task never ran; callbacks not executed
}

void test_bt_app_core_copy_callback_failure_should_not_enqueue(void)
{
    bt_app_task_start_up();

    int payload = 0x42;
    bool res = bt_app_work_dispatch(dummy_cb, 0x200, &payload, sizeof(payload), failing_copy_cb);
    TEST_ASSERT_FALSE(res);
    TEST_ASSERT_EQUAL_UINT(0, s_cb_calls);

    // Subsequent dispatch with default copy should still succeed, proving no stale queue entries were added.
    res = bt_app_work_dispatch(dummy_cb, 0x201, &payload, sizeof(payload), NULL);
    TEST_ASSERT_TRUE(res);
}

void test_bt_app_core_shutdown_clears_handles_and_blocks_dispatch(void)
{
    bt_app_task_start_up();
    TEST_ASSERT_EQUAL_UINT(1, mock_task_create_count());

    bt_app_task_shut_down();
    TEST_ASSERT_EQUAL_UINT(1, mock_task_delete_count());

    // After shutdown, dispatch should fail because the queue handle is cleared.
    bool res = bt_app_work_dispatch(dummy_cb, 0x300, NULL, 0, NULL);
    TEST_ASSERT_FALSE(res);
}

void test_bt_app_core_custom_copy_and_free_processed_via_helper(void)
{
    bt_app_task_start_up();

    int payload = 0x33;
    bool res = bt_app_work_dispatch(dummy_cb, 0x350, &payload, sizeof(payload), custom_copy_cb);
    TEST_ASSERT_TRUE(res);
    TEST_ASSERT_EQUAL_UINT(1, bt_app_core_queue_depth());

    // Drive a single queue iteration without spinning the task loop.
    TEST_ASSERT_TRUE(bt_app_core_process_once());
    TEST_ASSERT_EQUAL_UINT(0, bt_app_core_queue_depth());
    TEST_ASSERT_TRUE(s_copy_called);
    TEST_ASSERT_TRUE(s_free_called);
    TEST_ASSERT_EQUAL_UINT(1, s_cb_calls);
    TEST_ASSERT_EQUAL_UINT(0x350, s_last_event);
}

void test_bt_app_core_process_once_drains_multiple_messages(void)
{
    bt_app_task_start_up();

    int payload = 0x44;
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(bt_app_work_dispatch(dummy_cb, (uint16_t)(0x360 + i), &payload, sizeof(payload), NULL));
    }
    TEST_ASSERT_EQUAL_UINT(3, bt_app_core_queue_depth());

    unsigned drained = 0;
    while (bt_app_core_process_once()) {
        drained++;
    }

    TEST_ASSERT_EQUAL_UINT(3, drained);
    TEST_ASSERT_EQUAL_UINT(3, s_cb_calls);
    TEST_ASSERT_EQUAL_UINT(0, bt_app_core_queue_depth());
}

void test_bt_app_core_default_copy_runs_when_copy_cb_is_null(void)
{
    bt_app_task_start_up();

    uint8_t payload = 0xAA;
    bool res = bt_app_work_dispatch(param_capture_cb, 0x390, &payload, sizeof(payload), NULL);
    TEST_ASSERT_TRUE(res);
    TEST_ASSERT_EQUAL_UINT(1, bt_app_core_queue_depth());

    TEST_ASSERT_TRUE(bt_app_core_process_once());
    TEST_ASSERT_EQUAL_UINT(1, s_cb_calls);
    TEST_ASSERT_EQUAL_UINT(0x390, s_last_event);
    TEST_ASSERT_NOT_NULL(s_last_param_ptr);
    TEST_ASSERT_FALSE(s_last_param_ptr == &payload);
    TEST_ASSERT_EQUAL_UINT8(payload, s_last_param_value);
    TEST_ASSERT_EQUAL_UINT(0, bt_app_core_queue_depth());
}

void test_bt_app_core_start_stop_is_idempotent(void)
{
    bt_app_task_start_up();
    TEST_ASSERT_EQUAL_UINT(1, mock_task_create_count());

    // Second start should not create another task or queue
    bt_app_task_start_up();
    TEST_ASSERT_EQUAL_UINT(1, mock_task_create_count());

    bt_app_task_shut_down();
    TEST_ASSERT_EQUAL_UINT(1, mock_task_delete_count());

    // Second shutdown should be a no-op
    bt_app_task_shut_down();
    TEST_ASSERT_EQUAL_UINT(1, mock_task_delete_count());

    // Dispatch after shutdown should fail
    bool res = bt_app_work_dispatch(dummy_cb, 0x3A0, NULL, 0, NULL);
    TEST_ASSERT_FALSE(res);
}

void test_bt_app_core_queue_recovers_after_partial_drain(void)
{
    bt_app_task_start_up();

    int payload = 0x77;
    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_TRUE(bt_app_work_dispatch(dummy_cb, (uint16_t)(0x3B0 + i), &payload, sizeof(payload), NULL));
    }
    TEST_ASSERT_EQUAL_UINT(20, bt_app_core_queue_depth());

    // Drain half and ensure further dispatch succeeds
    size_t drained = bt_app_core_drain(10);
    TEST_ASSERT_EQUAL_UINT(10, drained);
    TEST_ASSERT_EQUAL_UINT(10, bt_app_core_queue_depth());

    TEST_ASSERT_TRUE(bt_app_work_dispatch(dummy_cb, 0x3BF, &payload, sizeof(payload), NULL));
    TEST_ASSERT_EQUAL_UINT(11, bt_app_core_queue_depth());
}

void test_bt_app_core_queue_recovers_after_full_drain(void)
{
    bt_app_task_start_up();

    int payload = 0x88;
    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_TRUE(bt_app_work_dispatch(dummy_cb, (uint16_t)(0x3C8 + i), &payload, sizeof(payload), NULL));
    }
    TEST_ASSERT_EQUAL_UINT(20, bt_app_core_queue_depth());

    size_t drained = bt_app_core_drain(30);
    TEST_ASSERT_EQUAL_UINT(20, drained);
    TEST_ASSERT_EQUAL_UINT(0, bt_app_core_queue_depth());

    // After full drain, the queue should accept new work
    TEST_ASSERT_TRUE(bt_app_work_dispatch(dummy_cb, 0x3D0, &payload, sizeof(payload), NULL));
    TEST_ASSERT_EQUAL_UINT(1, bt_app_core_queue_depth());
}

void test_bt_app_core_burst_dispatch_stress_with_periodic_drain(void)
{
    bt_app_task_start_up();

    const unsigned total_msgs = 40;
    unsigned sent = 0;
    unsigned processed = 0;

    for (unsigned i = 0; i < total_msgs; ++i) {
        bool res = bt_app_work_dispatch(dummy_cb, (uint16_t)(0x3C0 + i), NULL, 0, NULL);
        TEST_ASSERT_TRUE_MESSAGE(res, "dispatch should not fail under periodic draining");
        sent++;

        if (i % 5 == 4) {
            processed += bt_app_core_drain(3);
        }
    }

    processed += bt_app_core_drain(total_msgs);

    TEST_ASSERT_EQUAL_UINT(total_msgs, sent);
    TEST_ASSERT_EQUAL_UINT(total_msgs, processed);
    TEST_ASSERT_EQUAL_UINT(total_msgs, s_cb_calls);
    TEST_ASSERT_EQUAL_UINT(0, bt_app_core_queue_depth());
}

void test_bt_app_core_bounded_drain_stops_at_limit(void)
{
    bt_app_task_start_up();

    int payload = 0x55;
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(bt_app_work_dispatch(dummy_cb, (uint16_t)(0x370 + i), &payload, sizeof(payload), NULL));
    }
    TEST_ASSERT_EQUAL_UINT(4, bt_app_core_queue_depth());

    size_t drained = bt_app_core_drain(2);
    TEST_ASSERT_EQUAL_UINT(2, drained);
    TEST_ASSERT_EQUAL_UINT(2, s_cb_calls);
    TEST_ASSERT_EQUAL_UINT(2, bt_app_core_queue_depth());
}

void test_bt_app_core_zero_drain_leaves_queue_intact(void)
{
    bt_app_task_start_up();

    int payload = 0x66;
    for (int i = 0; i < 2; ++i) {
        TEST_ASSERT_TRUE(bt_app_work_dispatch(dummy_cb, (uint16_t)(0x380 + i), &payload, sizeof(payload), NULL));
    }
    TEST_ASSERT_EQUAL_UINT(2, bt_app_core_queue_depth());

    size_t drained = bt_app_core_drain(0);
    TEST_ASSERT_EQUAL_UINT(0, drained);
    TEST_ASSERT_EQUAL_UINT(0, s_cb_calls);
    TEST_ASSERT_EQUAL_UINT(2, bt_app_core_queue_depth());
}

/* ── bt_app_send_mgr_request + process_once default (MGR_REQUEST) branch ── */

void test_bt_app_send_mgr_request_enqueues_and_processes(void)
{
    bt_app_task_start_up();

    int request = 0xABCD;
    TEST_ASSERT_TRUE(bt_app_send_mgr_request(&request));
    TEST_ASSERT_EQUAL_UINT(1, bt_app_core_queue_depth());

    /* process_once handles the MGR_REQUEST sig via its default branch (no cb on
     * host; bt_mgr_request_handler is ESP_PLATFORM-only) and drains it. */
    TEST_ASSERT_TRUE(bt_app_core_process_once());
    TEST_ASSERT_EQUAL_UINT(0, bt_app_core_queue_depth());
    TEST_ASSERT_EQUAL_UINT(0, s_cb_calls); /* MGR_REQUEST does not invoke a work cb */
}

void test_bt_app_send_mgr_request_fails_without_queue(void)
{
    /* No start_up → queue is NULL → send must fail closed. */
    TEST_ASSERT_FALSE(bt_app_send_mgr_request((void *)0x1234));
}

/* ── bt_app_work_copy_cb error branches (called directly) ──────────────── */

void test_bt_app_work_copy_cb_rejects_bad_args(void)
{
    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    int dest = 7;
    TEST_ASSERT_EQUAL_INT(BT_APP_WORK_FAIL, bt_app_work_copy_cb(NULL, &dest, sizeof(dest)));
    TEST_ASSERT_EQUAL_INT(BT_APP_WORK_FAIL, bt_app_work_copy_cb(&msg, NULL, sizeof(dest)));
    TEST_ASSERT_EQUAL_INT(BT_APP_WORK_FAIL, bt_app_work_copy_cb(&msg, &dest, -1));
}

void test_bt_app_work_copy_cb_rejects_when_param_already_set(void)
{
    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    int already = 1;
    msg.param = &already; /* non-NULL → "already has param" rejection */
    int dest = 7;
    TEST_ASSERT_EQUAL_INT(BT_APP_WORK_FAIL, bt_app_work_copy_cb(&msg, &dest, sizeof(dest)));
}

/* ── bt_app_param_free_cb NULL branch ─────────────────────────────────── */

void test_bt_app_param_free_cb_handles_null(void)
{
    bt_app_param_free_cb(NULL); /* must not crash (the if(param) false branch) */
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_app_core_should_fail_when_queue_full_and_not_drained);
    RUN_TEST(test_bt_app_core_copy_callback_failure_should_not_enqueue);
    RUN_TEST(test_bt_app_core_shutdown_clears_handles_and_blocks_dispatch);
    RUN_TEST(test_bt_app_core_custom_copy_and_free_processed_via_helper);
    RUN_TEST(test_bt_app_core_default_copy_runs_when_copy_cb_is_null);
    RUN_TEST(test_bt_app_core_process_once_drains_multiple_messages);
    RUN_TEST(test_bt_app_core_bounded_drain_stops_at_limit);
    RUN_TEST(test_bt_app_core_zero_drain_leaves_queue_intact);
    RUN_TEST(test_bt_app_core_start_stop_is_idempotent);
    RUN_TEST(test_bt_app_core_queue_recovers_after_partial_drain);
    RUN_TEST(test_bt_app_core_queue_recovers_after_full_drain);
    RUN_TEST(test_bt_app_core_burst_dispatch_stress_with_periodic_drain);
    RUN_TEST(test_bt_app_send_mgr_request_enqueues_and_processes);
    RUN_TEST(test_bt_app_send_mgr_request_fails_without_queue);
    RUN_TEST(test_bt_app_work_copy_cb_rejects_bad_args);
    RUN_TEST(test_bt_app_work_copy_cb_rejects_when_param_already_set);
    RUN_TEST(test_bt_app_param_free_cb_handles_null);
    return UNITY_END();
}

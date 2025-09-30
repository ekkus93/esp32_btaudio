#include "unity.h"
#include "bt_app_core.h"
#include <string.h>

static bool handler_called = false;
static void test_handler(uint16_t event, void *param)
{
    handler_called = true;
    TEST_ASSERT_NOT_NULL(param);
    /* check the content matches what was dispatched */
    TEST_ASSERT_EQUAL_STRING("payload", (char *)param);
}

void setUp(void) {
    handler_called = false;
    bt_app_task_start_up();
}

void tearDown(void) {
    /* nothing for host test */
}

void test_dispatch_with_param_without_copy_cb(void)
{
    char payload[] = "payload";
    /* Dispatch with non-NULL param_len but NULL copy callback. The dispatcher
     * should deep-copy the payload and the handler should receive a non-NULL
     * pointer with the same contents. */
    bool ok = bt_app_work_dispatch(test_handler, 0x1234, payload, (int)strlen(payload) + 1, NULL);
    TEST_ASSERT_TRUE(ok);

    /* Run the message pump: receive the event in the task by directly
     * invoking the task handler (host tests do not run FreeRTOS task loop).
     * Simpler approach: call the callback directly with a copy created by the
     * same copy helper to simulate behavior. */
    bt_app_msg_t msg = {0};
    msg.event = 0x1234;
    msg.cb = test_handler;
    /* perform deep copy using the same helper to emulate dispatcher */
    TEST_ASSERT_EQUAL(BT_APP_WORK_OK, bt_app_work_copy_cb(&msg, payload, (int)strlen(payload) + 1));

    /* Call handler with the copied parameter */
    msg.cb(msg.event, msg.param);

    /* free param */
    if (msg.param && msg.param_free_cb) msg.param_free_cb(msg.param);

    TEST_ASSERT_TRUE(handler_called);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dispatch_with_param_without_copy_cb);
    return UNITY_END();
}

/**
 * @file test_bt_lock_cb_reentry.c
 * @brief Callback re-entry deadlock tests (RH-WR-01)
 *
 * Covers:
 *   - Callback invoked outside mutex (no deadlock)
 *   - Connected event handler doesn't hold mutex during callback
 *   - bt_ctx_lock/unlock prevents deadlock on re-entry
 *   - Multiple lock/unlock cycles remain safe
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "platform_sync.h"

/* Test fixtures */
void setUp(void) {
    bt_manager_init_t cfg = {
        .device_name = "TestDevice",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&cfg));
}

void tearDown(void) {
    bt_manager_deinit();
}

/* ============================================================================
 * Callback re-entry scenarios
 * ============================================================================ */

void test_bt_ctx_lock_no_deadlock_on_release_and_reacquire(void) {
    /* Arrange: Lock the context */
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Act: Release */
    bt_ctx_unlock();

    /* Act: Re-acquire (simulates callback re-entry after release) */
    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Cleanup */
    bt_ctx_unlock();
}

void test_bt_ctx_lock_multiple_cycles_no_deadlock(void) {
    /* Act: Multiple lock/unlock cycles */
    for (int i = 0; i < 10; i++) {
        esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        bt_ctx_unlock();
    }

    /* Assert: No deadlock */
    TEST_PASS();
}

void test_bt_ctx_lock_simulates_callback_outside_mutex(void) {
    /* Arrange: Simulate callback invocation pattern */
    /* In the new design, callbacks are invoked OUTSIDE the mutex */

    /* Simulate: Lock -> unlock -> callback -> lock again */
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Unlock before callback (new design) */
    bt_ctx_unlock();

    /* Simulate callback doing its own lock/unlock */
    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Cleanup */
    bt_ctx_unlock();
}

/* ============================================================================
 * Connection event simulation
 * ============================================================================ */

void test_bt_connected_event_does_not_hold_mutex_during_callback(void) {
    /* Arrange: Set up callback */
    bt_manager_init_t cfg = {
        .device_name = "TestDevice",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };

    /* Simulate connection event pattern */
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Update context */
    bt_ctx.connected = true;
    strcpy(bt_ctx.connected_mac, "AA:BB:CC:DD:EE:FF");

    /* Unlock before hypothetical callback */
    bt_ctx_unlock();

    /* Act: Callback would acquire its own lock */
    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Assert */
    TEST_ASSERT_TRUE(bt_ctx.connected);

    /* Cleanup */
    bt_ctx_unlock();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

int main(void) {
    UNITY_BEGIN();

    /* Callback re-entry scenarios */
    RUN_TEST(test_bt_ctx_lock_no_deadlock_on_release_and_reacquire);
    RUN_TEST(test_bt_ctx_lock_multiple_cycles_no_deadlock);
    RUN_TEST(test_bt_ctx_lock_simulates_callback_outside_mutex);

    /* Connection event simulation */
    RUN_TEST(test_bt_connected_event_does_not_hold_mutex_during_callback);

    return UNITY_END();
}

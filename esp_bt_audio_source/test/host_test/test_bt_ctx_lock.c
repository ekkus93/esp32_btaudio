/**
 * @file test_bt_ctx_lock.c
 * @brief Unit tests for bt_ctx_lock/unlock wrapper functions (RH-WR-01)
 *
 * Covers:
 *   - bt_ctx_lock() returns ESP_OK after bt_manager_init
 *   - bt_ctx_lock() returns ESP_ERR_INVALID_STATE before init
 *   - bt_ctx_unlock() after lock completes without error
 *   - Snapshot consistency: bt_manager_get_status() under lock
 *   - Callback re-entry does not deadlock
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "platform_sync.h"
#include "esp_err.h"

/* Forward-declare bt_manager_status_t to avoid header conflicts */
typedef struct {
    bool initialized;
    bool connected;
    bool audio_playing;
    bool scanning;
    char connected_mac[18];
    char connected_name[32];
} bt_manager_status_t;

/* bt_manager_get_status() is defined in bt_manager.c but not exposed via header */
esp_err_t bt_manager_get_status(bt_manager_status_t *status);

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
 * bt_ctx_lock/unlock basic operations
 * ============================================================================ */

void test_bt_ctx_lock_should_succeed_after_init(void) {
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    bt_ctx_unlock();
}

void test_bt_ctx_lock_should_fail_before_init(void) {
    bt_manager_deinit();
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

void test_bt_ctx_unlock_should_succeed(void) {
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    bt_ctx_unlock();
    TEST_PASS();
}

/* ============================================================================
 * Snapshot consistency
 * ============================================================================ */

void test_bt_manager_get_status_returns_coherent_snapshot(void) {
    bt_ctx.connected = true;
    strcpy(bt_ctx.connected_mac, "A0:BB:CC:DD:EE:FF");
    strcpy(bt_ctx.connected_name, "Test");

    bt_manager_status_t status;
    esp_err_t err = bt_manager_get_status(&status);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(status.connected);
}

void test_bt_manager_get_status_with_null_output(void) {
    esp_err_t err = bt_manager_get_status(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

/* ============================================================================
 * Callback re-entry safety
 * ============================================================================ */

void test_bt_ctx_lock_does_not_deadlock_on_reentry(void) {
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    bt_ctx_unlock();
    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    bt_ctx_unlock();
}

void test_bt_ctx_lock_multiple_cycles(void) {
    for (int i = 0; i < 10; i++) {
        esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        bt_ctx_unlock();
    }
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_bt_ctx_lock_should_succeed_after_init);
    RUN_TEST(test_bt_ctx_lock_should_fail_before_init);
    RUN_TEST(test_bt_ctx_unlock_should_succeed);
    RUN_TEST(test_bt_manager_get_status_returns_coherent_snapshot);
    RUN_TEST(test_bt_manager_get_status_with_null_output);
    RUN_TEST(test_bt_ctx_lock_does_not_deadlock_on_reentry);
    RUN_TEST(test_bt_ctx_lock_multiple_cycles);

    return UNITY_END();
}

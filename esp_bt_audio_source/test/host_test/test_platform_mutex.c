/**
 * @file test_platform_mutex.c
 * @brief Unit tests for platform_mutex_t (RH-WR-01)
 *
 * Tests verify the platform mutex API contract:
 * - Create/lock/unlock/delete lifecycle
 * - NULL safety
 * - Lock/unlock error codes
 */

#include "unity.h"
#include "platform_sync.h"
#include <string.h>

/* Test fixtures */
void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * Platform Mutex Tests
 * ============================================================================ */

void test_platform_mutex_create_should_return_non_null(void) {
    /* Act */
    platform_mutex_t mutex = platform_mutex_create();

    /* Assert */
    TEST_ASSERT_NOT_NULL(mutex);

    /* Cleanup */
    platform_mutex_delete(mutex);
}

void test_platform_mutex_lock_should_succeed(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act */
    esp_err_t err = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Cleanup */
    platform_mutex_unlock(mutex);
    platform_mutex_delete(mutex);
}

void test_platform_mutex_unlock_should_succeed(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act */
    esp_err_t err_lock = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err_lock);

    esp_err_t err_unlock = platform_mutex_unlock(mutex);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_OK, err_unlock);

    /* Cleanup */
    platform_mutex_delete(mutex);
}

void test_platform_mutex_lock_NULL_should_fail(void) {
    /* Act */
    esp_err_t err = platform_mutex_lock(NULL, PLATFORM_WAIT_FOREVER);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_platform_mutex_unlock_NULL_should_fail(void) {
    /* Act */
    esp_err_t err = platform_mutex_unlock(NULL);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_platform_mutex_delete_NULL_should_not_crash(void) {
    /* Act - should not crash */
    platform_mutex_delete(NULL);

    /* Assert */
    TEST_PASS();
}

void test_platform_mutex_lock_after_double_lock_should_fail(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act - lock twice (non-recursive mutex) */
    esp_err_t err1 = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err1);

    /* Second lock should fail (pthread mutex is not recursive) */
    esp_err_t err2 = platform_mutex_lock(mutex, 0);

    /* Assert - the second lock should fail/timeout */
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err2);

    /* Cleanup - unlock once to release the first lock */
    platform_mutex_unlock(mutex);
    platform_mutex_delete(mutex);
}

void test_platform_mutex_multiple_lock_unlock_cycles(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act - multiple lock/unlock cycles */
    for (int i = 0; i < 10; i++) {
        esp_err_t err_lock = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);
        TEST_ASSERT_EQUAL(ESP_OK, err_lock);

        esp_err_t err_unlock = platform_mutex_unlock(mutex);
        TEST_ASSERT_EQUAL(ESP_OK, err_unlock);
    }

    /* Cleanup */
    platform_mutex_delete(mutex);
}

/* ============================================================================
 * Timeout behavior (RH-WR-01)
 * ============================================================================ */

void test_platform_mutex_lock_timeout_when_held(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Lock the mutex */
    esp_err_t err_lock = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err_lock);

    /* Act - try to lock with timeout=0 while held (must timeout immediately) */
    esp_err_t err_timeout = platform_mutex_lock(mutex, 0);

    /* Assert */
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err_timeout);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err_timeout);

    /* Cleanup */
    platform_mutex_unlock(mutex);
    platform_mutex_delete(mutex);
}

void test_platform_mutex_lock_timeout_zero_when_unlocked(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act - lock with timeout=0 when available */
    esp_err_t err = platform_mutex_lock(mutex, 0);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Cleanup */
    platform_mutex_unlock(mutex);
    platform_mutex_delete(mutex);
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

int main(void) {
    UNITY_BEGIN();

    /* Platform mutex tests */
    RUN_TEST(test_platform_mutex_create_should_return_non_null);
    RUN_TEST(test_platform_mutex_lock_should_succeed);
    RUN_TEST(test_platform_mutex_unlock_should_succeed);
    RUN_TEST(test_platform_mutex_lock_NULL_should_fail);
    RUN_TEST(test_platform_mutex_unlock_NULL_should_fail);
    RUN_TEST(test_platform_mutex_delete_NULL_should_not_crash);
    RUN_TEST(test_platform_mutex_lock_after_double_lock_should_fail);
    RUN_TEST(test_platform_mutex_multiple_lock_unlock_cycles);

    /* Timeout behavior (RH-WR-01) */
    RUN_TEST(test_platform_mutex_lock_timeout_when_held);
    RUN_TEST(test_platform_mutex_lock_timeout_zero_when_unlocked);

    return UNITY_END();
}

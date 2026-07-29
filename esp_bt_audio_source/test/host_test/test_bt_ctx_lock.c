/**
 * @file test_bt_ctx_lock.c
 * @brief Unit tests for bt_ctx synchronization and init transactionality
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "platform_sync.h"
#include "esp_err.h"

typedef struct {
    bool initialized;
    bool connected;
    bool audio_playing;
    bool scanning;
    char connected_mac[18];
    char connected_name[32];
} bt_manager_status_t;

esp_err_t bt_manager_get_status(bt_manager_status_t *status);

static bt_manager_init_t test_config(void)
{
    bt_manager_init_t config = {
        .device_name = "TestDevice",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    return config;
}

void setUp(void)
{
    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
}

void tearDown(void)
{
    if (bt_ctx.initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    } else {
        bt_manager_test_deinit_mutex();
    }
}

void test_bt_ctx_lock_should_succeed_after_init(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
}

void test_bt_ctx_lock_should_fail_before_init(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_ctx_lock(PLATFORM_WAIT_FOREVER));
}

void test_bt_ctx_unlock_should_succeed(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
    TEST_PASS();
}

void test_bt_manager_get_status_returns_coherent_snapshot(void)
{
    bt_ctx.connected = true;
    strcpy(bt_ctx.connected_mac, "A0:BB:CC:DD:EE:FF");
    strcpy(bt_ctx.connected_name, "Test");

    bt_manager_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_get_status(&status));
    TEST_ASSERT_TRUE(status.connected);
}

void test_bt_manager_get_status_with_null_output(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_manager_get_status(NULL));
}

void test_bt_ctx_lock_does_not_deadlock_on_reentry(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
}

void test_bt_ctx_lock_multiple_cycles(void)
{
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
        bt_ctx_unlock();
    }
}

void test_bt_manager_init_reset_failure_is_exact_and_transactional(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    TEST_ASSERT_FALSE(bt_ctx.initialized);

    bt_manager_test_force_next_ctx_lock_result(ESP_ERR_TIMEOUT);
    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_manager_init(&config));
    TEST_ASSERT_FALSE(bt_ctx.initialized);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_ctx_lock(PLATFORM_WAIT_FOREVER));

    /* A failed pre-callback reset is a clean, retryable init failure. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
    TEST_ASSERT_TRUE(bt_ctx.initialized);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_ctx_lock_should_succeed_after_init);
    RUN_TEST(test_bt_ctx_lock_should_fail_before_init);
    RUN_TEST(test_bt_ctx_unlock_should_succeed);
    RUN_TEST(test_bt_manager_get_status_returns_coherent_snapshot);
    RUN_TEST(test_bt_manager_get_status_with_null_output);
    RUN_TEST(test_bt_ctx_lock_does_not_deadlock_on_reentry);
    RUN_TEST(test_bt_ctx_lock_multiple_cycles);
    RUN_TEST(test_bt_manager_init_reset_failure_is_exact_and_transactional);
    return UNITY_END();
}

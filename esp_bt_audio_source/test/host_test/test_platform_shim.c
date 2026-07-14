/**
 * @file test_platform_shim.c
 * @brief Unit tests for platform_shim layer (memory, timing, storage wrappers)
 *
 * Tests cover basic functionality of thin wrapper functions:
 * - platform_memory: malloc/calloc/free
 * - platform_timing: delay_ms, get_time_ms/us
 *
 * TDD Phase: Section 8.3 - Untested Supporting Code (platform_shim)
 *
 * Note: These are thin wrappers around stdlib/POSIX, so tests verify
 * correct delegation and NULL safety rather than reimplementing stdlib tests.
 */

#include "unity.h"
#include "platform_sync.h"
#include "platform_memory.h"
#include "platform_timing.h"
#include <string.h>
#include <unistd.h>

/* Test fixtures */
void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * Platform Memory Tests
 * ========================================================================= */

void test_platform_malloc_should_allocate_memory(void) {
    /* Act */
    void *ptr = platform_malloc(128, PLATFORM_MEM_CAP_8BIT);
    
    /* Assert */
    TEST_ASSERT_NOT_NULL(ptr);
    
    /* Verify writable */
    memset(ptr, 0xAA, 128);
    
    /* Cleanup */
    platform_free(ptr);
}

void test_platform_malloc_zero_size_should_return_valid_or_null(void) {
    /* Act - behavior is implementation-defined */
    void *ptr = platform_malloc(0, PLATFORM_MEM_CAP_8BIT);
    
    /* Assert - just verify doesn't crash */
    /* C standard allows malloc(0) to return NULL or unique pointer */
    
    /* Cleanup */
    platform_free(ptr);  /* NULL-safe per C standard */
}

void test_platform_calloc_should_allocate_zeroed_memory(void) {
    /* Act */
    uint8_t *ptr = (uint8_t *)platform_calloc(16, sizeof(uint8_t), PLATFORM_MEM_CAP_8BIT);
    
    /* Assert */
    TEST_ASSERT_NOT_NULL(ptr);
    
    /* Verify zero-initialized */
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL(0, ptr[i]);
    }
    
    /* Cleanup */
    platform_free(ptr);
}

void test_platform_calloc_with_caps_should_ignore_caps_on_host(void) {
    /* Arrange & Act - caps are ignored on host build */
    void *ptr1 = platform_calloc(8, 4, PLATFORM_MEM_CAP_8BIT);
    void *ptr2 = platform_calloc(8, 4, PLATFORM_MEM_CAP_DMA);
    void *ptr3 = platform_calloc(8, 4, PLATFORM_MEM_CAP_SPIRAM);
    
    /* Assert - all allocations should succeed (caps ignored) */
    TEST_ASSERT_NOT_NULL(ptr1);
    TEST_ASSERT_NOT_NULL(ptr2);
    TEST_ASSERT_NOT_NULL(ptr3);
    
    /* Cleanup */
    platform_free(ptr1);
    platform_free(ptr2);
    platform_free(ptr3);
}

void test_platform_free_null_should_be_safe(void) {
    /* Act - should not crash (NULL-safe per C standard) */
    platform_free(NULL);
    
    /* Assert - test passes if no crash */
    TEST_PASS();
}

void test_platform_free_allocated_memory_should_succeed(void) {
    /* Arrange */
    void *ptr = platform_malloc(64, PLATFORM_MEM_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(ptr);
    
    /* Act - should not crash */
    platform_free(ptr);
    
    /* Assert */
    TEST_PASS();
}

/* ============================================================================
 * Platform Timing Tests
 * ========================================================================= */

void test_platform_delay_ms_zero_should_return_immediately(void) {
    /* Arrange */
    uint64_t start = platform_get_time_ms();
    
    /* Act */
    platform_delay_ms(0);
    
    /* Assert - should take <1ms */
    uint64_t elapsed = platform_get_time_ms() - start;
    TEST_ASSERT_LESS_THAN(2, elapsed);  /* Allow 1ms margin */
}

void test_platform_delay_ms_should_delay_approximately(void) {
    /* Arrange */
    uint64_t start = platform_get_time_ms();
    
    /* Act */
    platform_delay_ms(10);
    
    /* Assert - should take 10ms ±5ms (host scheduler variance) */
    uint64_t elapsed = platform_get_time_ms() - start;
    TEST_ASSERT_GREATER_OR_EQUAL(8, elapsed);   /* Min 8ms */
    TEST_ASSERT_LESS_THAN(20, elapsed);         /* Max 20ms (generous margin for CI) */
}

void test_platform_get_time_ms_should_increment_monotonically(void) {
    /* Act */
    uint64_t t1 = platform_get_time_ms();
    platform_delay_ms(1);
    uint64_t t2 = platform_get_time_ms();
    
    /* Assert - time should advance */
    TEST_ASSERT_GREATER_THAN(t1, t2);
}

void test_platform_get_time_us_should_have_microsecond_resolution(void) {
    /* Act */
    uint64_t t1_us = platform_get_time_us();
    usleep(100);  /* Sleep 100us */
    uint64_t t2_us = platform_get_time_us();
    
    /* Assert - should show microsecond difference */
    uint64_t diff_us = t2_us - t1_us;
    TEST_ASSERT_GREATER_OR_EQUAL(50, diff_us);    /* At least 50us */
    TEST_ASSERT_LESS_THAN(1000, diff_us);        /* Less than 1ms */
}

void test_platform_get_time_ms_and_us_should_be_consistent(void) {
    /* Act */
    uint64_t t_ms = platform_get_time_ms();
    uint64_t t_us = platform_get_time_us();
    
    /* Assert - us/1000 should be within 1ms of ms reading */
    uint64_t ms_from_us = t_us / 1000;
    int64_t diff = (int64_t)ms_from_us - (int64_t)t_ms;
    TEST_ASSERT_LESS_OR_EQUAL(1, (diff >= 0 ? diff : -diff));
}

/* ============================================================================
 * Platform Mutex Tests (RH-WR-01: mutex lock/unlock + NULL safety)
 * ========================================================================= */

void test_platform_mutex_create_should_return_non_null(void) {
    /* Act */
    platform_mutex_t mutex = platform_mutex_create();

    /* Assert */
    TEST_ASSERT_NOT_NULL(mutex);

    /* Cleanup */
    platform_mutex_delete(mutex);
}

void test_platform_mutex_delete_null_should_be_safe(void) {
    /* Act - must not crash */
    platform_mutex_delete(NULL);

    /* Assert */
    TEST_PASS();
}

void test_platform_mutex_lock_unlock_cycle(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act */
    esp_err_t err = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Assert - unlock must succeed */
    err = platform_mutex_unlock(mutex);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Cleanup */
    platform_mutex_delete(mutex);
}

void test_platform_mutex_lock_null_should_return_invalid_arg(void) {
    /* Act */
    esp_err_t err = platform_mutex_lock(NULL, PLATFORM_WAIT_FOREVER);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_platform_mutex_unlock_null_should_return_invalid_arg(void) {
    /* Act */
    esp_err_t err = platform_mutex_unlock(NULL);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_platform_mutex_lock_then_delete(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act - lock the mutex */
    esp_err_t err = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Delete while locked (cleanup should be safe) */
    platform_mutex_delete(mutex);

    /* Assert - test passes if no crash on delete */
    TEST_PASS();
}

void test_platform_mutex_multiple_lock_unlock_cycles(void) {
    /* Arrange */
    platform_mutex_t mutex = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    /* Act & Assert - 5 consecutive lock/unlock cycles */
    for (int i = 0; i < 5; i++) {
        esp_err_t err = platform_mutex_lock(mutex, PLATFORM_WAIT_FOREVER);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        err = platform_mutex_unlock(mutex);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    /* Cleanup */
    platform_mutex_delete(mutex);
}

void test_platform_binary_sem_create_delete(void) {
    /* Arrange */
    platform_binary_sem_t sem = platform_binary_sem_create();
    TEST_ASSERT_NOT_NULL(sem);

    /* Act */
    platform_binary_sem_delete(sem);

    /* Assert - delete(NULL) must be safe */
    platform_binary_sem_delete(NULL);
    TEST_PASS();
}

void test_platform_binary_sem_take_null_should_return_invalid_arg(void) {
    /* Act */
    esp_err_t err = platform_binary_sem_take(NULL, 0);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_platform_binary_sem_give_null_should_return_invalid_arg(void) {
    /* Act */
    esp_err_t err = platform_binary_sem_give(NULL);

    /* Assert */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_platform_binary_sem_give_then_take(void) {
    /* Arrange */
    platform_binary_sem_t sem = platform_binary_sem_create();
    TEST_ASSERT_NOT_NULL(sem);

    /* Act - give the semaphore first */
    esp_err_t err = platform_binary_sem_give(sem);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Take should succeed after a give */
    err = platform_binary_sem_take(sem, 0);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Cleanup */
    platform_binary_sem_delete(sem);
}

/* ============================================================================
 * Test Runner
 * ========================================================================= */

int main(void) {
    UNITY_BEGIN();
    
    /* Platform memory tests */
    RUN_TEST(test_platform_malloc_should_allocate_memory);
    RUN_TEST(test_platform_malloc_zero_size_should_return_valid_or_null);
    RUN_TEST(test_platform_calloc_should_allocate_zeroed_memory);
    RUN_TEST(test_platform_calloc_with_caps_should_ignore_caps_on_host);
    RUN_TEST(test_platform_free_null_should_be_safe);
    RUN_TEST(test_platform_free_allocated_memory_should_succeed);
    
    /* Platform timing tests */
    RUN_TEST(test_platform_delay_ms_zero_should_return_immediately);
    RUN_TEST(test_platform_delay_ms_should_delay_approximately);
    RUN_TEST(test_platform_get_time_ms_should_increment_monotonically);
    RUN_TEST(test_platform_get_time_us_should_have_microsecond_resolution);
    RUN_TEST(test_platform_get_time_ms_and_us_should_be_consistent);

    /* Platform mutex tests (RH-WR-01) */
    RUN_TEST(test_platform_mutex_create_should_return_non_null);
    RUN_TEST(test_platform_mutex_delete_null_should_be_safe);
    RUN_TEST(test_platform_mutex_lock_unlock_cycle);
    RUN_TEST(test_platform_mutex_lock_null_should_return_invalid_arg);
    RUN_TEST(test_platform_mutex_unlock_null_should_return_invalid_arg);
    RUN_TEST(test_platform_mutex_lock_then_delete);
    RUN_TEST(test_platform_mutex_multiple_lock_unlock_cycles);

    /* Platform binary semaphore tests */
    RUN_TEST(test_platform_binary_sem_create_delete);
    RUN_TEST(test_platform_binary_sem_take_null_should_return_invalid_arg);
    RUN_TEST(test_platform_binary_sem_give_null_should_return_invalid_arg);
    RUN_TEST(test_platform_binary_sem_give_then_take);
    
    return UNITY_END();
}

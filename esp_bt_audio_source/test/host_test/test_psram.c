/* Host unit tests for PSRAM allocation behavior (mock-driven)
 * Verifies the heap_caps mock simulates PSRAM present/absent and
 * that allocation callers can detect and fallback as expected.
 */

#include "unity.h"
#include <stdbool.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "audio_alloc_host.h"

void setUp(void) {
    /* Ensure PSRAM available by default for tests; individual tests
     * will override as needed. */
    esp_heap_caps_mock_set_psram_available(true);
    esp_heap_caps_mock_reset_allocations();
}

void tearDown(void) {
}

void test_heap_caps_psram_allocation_success(void)
{
    /* Request PSRAM allocation; mock should succeed */
    void* p = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(p);
    /* Allocation should be recorded as coming from PSRAM */
    TEST_ASSERT_TRUE(esp_heap_caps_mock_was_allocated_from_spiram(p));
    heap_caps_free(p);
}

void test_heap_caps_psram_allocation_failure_and_fallback(void)
{
    /* Simulate PSRAM absent */
    esp_heap_caps_mock_set_psram_available(false);

    /* PSRAM allocation should fail */
    void* p = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NULL(p);

    /* Fallback to default allocator should succeed */
    void* q = heap_caps_malloc(512, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(q);
    /* Default allocator should not be from PSRAM */
    TEST_ASSERT_FALSE(esp_heap_caps_mock_was_allocated_from_spiram(q));
    heap_caps_free(q);
}

void test_audio_alloc_work_buffers_psram_and_fallback(void)
{
    /* Ensure PSRAM available */
    esp_heap_caps_mock_set_psram_available(true);
    audio_processor_free_work_buffers();
    esp_heap_caps_mock_reset_allocations();
    int ret = audio_processor_alloc_work_buffers(4096);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_UINT64(4096, audio_processor_get_work_buffer_bytes());
    /* At least one allocation should have come from PSRAM when available */
    TEST_ASSERT_TRUE(esp_heap_caps_mock_count_allocations_spiram() > 0);
    audio_processor_free_work_buffers();

    /* Simulate PSRAM absent: allocation should fall back to default and still succeed */
    esp_heap_caps_mock_set_psram_available(false);
    ret = audio_processor_alloc_work_buffers(4096);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_UINT64(4096, audio_processor_get_work_buffer_bytes());
    /* Now allocations should be from DRAM */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)esp_heap_caps_mock_count_allocations_spiram());
    TEST_ASSERT_TRUE(esp_heap_caps_mock_count_allocations_dram() > 0);
    audio_processor_free_work_buffers();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_heap_caps_psram_allocation_success);
    RUN_TEST(test_heap_caps_psram_allocation_failure_and_fallback);
    RUN_TEST(test_audio_alloc_work_buffers_psram_and_fallback);
    return UNITY_END();
}

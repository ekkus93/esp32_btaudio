// PSRAM fragmentation test with concrete assertions
#include "sdkconfig.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "test_utils.h"
#include "esp_heap_caps.h"
#include <malloc.h>
#include "esp_log.h"
#include "esp_psram.h"
#include <string.h>

static const char *TAG = "test_psram_fragmentation";

#if CONFIG_SPIRAM
TEST_CASE("PSRAM fragmentation: validate largest-free vs allocation outcome", "psram")
{
    /* runtime guard: if PSRAM support is compiled but PSRAM isn't initialized at runtime,
       skip the test with an explanatory warning. This handles boards without external PSRAM. */
    if (!esp_psram_is_initialized()) {
        ESP_LOGW(TAG, "PSRAM not initialized at runtime; skipping PSRAM fragmentation test");
        TEST_IGNORE_MESSAGE("PSRAM not present at runtime; skipping PSRAM fragmentation test");
        return;
    }
    const int ALLOC_COUNT = 16;
    const size_t SMALL_SIZE = 16 * 1024; /* 16 KiB */
    const size_t LARGE_SIZE = 128 * 1024; /* 128 KiB */
    void *ptrs[ALLOC_COUNT];
    int i;

    ESP_LOGI(TAG, "Starting PSRAM fragmentation test");

    /* Record baseline free / largest block */
    size_t free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Before alloc: free=%u largest=%u", (unsigned)free_before, (unsigned)largest_before);

    /* Allocate a sequence of small blocks to consume PSRAM */
    for (i = 0; i < ALLOC_COUNT; ++i) {
        ptrs[i] = heap_caps_malloc(SMALL_SIZE, MALLOC_CAP_SPIRAM);
        if (!ptrs[i]) {
            ESP_LOGW(TAG, "Allocation %d failed during fill; stopping early", i);
            break;
        }
        memset(ptrs[i], 0xA5, SMALL_SIZE);
        size_t usable = malloc_usable_size(ptrs[i]);
        ESP_LOGI(TAG, "Allocated small[%d] = %p (requested=%u usable=%u)", i, ptrs[i], (unsigned)SMALL_SIZE, (unsigned)usable);
    }
    int allocated = i;
    ESP_LOGI(TAG, "Allocated %d small blocks", allocated);

    /* Free every other allocation to intentionally create holes */
    for (i = 0; i < allocated; i += 2) {
        ESP_LOGI(TAG, "Freeing small[%d] = %p", i, ptrs[i]);
        heap_caps_free(ptrs[i]);
        ptrs[i] = NULL;
    }

    /* Measure fragmentation: largest free block after freeing alternating blocks */
    size_t free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_after = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "After free alt: free=%u largest=%u", (unsigned)free_after, (unsigned)largest_after);

    /* Attempt a large allocation and verify result matches reported largest free block */
    void *big = heap_caps_malloc(LARGE_SIZE, MALLOC_CAP_SPIRAM);
    if (largest_after < LARGE_SIZE) {
        ESP_LOGI(TAG, "Reported largest free (%u) < LARGE_SIZE (%u); expecting allocation to fail", (unsigned)largest_after, (unsigned)LARGE_SIZE);
        TEST_ASSERT_NULL_MESSAGE(big, "Large allocation unexpectedly succeeded despite reported insufficient contiguous free block");

        /* Verify small allocation still succeeds */
        void *small = heap_caps_malloc(SMALL_SIZE, MALLOC_CAP_SPIRAM);
        TEST_ASSERT_NOT_NULL_MESSAGE(small, "Small allocation failed when expected to succeed after fragmentation");
        size_t small_usable = malloc_usable_size(small);
        ESP_LOGI(TAG, "Allocated verification small = %p (requested=%u usable=%u)", small, (unsigned)SMALL_SIZE, (unsigned)small_usable);
        heap_caps_free(small);
    } else {
        ESP_LOGI(TAG, "Reported largest free (%u) >= LARGE_SIZE (%u); expecting allocation to succeed", (unsigned)largest_after, (unsigned)LARGE_SIZE);
        TEST_ASSERT_NOT_NULL_MESSAGE(big, "Large allocation failed even though largest free block reported as sufficient");
        size_t big_usable = malloc_usable_size(big);
        ESP_LOGI(TAG, "Allocated big = %p (requested=%u usable=%u)", big, (unsigned)LARGE_SIZE, (unsigned)big_usable);
        /* If succeeded, touch and free it */
        memset(big, 0x5A, LARGE_SIZE);
        heap_caps_free(big);
    }

    /* Cleanup remaining allocations */
    for (i = 0; i < allocated; ++i) {
        if (ptrs[i]) {
            heap_caps_free(ptrs[i]);
            ptrs[i] = NULL;
        }
    }

    ESP_LOGI(TAG, "PSRAM fragmentation test completed");
}
#else
TEST_CASE("PSRAM fragmentation: skipped when SPIRAM disabled", "psram")
{
    TEST_IGNORE_MESSAGE("SPIRAM not enabled; skipping PSRAM fragmentation test");
}
#endif

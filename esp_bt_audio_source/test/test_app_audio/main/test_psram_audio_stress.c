// Minimal PSRAM audio-stress skeleton Unity test
#include "sdkconfig.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "test_utils.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include <string.h>

static const char *TAG2 = "test_psram_audio_stress";

#if CONFIG_SPIRAM
TEST_CASE("PSRAM audio stress: alloc/free under simulated load", "psram")
{
    /* runtime guard: if PSRAM support is compiled but PSRAM isn't initialized at runtime,
       skip the test with an explanatory warning. This handles boards without external PSRAM. */
    if (!esp_psram_is_initialized()) {
        ESP_LOGW(TAG2, "PSRAM not initialized at runtime; skipping PSRAM audio-stress test");
        TEST_IGNORE_MESSAGE("PSRAM not present at runtime; skipping PSRAM audio-stress test");
        return;
    }
    const int N = 8;
    const size_t BIG_BUF = 64 * 1024; // 64 KiB
    const size_t SMALL_BUF = 16 * 1024; // 16 KiB
    void *bufs[N];
    int i;

    ESP_LOGI(TAG2, "Starting PSRAM audio-stress skeleton test");

    for (i = 0; i < N; ++i) {
        bufs[i] = heap_caps_malloc(BIG_BUF, MALLOC_CAP_SPIRAM);
        TEST_ASSERT_NOT_NULL_MESSAGE(bufs[i], "Big PSRAM allocation failed");
        memset(bufs[i], 0x7E, BIG_BUF);
    }

    /* Free and reallocate smaller blocks to simulate churn */
    for (i = 0; i < N; ++i) {
        heap_caps_free(bufs[i]);
        bufs[i] = NULL;
        void *s = heap_caps_malloc(SMALL_BUF, MALLOC_CAP_SPIRAM);
        TEST_ASSERT_NOT_NULL_MESSAGE(s, "Small PSRAM re-allocation failed during stress");
        memset(s, 0x3C, SMALL_BUF);
        heap_caps_free(s);
    }

    ESP_LOGI(TAG2, "PSRAM audio-stress skeleton test completed");
}
#else
TEST_CASE("PSRAM audio stress: skipped when SPIRAM disabled", "psram")
{
    TEST_IGNORE_MESSAGE("SPIRAM not enabled; skipping PSRAM audio stress test");
}
#endif

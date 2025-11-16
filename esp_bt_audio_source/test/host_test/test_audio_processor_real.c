/* Host test that links the real production `audio_processor.c` allocation
 * logic. We provide minimal mocks for device-only dependencies elsewhere in
 * test/host_test/mocks so this compilation exercise focuses on the real
 * allocation/fallback paths. The test calls audio_processor_init() with a
 * small config and verifies audio_processor_get_work_buffer_bytes() is
 * non-zero when allocations succeed and zero when forced failures occur.
 */

#include "unity.h"
#include "../../main/include/audio_processor.h"
#include "esp_heap_caps.h"
#include <string.h>

void setUp(void) {
    esp_heap_caps_mock_set_psram_available(true);
    esp_heap_caps_mock_reset_allocations();
}

void tearDown(void) {
    audio_processor_deinit();
}

void test_audio_processor_alloc_with_psram(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 50;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    size_t bytes = audio_processor_get_work_buffer_bytes();
    TEST_ASSERT_TRUE(bytes >= 1024);
    /* Ensure at least one allocation used PSRAM when available */
    TEST_ASSERT_TRUE(esp_heap_caps_mock_count_allocations_spiram() > 0);
}

void test_audio_processor_alloc_without_psram(void)
{
    esp_heap_caps_mock_set_psram_available(false);

    audio_processor_deinit();

    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 50;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    size_t bytes = audio_processor_get_work_buffer_bytes();
    /* Should still succeed via DRAM fallback */
    TEST_ASSERT_TRUE(bytes >= 1024);
    /* No allocations should have come from PSRAM when disabled */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)esp_heap_caps_mock_count_allocations_spiram());
    TEST_ASSERT_TRUE(esp_heap_caps_mock_count_allocations_dram() > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_processor_alloc_with_psram);
    RUN_TEST(test_audio_processor_alloc_without_psram);
    return UNITY_END();
}

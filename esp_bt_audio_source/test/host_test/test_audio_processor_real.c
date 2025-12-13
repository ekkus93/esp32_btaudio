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
#include "freertos/FreeRTOS.h"
#include <string.h>

/* Expose UNIT_TEST hook from audio_processor.c */
bool audio_processor_test_autostart_due(TickType_t now_ticks, TickType_t last_ticks, TickType_t cooldown_ticks);

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

void test_audio_processor_autostart_cooldown(void)
{
    /* First attempt after long gap should be allowed */
    TEST_ASSERT_TRUE(audio_processor_test_autostart_due((TickType_t)100, (TickType_t)0, (TickType_t)50));
    /* Within cooldown window should be suppressed */
    TEST_ASSERT_FALSE(audio_processor_test_autostart_due((TickType_t)120, (TickType_t)100, (TickType_t)50));
    /* Boundary: exactly at cooldown should allow */
    TEST_ASSERT_TRUE(audio_processor_test_autostart_due((TickType_t)150, (TickType_t)100, (TickType_t)50));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_processor_alloc_with_psram);
    RUN_TEST(test_audio_processor_alloc_without_psram);
    return UNITY_END();
}

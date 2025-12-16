#include "unity.h"
#include "../../main/include/audio_processor.h"
#include "esp_heap_caps.h"
#include <string.h>

void setUp(void)
{
    /* Ensure mocked PSRAM is available for normal init paths */
    esp_heap_caps_mock_set_psram_available(true);
    esp_heap_caps_mock_reset_allocations();
}

void tearDown(void)
{
    audio_processor_deinit();
}

void test_producer_consumer_tag_alignment_beep(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 80;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_start());

    /* Ensure no tags to start */
    size_t tags_before = audio_processor_test_get_tag_used();
    TEST_ASSERT_EQUAL_size_t(0, tags_before);

    /* Enqueue a very short beep (reduced for faster host test runs).
     * Duration small so the host test finishes quickly and produces
     * manageable diagnostic output. */
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_beep(10));

    /* After enqueuing we should have some tag bytes used (at least 1). */
    size_t tags_after = audio_processor_test_get_tag_used();
    printf("TEST: tags after enqueue = %zu\n", tags_after);
    TEST_ASSERT_TRUE_MESSAGE(tags_after > 0, "Expected tags pushed after beep");

    /* Read out all available audio until none is returned. This should
     * cause the consumer-side tag take to run in lockstep with reads and
     * drain the tag buffer. */
    uint8_t buf[1024];
    size_t bytes_read = 0;
    size_t total_read = 0;
    int loops = 0;
    do {
        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        total_read += bytes_read;
        size_t tags_now = audio_processor_test_get_tag_used();
        printf("TEST: loop %d bytes_read=%zu total_read=%zu tags_now=%zu\n", loops, bytes_read, total_read, tags_now);
        loops++;
        /* Safety: avoid infinite loop on test failure */
        if (loops > 1000) break;
    } while (bytes_read > 0);

    /* After consuming all audio and fallback, tags should be cleared. */
    size_t tags_final = audio_processor_test_get_tag_used();
    TEST_ASSERT_EQUAL_size_t(0, tags_final);
}

void test_tag_miss_recovery_should_drop_stale_beep(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 80;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_start());

    audio_processor_test_reset_tag_miss_count();
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_beep(200));

    /* Simulate lost metadata so the next dequeue observes a tag miss. */
    audio_source_tag_test_reset_buffer();

    uint8_t buf[2048];
    size_t bytes_read = 0;
    int loops = 0;
    do {
        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        loops++;
        if (loops > 32) break;
    } while (bytes_read > 0);

    /* Recovery should have drained stale items so only one TAG-MISS is counted. */
    uint32_t miss = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_EQUAL_UINT32(1, miss);

    /* Tag buffer should be empty after the recovery path runs. */
    size_t tags_final = audio_processor_test_get_tag_used();
    TEST_ASSERT_EQUAL_size_t(0, tags_final);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_producer_consumer_tag_alignment_beep);
    RUN_TEST(test_tag_miss_recovery_should_drop_stale_beep);
    return UNITY_END();
}

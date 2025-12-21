#include "unity.h"
#include "../../main/include/audio_processor.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

void setUp(void)
{
    /* Ensure mocked PSRAM is available for normal init paths */
    esp_heap_caps_mock_set_psram_available(true);
    esp_heap_caps_mock_reset_allocations();
    audio_processor_test_reset_tag_recover_window();
}

void tearDown(void)
{
    audio_processor_deinit();
}

static void trigger_desync_recover_once(size_t payload_bytes)
{
    uint8_t *payload = (uint8_t *)malloc(payload_bytes);
    TEST_ASSERT_NOT_NULL(payload);
    memset(payload, 0x33, payload_bytes);
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(payload, payload_bytes));
    free(payload);

    /* Drop all metadata so the next read observes a tag miss. */
    audio_source_tag_test_reset_buffer();

    uint8_t sink[512];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(sink, sizeof(sink), &bytes_read));
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

void test_tag_recover_should_drop_untracked_audio_and_reset_counters(void)
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

    /* Baseline: clear tag miss counter and ensure tag buffer is empty. */
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();
    size_t audio_free_before = audio_processor_test_get_audio_free_bytes();

    /* Inject one audio payload; this pushes a tag to the metadata ringbuffer. */
    uint8_t payload[256];
    memset(payload, 0x7E, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(payload, sizeof(payload)));

    /* Confirm both audio and tag buffers now hold data. */
    size_t free_after_inject = audio_processor_test_get_audio_free_bytes();
    TEST_ASSERT_TRUE_MESSAGE(free_after_inject < audio_free_before, "Expected audio ringbuffer usage after inject");
    TEST_ASSERT_TRUE_MESSAGE(audio_processor_test_get_tag_used() > 0, "Expected metadata tag after inject");

    /* Force desync: drop all tags so the queued audio has no matching metadata. */
    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_tag_used());

    /* Read once to trigger TAG-RECOVER on audio_rb path. */
    uint8_t out[256];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(out, sizeof(out), &bytes_read));
    /* After recovery the audio item should have been dropped, so the read may be empty. */
    TEST_ASSERT_TRUE(bytes_read == 0 || bytes_read == sizeof(out));

    /* Recovery should have incremented tag miss count and drained metadata/audio. */
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_test_get_tag_miss_count());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_tag_used());
    TEST_ASSERT_TRUE_MESSAGE(audio_processor_test_get_audio_free_bytes() >= audio_free_before,
                              "Audio ringbuffer did not free back to baseline after recover");
}

void test_tag_recover_should_throttle_and_rearm_after_window(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 60;
    cfg.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_start());

    audio_processor_test_reset_tag_miss_count();
    audio_processor_test_reset_tag_recover_window();
    audio_source_tag_test_reset_buffer();

    /* First desync should increment the counter and arm the mute window. */
    trigger_desync_recover_once(256);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_test_get_tag_miss_count());

    /* Second desync inside the throttle window should be ignored (bounded counter). */
    trigger_desync_recover_once(256);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_test_get_tag_miss_count());

    /* Advance beyond the mute window and ensure recover can re-arm and count again.
     * Host tick may not advance without a scheduler, so also force expiry of the throttle window. */
    vTaskDelay(pdMS_TO_TICKS(600));
#if defined(INCLUDE_vTaskStepTick) && (INCLUDE_vTaskStepTick == 1)
    vTaskStepTick(pdMS_TO_TICKS(600));
#else
    audio_processor_test_reset_tag_recover_window();
#endif
    trigger_desync_recover_once(256);
    TEST_ASSERT_EQUAL_UINT32(2, audio_processor_test_get_tag_miss_count());

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_deinit());
}

#ifdef CONFIG_BT_MOCK_TESTING
void test_beep_fallback_should_not_double_activate_when_tags_drained(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 60;
    cfg.mute = false;

    audio_source_tag_test_reset_buffer();
    audio_processor_test_reset_tag_miss_count();

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_start());

    size_t free_bytes = audio_processor_test_get_audio_free_bytes();
    if (free_bytes > 0) {
        uint8_t *pad = (uint8_t *)malloc(free_bytes);
        TEST_ASSERT_NOT_NULL(pad);
        memset(pad, 0x11, free_bytes);
        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(pad, free_bytes));
        free(pad);
    }
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_audio_free_bytes());

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_beep(800));
    TEST_ASSERT_TRUE_MESSAGE(audio_processor_test_is_beep_fallback_active(), "Fallback synth did not activate");

    size_t fallback_total_initial = audio_processor_test_get_beep_fallback_total_frames();
    TEST_ASSERT_TRUE_MESSAGE(fallback_total_initial > 0, "Fallback frames not initialized");

    uint32_t tag_miss_before = audio_processor_test_get_tag_miss_count();

    uint8_t out[1024];
    int iterations = 0;
    while (audio_processor_test_is_beep_fallback_active() && iterations < 200) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(out, sizeof(out), &bytes_read));

        /* Simulate concurrent tag drain while fallback is active. */
        audio_source_tag_test_reset_buffer();

        size_t fallback_total_now = audio_processor_test_get_beep_fallback_total_frames();
        if (!audio_processor_test_is_beep_fallback_active()) {
            break;
        }
        TEST_ASSERT_EQUAL_size_t(fallback_total_initial, fallback_total_now);
        iterations++;
    }

    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_beep_fallback_frames_remaining());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_beep_fallback_total_frames());

    uint32_t tag_miss_after = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_TRUE_MESSAGE((tag_miss_after - tag_miss_before) <= 1, "Tag miss count grew unexpectedly during fallback");
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_tag_used());

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_deinit());
}

void test_fallback_then_wav_should_keep_tags_aligned(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 50;
    cfg.mute = false;

    audio_source_tag_test_reset_buffer();
    audio_processor_test_reset_tag_miss_count();

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_start());

    size_t free_bytes = audio_processor_test_get_audio_free_bytes();
    size_t wav_bytes = free_bytes / 4;
    if (wav_bytes < 2048) {
        wav_bytes = (free_bytes > 0 && free_bytes < 2048) ? free_bytes : 2048;
    }
    uint8_t *wav_payload = (uint8_t *)malloc(wav_bytes);
    TEST_ASSERT_NOT_NULL(wav_payload);
    memset(wav_payload, 0x5A, wav_bytes);
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(wav_payload, wav_bytes));
    free(wav_payload);

    size_t tags_after_wav = audio_processor_test_get_tag_used();
    TEST_ASSERT_TRUE_MESSAGE(tags_after_wav > 0, "Expected tags queued for WAV payload");

    size_t remaining = audio_processor_test_get_audio_free_bytes();
    if (remaining > 0) {
        uint8_t *pad = (uint8_t *)malloc(remaining);
        TEST_ASSERT_NOT_NULL(pad);
        memset(pad, 0x22, remaining);
        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(pad, remaining));
        free(pad);
    }

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_audio_free_bytes());

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_beep(600));
    TEST_ASSERT_TRUE_MESSAGE(audio_processor_test_is_beep_fallback_active(), "Fallback synth did not activate");

    uint8_t out[2048];
    bool saw_nonzero = false;
    bool saw_wav = false;
    for (int i = 0; i < 120; ++i) {
        size_t bytes_read = 0;
        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(out, sizeof(out), &bytes_read));
        if (bytes_read == 0) {
            break;
        }
        for (size_t j = 0; j < bytes_read; ++j) {
            if (out[j] != 0) {
                saw_nonzero = true;
                break;
            }
        }
        if (!audio_processor_test_is_beep_fallback_active()) {
            saw_wav = true;
        }
    }

    TEST_ASSERT_FALSE(audio_processor_test_is_beep_fallback_active());
    uint32_t tag_miss = audio_processor_test_get_tag_miss_count();
    TEST_ASSERT_TRUE_MESSAGE(tag_miss <= 1, "Unexpected tag miss count during fallback/wav sequence");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)audio_processor_test_get_tag_used());
    TEST_ASSERT_TRUE_MESSAGE(saw_nonzero, "Fallback output was unexpectedly silent");
    TEST_ASSERT_TRUE_MESSAGE(saw_wav, "Queued WAV audio did not resume after fallback");

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_deinit());
}

void test_wav_and_fallback_partial_reads_should_keep_tags_aligned(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_44K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 75;
    cfg.mute = false;

    audio_source_tag_test_reset_buffer();
    audio_processor_test_reset_tag_miss_count();

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_start());

    const size_t partial_sizes[] = {300, 700, 900};

    for (int cycle = 0; cycle < 2; ++cycle) {
        /* Prime WAV data then saturate the audio buffer to force fallback on beep. */
        size_t free_bytes = audio_processor_test_get_audio_free_bytes();
        size_t wav_bytes = free_bytes / 2;
        if (wav_bytes < 2048) {
            wav_bytes = (free_bytes > 0 && free_bytes < 2048) ? free_bytes : 2048;
        }
        uint8_t *wav_payload = (uint8_t *)malloc(wav_bytes);
        TEST_ASSERT_NOT_NULL(wav_payload);
        memset(wav_payload, 0x3C, wav_bytes);
        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(wav_payload, wav_bytes));
        free(wav_payload);

        /* Fill remaining buffer space to guarantee beep enqueue will fall back. */
        size_t remaining = audio_processor_test_get_audio_free_bytes();
        if (remaining > 0) {
            uint8_t *pad = (uint8_t *)malloc(remaining);
            TEST_ASSERT_NOT_NULL(pad);
            memset(pad, 0x55, remaining);
            TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_test_inject_audio_data(pad, remaining));
            free(pad);
        }

        size_t tags_after_wav = audio_processor_test_get_tag_used();
        TEST_ASSERT_TRUE_MESSAGE(tags_after_wav > 0, "Expected tags queued for WAV payload");

        TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_beep(400));
        TEST_ASSERT_TRUE_MESSAGE(audio_processor_test_is_beep_fallback_active(), "Fallback synth did not activate");

        size_t tag_miss_start = audio_processor_test_get_tag_miss_count();

        /* Alternate partial read sizes while fallback drains and WAV resumes. */
        uint8_t out[1024];
        int iter = 0;
        while (iter < 120) {
            size_t read_size = partial_sizes[iter % (sizeof(partial_sizes) / sizeof(partial_sizes[0]))];
            size_t bytes_read = 0;
            TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_read(out, read_size, &bytes_read));

            /* After fallback ends, ensure tag debt cleared before proceeding. */
            if (!audio_processor_test_is_beep_fallback_active()) {
                TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_fallback_tag_debt());
            }

            /* Stop once both fallback is inactive and no more audio is produced. */
            if (!audio_processor_test_is_beep_fallback_active() && bytes_read == 0) {
                break;
            }

            iter++;
        }

        uint32_t tag_miss_delta = audio_processor_test_get_tag_miss_count() - tag_miss_start;
        TEST_ASSERT_TRUE_MESSAGE(tag_miss_delta <= 1, "Unexpected tag miss growth during fallback/WAV partial reads");
        TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_tag_used());
        TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_fallback_tag_debt());
    }

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_deinit());
}
#endif

#ifdef CONFIG_BT_MOCK_TESTING

static audio_config_t make_basic_cfg(void)
{
    audio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = AUDIO_SAMPLE_RATE_48K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 80;
    cfg.mute = false;
    return cfg;
}

void test_wav_enqueue_should_align_to_full_frames(void)
{
    audio_config_t cfg = make_basic_cfg();
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));

    /* Force a known frame size and enqueue a non-multiple length. */
    audio_processor_test_force_wav_frame_bytes_dst(4);
    uint8_t payload[13];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i + 1);
    }

    size_t sent = audio_processor_test_wav_try_enqueue(payload, sizeof(payload));

    TEST_ASSERT_EQUAL_size_t((sizeof(payload) / 4U) * 4U, sent);
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    audio_processor_test_force_wav_frame_bytes_dst(0);
    audio_processor_test_set_ringbuffer_max_item_override(0);
    audio_processor_deinit();
}

void test_wav_enqueue_should_skip_when_max_item_smaller_than_frame(void)
{
    audio_config_t cfg = make_basic_cfg();
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));

    audio_processor_test_force_wav_frame_bytes_dst(4);
    audio_processor_test_set_ringbuffer_max_item_override(2);

    uint8_t payload[8] = {0};
    size_t sent = audio_processor_test_wav_try_enqueue(payload, sizeof(payload));

    TEST_ASSERT_EQUAL_size_t(0, sent);
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_tag_used());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_tag_miss_count());

    audio_processor_test_force_wav_frame_bytes_dst(0);
    audio_processor_test_set_ringbuffer_max_item_override(0);
    audio_processor_deinit();
}

void test_wav_residual_flush_should_wait_for_full_frame(void)
{
    audio_config_t cfg = make_basic_cfg();
    audio_processor_test_reset_tag_miss_count();
    audio_source_tag_test_reset_buffer();

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_init(&cfg));

    audio_processor_test_force_wav_frame_bytes_dst(4);
    audio_processor_test_set_ringbuffer_max_item_override(2);

    uint8_t residual[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
    audio_processor_test_seed_wav_residual(residual, sizeof(residual));

    bool pending = audio_processor_test_flush_wav_residual();
    TEST_ASSERT_TRUE(pending);
    TEST_ASSERT_EQUAL_size_t(sizeof(residual), audio_processor_test_get_wav_residual_remaining());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_tag_used());

    /* Clear override so a second flush can succeed and drain fully. */
    audio_processor_test_set_ringbuffer_max_item_override(0);
    pending = audio_processor_test_flush_wav_residual();
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_wav_residual_remaining());
    TEST_ASSERT_EQUAL_size_t(1, audio_processor_test_get_tag_used());

    audio_processor_test_force_wav_frame_bytes_dst(0);
    audio_processor_test_clear_wav_residual();
    audio_processor_deinit();
}

#endif /* CONFIG_BT_MOCK_TESTING */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_producer_consumer_tag_alignment_beep);
    RUN_TEST(test_tag_miss_recovery_should_drop_stale_beep);
    RUN_TEST(test_tag_recover_should_drop_untracked_audio_and_reset_counters);
    RUN_TEST(test_tag_recover_should_throttle_and_rearm_after_window);
#ifdef CONFIG_BT_MOCK_TESTING
    RUN_TEST(test_beep_fallback_should_not_double_activate_when_tags_drained);
    RUN_TEST(test_fallback_then_wav_should_keep_tags_aligned);
    RUN_TEST(test_wav_and_fallback_partial_reads_should_keep_tags_aligned);
    RUN_TEST(test_wav_enqueue_should_align_to_full_frames);
    RUN_TEST(test_wav_enqueue_should_skip_when_max_item_smaller_than_frame);
    RUN_TEST(test_wav_residual_flush_should_wait_for_full_frame);
#endif
    return UNITY_END();
}

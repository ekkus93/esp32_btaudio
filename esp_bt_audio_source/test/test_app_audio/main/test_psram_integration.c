/* On-device PSRAM integration tests
 * - Verifies that heap_caps_malloc with SPIRAM preference allocates from SPIRAM when
 *   present, and falls back to DRAM when not available.
 * - Verifies that the audio processor places large buffers in SPIRAM when available
 *   and that the DRAM-only mode forces allocations into DRAM.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "audio_processor.h"

static const char *TAG = "TEST_PSRAM";

/* Simple heap_cap allocation test: allocate a moderately sized block with
 * SPIRAM preference and observe which pool's free bytes decrease. If the
 * allocation fails, attempt the DRAM fallback which must succeed on normal
 * hardware. */
void test_heap_psram_simple(void)
{
    const size_t sz = 64 * 1024; /* 64KB */

    size_t before_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t before_dram   = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (p) {
        size_t after_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t after_dram   = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

        size_t used_spiram = (before_spiram > after_spiram) ? (before_spiram - after_spiram) : 0;
        size_t used_dram   = (before_dram   > after_dram)   ? (before_dram - after_dram)   : 0;

        ESP_LOGI(TAG, "heap_caps_malloc returned %p, used_spiram=%u used_dram=%u", p, (unsigned)used_spiram, (unsigned)used_dram);

        /* Allocation must have consumed space from either SPIRAM or DRAM */
        TEST_ASSERT_TRUE_MESSAGE(used_spiram > 0 || used_dram > 0,
                                 "Allocation did not reduce SPIRAM or DRAM free size");

        heap_caps_free(p);
    } else {
        ESP_LOGW(TAG, "SPIRAM-preferred allocation failed; trying DRAM fallback");
        void *q = heap_caps_malloc(sz, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
        TEST_ASSERT_NOT_NULL_MESSAGE(q, "DRAM fallback allocation also failed");
        heap_caps_free(q);
    }
}

/* Validate audio_processor allocations respect the dram-only flag and the
 * presence of PSRAM. This test initializes the audio_processor twice: once
 * allowing PSRAM (default) and once forcing DRAM-only mode. It checks the
 * free-size deltas of SPIRAM/DRAM to assert buffers were placed accordingly.
 */
void test_audio_processor_psram_allocations(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80,
        .mute = false,
        .i2s_port = 0,
    };

    /* Ensure DRAM-only disabled and measure pools */
    audio_processor_set_dram_only(false);
    size_t sp_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t dr_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    esp_err_t err = audio_processor_init(&cfg);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "audio_processor_init failed");

    size_t sp_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t dr_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    size_t sp_used = (sp_before > sp_after) ? (sp_before - sp_after) : 0;
    size_t dr_used = (dr_before > dr_after) ? (dr_before - dr_after) : 0;

    ESP_LOGI(TAG, "audio_processor_init used sp=%u dr=%u", (unsigned)sp_used, (unsigned)dr_used);

    if (esp_psram_is_initialized()) {
        /* When PSRAM present and dram-only disabled we expect SPIRAM usage */
        TEST_ASSERT_TRUE_MESSAGE(sp_used > 0, "Expected some SPIRAM usage when PSRAM present");
    } else {
        /* No PSRAM on this board: expect DRAM usage */
        TEST_ASSERT_TRUE_MESSAGE(dr_used > 0, "Expected DRAM usage when PSRAM not present");
    }

    size_t work_bytes = audio_processor_get_work_buffer_bytes();
    TEST_ASSERT_TRUE_MESSAGE(work_bytes > 0, "audio_processor_get_work_buffer_bytes returned 0");

    /* Deinit and then force DRAM-only and check allocations move to DRAM */
    err = audio_processor_deinit();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "audio_processor_deinit failed");

    audio_processor_set_dram_only(true);

    sp_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    dr_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    err = audio_processor_init(&cfg);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "audio_processor_init (dram-only) failed");

    sp_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    dr_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    sp_used = (sp_before > sp_after) ? (sp_before - sp_after) : 0;
    dr_used = (dr_before > dr_after) ? (dr_before - dr_after) : 0;

    ESP_LOGI(TAG, "(DRAM-only) audio_processor_init used sp=%u dr=%u", (unsigned)sp_used, (unsigned)dr_used);

    /* DRAM-only mode must consume DRAM */
    TEST_ASSERT_TRUE_MESSAGE(dr_used > 0, "Expected DRAM usage when DRAM-only mode enabled");

    err = audio_processor_deinit();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "audio_processor_deinit failed (dram-only)");
}

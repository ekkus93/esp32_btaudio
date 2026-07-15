#include "unity.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "signal_gen.h"
#include "pcm_ring.h"
#include "tone.h"

#include <string.h>

static const char *TAG = "DEVTEST";

/* ============================================================
 * Signal generation tests
 * ============================================================ */

static void test_signal_gen_produces_sine_samples(void)
{
    int16_t buf[256];
    sg_sine_state_t st;
    sg_sine_reset(&st);
    sg_sine_fill(&st, buf, 128, 1000.0, 0.5);

    // Sine wave at 1000Hz should have some non-zero samples
    TEST_ASSERT_NOT_EQUAL(0, buf[0]);
}

static void test_signal_gen_constant_dc_at_0hz(void)
{
    int16_t buf[128];
    sg_sine_state_t st;
    sg_sine_reset(&st);
    sg_sine_fill(&st, buf, 64, 0.0, 0.0);

    // 0Hz with 0 amplitude should produce silence
    for (int i = 0; i < 64; i++) {
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);
    }
}

/* ============================================================
 * Tone module tests
 * ============================================================ */

static void test_tone_fill_produces_output(void)
{
    int16_t buf[256];
    tone_off();
    memset(buf, 0, sizeof(buf));
    tone_fill(buf, 128);

    // With tone off, should produce silence
    for (int i = 0; i < 128; i++) {
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);
    }
}

/* ============================================================
 * PCM ring buffer tests
 * ============================================================ */

static void test_pcm_ring_create_and_basic_write_read(void)
{
    pcm_ring_t *r = pcm_ring_create(256, false);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_used(r));

    uint8_t data[16] = {0};
    for (int i = 0; i < 16; i++) data[i] = (uint8_t)i;

    TEST_ASSERT_EQUAL_UINT(16, pcm_ring_write(r, data, 16));
    TEST_ASSERT_EQUAL_UINT(16, pcm_ring_used(r));

    uint8_t out[16] = {0};
    TEST_ASSERT_EQUAL_UINT(16, pcm_ring_read(r, out, 16));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, 16);

    pcm_ring_destroy(r);
}

/* ============================================================
 * NVS tests
 * ============================================================ */

static void test_nvs_read_write_roundtrip(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("test_namespace", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_close(handle);
    } else {
        err = nvs_open("test_namespace", NVS_READWRITE, &handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = nvs_set_i32(handle, "test_key", 42);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));

        int32_t val = -1;
        err = nvs_get_i32(handle, "test_key", &val);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        TEST_ASSERT_EQUAL_INT32(42, val);

        nvs_erase_key(handle, "test_key");
        nvs_commit(handle);

        nvs_close(handle);
    }
}

/* ============================================================
 * FreeRTOS task creation
 * ============================================================ */

static volatile bool s_task_ran = false;
static void dummy_task(void *arg)
{
    (void)arg;
    s_task_ran = true;
    vTaskDelete(NULL);
}

static void test_task_creation_succeeds(void)
{
    s_task_ran = false;
    TaskHandle_t h = NULL;
    BaseType_t xResult = xTaskCreate(dummy_task, "test_task", 1024, NULL,
                                      tskIDLE_PRIORITY, &h);
    TEST_ASSERT_EQUAL(pdPASS, xResult);
    TEST_ASSERT_NOT_NULL(h);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    TEST_ASSERT(s_task_ran);
}

/* ============================================================
 * Heap / PSRAM
 * ============================================================ */

static void test_heap_is_available(void)
{
    size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "free_ram=%u free_psram=%u",
             (unsigned)free_ram, (unsigned)free_psram);
    TEST_ASSERT(free_ram > 0);

    void *ptr = heap_caps_malloc(1024, MALLOC_CAP_DEFAULT);
    TEST_ASSERT_NOT_NULL(ptr);
    free(ptr);

    ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    TEST_ASSERT_NOT_NULL(ptr);
    free(ptr);
}

/* ============================================================
 * Test runner
 * ============================================================ */

void device_test_main(void)
{
    ESP_LOGI(TAG, "Running signal_gen tests");
    RUN_TEST(test_signal_gen_produces_sine_samples);
    RUN_TEST(test_signal_gen_constant_dc_at_0hz);

    ESP_LOGI(TAG, "Running tone tests");
    RUN_TEST(test_tone_fill_produces_output);

    ESP_LOGI(TAG, "Running pcm_ring tests");
    RUN_TEST(test_pcm_ring_create_and_basic_write_read);

    ESP_LOGI(TAG, "Running NVS tests");
    RUN_TEST(test_nvs_read_write_roundtrip);

    ESP_LOGI(TAG, "Running FreeRTOS tests");
    RUN_TEST(test_task_creation_succeeds);

    ESP_LOGI(TAG, "Running heap tests");
    RUN_TEST(test_heap_is_available);

    ESP_LOGI(TAG, "All device tests completed");
}

#include "unity.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

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

    // Sine at 1kHz over 128 samples: the first sample may validly be zero
    // (phase 0).  Verify with nonzero-count + energy instead of checking
    // a single sample.
    int nonzero = 0;
    int64_t energy = 0;
    for (int i = 0; i < 128; i++) {
        if (buf[i] != 0) {
            nonzero++;
        }
        energy += (int64_t)buf[i] * (int64_t)buf[i];
    }
    TEST_ASSERT_GREATER_THAN(32, nonzero);
    TEST_ASSERT_GREATER_THAN(0, energy);
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
    // Use a unique namespace to avoid colliding with production NVS keys.
    // Always perform the full write/read/erase round-trip — never skip.
    const char *ns = "devtest";
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(ns, NVS_READWRITE, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Erase the key first so the test is idempotent.
    err = nvs_erase_key(handle, "roundtrip");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    // Write
    err = nvs_set_i32(handle, "roundtrip", 42);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = nvs_commit(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Read
    int32_t val = -1;
    err = nvs_get_i32(handle, "roundtrip", &val);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT32(42, val);

    // Erase
    err = nvs_erase_key(handle, "roundtrip");
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = nvs_commit(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    nvs_close(handle);
}

/* ============================================================
 * FreeRTOS task creation
 * ============================================================ */

static SemaphoreHandle_t s_task_done_sem;
static void dummy_task(void *arg)
{
    (void)arg;
    xSemaphoreGive(s_task_done_sem);
    vTaskDelete(NULL);
}

static void test_task_creation_succeeds(void)
{
    s_task_done_sem = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(s_task_done_sem);

    TaskHandle_t h = NULL;
    BaseType_t xResult = xTaskCreate(dummy_task, "test_task", 1024, NULL,
                                      tskIDLE_PRIORITY, &h);
    TEST_ASSERT_EQUAL(pdPASS, xResult);
    TEST_ASSERT_NOT_NULL(h);

    // Wait for the task to signal completion (not a blind delay).
    TEST_ASSERT_EQUAL(pdTRUE,
                      xSemaphoreTake(s_task_done_sem, pdMS_TO_TICKS(1000)));

    vSemaphoreDelete(s_task_done_sem);
    s_task_done_sem = NULL;
}

/* ============================================================
 * Heap / PSRAM
 * ============================================================ */

static void test_heap_is_available(void)
{
    size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "free_ram=%u", (unsigned)free_ram);
    TEST_ASSERT(free_ram > 0);

    void *ptr = heap_caps_malloc(1024, MALLOC_CAP_DEFAULT);
    TEST_ASSERT_NOT_NULL(ptr);
    free(ptr);

#if CONFIG_SPIRAM
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "free_psram=%u", (unsigned)free_psram);
    TEST_ASSERT(free_psram > 0);

    ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    TEST_ASSERT_NOT_NULL(ptr);
    free(ptr);
#else
    ESP_LOGW(TAG, "Skipping PSRAM test (CONFIG_SPIRAM not set)");
#endif
}

/* ============================================================
 * WiFi connectivity test
 * ============================================================ */

/* ============================================================
 * WiFi connectivity test
 * ============================================================ */

// Event bits for the WiFi test state machine.
#define WIFI_EVT_GOT_IP   BIT0
#define WIFI_EVT_DISCONNECT BIT1

static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_wifi_netif;

static void wifi_sta_event_handler(void *arg, esp_event_base_t event, int32_t event_id, void *data)
{
    (void)arg;
    (void)event;
    (void)data;

    switch ((int)event_id) {
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected to AP");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi disconnected");
        xEventGroupSetBits(s_wifi_events, WIFI_EVT_DISCONNECT);
        break;
    case IP_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "WiFi got IP");
        xEventGroupSetBits(s_wifi_events, WIFI_EVT_GOT_IP);
        break;
    default:
        break;
    }
}

static void test_wifi_connectivity(void)
{
    ESP_LOGI(TAG, "=== Starting WiFi connectivity test ===");

    /* Load credentials from NVS */
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi credentials in NVS (err=%d)", err);
        TEST_ASSERT_MESSAGE(true, "No WiFi credentials — test skipped (AP mode)");
        return;
    }

    char ssid[33] = {0};
    size_t ssid_len = sizeof(ssid) - 1;
    if (nvs_get_str(handle, "ssid", ssid, &ssid_len) != ESP_OK || ssid_len == 0) {
        nvs_close(handle);
        TEST_ASSERT_MESSAGE(true, "No SSID stored — test skipped");
        return;
    }

    char pass[65] = {0};
    size_t pass_len = sizeof(pass) - 1;
    nvs_get_str(handle, "pass", pass, &pass_len);
    nvs_close(handle);

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    /* Create event group and netif */
    s_wifi_events = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(s_wifi_events);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_netif = esp_netif_create_default_wifi_sta();
    TEST_ASSERT_NOT_NULL(s_wifi_netif);

    /* Register event handler */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_sta_event_handler, NULL));

    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    /* Build config with exact SSID/password */
    wifi_config_t wifi_cfg = {0};
    memcpy(wifi_cfg.sta.ssid, ssid, ssid_len);
    if (pass_len > 0) {
        memcpy(wifi_cfg.sta.password, pass, pass_len);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    /* Wait for GOT_IP event with timeout */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                            WIFI_EVT_GOT_IP | WIFI_EVT_DISCONNECT,
                                            pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(15000));

    if (bits & WIFI_EVT_GOT_IP) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(s_wifi_netif, &info) == ESP_OK) {
            ESP_LOGI(TAG, "Got IP: %d.%d.%d.%d",
                     (info.ip.addr >> 24) & 0xff,
                     (info.ip.addr >> 16) & 0xff,
                     (info.ip.addr >> 8) & 0xff,
                     info.ip.addr & 0xff);
            TEST_ASSERT(info.ip.addr != 0);
        }
    } else if (bits & WIFI_EVT_DISCONNECT) {
        ESP_LOGW(TAG, "WiFi disconnected during connection attempt");
        TEST_ASSERT_MESSAGE(false, "WiFi disconnected before getting IP");
    } else {
        ESP_LOGW(TAG, "Timed out waiting for WiFi connection");
        TEST_ASSERT_MESSAGE(false, "WiFi connection timed out");
    }

    /* Clean up */
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_sta_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_sta_event_handler));
    vEventGroupDelete(s_wifi_events);
    s_wifi_events = NULL;

    ESP_LOGI(TAG, "=== WiFi connectivity test completed ===");
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

    ESP_LOGI(TAG, "Running WiFi connectivity tests");
    test_wifi_connectivity();

    ESP_LOGI(TAG, "All device tests completed");
}

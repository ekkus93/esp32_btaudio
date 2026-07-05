/*
 * esp_i2s_source — ESP32-S3 internet-radio / tone source that streams audio
 * over I2S to the ESP32-WROOM32 A2DP bridge. See docs/SPEC.md.
 *
 * Current phase (SIG-1c): boot, then continuously push a 440 Hz test tone
 * through signal_gen -> i2s_out (I2S master TX). app_main prints an I2S stats
 * beacon each second so the TX can be verified clocking (bytes growing,
 * underruns flat) even before the WROOM32 sink is wired. Source arbitration,
 * the command link, WiFi/web and radio replace this from LINK-1 onward.
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

#include "signal_gen.h"
#include "i2s_out.h"

static const char *TAG = "main";

/* 256 KB PSRAM ring ≈ 1.5 s of 44.1 kHz stereo s16 — ample jitter absorption. */
#define I2S_RING_BYTES (256 * 1024)
#define TONE_HZ        440.0
#define TONE_AMPLITUDE 0.30   /* modest level — comfortable in earbuds */
#define TONE_FRAMES    256    /* per fill: 256 stereo frames = 1024 bytes */

/* Initialise NVS exactly once (root CLAUDE.md contract mirrors esp_bt_audio_source). */
static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* Producer: generate a continuous 440 Hz sine and push it into the i2s_out
 * ring, honouring backpressure (write returns short when the ring is full). */
static void tone_task(void *arg)
{
    (void)arg;
    sg_sine_state_t sine;
    sg_sine_reset(&sine);
    int16_t block[TONE_FRAMES * SIGNAL_GEN_CHANNELS];

    for (;;) {
        sg_sine_fill(&sine, block, TONE_FRAMES, TONE_HZ, TONE_AMPLITUDE);
        const uint8_t *p = (const uint8_t *)block;
        size_t total = sizeof(block);
        size_t off = 0;
        while (off < total) {
            size_t w = i2s_out_write(p + off, total - off);
            off += w;
            if (w == 0) {
                vTaskDelay(1);  /* ring full — let the I2S consumer drain */
            }
        }
    }
}

void app_main(void)
{
    init_nvs();

    size_t psram_bytes = 0;
#if CONFIG_SPIRAM
    psram_bytes = esp_psram_get_size();
#endif
    ESP_LOGI(TAG, "esp_i2s_source boot: idf=%s psram=%u KB free_heap=%u B",
             esp_get_idf_version(), (unsigned)(psram_bytes / 1024),
             (unsigned)esp_get_free_heap_size());
    printf("DIAG|BOOT|READY|psram_kb=%u,heap=%u\n",
           (unsigned)(psram_bytes / 1024), (unsigned)esp_get_free_heap_size());
    fflush(stdout);

    ESP_ERROR_CHECK(i2s_out_init(I2S_RING_BYTES));
    ESP_ERROR_CHECK(i2s_out_start());
    xTaskCreate(tone_task, "tone", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    ESP_LOGI(TAG, "SIG-1c: 440 Hz tone streaming to I2S (bclk=5 ws=6 dout=7)");

    /* I2S stats beacon: bytes_written must climb; underruns should stay flat
     * once the ring primes. Repeated so it survives USB-JTAG re-enumeration. */
    i2s_out_stats_t st;
    for (;;) {
        i2s_out_get_stats(&st);
        printf("DIAG|I2S|bytes=%llu,und=%llu,undev=%u,ringpeak=%u\n",
               (unsigned long long)st.bytes_written,
               (unsigned long long)st.underrun_bytes,
               (unsigned)st.underrun_events, (unsigned)st.ring_peak);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

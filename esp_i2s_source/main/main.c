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

#include "driver/gpio.h"

#include "signal_gen.h"
#include "i2s_out.h"

static const char *TAG = "main";

/* Exact frequency meter: PCNT counts rising edges at full hardware speed
 * (no aliasing, unlike a gpio_get_level poller). Reports the WROOM32 master
 * clock's TRUE frequency and the BCLK:WS ratio the S3 slave actually sees. */
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
static float s3_measure_hz(int gpio, int ms)
{
    pcnt_unit_config_t ucfg = {
        .high_limit = 32000,
        .low_limit = -1,
        .flags = { .accum_count = true },
    };
    pcnt_unit_handle_t unit;
    if (pcnt_new_unit(&ucfg, &unit) != ESP_OK) return -1.0f;
    pcnt_chan_config_t ccfg = { .edge_gpio_num = gpio, .level_gpio_num = -1 };
    pcnt_channel_handle_t ch;
    if (pcnt_new_channel(unit, &ccfg, &ch) != ESP_OK) {
        pcnt_del_unit(unit);
        return -1.0f;
    }
    pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_unit_add_watch_point(unit, 32000);  /* required for accum_count */
    pcnt_unit_enable(unit);
    pcnt_unit_clear_count(unit);
    pcnt_unit_start(unit);
    int64_t t0 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(ms));
    int count = 0;
    pcnt_unit_get_count(unit, &count);
    int64_t dt = esp_timer_get_time() - t0;
    pcnt_unit_stop(unit);
    pcnt_unit_disable(unit);
    pcnt_del_channel(ch);
    pcnt_del_unit(unit);
    return (dt > 0) ? (float)count * 1e6f / (float)dt : -1.0f;
}

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

    /* 16-in-32 with the sample in the TOP half (<<16): the classic-side
     * capture window lags the S3's slots by half a slot, so HIGH-aligned
     * payload lands whole in the LOW half of each captured word, one word
     * per channel (verified via laptop A2DP capture; low-aligned payload
     * gets split across words and one channel goes silent). The WROOM32
     * shim then lifts it back to the top for its >>16 conversion. */
    static int32_t block32[TONE_FRAMES * SIGNAL_GEN_CHANNELS];
    for (;;) {
        sg_sine_fill(&sine, block, TONE_FRAMES, TONE_HZ, TONE_AMPLITUDE);
        for (size_t i = 0; i < TONE_FRAMES * SIGNAL_GEN_CHANNELS; i++) {
            block32[i] = (int32_t)block[i] << 16;
        }
        const uint8_t *p = (const uint8_t *)block32;
        size_t total = sizeof(block32);
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
    /* Prime the ring with tone BEFORE starting the writer, so the first DMA
     * load is real tone data, not underrun zeros. */
    xTaskCreate(tone_task, "tone", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_ERROR_CHECK(i2s_out_start());
    ESP_LOGI(TAG, "SIG-1c: 440 Hz tone streaming to I2S (bclk=%d ws=%d dout=%d)",
             I2S_OUT_GPIO_BCLK, I2S_OUT_GPIO_WS, I2S_OUT_GPIO_DOUT);

    /* I2S stats beacon: bytes_written must climb and underruns stay flat once
     * the ring primes; the PCNT freq meter confirms the WROOM32 master clock
     * (bclk≈2.8224 MHz, ws≈44.1 kHz, ratio≈64). Repeated so it survives
     * USB-JTAG re-enumeration. */
    i2s_out_stats_t st;
    for (;;) {
        i2s_out_get_stats(&st);
        float bclk_hz = s3_measure_hz(I2S_OUT_GPIO_BCLK, 200);
        float ws_hz = s3_measure_hz(I2S_OUT_GPIO_WS, 200);
        printf("DIAG|I2S|bytes=%llu,und=%llu,undev=%u,ringpeak=%u\n",
               (unsigned long long)st.bytes_written,
               (unsigned long long)st.underrun_bytes,
               (unsigned)st.underrun_events, (unsigned)st.ring_peak);
        printf("DIAG|I2SFREQ|bclk_hz=%.0f,ws_hz=%.0f,ratio=%.2f\n",
               bclk_hz, ws_hz, (ws_hz > 0) ? bclk_hz / ws_hz : -1.0f);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

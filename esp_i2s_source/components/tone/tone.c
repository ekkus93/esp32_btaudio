/* tone — controllable test-tone writer feeding i2s_out (WEB-1d). See tone.h. */
#include "tone.h"

#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "signal_gen.h"
#include "i2s_out.h"

static const char *TAG = "tone";

#define TONE_FRAMES     256   /* 256 stereo frames = 1024 B per fill */
#define TONE_AMPLITUDE  0.30  /* modest level, comfortable in earbuds */

static _Atomic bool s_on = true;                 /* default on (SIG-1c parity) */
static _Atomic int  s_hz = TONE_HZ_DEFAULT;

static void tone_task(void *arg)
{
    (void)arg;
    sg_sine_state_t sine;
    sg_sine_reset(&sine);
    int16_t block[TONE_FRAMES * SIGNAL_GEN_CHANNELS];
    /* 16-in-32, sample in the TOP half (<<16): matches the WROOM32 slave RX
     * capture phase (see i2s_manager / SPEC §3.3). */
    static int32_t block32[TONE_FRAMES * SIGNAL_GEN_CHANNELS];

    for (;;) {
        if (atomic_load(&s_on)) {
            sg_sine_fill(&sine, block, TONE_FRAMES, (double)atomic_load(&s_hz), TONE_AMPLITUDE);
        } else {
            /* Silence, but keep advancing phase so re-enabling is glitch-free. */
            sg_sine_fill(&sine, block, TONE_FRAMES, (double)atomic_load(&s_hz), 0.0);
        }
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

esp_err_t tone_start(void)
{
    if (xTaskCreate(tone_task, "tone", 4096, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void tone_set(int freq_hz)
{
    if (freq_hz < TONE_HZ_MIN) freq_hz = TONE_HZ_MIN;
    if (freq_hz > TONE_HZ_MAX) freq_hz = TONE_HZ_MAX;
    atomic_store(&s_hz, freq_hz);
    atomic_store(&s_on, true);
    ESP_LOGI(TAG, "tone on @ %d Hz", freq_hz);
}

void tone_off(void)
{
    atomic_store(&s_on, false);
    ESP_LOGI(TAG, "tone off");
}

void tone_get(bool *enabled, int *freq_hz)
{
    if (enabled) *enabled = atomic_load(&s_on);
    if (freq_hz) *freq_hz = atomic_load(&s_hz);
}

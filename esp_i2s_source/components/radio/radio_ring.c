/*
 * radio_ring.c — the two SPSC byte rings radio.c's workers move data
 * through: the compressed network ring (producer: stream_task, consumer:
 * decoder_task) and the decoded-PCM ring (producer: decoder_task, consumer:
 * the I2S feeder in main.c). Split out of radio.c (RADIO-1b); see radio.h.
 */
#include "radio_internal.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ---- PSRAM byte ring (SPSC, mutex-guarded) ---- */
uint8_t          *g_radio_ring;
size_t             g_radio_ring_cap, g_radio_ring_head, g_radio_ring_tail, g_radio_ring_count;
SemaphoreHandle_t  g_radio_ring_mtx;

size_t ring_write(const uint8_t *d, size_t n)
{
    xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
    size_t w = (n < g_radio_ring_cap - g_radio_ring_count) ? n : g_radio_ring_cap - g_radio_ring_count;
    size_t first = g_radio_ring_cap - g_radio_ring_head;
    if (first > w) first = w;
    memcpy(g_radio_ring + g_radio_ring_head, d, first);
    if (w > first) memcpy(g_radio_ring, d + first, w - first);
    g_radio_ring_head = (g_radio_ring_head + w) % g_radio_ring_cap;
    g_radio_ring_count += w;
    xSemaphoreGive(g_radio_ring_mtx);
    return w;
}

size_t radio_read(uint8_t *dst, size_t len)
{
    if (!g_radio_ring || !dst) return 0;
    xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
    size_t r = (len < g_radio_ring_count) ? len : g_radio_ring_count;
    size_t first = g_radio_ring_cap - g_radio_ring_tail;
    if (first > r) first = r;
    memcpy(dst, g_radio_ring + g_radio_ring_tail, first);
    if (r > first) memcpy(dst + first, g_radio_ring, r - first);
    g_radio_ring_tail = (g_radio_ring_tail + r) % g_radio_ring_cap;
    g_radio_ring_count -= r;
    xSemaphoreGive(g_radio_ring_mtx);
    return r;
}

/* ---- decoded-PCM ring (44.1 kHz stereo s16), producer=decoder, consumer=I2S ----
 * 4 bytes/frame @ 44100 Hz: 1 MiB ~= 5.9 s of decoded audio — a deep jitter
 * buffer so a multi-second TCP/WiFi stall drains the cushion instead of the
 * output. Playback is gated (g_radio_prebuffered) until the ring first fills to
 * PCM_PREBUFFER_BYTES (~3 s), and re-gated if it ever fully drains, so recovery
 * re-buffers cleanly rather than restarting choppy. */
uint8_t          *g_radio_pcm;
size_t             g_radio_pcm_cap, g_radio_pcm_head, g_radio_pcm_tail, g_radio_pcm_count;
SemaphoreHandle_t  g_radio_pcm_mtx;
volatile bool      g_radio_prebuffered;    /* PCM cushion reached -> ok to feed I2S */

/* 7.9: atomic prebuffer threshold — readers check under g_radio_pcm_mtx, writers
   use atomic_store for safe concurrent updates. */
atomic_size_t     g_radio_prebuffer_bytes;

size_t pcm_write(const uint8_t *d, size_t n)
{
    xSemaphoreTake(g_radio_pcm_mtx, portMAX_DELAY);
    size_t w = (n < g_radio_pcm_cap - g_radio_pcm_count) ? n : g_radio_pcm_cap - g_radio_pcm_count;
    size_t first = g_radio_pcm_cap - g_radio_pcm_head;
    if (first > w) first = w;
    memcpy(g_radio_pcm + g_radio_pcm_head, d, first);
    if (w > first) memcpy(g_radio_pcm, d + first, w - first);
    g_radio_pcm_head = (g_radio_pcm_head + w) % g_radio_pcm_cap;
    g_radio_pcm_count += w;
    if (!g_radio_prebuffered && g_radio_pcm_count >= g_radio_prebuffer_bytes) g_radio_prebuffered = true;
    xSemaphoreGive(g_radio_pcm_mtx);
    return w;
}

size_t radio_pcm_read(int16_t *dst, size_t frames)
{
    if (!g_radio_pcm || !dst) return 0;
    size_t want = frames * 4;   /* stereo s16 = 4 bytes/frame */
    xSemaphoreTake(g_radio_pcm_mtx, portMAX_DELAY);
    size_t r = (want < g_radio_pcm_count) ? want : g_radio_pcm_count;
    r &= ~(size_t)3;            /* whole frames only */
    size_t first = g_radio_pcm_cap - g_radio_pcm_tail;
    if (first > r) first = r;
    memcpy(dst, g_radio_pcm + g_radio_pcm_tail, first);
    if (r > first) memcpy((uint8_t *)dst + first, g_radio_pcm, r - first);
    g_radio_pcm_tail = (g_radio_pcm_tail + r) % g_radio_pcm_cap;
    g_radio_pcm_count -= r;
    /* Fully drained -> re-arm the prebuffer gate so the arbiter falls back to
     * silence until the cushion rebuilds, instead of feeding a starving ring. */
    if (g_radio_prebuffered && g_radio_pcm_count == 0) g_radio_prebuffered = false;
    xSemaphoreGive(g_radio_pcm_mtx);
    return r / 4;
}

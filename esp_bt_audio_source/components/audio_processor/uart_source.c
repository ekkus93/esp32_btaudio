/**
 * uart_source.c — UART audio source for the audio processor
 *
 * Staging path: the UART reader task pushes 22050 Hz stereo s16le PCM
 * into an SPSC staging ring (uart_source_write); the audio engine pulls
 * 44100 Hz audio out (uart_source_fill), which drains the ring through
 * the 2x linear upsampler. PREBUFFER holds the source inactive until
 * the ring is 50% full so brief host-side jitter can't underrun the
 * stream right at start.
 *
 * SPSC contract: write() is called only by the reader task, fill() only
 * by the audio engine task — matching the ring buffer's single-producer/
 * single-consumer requirement. stop() deactivates the source, then waits
 * >= 2 engine ticks before freeing the ring so an in-flight fill()
 * observes the deactivation or completes against valid memory.
 */

#include "uart_source.h"

#include <string.h>

#include "audio_ringbuffer.h"
#include "esp_log.h"
#include "platform_timing.h"

static const char *TAG = "uart_source";

/* One fill() pass moves at most this many input bytes per iteration;
 * bounds the static scratch instead of putting 2 KB on the engine stack. */
#define UART_SOURCE_SCRATCH_BYTES 512U

/* 22.05k stereo frame = 4 bytes in, 8 bytes out after 2x upsampling */
#define UART_SOURCE_IN_FRAME_BYTES  4U
#define UART_SOURCE_OUT_FRAME_BYTES 8U

/* delay between deactivation and ring free in stop(); >= 2 engine ticks */
#define UART_SOURCE_STOP_QUIESCE_MS 10U

static volatile uart_source_state_t s_state = UART_SOURCE_STATE_INACTIVE;
static audio_rb_t *s_ring = NULL;
static size_t s_prebuffer_target = 0;
static int16_t s_prev[2] = { 0, 0 };

static uint32_t s_bytes_in = 0;
static uint32_t s_bytes_out = 0;
static uint32_t s_underrun_events = 0;
static uint32_t s_overflow_events = 0;

esp_err_t uart_source_start(size_t ring_bytes)
{
    if (ring_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state != UART_SOURCE_STATE_INACTIVE || s_ring != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_rb_init(&s_ring, ring_bytes, false /* DRAM */);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "staging ring alloc failed (%zu bytes)", ring_bytes);
        return err;
    }

    s_prebuffer_target = ring_bytes / 2;
    s_prev[0] = 0;
    s_prev[1] = 0;
    s_bytes_in = 0;
    s_bytes_out = 0;
    s_underrun_events = 0;
    s_overflow_events = 0;

    s_state = UART_SOURCE_STATE_PREBUFFER;
    ESP_LOGI(TAG, "started: ring=%zu prebuffer=%zu", ring_bytes, s_prebuffer_target);
    return ESP_OK;
}

size_t uart_source_write(const uint8_t *data, size_t len)
{
    if (s_state == UART_SOURCE_STATE_INACTIVE || s_ring == NULL ||
        data == NULL || len == 0) {
        return 0;
    }

    size_t written = audio_rb_write(s_ring, data, len);
    s_bytes_in += (uint32_t)written;
    if (written < len) {
        s_overflow_events++;
    }

    if (s_state == UART_SOURCE_STATE_PREBUFFER &&
        audio_rb_available_to_read(s_ring) >= s_prebuffer_target) {
        s_state = UART_SOURCE_STATE_ACTIVE;
        ESP_LOGI(TAG, "prebuffer complete, source active");
    }
    return written;
}

size_t uart_source_fill(uint8_t *dst, size_t dst_bytes)
{
    static uint8_t scratch[UART_SOURCE_SCRATCH_BYTES];

    if (dst == NULL || dst_bytes == 0) {
        return 0;
    }
    uart_source_state_t state = s_state;
    if (state != UART_SOURCE_STATE_ACTIVE && state != UART_SOURCE_STATE_DRAINING) {
        return 0;
    }
    audio_rb_t *ring = s_ring;
    if (ring == NULL) {
        return 0;
    }

    size_t produced = 0;
    bool shortfall = false;

    while (produced < dst_bytes) {
        size_t out_want = dst_bytes - produced;
        if (out_want > 2U * UART_SOURCE_SCRATCH_BYTES) {
            out_want = 2U * UART_SOURCE_SCRATCH_BYTES;
        }
        /* whole output frames only; sub-frame tail is zero-filled below */
        size_t in_want = (out_want / UART_SOURCE_OUT_FRAME_BYTES) *
                         UART_SOURCE_IN_FRAME_BYTES;
        if (in_want == 0) {
            break;
        }

        size_t got = audio_rb_read(ring, scratch, in_want);
        size_t frames = got / UART_SOURCE_IN_FRAME_BYTES;
        if (frames > 0) {
            audio_upsample2x_s16_stereo((const int16_t *)(const void *)scratch,
                                        frames,
                                        (int16_t *)(void *)(dst + produced),
                                        s_prev);
            produced += frames * UART_SOURCE_OUT_FRAME_BYTES;
        }
        if (got < in_want) {
            shortfall = true;
            break;
        }
    }

    if (produced < dst_bytes) {
        memset(dst + produced, 0, dst_bytes - produced);
    }

    if (shortfall) {
        if (s_state == UART_SOURCE_STATE_DRAINING) {
            /* played out the tail — drain complete */
            s_state = UART_SOURCE_STATE_INACTIVE;
            ESP_LOGI(TAG, "drain complete");
        } else if (s_state == UART_SOURCE_STATE_ACTIVE) {
            s_underrun_events++;
        }
    }

    s_bytes_out += (uint32_t)dst_bytes;
    return dst_bytes;
}

void uart_source_request_drain(void)
{
    if (s_state == UART_SOURCE_STATE_ACTIVE ||
        s_state == UART_SOURCE_STATE_PREBUFFER) {
        /* a short stream still prebuffering must play out its tail too */
        s_state = UART_SOURCE_STATE_DRAINING;
    }
}

void uart_source_stop(void)
{
    if (s_ring == NULL) {
        s_state = UART_SOURCE_STATE_INACTIVE;
        return;
    }

    /* Deactivate first: after the quiesce delay no fill() can be inside
     * the ring, so freeing it is safe. */
    s_state = UART_SOURCE_STATE_INACTIVE;
    platform_delay_ms(UART_SOURCE_STOP_QUIESCE_MS);

    audio_rb_t *ring = s_ring;
    s_ring = NULL;
    audio_rb_deinit(ring);
    ESP_LOGI(TAG, "stopped");
}

bool uart_source_is_active(void)
{
    uart_source_state_t state = s_state;
    return (state == UART_SOURCE_STATE_ACTIVE) ||
           (state == UART_SOURCE_STATE_DRAINING);
}

uart_source_state_t uart_source_get_state(void)
{
    return s_state;
}

void uart_source_get_stats(uart_source_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->state = s_state;
    out->ring_used = audio_rb_available_to_read(s_ring);
    out->ring_capacity = audio_rb_capacity(s_ring);
    out->prebuffer_target = (s_ring != NULL) ? s_prebuffer_target : 0;
    out->bytes_in = s_bytes_in;
    out->bytes_out = s_bytes_out;
    out->underrun_events = s_underrun_events;
    out->overflow_events = s_overflow_events;
}

void audio_upsample2x_s16_stereo(const int16_t *in, size_t in_frames,
                                 int16_t *out, int16_t prev[2])
{
    int32_t prev_l = prev[0];
    int32_t prev_r = prev[1];

    for (size_t i = 0; i < in_frames; i++) {
        const int32_t cur_l = in[2U * i];
        const int32_t cur_r = in[(2U * i) + 1U];

        out[4U * i] = (int16_t)((prev_l + cur_l) / 2);
        out[(4U * i) + 1U] = (int16_t)((prev_r + cur_r) / 2);
        out[(4U * i) + 2U] = (int16_t)cur_l;
        out[(4U * i) + 3U] = (int16_t)cur_r;

        prev_l = cur_l;
        prev_r = cur_r;
    }

    prev[0] = (int16_t)prev_l;
    prev[1] = (int16_t)prev_r;
}

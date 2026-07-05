/*
 * i2s_out — I2S master transmitter to the WROOM32 slave-RX (SPEC §3.3):
 * Philips, 16-bit data in 32-bit slots, stereo, 44.1 kHz, MCLK unused.
 * BCLK=GPIO5, WS=GPIO6, DOUT=GPIO7.
 *
 * Split for testability (SIG-1b):
 *   - i2s_out_pump_once() is a PURE function (drain ring, zero-fill shortfall,
 *     sink) — host-tested against a mock sink.
 *   - the device lifecycle (channel config + writer task) is thin IDF glue
 *     built on the pump, verified on hardware at SIG-1c.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pcm_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/* §3.3 contract */
#define I2S_OUT_SAMPLE_RATE_HZ   44100
#define I2S_OUT_BITS_PER_SAMPLE  16
#define I2S_OUT_CHANNELS         2
#define I2S_OUT_GPIO_BCLK        5
#define I2S_OUT_GPIO_WS          6
#define I2S_OUT_GPIO_DOUT        7

typedef struct {
    uint64_t bytes_written;    /* total bytes pushed to the sink */
    uint64_t underrun_bytes;   /* total zero-filled bytes */
    uint32_t underrun_events;  /* pumps that had to zero-fill */
    size_t   ring_peak;        /* peak ring fill observed */
} i2s_out_stats_t;

/* Sink callback: consume exactly `len` bytes. Return 0 on success, <0 on error. */
typedef int (*i2s_out_sink_fn)(void *ctx, const uint8_t *data, size_t len);

/*
 * Pure pump (host-testable): drain up to `block_len` bytes from `ring` into
 * `scratch`, zero-fill any shortfall (underrun accounting), then hand the full
 * `block_len`-byte block to `sink`. Always presents a full block downstream so
 * the I2S clock never starves. Returns the number of *real* (non-zero-filled)
 * bytes drained from the ring. `scratch` must be at least `block_len` bytes.
 */
size_t i2s_out_pump_once(pcm_ring_t *ring, uint8_t *scratch, size_t block_len,
                         i2s_out_sink_fn sink, void *ctx,
                         i2s_out_stats_t *stats);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* Device lifecycle (IDF only). */
esp_err_t i2s_out_init(size_t ring_capacity_bytes);  /* config I2S + alloc ring */
esp_err_t i2s_out_start(void);                        /* enable + spawn writer */
esp_err_t i2s_out_stop(void);
size_t    i2s_out_write(const uint8_t *data, size_t len);  /* producer -> ring */
void      i2s_out_get_stats(i2s_out_stats_t *out);
#endif /* ESP_PLATFORM */

#ifdef __cplusplus
}
#endif

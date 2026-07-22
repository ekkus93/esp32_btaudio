/*
 * i2s_out — I2S device glue (SPEC §3.3 contract, TODO Phase 3): ESP32-S3
 * slave transmitter, 44.1 kHz stereo, Philips I2S, 32-bit slots containing
 * signed 16-bit PCM in bits 31..16. WROOM32 is the clock master (drives
 * BCLK+WS); the S3 shifts data out synced to that external clock.
 * BCLK=GPIO15, WS=GPIO16, DOUT=GPIO7.
 *
 * Split for testability (SIG-1b):
 *   - i2s_pending_advance() (below) is a PURE function covering the tricky
 *     "how many real vs zero-filled bytes did a partial write actually
 *     accept" arithmetic — host-tested directly.
 *   - the device lifecycle (channel config + writer task, which calls
 *     pcm_ring_peek()/pcm_ring_consume() and i2s_channel_write() directly —
 *     no sink-callback indirection) is thin IDF glue, verified on hardware.
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
/* DBG pin test: the slave provably syncs with its clock arriving on 15/16
 * (on-chip loopback) and never on 5/6 — feed the WROOM32's clock into the
 * proven pins to separate "bad pins" from "bad clock". */
#define I2S_OUT_GPIO_BCLK        15
#define I2S_OUT_GPIO_WS          16
#define I2S_OUT_GPIO_DOUT        7

/* Lifecycle state (TODO 3.1/3.7). FAULTED_JOIN_PENDING means the writer
 * task may still be running and may still access s_ring/s_tx_chan/s_events
 * — deinit is forbidden in this state; only a retried stop()/start() may
 * attempt the join again. */
typedef enum {
    I2S_STATE_UNINITIALIZED = 0,
    I2S_STATE_IDLE,
    I2S_STATE_STARTING,
    I2S_STATE_RUNNING,
    I2S_STATE_WAITING_FOR_CLOCK,
    I2S_STATE_STOPPING,
    I2S_STATE_FAULTED,
    I2S_STATE_FAULTED_JOIN_PENDING,
} i2s_out_state_t;

typedef struct {
    uint64_t bytes_written;     /* total bytes actually accepted by i2s_channel_write() */
    uint64_t underrun_bytes;    /* of bytes_written, how many were zero-fill (ring was short) */
    uint64_t underrun_events;   /* writes that included any zero-fill */
    uint64_t write_timeouts;    /* i2s_channel_write() returned ESP_ERR_TIMEOUT */
    uint64_t write_errors;      /* i2s_channel_write() returned any other non-OK */
    uint64_t partial_writes;    /* writes where accepted < requested */
    uint64_t source_drop_bytes; /* reserved: bytes dropped upstream of this component */
    size_t   ring_used;         /* ring occupancy at last stats snapshot */
    size_t   ring_capacity;
    size_t   ring_peak;         /* peak ring fill observed */
    i2s_out_state_t state;
    int      last_error;        /* esp_err_t of the most recent write_errors event, or 0 */
} i2s_out_stats_t;

/* Pure pending-block arithmetic (TODO 3.4/3.5, host-testable): `pending` holds
 * `*pending_len` bytes queued for the driver, of which the leading
 * `*pending_real` bytes are real ring data (the rest, if any, is zero-fill
 * padding already appended by the caller). Given that the driver just
 * accepted `written` (<= *pending_len) bytes of `pending`, shift the
 * remainder to the front of `pending` and update both lengths in place.
 * Returns how many REAL bytes were part of the accepted prefix — the caller
 * consumes exactly that many bytes from the ring, never more (a partial
 * write into the zero-fill tail must not touch real ring data). */
size_t i2s_pending_advance(uint8_t *pending, size_t *pending_len,
                           size_t *pending_real, size_t written);

/* Pure pre-I2S volume: scale `count` interleaved int16 samples in place by
 * `pct` (0..100). pct>=100 is a no-op (unity), pct<=0 mutes. Host-tested. */
void i2s_out_apply_gain(int16_t *samples, size_t count, int pct);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* Device lifecycle (IDF only). init/start/stop/deinit are idempotent: a
 * repeat call in a state where the requested transition is already the
 * current state returns ESP_OK without touching hardware; a call in an
 * incompatible state returns ESP_ERR_INVALID_STATE. */
esp_err_t i2s_out_init(size_t ring_capacity_bytes);  /* config I2S + alloc ring */
esp_err_t i2s_out_start(void);                        /* enable + spawn writer */
esp_err_t i2s_out_stop(void);
esp_err_t i2s_out_deinit(void);
size_t    i2s_out_write(const uint8_t *data, size_t len);  /* producer -> ring */
void      i2s_out_get_stats(i2s_out_stats_t *out);
i2s_out_state_t i2s_out_get_state(void);

/* Pre-I2S software volume state (0..100 %). The audio_out feeder reads
 * i2s_out_get_gain() and applies it via i2s_out_apply_gain() to the mixed PCM
 * before I2S — a source-side trim independent of the WROOM32's post-mix VOLUME.
 * NVS-persisted; default 30%. set clamps to [0,100] and persists transactionally
 * (RAM value only publishes after a successful NVS commit).
 * Returns ESP_OK on success, or the NVS error if persistence failed. */
void       i2s_out_gain_load(void);   /* restore persisted gain; called by init */
esp_err_t  i2s_out_set_gain(int pct);
int        i2s_out_get_gain(void);

/* ---- Test injection hooks (TODO 3.7) ----
 * Since a mocked xTaskCreate() does not actually run the writer, host tests
 * simulate what the writer would publish via these. uint32_t (not
 * EventBits_t) to keep FreeRTOS event-group types out of this header. Bit
 * values match I2S_EVT_WRITER_* in i2s_out.c. */
void i2s_test_inject_writer_state(i2s_out_state_t state, esp_err_t err);
void i2s_test_inject_writer_bits(uint32_t bits);
#define I2S_TEST_EVT_WRITER_ENTERED ((uint32_t)1)
#define I2S_TEST_EVT_WRITER_READY   ((uint32_t)2)
#define I2S_TEST_EVT_WRITER_EXITED  ((uint32_t)4)

#ifdef UNIT_TEST
/* Test-isolation only (never compiled into production): force every
 * module static back to its pre-init state regardless of the current
 * lifecycle state, so each test starts clean. Unlike the injection hooks
 * above, this deliberately bypasses the safety gates under test — it
 * exists for teardown() between independent test cases, not to exercise
 * any real lifecycle path. */
void i2s_test_reset_module_state(void);
#endif
#endif /* ESP_PLATFORM */

#ifdef __cplusplus
}
#endif

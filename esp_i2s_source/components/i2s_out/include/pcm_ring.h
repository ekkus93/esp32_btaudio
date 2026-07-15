/*
 * pcm_ring — lock-free SPSC byte ring for PCM (SIG-1b).
 *
 * Single-producer / single-consumer only. The producer owns `head`, the
 * consumer owns `tail`; each is a _Atomic size_t with acquire/release
 * ordering, so the cross-core visibility of the latest index is guaranteed
 * without a lock. A wasted slot disambiguates full vs empty.
 *
 *   Producer: pcm_ring_write()      Consumer: pcm_ring_read()
 *
 * Backing buffer is PSRAM on device (use_psram) — sized >=256 KB for radio
 * jitter absorption — and plain malloc on host.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcm_ring pcm_ring_t;

/* Backing-memory policy (SPEC §6.7 / TODO 3.2). REQUIRED must never silently
 * fall back to internal RAM — a large ring landing in internal heap can
 * starve unrelated allocations. PREFERRED may fall back; INTERNAL_ONLY never
 * requests PSRAM (used on host, where PSRAM is meaningless). */
typedef enum {
    PCM_RING_INTERNAL_ONLY = 0,
    PCM_RING_PSRAM_REQUIRED,
    PCM_RING_PSRAM_PREFERRED,
} pcm_ring_memory_t;

/* Create a ring with `capacity` usable bytes. Returns NULL on allocation
 * failure, on capacity == 0, or on capacity == SIZE_MAX (would overflow the
 * internal size = capacity + 1 wasted-slot accounting). PCM_RING_PSRAM_REQUIRED
 * returns NULL rather than falling back to internal RAM if PSRAM allocation
 * fails. `memory` is ignored on host (plain malloc). */
pcm_ring_t *pcm_ring_create(size_t capacity, pcm_ring_memory_t memory);
void pcm_ring_destroy(pcm_ring_t *r);

/* Producer side: copy up to `len` bytes in; returns bytes actually written
 * (< len when the ring fills). */
size_t pcm_ring_write(pcm_ring_t *r, const uint8_t *src, size_t len);

/* Consumer side: copy up to `len` bytes out; returns bytes actually read
 * (< len when the ring drains). */
size_t pcm_ring_read(pcm_ring_t *r, uint8_t *dst, size_t len);

/* Consumer side: copy up to `len` bytes out WITHOUT advancing tail (unlike
 * pcm_ring_read()). Returns bytes actually copied (< len when the ring has
 * less than `len` bytes available). Pair with pcm_ring_consume() once the
 * caller knows how many of the peeked bytes were actually accepted
 * downstream (TODO 3.3 — fixes I2S-002: don't drop ring bytes before
 * knowing whether the sink took them). Single consumer only. */
size_t pcm_ring_peek(const pcm_ring_t *r, uint8_t *dst, size_t len);

/* Consumer side: advance tail by up to `len` bytes (bounded by bytes
 * actually used). Returns bytes actually consumed. Single consumer only. */
size_t pcm_ring_consume(pcm_ring_t *r, size_t len);

size_t pcm_ring_used(const pcm_ring_t *r);
size_t pcm_ring_free(const pcm_ring_t *r);
size_t pcm_ring_capacity(const pcm_ring_t *r);
size_t pcm_ring_peak_used(const pcm_ring_t *r);

/* Consumer-side reset (drops buffered data). Not safe while the producer
 * runs concurrently — call only when the producer is quiesced. */
void pcm_ring_reset(pcm_ring_t *r);

#ifdef __cplusplus
}
#endif

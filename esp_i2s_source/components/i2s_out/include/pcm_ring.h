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

/* Create a ring with `capacity` usable bytes (returns NULL on alloc failure).
 * use_psram requests PSRAM backing on device; ignored on host. */
pcm_ring_t *pcm_ring_create(size_t capacity, bool use_psram);
void pcm_ring_destroy(pcm_ring_t *r);

/* Producer side: copy up to `len` bytes in; returns bytes actually written
 * (< len when the ring fills). */
size_t pcm_ring_write(pcm_ring_t *r, const uint8_t *src, size_t len);

/* Consumer side: copy up to `len` bytes out; returns bytes actually read
 * (< len when the ring drains). */
size_t pcm_ring_read(pcm_ring_t *r, uint8_t *dst, size_t len);

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

#include "hfp_pcm_ring.h"

#include <limits.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "HFP PCM ring requires lock-free 32-bit integer atomics");
_Static_assert(sizeof(uint32_t) == sizeof(unsigned int),
               "HFP PCM ring expects 32-bit unsigned int atomics");

static void counter64_init(hfp_pcm_counter64_t *counter)
{
    atomic_init(&counter->sequence, 0U);
    atomic_init(&counter->low, 0U);
    atomic_init(&counter->high, 0U);
}

/* Single-writer counter update. The sequence makes a concurrent snapshot
 * retry instead of observing a torn 64-bit value on the 32-bit target. */
static void counter64_add(hfp_pcm_counter64_t *counter, uint64_t amount)
{
    (void)atomic_fetch_add_explicit(&counter->sequence, 1U,
                                    memory_order_acq_rel);
    uint64_t current =
        ((uint64_t)atomic_load_explicit(&counter->high,
                                        memory_order_relaxed) << 32U) |
        atomic_load_explicit(&counter->low, memory_order_relaxed);
    current += amount;
    atomic_store_explicit(&counter->low, (uint32_t)current,
                          memory_order_relaxed);
    atomic_store_explicit(&counter->high, (uint32_t)(current >> 32U),
                          memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&counter->sequence, 1U,
                                    memory_order_release);
}

static uint64_t counter64_read(const hfp_pcm_counter64_t *counter)
{
    for (;;) {
        uint32_t before = atomic_load_explicit(&counter->sequence,
                                               memory_order_acquire);
        if ((before & 1U) != 0U) {
            continue;
        }
        uint32_t low = atomic_load_explicit(&counter->low,
                                            memory_order_relaxed);
        uint32_t high = atomic_load_explicit(&counter->high,
                                             memory_order_relaxed);
        uint32_t after = atomic_load_explicit(&counter->sequence,
                                              memory_order_acquire);
        if (before == after) {
            return ((uint64_t)high << 32U) | low;
        }
    }
}

static bool ring_initialized(const hfp_pcm_ring_t *ring)
{
    return ring != NULL &&
           atomic_load_explicit(&ring->initialized, memory_order_acquire);
}

static bool producer_generation_valid(hfp_pcm_ring_t *ring,
                                      uint32_t generation)
{
    uint32_t current = atomic_load_explicit(&ring->generation,
                                            memory_order_acquire);
    if (generation == 0U || generation != current) {
        counter64_add(&ring->stale_write_operations, 1U);
        return false;
    }
    return true;
}

static bool consumer_generation_valid(hfp_pcm_ring_t *ring,
                                      uint32_t generation)
{
    uint32_t current = atomic_load_explicit(&ring->generation,
                                            memory_order_acquire);
    if (generation == 0U || generation != current) {
        counter64_add(&ring->stale_read_operations, 1U);
        return false;
    }
    return true;
}

static void copy_into_ring(hfp_pcm_ring_t *ring,
                           uint32_t head,
                           const uint8_t *src,
                           size_t bytes)
{
    size_t offset = (size_t)(head % (uint32_t)ring->capacity);
    size_t first = bytes;
    if (first > ring->capacity - offset) {
        first = ring->capacity - offset;
    }
    memcpy(ring->storage + offset, src, first);
    if (bytes > first) {
        memcpy(ring->storage, src + first, bytes - first);
    }
}

static void copy_from_ring(const hfp_pcm_ring_t *ring,
                           uint32_t tail,
                           uint8_t *dst,
                           size_t bytes)
{
    size_t offset = (size_t)(tail % (uint32_t)ring->capacity);
    size_t first = bytes;
    if (first > ring->capacity - offset) {
        first = ring->capacity - offset;
    }
    memcpy(dst, ring->storage + offset, first);
    if (bytes > first) {
        memcpy(dst + first, ring->storage, bytes - first);
    }
}

static void update_peak(hfp_pcm_ring_t *ring, uint32_t used)
{
    uint32_t peak = atomic_load_explicit(&ring->peak_used,
                                         memory_order_relaxed);
    while (used > peak &&
           !atomic_compare_exchange_weak_explicit(&ring->peak_used,
                                                  &peak,
                                                  used,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

esp_err_t hfp_pcm_ring_init(hfp_pcm_ring_t *ring,
                            uint8_t *storage,
                            size_t capacity,
                            uint32_t generation)
{
    if (ring == NULL || storage == NULL || capacity == 0U ||
        capacity > (size_t)(UINT32_MAX / 2U) || generation == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ring, 0, sizeof(*ring));
    ring->storage = storage;
    ring->capacity = capacity;
    atomic_init(&ring->generation, generation);
    atomic_init(&ring->head, 0U);
    atomic_init(&ring->tail, 0U);
    atomic_init(&ring->peak_used, 0U);
    atomic_init(&ring->initialized, false);

    counter64_init(&ring->total_written_bytes);
    counter64_init(&ring->overflow_frames);
    counter64_init(&ring->overflow_bytes);
    counter64_init(&ring->stale_write_operations);
    counter64_init(&ring->invalid_write_operations);
    counter64_init(&ring->total_read_bytes);
    counter64_init(&ring->underflow_events);
    counter64_init(&ring->underflow_bytes);
    counter64_init(&ring->stale_read_operations);
    counter64_init(&ring->invalid_read_operations);

    atomic_store_explicit(&ring->initialized, true, memory_order_release);
    return ESP_OK;
}

bool hfp_pcm_ring_write_frame(hfp_pcm_ring_t *ring,
                              const void *src,
                              size_t frame_bytes,
                              uint32_t generation)
{
    if (!ring_initialized(ring)) {
        return false;
    }
    if (src == NULL || frame_bytes == 0U || frame_bytes > ring->capacity) {
        counter64_add(&ring->invalid_write_operations, 1U);
        return false;
    }
    if (!producer_generation_valid(ring, generation)) {
        return false;
    }

    uint32_t head = atomic_load_explicit(&ring->head,
                                         memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail,
                                         memory_order_acquire);
    uint32_t used = head - tail;
    if (used > ring->capacity || frame_bytes > ring->capacity - used) {
        counter64_add(&ring->overflow_frames, 1U);
        counter64_add(&ring->overflow_bytes, frame_bytes);
        return false;
    }

    copy_into_ring(ring, head, (const uint8_t *)src, frame_bytes);
    uint32_t new_head = head + (uint32_t)frame_bytes;
    atomic_store_explicit(&ring->head, new_head, memory_order_release);
    counter64_add(&ring->total_written_bytes, frame_bytes);
    update_peak(ring, used + (uint32_t)frame_bytes);
    return true;
}

size_t hfp_pcm_ring_read(hfp_pcm_ring_t *ring,
                         void *dst,
                         size_t requested_bytes,
                         uint32_t generation)
{
    if (!ring_initialized(ring)) {
        return 0U;
    }
    if (dst == NULL || requested_bytes == 0U) {
        counter64_add(&ring->invalid_read_operations, 1U);
        return 0U;
    }
    if (!consumer_generation_valid(ring, generation)) {
        return 0U;
    }

    uint32_t tail = atomic_load_explicit(&ring->tail,
                                         memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head,
                                         memory_order_acquire);
    uint32_t available = head - tail;
    if (available > ring->capacity) {
        counter64_add(&ring->invalid_read_operations, 1U);
        return 0U;
    }

    size_t to_read = requested_bytes;
    if (to_read > available) {
        to_read = available;
        counter64_add(&ring->underflow_events, 1U);
        counter64_add(&ring->underflow_bytes, requested_bytes - to_read);
    }
    if (to_read == 0U) {
        return 0U;
    }

    copy_from_ring(ring, tail, (uint8_t *)dst, to_read);
    atomic_store_explicit(&ring->tail, tail + (uint32_t)to_read,
                          memory_order_release);
    counter64_add(&ring->total_read_bytes, to_read);
    return to_read;
}

esp_err_t hfp_pcm_ring_reset(hfp_pcm_ring_t *ring,
                             uint32_t new_generation,
                             bool endpoints_stopped)
{
    if (!ring_initialized(ring)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!endpoints_stopped || new_generation == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t current = atomic_load_explicit(&ring->generation,
                                            memory_order_acquire);
    if (new_generation == current) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store_explicit(&ring->head, 0U, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0U, memory_order_relaxed);
    atomic_store_explicit(&ring->peak_used, 0U, memory_order_relaxed);
    atomic_store_explicit(&ring->generation, new_generation,
                          memory_order_release);
    return ESP_OK;
}

esp_err_t hfp_pcm_ring_get_snapshot(const hfp_pcm_ring_t *ring,
                                    hfp_pcm_ring_snapshot_t *out)
{
    if (ring == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->initialized = atomic_load_explicit(&ring->initialized,
                                            memory_order_acquire);
    if (!out->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    out->generation = atomic_load_explicit(&ring->generation,
                                           memory_order_acquire);
    out->capacity = ring->capacity;

    for (;;) {
        uint32_t tail_before = atomic_load_explicit(&ring->tail,
                                                    memory_order_acquire);
        uint32_t head = atomic_load_explicit(&ring->head,
                                             memory_order_acquire);
        uint32_t tail_after = atomic_load_explicit(&ring->tail,
                                                   memory_order_acquire);
        uint32_t used = head - tail_before;
        if (tail_before == tail_after && used <= ring->capacity) {
            out->current_used = used;
            out->current_free = ring->capacity - used;
            break;
        }
    }

    out->peak_used = atomic_load_explicit(&ring->peak_used,
                                          memory_order_relaxed);
    out->total_written_bytes = counter64_read(&ring->total_written_bytes);
    out->total_read_bytes = counter64_read(&ring->total_read_bytes);
    out->overflow_frames = counter64_read(&ring->overflow_frames);
    out->overflow_bytes = counter64_read(&ring->overflow_bytes);
    out->underflow_events = counter64_read(&ring->underflow_events);
    out->underflow_bytes = counter64_read(&ring->underflow_bytes);
    out->stale_generation_operations =
        counter64_read(&ring->stale_write_operations) +
        counter64_read(&ring->stale_read_operations);
    out->invalid_operations =
        counter64_read(&ring->invalid_write_operations) +
        counter64_read(&ring->invalid_read_operations);
    return ESP_OK;
}

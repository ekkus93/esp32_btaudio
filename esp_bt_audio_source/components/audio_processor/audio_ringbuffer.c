/**
 * @file audio_ringbuffer.c
 * @brief SPSC ring buffer implementation
 *
 * ⚠️  SINGLE PRODUCER SINGLE CONSUMER (SPSC) ONLY ⚠️
 * 
 * This implementation is NOT thread-safe for:
 *   - Multiple producers calling audio_rb_write()
 *   - Multiple consumers calling audio_rb_read()
 *   - MPMC (multi-producer multi-consumer) scenarios
 *
 * Current usage in this project (CORRECT):
 *   - Producer: audio_engine_task (main.c) - ONE task only
 *   - Consumer: BT A2DP callback (bt_app_core.c) - ONE callback context only
 *
 * If you need MPMC, you must:
 *   - Use FreeRTOS queue instead, OR
 *   - Add external synchronization (mutex/semaphore), OR
 *   - Use lock-free MPMC ring buffer (more complex)
 *
 * Implementation details:
 * - head: write position (producer advances)
 * - tail: read position (consumer advances)
 * - used_bytes: current occupancy (disambiguates full vs empty)
 * - capacity: total buffer size
 * - peak_used: high-water mark for sizing analysis
 *
 * Synchronization strategy (SPSC only):
 * - Very short critical sections (just state updates)
 * - No blocking operations
 * - Suitable for ISR-like contexts (A2DP callback)
 *
 * Memory layout:
 * - Ring buffer struct allocated in DRAM (small, frequently accessed)
 * - Backing buffer allocated per use_psram flag (large, bulk storage)
 *
 * CODE_REVIEW6 Phase 1, Task 1.1
 * CODE_REVIEW7 Priority 4, Task 4.1 (SPSC contract documentation)
 */

#include "audio_ringbuffer.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#ifndef UNIT_TEST
#include "freertos/portmacro.h"
#else
#include "freertos/semphr.h"  /* For portENTER_CRITICAL/portEXIT_CRITICAL macros */
#endif
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "audio_rb";

/**
 * Spinlock for critical sections
 * 
 * WHY: Protect concurrent access to ring buffer state (head, tail, used_bytes)
 * HOW: Very short critical sections (just pointer arithmetic)
 * CORRECTNESS: Safe for ISR-like contexts (A2DP callback)
 */
static portMUX_TYPE s_rb_spinlock = portMUX_INITIALIZER_UNLOCKED;

/**
 * Ring buffer structure (internal)
 *
 * WHY separate struct from handle: allows opaque type in header
 * HOW: classic circular buffer with used_bytes for full/empty detection
 * CORRECTNESS: Critical sections protect concurrent access
 */
struct audio_rb {
    uint8_t *buffer;        ///< Backing buffer (DRAM or PSRAM)
    size_t   capacity;      ///< Total buffer size in bytes
    size_t   head;          ///< Write position (0 to capacity-1)
    size_t   tail;          ///< Read position (0 to capacity-1)
    size_t   used_bytes;    ///< Current occupancy
    size_t   peak_used;     ///< High-water mark
    bool     use_psram;     ///< Allocation type (for deinit)
};

esp_err_t audio_rb_init(audio_rb_t **rb, size_t capacity_bytes, bool use_psram)
{
    if (rb == NULL || capacity_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Allocate ring buffer structure (always DRAM, small) */
    audio_rb_t *new_rb = heap_caps_malloc(sizeof(audio_rb_t), MALLOC_CAP_8BIT);
    if (new_rb == NULL) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer struct");
        return ESP_ERR_NO_MEM;
    }

    /* Allocate backing buffer (DRAM or PSRAM) */
    uint32_t caps = use_psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : MALLOC_CAP_8BIT;
    uint8_t *buffer = heap_caps_malloc(capacity_bytes, caps);
    if (buffer == NULL) {
        /* If PSRAM allocation fails, try DRAM fallback */
        if (use_psram) {
            ESP_LOGW(TAG, "PSRAM allocation failed, falling back to DRAM");
            buffer = heap_caps_malloc(capacity_bytes, MALLOC_CAP_8BIT);
            use_psram = false;  /* Update flag to reflect actual allocation */
        }
        
        if (buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate ring buffer backing store (%zu bytes)", capacity_bytes);
            heap_caps_free(new_rb);
            return ESP_ERR_NO_MEM;
        }
    }

    /* Initialize ring buffer state */
    new_rb->buffer = buffer;
    new_rb->capacity = capacity_bytes;
    new_rb->head = 0;
    new_rb->tail = 0;
    new_rb->used_bytes = 0;
    new_rb->peak_used = 0;
    new_rb->use_psram = use_psram;

    *rb = new_rb;

    ESP_LOGI(TAG, "Ring buffer initialized: %zu bytes (%s)",
             capacity_bytes, use_psram ? "PSRAM" : "DRAM");

    return ESP_OK;
}

void audio_rb_deinit(audio_rb_t *rb)
{
    if (rb == NULL) {
        return;
    }

    if (rb->buffer != NULL) {
        heap_caps_free(rb->buffer);
    }

    heap_caps_free(rb);
}

size_t audio_rb_write(audio_rb_t *rb, const uint8_t *src, size_t len)
{
    if (rb == NULL || src == NULL || len == 0) {
        return 0;
    }

    /* Calculate available space (non-critical, snapshot only) */
    size_t available = rb->capacity - rb->used_bytes;
    if (available == 0) {
        return 0;  /* Buffer full */
    }

    /* Clamp write length to available space */
    size_t to_write = (len < available) ? len : available;

    /* Handle wrap-around: may need two memcpy operations */
    size_t first_chunk = to_write;
    size_t wrap_chunk = 0;

    if (rb->head + to_write > rb->capacity) {
        /* Write wraps around end of buffer */
        first_chunk = rb->capacity - rb->head;
        wrap_chunk = to_write - first_chunk;
    }

    /* First chunk: head to end of buffer (or complete write if no wrap) */
    // NOLINTBEGIN(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Bounds checked above: first_chunk + head <= capacity
    memcpy(rb->buffer + rb->head, src, first_chunk);
    // NOLINTEND(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)

    /* Second chunk: wrap-around to beginning of buffer (if needed) */
    if (wrap_chunk > 0) {
        // NOLINTBEGIN(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        // Bounds checked above: wrap_chunk <= capacity
        memcpy(rb->buffer, src + first_chunk, wrap_chunk);
        // NOLINTEND(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    }

    /* Update state in critical section (very short) */
    portENTER_CRITICAL(&s_rb_spinlock);
    {
        rb->head = (rb->head + to_write) % rb->capacity;
        rb->used_bytes += to_write;

        /* Track peak usage */
        if (rb->used_bytes > rb->peak_used) {
            rb->peak_used = rb->used_bytes;
        }
    }
    portEXIT_CRITICAL(&s_rb_spinlock);

    return to_write;
}

size_t audio_rb_read(audio_rb_t *rb, uint8_t *dst, size_t len)
{
    if (rb == NULL || dst == NULL || len == 0) {
        return 0;
    }

    /* Calculate available data (non-critical, snapshot only) */
    size_t available = rb->used_bytes;
    if (available == 0) {
        return 0;  /* Buffer empty */
    }

    /* Clamp read length to available data */
    size_t to_read = (len < available) ? len : available;

    /* Handle wrap-around: may need two memcpy operations */
    size_t first_chunk = to_read;
    size_t wrap_chunk = 0;

    if (rb->tail + to_read > rb->capacity) {
        /* Read wraps around end of buffer */
        first_chunk = rb->capacity - rb->tail;
        wrap_chunk = to_read - first_chunk;
    }

    /* First chunk: tail to end of buffer (or complete read if no wrap) */
    // NOLINTBEGIN(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Bounds checked above: first_chunk + tail <= capacity
    memcpy(dst, rb->buffer + rb->tail, first_chunk);
    // NOLINTEND(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)

    /* Second chunk: wrap-around from beginning of buffer (if needed) */
    if (wrap_chunk > 0) {
        // NOLINTBEGIN(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        // Bounds checked above: wrap_chunk <= capacity
        memcpy(dst + first_chunk, rb->buffer, wrap_chunk);
        // NOLINTEND(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    }

    /* Update state in critical section (very short) */
    portENTER_CRITICAL(&s_rb_spinlock);
    {
        rb->tail = (rb->tail + to_read) % rb->capacity;
        rb->used_bytes -= to_read;
    }
    portEXIT_CRITICAL(&s_rb_spinlock);

    return to_read;
}

size_t audio_rb_available_to_read(const audio_rb_t *rb)
{
    if (rb == NULL) {
        return 0;
    }

    /* Snapshot read (no critical section needed for single size_t read) */
    return rb->used_bytes;
}

size_t audio_rb_available_to_write(const audio_rb_t *rb)
{
    if (rb == NULL) {
        return 0;
    }

    /* Snapshot read (no critical section needed for single size_t read) */
    return rb->capacity - rb->used_bytes;
}

size_t audio_rb_capacity(const audio_rb_t *rb)
{
    if (rb == NULL) {
        return 0;
    }

    return rb->capacity;
}

size_t audio_rb_peak_used(const audio_rb_t *rb)
{
    if (rb == NULL) {
        return 0;
    }

    /* Snapshot read (no critical section needed for single size_t read) */
    return rb->peak_used;
}

void audio_rb_reset_stats(audio_rb_t *rb)
{
    if (rb == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_rb_spinlock);
    {
        rb->peak_used = rb->used_bytes;  /* Reset to current usage, not zero */
    }
    portEXIT_CRITICAL(&s_rb_spinlock);
}

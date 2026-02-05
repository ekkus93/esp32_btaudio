/**
 * @file audio_span_log.c
 * @brief Span log implementation for audio engine metadata tracking
 *
 * WHY: Debug visibility into audio engine behavior without coupling to audio data
 * HOW: Circular buffer of span entries with simple append/query API
 * CORRECTNESS: Thread-safe via portENTER_CRITICAL, non-blocking append
 *
 * CODE_REVIEW6 Phase 4, Task 4.1
 */

#include "audio_span_log.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define TAG "SPAN_LOG"

/**
 * Span log state (internal)
 */
typedef struct {
    audio_rb_span_t *entries;   /**< Circular buffer of span entries */
    size_t capacity;             /**< Maximum entries */
    size_t head;                 /**< Write index (next write position) */
    size_t count;                /**< Valid entries (0 to capacity) */
    portMUX_TYPE lock;           /**< Critical section lock */
    bool initialized;            /**< Init guard */
} span_log_state_t;

/* Global span log state */
static span_log_state_t s_span_log = {
    .entries = NULL,
    .capacity = 0,
    .head = 0,
    .count = 0,
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .initialized = false
};

bool span_log_init(size_t max_entries)
{
    if (s_span_log.initialized) {
        ESP_LOGW(TAG, "span_log already initialized");
        return false;
    }

    if (max_entries == 0) {
        ESP_LOGE(TAG, "span_log_init: max_entries must be > 0");
        return false;
    }

    /* Allocate span entry buffer (DRAM for fast access, small size ~4-8KB) */
    size_t alloc_size = max_entries * sizeof(audio_rb_span_t);
    s_span_log.entries = (audio_rb_span_t *)heap_caps_malloc(alloc_size, MALLOC_CAP_8BIT);
    
    if (s_span_log.entries == NULL) {
        ESP_LOGE(TAG, "span_log_init: failed to allocate %zu bytes", alloc_size);
        return false;
    }

    s_span_log.capacity = max_entries;
    s_span_log.head = 0;
    s_span_log.count = 0;
    s_span_log.initialized = true;

    ESP_LOGI(TAG, "span_log initialized: %zu entries (%zu bytes)", 
             max_entries, alloc_size);
    return true;
}

void span_log_deinit(void)
{
    portENTER_CRITICAL(&s_span_log.lock);
    
    if (s_span_log.entries != NULL) {
        free(s_span_log.entries);
        s_span_log.entries = NULL;
    }
    
    s_span_log.capacity = 0;
    s_span_log.head = 0;
    s_span_log.count = 0;
    s_span_log.initialized = false;
    
    portEXIT_CRITICAL(&s_span_log.lock);
}

void span_log_push(uint32_t seq, uint32_t timestamp_ms, size_t bytes,
                   size_t ring_used, uint8_t source, uint8_t flags)
{
    if (!s_span_log.initialized) {
        return;  /* Silently ignore if not initialized (span log is optional) */
    }

    portENTER_CRITICAL(&s_span_log.lock);
    
    /* Write to current head position (overwrites oldest if full) */
    audio_rb_span_t *entry = &s_span_log.entries[s_span_log.head];
    entry->seq = seq;
    entry->timestamp_ms = timestamp_ms;
    entry->bytes = bytes;
    entry->ring_used_kb = (uint16_t)((ring_used + 512) / 1024);  /* Round to KB */
    entry->source = source;
    entry->flags = flags;
    
    /* Advance head (wrap around) */
    s_span_log.head = (s_span_log.head + 1) % s_span_log.capacity;
    
    /* Update count (saturate at capacity) */
    if (s_span_log.count < s_span_log.capacity) {
        s_span_log.count++;
    }
    
    portEXIT_CRITICAL(&s_span_log.lock);
}

bool span_log_get_last_n(audio_rb_span_t *out, size_t n, size_t *actual)
{
    if (!s_span_log.initialized || out == NULL) {
        if (actual != NULL) {
            *actual = 0;
        }
        return false;
    }

    portENTER_CRITICAL(&s_span_log.lock);
    
    /* Determine how many entries to copy (min of requested and available) */
    size_t to_copy = (n < s_span_log.count) ? n : s_span_log.count;
    
    if (to_copy == 0) {
        portEXIT_CRITICAL(&s_span_log.lock);
        if (actual != NULL) {
            *actual = 0;
        }
        return true;
    }
    
    /* Calculate starting index (tail of circular buffer)
     * If buffer is full: tail = head (oldest entry)
     * If buffer is partial: tail = 0 (first entry written) */
    size_t tail;
    if (s_span_log.count == s_span_log.capacity) {
        tail = s_span_log.head;  /* Full: oldest is at head position */
    } else {
        tail = 0;  /* Partial: oldest is at index 0 */
    }
    
    /* Calculate actual start index (last N entries from tail)
     * If requesting more than available, start from tail
     * Otherwise, advance tail by (count - n) to get last N */
    size_t start_idx;
    if (to_copy == s_span_log.count) {
        start_idx = tail;  /* Requesting all available */
    } else {
        /* Advance tail to get last N entries */
        start_idx = (tail + (s_span_log.count - to_copy)) % s_span_log.capacity;
    }
    
    /* Copy entries (handle wrap-around) */
    size_t copied = 0;
    size_t idx = start_idx;
    while (copied < to_copy) {
        out[copied] = s_span_log.entries[idx];
        idx = (idx + 1) % s_span_log.capacity;
        copied++;
    }
    
    portEXIT_CRITICAL(&s_span_log.lock);
    
    if (actual != NULL) {
        *actual = to_copy;
    }
    return true;
}

void span_log_reset(void)
{
    if (!s_span_log.initialized) {
        return;
    }

    portENTER_CRITICAL(&s_span_log.lock);
    s_span_log.head = 0;
    s_span_log.count = 0;
    portEXIT_CRITICAL(&s_span_log.lock);
}

size_t span_log_capacity(void)
{
    if (!s_span_log.initialized) {
        return 0;
    }

    portENTER_CRITICAL(&s_span_log.lock);
    size_t cap = s_span_log.capacity;
    portEXIT_CRITICAL(&s_span_log.lock);
    
    return cap;
}

size_t span_log_count(void)
{
    if (!s_span_log.initialized) {
        return 0;
    }

    portENTER_CRITICAL(&s_span_log.lock);
    size_t cnt = s_span_log.count;
    portEXIT_CRITICAL(&s_span_log.lock);
    
    return cnt;
}

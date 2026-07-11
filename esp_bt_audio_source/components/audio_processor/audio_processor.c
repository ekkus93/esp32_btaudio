#ifdef _POSIX_READER_WRITER_LOCKS
// Clear toolchain define so newlib can set the standard value without a redefinition warning.
#undef _POSIX_READER_WRITER_LOCKS
#endif

#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
#include <stdatomic.h>

#include "audio_processor_internal.h"
#include "uart_source.h"
#include "util_safe.h"
#include "nvs_storage.h"
#include "esp_timer.h"
#include "platform_memory.h"
#include "audio_span_log.h"
#include "platform_timing.h"
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
#include "esp_psram.h"
#endif

#ifdef CONFIG_BT_MOCK_TESTING
typedef struct {
    bool enabled;
    uint32_t frame_counter;
} mock_i2s_state_t;

static mock_i2s_state_t s_mock_i2s_state = {0};

/* Simple mock I2S audio generator used for host-unit tests. Produces a
 * deterministic byte pattern so consumers can validate reads. Returns the
 * number of bytes written into `buffer`. */
static size_t mock_generate_i2s_audio(uint8_t* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }

    for (size_t i = 0; i < buffer_size; ++i) {
        buffer[i] = (uint8_t)((i + s_mock_i2s_state.frame_counter) & 0xFF);
    }

    s_mock_i2s_state.frame_counter += (uint32_t)(buffer_size / 2);
    return buffer_size;
}
#endif

static esp_err_t configure_i2s(const audio_config_t* config);

#ifdef UNIT_TEST
static bool s_test_force_beep_overlay_fail = false;
#endif

#ifndef UNIT_TEST
/**
 * Volume commit timer callback (CODE_REVIEW8 Task D)
 * 
 * WHY: Debounce rapid volume changes to reduce NVS flash wear
 * HOW: Triggered 500ms after last volume change, commits current value to NVS
 * CORRECTNESS: Only runs on timer expiration, safe to call nvs_storage from timer context
 */
static void volume_commit_timer_callback(void* arg)
{
    (void)arg;
    /* Commit current volume to NVS (deferred from audio_processor_set_volume) */
    nvs_storage_set_volume(s_volume_gain);
}

/**
 * Audio engine task (CODE_REVIEW6 Phase 2)
 * 
 * WHY: Single producer for ring buffer - centralizes source arbitration,
 *      eliminates multi-producer races, provides clean backpressure via watermarks
 * HOW: Runs at 2ms tick, produces 1KB chunks from active source into ring buffer
 *      Watermarks: pause at high (24KB), resume at low (8KB) for hysteresis
 * CORRECTNESS: Never blocks, respects watermarks, handles all source types
 */

/**
 * Get active audio source (Phase 2, Task 2.2)
 * 
 * Priority order (CODE_REVIEW7 Fix):
 *   1. SYNTH  - User explicitly requested synth mode via SYNTH ON command
 *   2. I2S    - Capture from I2S source (if manager running)
 *   3. SILENCE- Default fallback when no active source
 * 
 * RATIONALE: s_force_synth must take highest priority so users can override
 *            I2S capture with SYNTH mode at runtime (e.g., "START" then "SYNTH ON")
 */
typedef enum {
    AUDIO_SOURCE_I2S = 0,
    AUDIO_SOURCE_SYNTH,
    AUDIO_SOURCE_UART,     /* UARTAUDIO: laptop PCM stream over UART0 */
    AUDIO_SOURCE_SILENCE,
    NUM_AUDIO_SOURCES
} audio_source_t;

static audio_source_t s_last_source = AUDIO_SOURCE_SILENCE;

static audio_source_t get_active_source(void)
{
    /* F1.5.1: BEEP Priority - Return silence during beep (F1: BEEP Priority Mode)
     * WHY: BEEP must play pure, not mixed with I2S or SYNTH audio.
     * HOW: Check beep state before source priority, return SILENCE if active.
     *      beep_overlay_fill() will mix the beep tone over silence in produce_audio_chunk().
     * CORRECTNESS: beep_overlay_is_active() checks beep_manager state,
     *              s_beep_remaining_bytes tracks audio engine's beep duration estimate. */
    if (beep_overlay_is_active() || s_beep_remaining_bytes > 0) {
        return AUDIO_SOURCE_SILENCE;
    }

    /* Priority 1: Active UART audio stream (UARTAUDIO). Outranks forced
     * SYNTH: starting a stream is the most recent explicit user intent,
     * so SYNTH ON does not interrupt an in-progress stream. */
    if (uart_source_is_active()) {
        return AUDIO_SOURCE_UART;
    }

    /* Priority 2: Forced SYNTH mode (user explicitly requested via SYNTH ON) */
    if (s_force_synth) {
        return AUDIO_SOURCE_SYNTH;
    }

    /* Priority 3: I2S capture (if I2S manager is running) */
    if (i2s_manager_is_running()) {
        return AUDIO_SOURCE_I2S;
    }

    /* Priority 4: Silence as final fallback */
    return AUDIO_SOURCE_SILENCE;
}

/**
 * Produce audio chunk from active source with optional beep overlay (Phase 2, Task 2.3)
 * Returns bytes written to dst (may be less than dst_bytes at EOF or underrun)
 * Updates per-source stats (Phase 4, Task 4.2)
 */
static size_t produce_audio_chunk(uint8_t *dst, size_t dst_bytes)
{
    if (dst == NULL || dst_bytes == 0) {
        return 0;
    }
    
    audio_source_t base = get_active_source();


    /* Track source switches (Phase 4.2)
     * Protected by spinlock (CODE_REVIEW 2602101453, P1.2.4) */
    if (base != s_last_source) {
        portENTER_CRITICAL(&s_audio_stats_lock);
        s_audio_stats.source_switch_count++;
        portEXIT_CRITICAL(&s_audio_stats_lock);
        s_last_source = base;
    }
    
    size_t produced = 0;
    
    /* Produce base audio from active source
     * I2S uses i2s_source_fill() - real I2S capture ✅
     * Synth uses synth_source_fill() - synthetic tone ✅ */
    switch (base) {
        case AUDIO_SOURCE_I2S:
            produced = i2s_source_fill(dst, dst_bytes);
            break;
            
        case AUDIO_SOURCE_SYNTH:
            produced = synth_source_fill(dst, dst_bytes);
            break;

        case AUDIO_SOURCE_UART:
            produced = uart_source_fill(dst, dst_bytes);
            break;

        case AUDIO_SOURCE_SILENCE:
        default:
            util_safe_memset(dst, dst_bytes, 0, dst_bytes);
            produced = dst_bytes;
            break;
    }
    
    /* Track per-source bytes (Phase 4.2)
     * Protected by spinlock (CODE_REVIEW 2602101453, P1.2.4) */
    if (produced > 0 && base < NUM_AUDIO_SOURCES) {
        portENTER_CRITICAL(&s_audio_stats_lock);
        s_audio_stats.bytes_by_source[base] += produced;
        portEXIT_CRITICAL(&s_audio_stats_lock);
    }
    
    /* Mix beep overlay if active (CODE_REVIEW6 Phase 3.3)
     * WHY: Beep must mix over any base source (I2S, Synth, Silence)
     * HOW: beep_overlay_fill() modifies buffer in-place with clamped mixing
     * CORRECTNESS: beep_overlay_is_active() thread-safe check before mixing */
    bool beep_active = beep_overlay_is_active();
    if (beep_active && produced > 0) {
        beep_overlay_fill(dst, produced, &s_audio_config);
        
        /* Track beep overlay stats (Phase 4.2)
         * Protected by spinlock (CODE_REVIEW 2602101453, P1.2.4) */
        portENTER_CRITICAL(&s_audio_stats_lock);
        s_audio_stats.beep_overlay_count++;
        s_audio_stats.beep_overlay_bytes += produced;
        portEXIT_CRITICAL(&s_audio_stats_lock);

        /* Decrement remaining bytes for diagnostics/CLI gating. */
        portENTER_CRITICAL(&s_beep_lock);
        if (s_beep_remaining_bytes > produced) {
            s_beep_remaining_bytes -= produced;
        } else {
            s_beep_remaining_bytes = 0;
        }
        portEXIT_CRITICAL(&s_beep_lock);
    }
    
    return produced;
}

/**
 * Audio engine task main loop (Phase 2, Tasks 2.1 + 2.4)
 * Produces audio chunks into ring buffer, respecting watermarks
 * Tracks stats (Phase 4, Task 4.2)
 */
static void audio_engine_task(void *arg)
{
    (void)arg;
    
    TickType_t delay_ticks = pdMS_TO_TICKS(AUDIO_ENGINE_TICK_MS);
    if (delay_ticks == 0) {
        delay_ticks = 1;  /* Prevent zero-delay spin when tick rate is coarse */
    }
    
    /* Allocate chunk buffer (DMA-capable for future I2S reads) */
    uint8_t *chunk_buf = platform_malloc(AUDIO_ENGINE_CHUNK_BYTES, PLATFORM_MEM_CAP_DMA);
    if (chunk_buf == NULL) {
        ESP_LOGE(TAG, "audio_engine_task: failed to allocate chunk buffer");
#ifndef UNIT_TEST
        /* Signal stopped on error path so audio_processor_start() doesn't timeout (P0.1.5) */
        if (s_engine_events != NULL) {
            xEventGroupSetBits(s_engine_events, ENGINE_STOPPED_BIT);
        }
#endif
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "audio_engine_task: started (tick=%dms, chunk=%d bytes)", 
             AUDIO_ENGINE_TICK_MS, AUDIO_ENGINE_CHUNK_BYTES);
    
#ifndef UNIT_TEST
    /* Signal task is running (CODE_REVIEW 2602101453, P0.1.2/P0.1.5)
     * Allows audio_processor_start() to confirm successful startup */
    if (s_engine_events != NULL) {
        xEventGroupSetBits(s_engine_events, ENGINE_RUNNING_BIT);
    }
#endif
    
    for (;;) {
        /* Cooperative shutdown check (CODE_REVIEW 2602101453, P0.1.4)
         * Stop flag is set by audio_processor_stop() along with task notification.
         * Breaking here allows clean resource cleanup before self-delete.
         * WHY: Prevents deadlock/leak/corruption from external vTaskDelete(). */
        if (s_engine_stop_requested) {
            ESP_LOGI(TAG, "audio_engine_task: stop requested, breaking from main loop");
            break;
        }
        
        /* Watermark management (Phase 2, Task 2.4)
         * Check ring occupancy and apply hysteresis to prevent thrashing */
        size_t capacity = audio_rb_capacity(s_audio_ring);
        size_t free = audio_rb_available_to_write(s_audio_ring);
        size_t used = capacity - free;
        
        /* Track peak ring buffer usage (Phase 4.2)
         * Protected by spinlock (CODE_REVIEW 2602101453, P1.2.4) */
        portENTER_CRITICAL(&s_audio_stats_lock);
        if (used > s_audio_stats.ring_peak_used) {
            s_audio_stats.ring_peak_used = used;
        }
        portEXIT_CRITICAL(&s_audio_stats_lock);
        
        /* Watermark logic with pause tracking */
        bool was_paused = s_audio_engine_paused;
        if (used >= AUDIO_RB_HIGH_WATERMARK) {
            s_audio_engine_paused = true;
            if (!was_paused) {
                /* Protected by spinlock (CODE_REVIEW 2602101453, P1.2.4) */
                portENTER_CRITICAL(&s_audio_stats_lock);
                s_audio_stats.engine_pause_count++;  /* Count transitions to paused */
                portEXIT_CRITICAL(&s_audio_stats_lock);
            }
        }
        if (used <= AUDIO_RB_LOW_WATERMARK) {
            s_audio_engine_paused = false;
        }
        
        /* Produce audio if not paused and ring has space.
         * Multiple chunks per wake: the FreeRTOS tick (10 ms at 100 Hz)
         * is coarser than AUDIO_ENGINE_TICK_MS, so one chunk per wake
         * cannot keep up with the A2DP consumer — see
         * AUDIO_ENGINE_MAX_CHUNKS_PER_WAKE. Free space and the high
         * watermark are re-checked every iteration. */
        for (int chunk_n = 0; chunk_n < AUDIO_ENGINE_MAX_CHUNKS_PER_WAKE; chunk_n++) {
            free = audio_rb_available_to_write(s_audio_ring);
            used = capacity - free;
            if (used >= AUDIO_RB_HIGH_WATERMARK) {
                if (!s_audio_engine_paused) {
                    s_audio_engine_paused = true;
                    portENTER_CRITICAL(&s_audio_stats_lock);
                    s_audio_stats.engine_pause_count++;
                    portEXIT_CRITICAL(&s_audio_stats_lock);
                }
                break;
            }
            if (s_audio_engine_paused || free < AUDIO_ENGINE_CHUNK_BYTES) {
                break;
            }
            size_t produced = produce_audio_chunk(chunk_buf, AUDIO_ENGINE_CHUNK_BYTES);

            /* Live real-time I2S source: produced==0 just means the DMA is
             * mid-accumulation (it delivers at exactly real-time rate).
             * Stuffing silence here interleaved ~60% zeros into the stream
             * (measured via laptop A2DP capture — harsh chop). Stop producing
             * this wake instead; the real data arrives in time. */
            if (audio_engine_hold_for_live_i2s(produced,
                                               get_active_source() == AUDIO_SOURCE_I2S,
                                               i2s_manager_is_running())) {
                break;
            }

            /* Silence fallback: I2S source returns 0 on timeout (no audio input).
             * Fill with silence so the ring stays fed and the A2DP callback
             * doesn't underrun on every packet when no I2S source is connected. */
            if (produced == 0) {
                util_safe_memset(chunk_buf, AUDIO_ENGINE_CHUNK_BYTES, 0, AUDIO_ENGINE_CHUNK_BYTES);
                produced = AUDIO_ENGINE_CHUNK_BYTES;
                portENTER_CRITICAL(&s_audio_stats_lock);
                s_audio_stats.bytes_by_source[AUDIO_SOURCE_SILENCE] += produced;
                portEXIT_CRITICAL(&s_audio_stats_lock);
            }

            if (produced > 0) {
                size_t written = audio_rb_write(s_audio_ring, chunk_buf, produced);
                
                /* Track write stats (Phase 4.2)
                 * Protected by spinlock (CODE_REVIEW 2602101453, P1.2.4) */
                portENTER_CRITICAL(&s_audio_stats_lock);
                s_audio_stats.engine_write_calls++;
                s_audio_stats.engine_write_bytes += written;
                
                if (written < produced) {
                    /* Ring filled between check and write (rare but possible)
                     * Count as buffer overrun - producer ahead of consumer (CODE_REVIEW7 Priority 5, Task 5.1) */
                    s_audio_stats.buffer_overruns++;
                }
                uint32_t overruns_snapshot = s_audio_stats.buffer_overruns;
                portEXIT_CRITICAL(&s_audio_stats_lock);
                
                if (written < produced) {
                    ESP_LOGW(TAG, "audio_engine_task: partial write %zu/%zu (overrun #%u)", 
                             written, produced, (unsigned)overruns_snapshot);
                }
                
                /* Log span entry for debugging (CODE_REVIEW7 Priority 2, Task 2.1)
                 * Captures: sequence, timestamp, bytes written, ring state, source, flags
                 * Queryable via SPANLOG command for timeline reconstruction */
                if (written > 0) {
                    size_t ring_used_after = capacity - audio_rb_available_to_write(s_audio_ring);
                    audio_source_t active_source = get_active_source();
                    uint8_t span_flags = 0;
                    
                    /* Set BEEP_OVERLAY flag if beep is currently mixing */
                    if (beep_overlay_is_active()) {
                        span_flags |= SPAN_FLAG_BEEP_OVERLAY;
                    }
                    
                    /* Set PAUSED flag if we're at high watermark */
                    if (s_audio_engine_paused) {
                        span_flags |= SPAN_FLAG_PAUSED;
                    }
                    
                    /* Push span entry (timestamp in milliseconds) */
                    span_log_push(
                        s_span_seq++,
                        (uint32_t)platform_get_time_ms(),
                        written,
                        ring_used_after,
                        (uint8_t)active_source,
                        span_flags
                    );
                }
            }
        }
        
        /* Cooperative shutdown wait (CODE_REVIEW 2602101453, P0.1.4)
         * Use task notification instead of vTaskDelay for immediate wake on stop.
         * xTaskNotifyGive() from audio_processor_stop() wakes task instantly.
         * Timeout: 20ms tick maintains same polling rate as before. */
        ulTaskNotifyTake(pdTRUE, delay_ticks);
    }
    
    /* Cleanup on cooperative shutdown (CODE_REVIEW 2602101453, P0.1.5)
     * We reach here when s_engine_stop_requested causes loop break.
     * Free resources, signal completion, then self-delete.
     * WHY: External vTaskDelete() prevented cleanup and could leak chunk_buf. */
    ESP_LOGI(TAG, "audio_engine_task: shutting down, freeing resources");
    
    platform_free(chunk_buf);
    
#ifndef UNIT_TEST
    /* Signal task has stopped (P0.1.5)
     * Allows audio_processor_stop() to return without timeout */
    if (s_engine_events != NULL) {
        xEventGroupSetBits(s_engine_events, ENGINE_STOPPED_BIT);
    }
#endif
    
    ESP_LOGI(TAG, "audio_engine_task: exiting");
    vTaskDelete(NULL);
}

#else /* UNIT_TEST */

typedef enum {
    AUDIO_SOURCE_I2S = 0,
    AUDIO_SOURCE_SYNTH,
    AUDIO_SOURCE_UART,     /* UARTAUDIO: laptop PCM stream over UART0 */
    AUDIO_SOURCE_SILENCE,
    NUM_AUDIO_SOURCES
} audio_source_t;

static audio_source_t s_last_source = AUDIO_SOURCE_SILENCE;

static audio_source_t get_active_source(void)
{
    if (beep_overlay_is_active() || s_beep_remaining_bytes > 0) {
        return AUDIO_SOURCE_SILENCE;
    }

    /* active UART stream outranks forced SYNTH — mirrors device build */
    if (uart_source_is_active()) {
        return AUDIO_SOURCE_UART;
    }

    if (s_force_synth) {
        return AUDIO_SOURCE_SYNTH;
    }

    if (i2s_manager_is_running()) {
        return AUDIO_SOURCE_I2S;
    }

    return AUDIO_SOURCE_SILENCE;
}

static size_t produce_audio_chunk(uint8_t *dst, size_t dst_bytes)
{
    if (dst == NULL || dst_bytes == 0) {
        return 0;
    }

    audio_source_t base = get_active_source();

    if (base != s_last_source) {
        portENTER_CRITICAL(&s_audio_stats_lock);
        s_audio_stats.source_switch_count++;
        portEXIT_CRITICAL(&s_audio_stats_lock);
        s_last_source = base;
    }

    size_t produced = 0;
    switch (base) {
        case AUDIO_SOURCE_I2S:
            produced = i2s_source_fill(dst, dst_bytes);
            break;
        case AUDIO_SOURCE_SYNTH:
            produced = synth_source_fill(dst, dst_bytes);
            break;
        case AUDIO_SOURCE_UART:
            produced = uart_source_fill(dst, dst_bytes);
            break;
        case AUDIO_SOURCE_SILENCE:
        default:
            util_safe_memset(dst, dst_bytes, 0, dst_bytes);
            produced = dst_bytes;
            break;
    }

    if (produced > 0 && base < NUM_AUDIO_SOURCES) {
        portENTER_CRITICAL(&s_audio_stats_lock);
        s_audio_stats.bytes_by_source[base] += produced;
        portEXIT_CRITICAL(&s_audio_stats_lock);
    }

    bool beep_active = beep_overlay_is_active();
    if (beep_active && produced > 0) {
        bool overlay_ok = true;
#ifdef UNIT_TEST
        if (s_test_force_beep_overlay_fail) {
            overlay_ok = false;
        }
#endif
        if (overlay_ok) {
            beep_overlay_fill(dst, produced, &s_audio_config);
            portENTER_CRITICAL(&s_audio_stats_lock);
            s_audio_stats.beep_overlay_count++;
            s_audio_stats.beep_overlay_bytes += produced;
            portEXIT_CRITICAL(&s_audio_stats_lock);

            portENTER_CRITICAL(&s_beep_lock);
            if (s_beep_remaining_bytes > produced) {
                s_beep_remaining_bytes -= produced;
            } else {
                s_beep_remaining_bytes = 0;
            }
            portEXIT_CRITICAL(&s_beep_lock);
        }
    }

    return produced;
}

#endif /* UNIT_TEST */

void audio_processor_set_dram_only(bool enable)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    s_dram_only_alloc = enable ? true : false;
    ESP_LOGI(TAG, "audio_processor: DRAM-only allocations %s", s_dram_only_alloc ? "ENABLED" : "DISABLED");  // NOLINT(bugprone-branch-clone)
}

/**
 * @brief Initialize the audio processor
 */
esp_err_t audio_processor_init(const audio_config_t* config)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Audio processor already initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Null config provided");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    /* Reset keepalive arming on each init so PLAY failures cannot inherit
     * armed state from earlier tests or sessions. */
    s_keepalive_armed = false;

    synth_manager_reset_state();

    // Copy configuration
    safe_memcpy(&s_audio_config, sizeof(s_audio_config), config, sizeof(audio_config_t));

    bool runtime_psram_ready = false;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    runtime_psram_ready = esp_psram_is_initialized();
#endif
    if (s_dram_only_alloc) {
        runtime_psram_ready = false;
    }

    /* Allocate work buffers on the heap. Prefer SPIRAM/8-bit-capable memory
     * when available (heap_caps) to reduce internal DRAM pressure. These
     * buffers are moderately large and kept persistent for the life of the
     * audio processor. */
    /* Prefer allocating large, persistent audio work buffers in PSRAM
     * when available to reduce DRAM pressure. Fall back to DRAM (8-bit
     * capable) if PSRAM allocation fails. */
    /* Allocate work buffers on the heap. Prefer SPIRAM/8-bit-capable memory
     * when available (heap_caps) to reduce internal DRAM pressure. These
     * buffers are moderately large and kept persistent for the life of the
     * audio processor. If allocations fail (common on DRAM-only boards) try
     * progressively smaller buffer sizes for all three work buffers so the
     * system can boot with reduced capability. */
        size_t try_work_bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    s_runtime_work_bytes = 0U;
    /* Lower work buffer sizing on DRAM-only systems to reduce resident
     * DRAM usage (we'll still try progressively smaller sizes). */
    if (!runtime_psram_ready && try_work_bytes > 4096) {
        try_work_bytes = try_work_bytes / 2U;
    }
    /* Minimum per-work-buffer size. Lower this in the mock/unit-test build
     * to accommodate DRAM-only test images which have a much smaller heap
     * available. */
    const size_t min_work_bytes = 1024U;

    esp_err_t ret = ESP_FAIL;
    while (try_work_bytes >= min_work_bytes && s_runtime_work_bytes == 0U) {
        const uint32_t caps = runtime_psram_ready ? (PLATFORM_MEM_CAP_SPIRAM | PLATFORM_MEM_CAP_8BIT) : PLATFORM_MEM_CAP_8BIT;
        s_capture_buffer = platform_malloc(try_work_bytes, caps);
        s_proc_buffer = platform_malloc(try_work_bytes, caps);
        s_proc_buffer2 = platform_malloc(try_work_bytes, caps);
        if (s_capture_buffer != NULL && s_proc_buffer != NULL && s_proc_buffer2 != NULL) {
            s_runtime_work_bytes = try_work_bytes;
            break;
        }

        if (s_capture_buffer) { platform_free(s_capture_buffer); s_capture_buffer = NULL; }
        if (s_proc_buffer) { platform_free(s_proc_buffer); s_proc_buffer = NULL; }
        if (s_proc_buffer2) { platform_free(s_proc_buffer2); s_proc_buffer2 = NULL; }

        try_work_bytes /= 2U;
    }

    if (s_runtime_work_bytes == 0U) {
        ESP_LOGE(TAG, "audio_processor_init: failed to allocate work buffers");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_NO_MEM;
    }

    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: i2s_manager_init failed (%d)", (int)ret);  // NOLINT(bugprone-branch-clone)
        return ret;
    }

    ret = beep_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: beep_manager_init failed (%d)", (int)ret);  // NOLINT(bugprone-branch-clone)
        i2s_manager_deinit();
        return ret;
    }

    /* Create volume commit debounce timer (CODE_REVIEW8 Task D)
     * Reduces NVS flash wear by delaying commits until volume "settles" */
#ifndef UNIT_TEST
    const esp_timer_create_args_t volume_timer_args = {
        .callback = volume_commit_timer_callback,
        .name = "volume_nvs_commit"
    };
    ret = esp_timer_create(&volume_timer_args, &s_volume_commit_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: failed to create volume commit timer (%d)", (int)ret);
        i2s_manager_deinit();
        beep_manager_deinit();
        return ret;
    }
#endif

    /* Initialize ring buffer for audio engine architecture (CODE_REVIEW6 Phase 1, Task 1.3)
     * Capacity and PSRAM usage configurable via Kconfig.
     * Ring buffer coexists with old queue during migration (parallel operation). */
#ifdef CONFIG_AUDIO_RB_CAPACITY_KB
    size_t rb_capacity = CONFIG_AUDIO_RB_CAPACITY_KB * 1024;
#else
    size_t rb_capacity = 32 * 1024;  /* Fallback: 32KB default */
#endif

    /* Watermark sanity checks (CODE_REVIEW7 Priority 5, Task 5.2)
     * Validate watermark configuration at compile-time to catch misconfigurations */
    _Static_assert(AUDIO_RB_LOW_WATERMARK > 0, 
                   "AUDIO_RB_LOW_WATERMARK must be > 0");
    _Static_assert(AUDIO_RB_LOW_WATERMARK < AUDIO_RB_HIGH_WATERMARK, 
                   "AUDIO_RB_LOW_WATERMARK must be < AUDIO_RB_HIGH_WATERMARK");
    
    /* Runtime validation that HIGH watermark fits within configured capacity */
    if (AUDIO_RB_HIGH_WATERMARK >= rb_capacity) {
        ESP_LOGE(TAG, "Invalid watermarks: HIGH=%u >= capacity=%zu. Check sdkconfig.",
                 AUDIO_RB_HIGH_WATERMARK, rb_capacity);
        beep_manager_deinit();
        i2s_manager_deinit();
        return ESP_ERR_INVALID_ARG;
    }
#ifdef CONFIG_AUDIO_RB_USE_PSRAM
    bool use_psram = true;
#else
    bool use_psram = false;
#endif
    ret = audio_rb_init(&s_audio_ring, rb_capacity, use_psram);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: ring buffer init failed (%d)", (int)ret);  // NOLINT(bugprone-branch-clone)
        beep_manager_deinit();
        i2s_manager_deinit();
        return ret;
    }

    /* Initialize span log for debugging visibility (CODE_REVIEW7 Priority 2, Task 2.1)
     * Default capacity: 256 entries (~4KB memory)
     * Enables SPANLOG command to query recent audio engine behavior */
    const size_t span_log_entries = 256;
    if (!span_log_init(span_log_entries)) {
        ESP_LOGW(TAG, "audio_processor_init: span_log_init failed (non-critical)");
        /* Continue initialization - span log is debugging aid, not critical */
    }

#ifndef UNIT_TEST
    /* Create event group for cooperative task shutdown (CODE_REVIEW 2602101453, P0.1.1)
     * Prevents deadlock/leaks from vTaskDelete() - enables clean handshake between
     * audio_processor_stop() and audio_engine_task() */
    if (s_engine_events == NULL) {
        s_engine_events = xEventGroupCreate();
        if (s_engine_events == NULL) {
            ESP_LOGE(TAG, "audio_processor_init: failed to create engine event group");
            span_log_deinit();
            audio_rb_deinit(s_audio_ring);
            s_audio_ring = NULL;
            beep_manager_deinit();
            i2s_manager_deinit();
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "audio_processor_init: engine event group created");
    }
#endif

    s_is_initialized = true;
    ESP_LOGI(TAG, "audio_processor_init: work_bytes=%zu psram=%s ring_buf=%zu", s_runtime_work_bytes, runtime_psram_ready ? "yes" : "no", rb_capacity);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

esp_err_t audio_processor_start(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    ESP_LOGD(TAG, "audio_processor_start: initialized=%d running=%d force_synth=%d",
             (int)s_is_initialized, (int)s_is_running, (int)s_force_synth);
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_is_running) {
        ESP_LOGD(TAG, "audio_processor_start: already running — i2s not (re)started");
        return ESP_OK;
    }

    /* I2S capture has highest priority. Stop any ongoing BEEP playback
     * so capture owns the pipeline. */
    audio_processor_beep_reset();

    /* F1.6.2: I2S/SYNTH mutual exclusion - don't start I2S if SYNTH mode active
     * WHY: I2S and SYNTH must never run simultaneously (F1: BEEP Priority Mode).
     * HOW: Check s_force_synth before starting I2S. If SYNTH active, skip I2S start.
     * 
     * UPDATED from CODE_REVIEW7 Task 1.2: Previous policy was "always start I2S" for
     * auto-reconnect and fast switching. Now enforcing strict mutual exclusion per F1.6.
     * Trade-off: SYNTH mode users must explicitly disable SYNTH before I2S audio.
     * Benefit: Clear source ownership, no simultaneous hardware access. */
    if (s_force_synth) {
        ESP_LOGI(TAG, "audio_processor_start: skipping I2S start (SYNTH mode active)");
    } else {
        /* Start I2S manager for capture (CODE_REVIEW7 Task 1.2 rationale still applies):
         * 1. AUTO-RECONNECT: I2S streams immediately on BT reconnect
         * 2. FAST SWITCHING: No on-demand startup latency
         * 3. EARLY FAILURE DETECTION: Hardware issues found at startup
         * 4. POWER COST NEGLIGIBLE: ~1-2mA for dev/bench device */
        esp_err_t ret = i2s_manager_start();
        ESP_LOGI(TAG, "audio_processor_start: i2s_manager_start() -> %d (running=%d)",
                 (int)ret, (int)i2s_manager_is_running());
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_start: i2s_manager_start failed (%d)", (int)ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

#ifndef UNIT_TEST
    /* Create audio engine task with cooperative shutdown support (CODE_REVIEW 2602101453, P0.1.2)
     * Clear stop flag and wait for task to signal it's running for robust startup */
    if (s_audio_engine_task_handle == NULL) {
        /* Clear stop request flag before creating task */
        s_engine_stop_requested = false;
        
        /* Clear event bits from any previous run */
        if (s_engine_events != NULL) {
            xEventGroupClearBits(s_engine_events, ENGINE_RUNNING_BIT | ENGINE_STOPPED_BIT);
        }
        
        BaseType_t task_ret = xTaskCreate(
            audio_engine_task,
            "audio_engine",
            AUDIO_ENGINE_TASK_STACK_SIZE,
            NULL,
            AUDIO_ENGINE_TASK_PRIORITY,
            &s_audio_engine_task_handle
        );
        
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "audio_processor_start: failed to create audio engine task");
            i2s_manager_stop();
            return ESP_FAIL;
        }
        
        /* Wait for task to signal it's running (robustness check, P0.1.2)
         * Timeout: 100ms should be more than enough for task to start */
        if (s_engine_events != NULL) {
            EventBits_t bits = xEventGroupWaitBits(
                s_engine_events,
                ENGINE_RUNNING_BIT,
                pdFALSE,  /* don't clear on exit */
                pdFALSE,  /* wait for any bit */
                pdMS_TO_TICKS(100)
            );
            
            if ((bits & ENGINE_RUNNING_BIT) == 0) {
                ESP_LOGW(TAG, "audio_processor_start: task did not signal RUNNING within timeout (non-fatal)");
                /* Continue anyway - task may still be initializing */
            }
        }
        
        ESP_LOGI(TAG, "audio_processor_start: audio engine task created (priority=%d)", 
                 AUDIO_ENGINE_TASK_PRIORITY);
    }
#endif

    s_is_running = true;
    return ESP_OK;
}

esp_err_t audio_processor_stop(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_is_running) {
        return ESP_ERR_INVALID_STATE;  /* Already stopped - caller should track state */
    }

#ifndef UNIT_TEST
    /* Cooperative shutdown (CODE_REVIEW 2602101453, P0.1.3)
     * 
     * WHY: vTaskDelete(handle) from external context is UNSAFE - can deadlock if task
     *      holds spinlock, leaks resources (chunk_buf malloc), corrupts state mid-update.
     * 
     * HOW: Signal task to stop via flag, wake it with notification, wait for clean exit.
     *      Task checks flag, releases resources, sets STOPPED bit, then self-deletes.
     * 
     * SAFETY: Prevents deadlock (task completes critical sections), prevents leaks
     *         (task frees its own allocations), prevents corruption (task controls exit timing).
     */
    if (s_audio_engine_task_handle != NULL) {
        /* Signal task to stop */
        s_engine_stop_requested = true;
        
        /* Wake task immediately (in case it's blocked on delay/notification) */
        xTaskNotifyGive(s_audio_engine_task_handle);
        
        /* Wait for task to signal it has stopped (cooperative handshake)
         * Timeout: 500ms - generous but bounded. Task should exit within one tick cycle (2ms)
         * plus time to complete current iteration. 500ms allows for worst-case BT stack delays. */
        if (s_engine_events != NULL) {
            EventBits_t bits = xEventGroupWaitBits(
                s_engine_events,
                ENGINE_STOPPED_BIT,
                pdTRUE,   /* clear bit on exit (cleanup for next start) */
                pdFALSE,  /* wait for this bit only */
                pdMS_TO_TICKS(500)
            );
            
            if ((bits & ENGINE_STOPPED_BIT) == 0) {
                /* Task didn't stop in time - serious error but don't hang forever */
                ESP_LOGE(TAG, "audio_processor_stop: task did not stop within 500ms timeout!");
                ESP_LOGE(TAG, "  This may indicate task is deadlocked or system is overloaded.");
                ESP_LOGE(TAG, "  Continuing anyway to avoid blocking caller indefinitely.");
                /* Handle will be leaked but system can continue - better than hard hang */
            } else {
                ESP_LOGI(TAG, "audio_processor_stop: audio engine task stopped cleanly");
            }
        }
        
        /* Task has self-deleted (or timed out), clear our handle reference */
        s_audio_engine_task_handle = NULL;
        s_audio_engine_paused = false;  /* Reset pause state for next start */
    }
#endif

    i2s_manager_stop();
    s_is_running = false;
    s_keepalive_armed = false;
    s_force_synth = false;
    return ESP_OK;
}

esp_err_t audio_processor_drain_ring(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized || s_audio_ring == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear pending ring buffer data by reading it out. */
    uint8_t scratch[256];
    while (audio_rb_available_to_read(s_audio_ring) > 0) {
        size_t available = audio_rb_available_to_read(s_audio_ring);
        size_t to_read = (available > sizeof(scratch)) ? sizeof(scratch) : available;
        (void)audio_rb_read(s_audio_ring, scratch, to_read);
    }

    /* Reset transient playback state. */
    s_audio_rb_residual_len = 0;
    s_audio_rb_residual_pos = 0;
    s_keepalive_armed = false;
    s_force_synth = false;
    s_last_source_was_synth = false;

    audio_processor_beep_reset();
    beep_overlay_stop();

    ESP_LOGI(TAG, "audio_processor_drain_ring: cleared ring buffer and playback state");
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_is_running) {
        (void)audio_processor_stop();
    }

    /* Reset playback state so re-init starts cleanly. */
    audio_processor_beep_reset();
    s_keepalive_armed = false;
    s_force_synth = false;
    s_last_source_was_synth = false;

    beep_manager_deinit();
    i2s_manager_deinit();

    /* Cleanup volume commit timer (CODE_REVIEW8 Task D)
     * Stop and delete timer to prevent callback firing after deinit */
#ifndef UNIT_TEST
    if (s_volume_commit_timer != NULL) {
        esp_timer_stop(s_volume_commit_timer);  /* Stop any pending callback */
        esp_timer_delete(s_volume_commit_timer);
        s_volume_commit_timer = NULL;
    }
#endif

    /* Cleanup ring buffer (CODE_REVIEW6 Phase 1, Task 1.3) */
    if (s_audio_ring != NULL) {
        audio_rb_deinit(s_audio_ring);
        s_audio_ring = NULL;
    }

    /* Deinitialize span log (CODE_REVIEW7 Priority 2, Task 2.1) */
    span_log_deinit();

#ifndef UNIT_TEST
    /* Cleanup engine event group (CODE_REVIEW 2602101453, P0.1.1)
     * Clean up cooperative shutdown infrastructure */
    if (s_engine_events != NULL) {
        vEventGroupDelete(s_engine_events);
        s_engine_events = NULL;
    }
#endif

    synth_manager_reset_state();

    if (s_capture_buffer) {
        platform_free(s_capture_buffer);
        s_capture_buffer = NULL;
    }
    if (s_proc_buffer) {
        platform_free(s_proc_buffer);
        s_proc_buffer = NULL;
    }
    if (s_proc_buffer2) {
        platform_free(s_proc_buffer2);
        s_proc_buffer2 = NULL;
    }

    s_runtime_work_bytes = 0;
    s_audio_rb_residual_len = 0;
    s_audio_rb_residual_pos = 0;
    s_beep_prefill_active = false;
    s_beep_prefill_accum_bytes = 0;
    s_beep_prefill_goal_bytes = 0;
    s_beep_remaining_bytes = 0;
    /* WAV state variables removed (play_manager deleted) */
    s_trace_next_read_call = false;
    s_trace_read_until_beep_done = false;
    s_diag_next_log_tick = 0;
    s_diag_last_conv_size = SIZE_MAX;
    s_diag_last_frame_bytes = SIZE_MAX;
    s_diag_last_src_rate = -1;
    s_diag_last_dst_rate = -1;
    s_i2s_read_ops = 0;
    s_i2s_total_read_bytes = 0;
    s_i2s_timeout_count = 0;
    s_probe_captured = 0;
    s_probe_target = 0;
    s_tag_miss_count = 0;
    s_tag_recover_mute_until = 0;
    s_volume_gain = 100;
    s_audio_diag_enabled = false;
    s_beep_restore_synth = false;
#ifdef CONFIG_BT_MOCK_TESTING
    s_i2s_consecutive_failures = 0;
    s_last_i2s_failure_log = 0;
#endif

    safe_memset(&s_audio_config, sizeof(s_audio_config), 0, sizeof(s_audio_config));
    safe_memset(&s_audio_stats, sizeof(s_audio_stats), 0, sizeof(s_audio_stats));

    s_is_initialized = false;
    return ESP_OK;
}

/**
 * @brief Set the output sample rate
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_config.sample_rate == sample_rate) {
        return ESP_OK;
    }

    bool was_running = s_is_running;
    esp_err_t ret = ESP_OK;
    if (was_running) {
        ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    s_audio_config.sample_rate = sample_rate;

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);  // NOLINT(bugprone-branch-clone)
        return ret;
    }

    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio sample rate changed to %d Hz", sample_rate);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

/**
 * @brief Set audio volume
 */
esp_err_t audio_processor_set_volume(uint8_t volume)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    // Clamp volume to 0-100
    if (volume > 100) {
        volume = 100;
    }

    s_volume_gain = volume;
    s_audio_config.volume = volume;

    /* CODE_REVIEW8 Task D: Debounce NVS commit to prevent flash wear
     * Cancel pending timer (if any) and restart with 500ms delay.
     * Volume persisted only after user "settles" on final value.
     * Reduces flash writes from "every change" to "once per adjustment session" */
    #ifndef UNIT_TEST
    if (s_volume_commit_timer != NULL) {
        esp_timer_stop(s_volume_commit_timer);  /* Cancel pending commit */
        esp_timer_start_once(s_volume_commit_timer, 500000);  /* 500ms = 500,000 microseconds */
    }
    #endif

    ESP_LOGI(TAG, "Audio volume set to %d%% (NVS commit debounced)", volume);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

/**
 * @brief Mute or unmute audio
 */
esp_err_t audio_processor_set_mute(bool mute)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    s_audio_config.mute = mute;
    ESP_LOGI(TAG, "Audio %s", mute ? "muted" : "unmuted");  // NOLINT(bugprone-branch-clone)

    return ESP_OK;
}

/**
 * @brief Get current audio configuration
 */
esp_err_t audio_processor_get_config(audio_config_t* config)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Null config pointer");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    safe_memcpy(config, sizeof(*config), &s_audio_config, sizeof(audio_config_t));
    return ESP_OK;
}

/**
 * @brief Get audio processing statistics
 */
esp_err_t audio_processor_get_stats(audio_stats_t* stats)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (stats == NULL) {
        ESP_LOGE(TAG, "Null stats pointer");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    /* Protect read access (CODE_REVIEW 2602101453, P1.2.4)
     * WHY: Ensure atomic snapshot of all fields to prevent torn reads */
    portENTER_CRITICAL(&s_audio_stats_lock);
    safe_memcpy(stats, sizeof(*stats), &s_audio_stats, sizeof(audio_stats_t));
    portEXIT_CRITICAL(&s_audio_stats_lock);
    return ESP_OK;
}

/*******************************
 * Internal functions
 *******************************/

/**
 * @brief Configure capture path via i2s_manager using shared buffers.
 */
static esp_err_t configure_i2s(const audio_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_manager_deinit();

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };

    return i2s_manager_init(config, &bufs);
}

esp_err_t audio_processor_get_status(audio_status_t* status)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (status == NULL) {
    	return ESP_ERR_INVALID_ARG;
    }
    status->initialized = s_is_initialized;
    status->running = s_is_running;
    status->volume = s_volume_gain;
    status->mute = s_audio_config.mute;
    status->sample_rate = s_audio_config.sample_rate;
    status->bit_depth = s_audio_config.bit_depth;
    status->channels = s_audio_config.channels;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool was_running = s_is_running;
    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
        	return ret;
        }
    }

    s_audio_config.i2s_bclk_pin = bclk_pin;
    s_audio_config.i2s_ws_pin = ws_pin;
    s_audio_config.i2s_din_pin = din_pin;
    s_audio_config.i2s_dout_pin = dout_pin;

    // Persist pins
    nvs_storage_set_i2s_pins(bclk_pin, ws_pin, din_pin, dout_pin);

    esp_err_t ret = configure_i2s(&s_audio_config);

    if (was_running && ret == ESP_OK) {
        ret = audio_processor_start();
    }

    return ret;
}

/**
 * @brief Emit a synchronous worker-like diagnostic snapshot.
 *
 * This function generates a short block of audio using the same synth or
 * mock generator used by the reader/worker, runs the conversion/resample
 * helpers inline, and emits a DIAG:worker-out hex dump of the resulting
 * samples. It is intentionally bounded to avoid long blocking calls.
 */
esp_err_t audio_processor_emit_sync_worker_diag(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
    	return ESP_ERR_INVALID_STATE;
    }

    size_t target = audio_get_runtime_work_bytes();
    if (target == 0) {
    	return ESP_ERR_INVALID_ARG;
    }

    size_t generated = 0;

    /* Prefer using the same generator the worker uses: synth if enabled,
     * or the mock generator for unit tests. If neither is available,
     * synthesize the same beep tone so the snapshot is representative of
     * an actual audio tone rather than a memset test pattern. */
#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
    generated = synth_manager_generate_audio(s_proc_buffer, target, &s_audio_config, &s_force_synth, &s_beep_lock);
#elif defined(CONFIG_BT_MOCK_TESTING)
    generated = mock_generate_i2s_audio(s_proc_buffer, target);
#else
    /* Generate a sine-wave tone (1kHz) so the diagnostic snapshot is
     * musically pleasant and representative of real audio output. Fill
     * s_proc_buffer according to bit depth and channel count. */
    int sample_rate = s_audio_config.sample_rate > 0 ? s_audio_config.sample_rate : 44100;
    const int tone_hz = 1000;
    size_t bytes_per_sample = (size_t)audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample == 0) bytes_per_sample = 2;
    int channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    size_t frame_bytes = bytes_per_sample * (size_t)channels;
    size_t max_frames = target / frame_bytes;
    if (max_frames == 0) max_frames = 1;
    /* Use sine wave generation for nicer tone */
    const double two_pi = 2.0 * M_PI;
    double phase = 0.0;
    double phase_inc = (sample_rate > 0) ? ((two_pi * (double)tone_hz) / (double)sample_rate) : ((two_pi * (double)tone_hz) / 44100.0);
    /* Apply a short fade-in/out envelope to reduce transient clicks. Use
     * the same fade duration as the runtime beep so diagnostics and live
     * playback match. */
    size_t fade_frames = (size_t)(((double)sample_rate * (double)BEEP_FADE_MS) / 1000.0);
    if (fade_frames < 1) fade_frames = 1;

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* out = (int16_t*)s_proc_buffer;
        const double amp = 30000.0;
        for (size_t f = 0; f < max_frames; ++f) {
            double env = 1.0;
            if (f < fade_frames) {
                env = (double)f / (double)fade_frames;
            } else if (f + fade_frames > max_frames) {
                /* trailing fade */
                size_t tail_idx = max_frames - f;
                if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
            }
            double v = sin(phase) * amp * env;
            int16_t sample = (int16_t)v;
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = sample;
            }
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
        }
        generated = max_frames * frame_bytes;
    } else {
        int32_t* out32 = (int32_t*)s_proc_buffer;
        const double amp32 = 30000.0 * (1 << 16);
        for (size_t f = 0; f < max_frames; ++f) {
            double env = 1.0;
            if (f < fade_frames) {
                env = (double)f / (double)fade_frames;
            } else if (f + fade_frames > max_frames) {
                size_t tail_idx = max_frames - f;
                if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
            }
            double v = sin(phase) * amp32 * env;
            int32_t sample = (int32_t)v;
            for (int ch = 0; ch < channels; ++ch) {
                *out32++ = sample;
            }
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
        }
        generated = max_frames * frame_bytes;
    }
#endif

    if (generated == 0) {
    	return ESP_ERR_INVALID_SIZE;
    }

    size_t conv_size = 0;
    size_t res_size = 0;

    /* Convert/resample to the worker output format (no-op if identical) */
    audio_convert_args_t conv_args = {
        .src = s_proc_buffer,
        .dst = s_proc_buffer,
        .src_size = generated,
        .src_bit_depth = s_audio_config.bit_depth,
        .dst_bit_depth = s_audio_config.bit_depth,
        .dst_size = &conv_size,
        .work_bytes = audio_get_runtime_work_bytes(),
    };
    if (convert_audio_format(&conv_args) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (conv_size == 0) {
    	return ESP_ERR_INVALID_SIZE;
    }

    audio_resample_args_t res_args = {
        .src = s_proc_buffer,
        .dst = s_proc_buffer2,
        .src_size = conv_size,
        .src_rate = s_audio_config.sample_rate,
        .dst_rate = s_audio_config.sample_rate,
        .bit_depth = s_audio_config.bit_depth,
        .channels = s_audio_config.channels,
        .dst_size = &res_size,
        .work_bytes = audio_get_runtime_work_bytes(),
    };
    if (resample_audio(&res_args) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (res_size == 0) {
    	return ESP_ERR_INVALID_SIZE;
    }

    size_t dump = res_size < DIAG_DUMP_BYTES ? res_size : DIAG_DUMP_BYTES;
    diag_dump_bytes(s_proc_buffer2, dump, "DIAG:worker-out");
    return ESP_OK;
}


/**
 * @brief Set the audio bit depth
 */
esp_err_t audio_processor_set_channels(audio_channel_t channels)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (channels != AUDIO_CHANNEL_MONO && channels != AUDIO_CHANNEL_STEREO) {
        ESP_LOGE(TAG, "Invalid channel configuration: %d", (int)channels);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    if (s_audio_config.channels == channels) {
        return ESP_OK;
    }

    bool was_running = s_is_running;
    esp_err_t ret = ESP_OK;

    if (was_running) {
        ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    s_audio_config.channels = channels;

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);  // NOLINT(bugprone-branch-clone)
        return ret;
    }

    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio channels changed to %d", (int)channels);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

/**
 * @brief Set the audio bit depth
 */
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_config.bit_depth == bit_depth) {
        return ESP_OK; // Nothing to change
    }

    bool was_running = s_is_running;
    esp_err_t ret = ESP_OK;

    if (was_running) {
        ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    s_audio_config.bit_depth = bit_depth;

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);  // NOLINT(bugprone-branch-clone)
        return ret;
    }

    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio bit depth changed to %d bits", bit_depth);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

bool audio_processor_is_i2s_active(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    return s_is_running;
}

bool audio_processor_is_wav_active(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    return false;  /* WAV playback removed (play_manager deleted) */
}

void audio_processor_set_diag_enabled(bool enable)
{
    s_audio_diag_enabled = enable;
    ESP_LOGI(TAG, "audio_processor: diagnostics %s", enable ? "ENABLED" : "DISABLED");  // NOLINT(bugprone-branch-clone)
}

bool audio_processor_is_diag_enabled(void)
{
    return s_audio_diag_enabled;
}

/**
 * @brief Diagnostic helper to check whether audio processing is active
 *
 * Returns true when the processor is in the running state (I2S RX enabled).
 */
bool audio_processor_is_running(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    return s_is_running;
}

size_t audio_processor_get_work_buffer_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    return s_runtime_work_bytes;
}

bool audio_processor_is_synth_mode_enabled(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    return s_force_synth;
}

#ifdef UNIT_TEST
int audio_processor_test_get_active_source_id(void)
{
    return (int)get_active_source();
}

size_t audio_processor_test_produce_audio_chunk(uint8_t* dst, size_t dst_bytes)
{
    return produce_audio_chunk(dst, dst_bytes);
}

void audio_processor_test_reset_core_logic_state(void)
{
    s_last_source = AUDIO_SOURCE_SILENCE;
    s_test_force_beep_overlay_fail = false;
    safe_memset(&s_audio_stats, sizeof(s_audio_stats), 0, sizeof(s_audio_stats));
}

bool audio_processor_test_compute_engine_paused(bool was_paused, size_t used_bytes, uint32_t* pause_transition_out)
{
    bool paused = was_paused;
    uint32_t transitions = 0;

    if (used_bytes >= AUDIO_RB_HIGH_WATERMARK) {
        paused = true;
        if (!was_paused) {
            transitions = 1;
        }
    }
    if (used_bytes <= AUDIO_RB_LOW_WATERMARK) {
        paused = false;
    }

    if (pause_transition_out != NULL) {
        *pause_transition_out = transitions;
    }

    return paused;
}

bool audio_processor_test_should_produce_chunk(bool paused, size_t free_bytes)
{
    return (!paused && free_bytes >= AUDIO_ENGINE_CHUNK_BYTES);
}

esp_err_t audio_processor_test_commit_volume_now(void)
{
    return nvs_storage_set_volume(s_volume_gain);
}

void audio_processor_test_set_force_beep_overlay_fail(bool enable)
{
    s_test_force_beep_overlay_fail = enable;
}
#endif

void audio_processor_set_synth_mode(bool enable)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    
    /* F1.6.1: Enforce I2S/SYNTH mutual exclusion (F1: BEEP Priority Mode)
     * WHY: I2S and SYNTH must never run simultaneously - they're mutually exclusive sources.
     * HOW: When enabling SYNTH, stop I2S if running. When disabling SYNTH, start I2S if processor running.
     * CORRECTNESS: Source priority in get_active_source() ensures only one active at a time. */
    if (enable) {
        /* Enabling SYNTH mode - stop I2S if running */
        if (i2s_manager_is_running()) {
            ESP_LOGI(TAG, "audio_processor_set_synth_mode: stopping I2S (SYNTH mode enabled)");
            i2s_manager_stop();
        }
        s_force_synth = true;
    } else {
        /* Disabling SYNTH mode - start I2S if processor is running */
        s_force_synth = false;
        if (s_is_running) {
            ESP_LOGI(TAG, "audio_processor_set_synth_mode: starting I2S (SYNTH mode disabled)");
            esp_err_t ret = i2s_manager_start();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "audio_processor_set_synth_mode: i2s_manager_start failed (%d)", (int)ret);
            }
        }
    }
    
    ESP_LOGI(TAG, "audio_processor: synth mode %s", enable ? "ENABLED" : "DISABLED");  // NOLINT(bugprone-branch-clone)
}

uint32_t audio_processor_test_get_tag_miss_count(void)
{
    return __atomic_load_n(&s_tag_miss_count, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_miss_count(void)
{
    __atomic_store_n(&s_tag_miss_count, 0U, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_recover_window(void)
{
    s_tag_recover_mute_until = 0;
}

size_t audio_processor_test_get_audio_free_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    return s_audio_ring ? audio_rb_available_to_write(s_audio_ring) : 0;
}

size_t audio_processor_test_get_ring_used_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (s_audio_ring == NULL) {
        return 0;
    }
    return audio_rb_capacity(s_audio_ring) - audio_rb_available_to_write(s_audio_ring);
}

#ifdef CONFIG_BT_MOCK_TESTING
void audio_processor_test_idle_i2s_failures(int failures, bool synth_enabled, size_t beep_remaining, bool *synth_after, int *failures_after)
{
    AUDIO_PROC_LOG_ONCE();
    s_i2s_consecutive_failures = failures;
    s_force_synth = synth_enabled;
    s_beep_remaining_bytes = beep_remaining;
    s_keepalive_armed = true;
    /* s_wav_playback_active removed (play_manager deleted) */
    s_last_i2s_failure_log = -I2S_FAILURE_LOG_THROTTLE;
    if (s_i2s_consecutive_failures >= I2S_FAILURE_THRESHOLD &&
        (s_i2s_consecutive_failures - s_last_i2s_failure_log) >= I2S_FAILURE_LOG_THROTTLE &&
        s_keepalive_armed && s_beep_remaining_bytes == 0 && !s_force_synth) {
        s_last_i2s_failure_log = s_i2s_consecutive_failures;
        s_force_synth = true;
        s_i2s_consecutive_failures = 0;
    }
    if (synth_after != NULL) {
        *synth_after = s_force_synth;
    }
    if (failures_after != NULL) {
        *failures_after = s_i2s_consecutive_failures;
    }
}

/* WAV playback test functions removed (play_manager deleted) */
#endif /* CONFIG_BT_MOCK_TESTING */

#ifdef CONFIG_BT_MOCK_TESTING
/* Return number of queued audio descriptors (each carries its source tag).
 * Callers can use this to validate producer/consumer balance during tests. */
size_t audio_processor_test_get_tag_used(void)
{
    if (s_audio_ring == NULL) {
        return 0;
    }
    return audio_rb_capacity(s_audio_ring) - audio_rb_available_to_write(s_audio_ring);
}
#endif

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

#ifndef UNIT_TEST
/**
 * Volume commit timer callback (CODE_REVIEW8 Task D)
 * 
 * WHY: Debounce rapid volume changes to reduce NVS flash wear
 * HOW: Triggered 500ms after last volume change, commits current value to NVS
 * CORRECTNESS: Only runs on timer expiration, safe to call nvs_storage from timer context
 */
void volume_commit_timer_callback(void* arg)
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

audio_source_t s_last_source = AUDIO_SOURCE_SILENCE;

audio_source_t get_active_source(void)
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
size_t produce_audio_chunk(uint8_t *dst, size_t dst_bytes)
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
void audio_engine_task(void *arg)
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
        /* Store startup error so audio_processor_start() can report it (RH-WR-03) */
        s_engine_start_error = ESP_ERR_NO_MEM;
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


audio_source_t s_last_source = AUDIO_SOURCE_SILENCE;

audio_source_t get_active_source(void)
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

size_t produce_audio_chunk(uint8_t *dst, size_t dst_bytes)
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

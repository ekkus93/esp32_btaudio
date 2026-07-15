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

void audio_processor_set_dram_only(bool enable)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    s_dram_only_alloc = enable ? true : false;
    ESP_LOGI(TAG, "audio_processor: DRAM-only allocations %s", s_dram_only_alloc ? "ENABLED" : "DISABLED");  // NOLINT(bugprone-branch-clone)
}

/**
 * @brief Initialize the audio processor
 */
/**
 * @brief (RH-WR-04) Cleanup partially-initialized state on init failure
 *
 * This is called from the single failure label in audio_processor_init().
 * It reverses all successful allocation/init steps in reverse order.
 */
static void audio_processor_cleanup_partial_init(void)
{
#ifndef UNIT_TEST
    if (s_engine_events) {
        vEventGroupDelete(s_engine_events);
        s_engine_events = NULL;
    }

    if (s_volume_commit_timer) {
        esp_timer_stop(s_volume_commit_timer);
        esp_timer_delete(s_volume_commit_timer);
        s_volume_commit_timer = NULL;
    }
#endif

    span_log_deinit();
    audio_rb_deinit(s_audio_ring);
    s_audio_ring = NULL;
    beep_manager_deinit();
    i2s_manager_deinit();

    platform_free(s_proc_buffer2); s_proc_buffer2 = NULL;
    platform_free(s_proc_buffer);  s_proc_buffer = NULL;
    platform_free(s_capture_buffer); s_capture_buffer = NULL;
    s_runtime_work_bytes = 0;

    s_is_initialized = false;
}

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
        goto fail;
    }

    ret = beep_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: beep_manager_init failed (%d)", (int)ret);  // NOLINT(bugprone-branch-clone)
        goto fail;
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
        goto fail;
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
        ret = ESP_ERR_INVALID_ARG;
        goto fail;
    }
#ifdef CONFIG_AUDIO_RB_USE_PSRAM
    bool use_psram = true;
#else
    bool use_psram = false;
#endif
    ret = audio_rb_init(&s_audio_ring, rb_capacity, use_psram);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: ring buffer init failed (%d)", (int)ret);  // NOLINT(bugprone-branch-clone)
        goto fail;
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
            ret = ESP_ERR_NO_MEM;
            goto fail;
        }
        ESP_LOGI(TAG, "audio_processor_init: engine event group created");
    }
#endif

    s_is_initialized = true;
    ESP_LOGI(TAG, "audio_processor_init: work_bytes=%zu psram=%s ring_buf=%zu", s_runtime_work_bytes, runtime_psram_ready ? "yes" : "no", rb_capacity);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;

fail:
    audio_processor_cleanup_partial_init();
    return ret;
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

    /* (RH-WR-02) Transition to STARTING */
    s_audio_state = AUDIO_STATE_STARTING;

#ifndef UNIT_TEST
    /* (RH-WR-02) Reject restart while STOPPING or FAULTED with live handle.
     * This prevents overlapping engine generations and silent resource leaks. */
    if (s_audio_state == AUDIO_STATE_STOPPING || s_audio_state == AUDIO_STATE_FAULTED) {
        if (s_audio_engine_task_handle != NULL) {
            ESP_LOGE(TAG, "audio_processor_start: rejected while %s (task still owned)",
                     s_audio_state == AUDIO_STATE_STOPPING ? "STOPPING" : "FAULTED");
            s_audio_state = AUDIO_STATE_FAULTED;
            return ESP_ERR_INVALID_STATE;
        }
    }
#endif

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
            s_audio_state = AUDIO_STATE_FAULTED;
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
            s_audio_state = AUDIO_STATE_FAULTED;
            return ESP_FAIL;
        }

        /* Wait for task to signal RUNNING or STOPPED (RH-WR-03)
         * If ENGINE_STOPPED_BIT arrives, the engine failed during startup.
         * Return the stored error rather than pretending success.
         * Timeout: 100ms should be more than enough for task to start */
        if (s_engine_events != NULL) {
            EventBits_t bits = xEventGroupWaitBits(
                s_engine_events,
                ENGINE_RUNNING_BIT | ENGINE_STOPPED_BIT,
                pdFALSE,  /* don't clear on exit */
                pdFALSE,  /* wait for any bit */
                pdMS_TO_TICKS(100)
            );

            if (bits & ENGINE_RUNNING_BIT) {
                /* Engine signaled RUNNING — startup succeeded */
            } else if (bits & ENGINE_STOPPED_BIT) {
                /* Engine exited before RUNNING — report stored error */
                ESP_LOGE(TAG, "audio_processor_start: engine exited during startup (error=%d)",
                         (int)s_engine_start_error);
                i2s_manager_stop();
                s_audio_state = AUDIO_STATE_FAULTED;
                return s_engine_start_error != ESP_OK ? s_engine_start_error : ESP_FAIL;
            } else {
                /* Timeout: neither bit arrived */
                ESP_LOGE(TAG, "audio_processor_start: engine startup timed out");
                i2s_manager_stop();
                s_audio_state = AUDIO_STATE_FAULTED;
                return ESP_ERR_TIMEOUT;
            }
        }

        ESP_LOGI(TAG, "audio_processor_start: audio engine task created (priority=%d)",
                 AUDIO_ENGINE_TASK_PRIORITY);
    }
#endif

    /* (RH-WR-02) Transition to RUNNING */
    s_audio_state = AUDIO_STATE_RUNNING;
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

    /* (RH-WR-02) Explicit lifecycle transition: RUNNING -> STOPPING */
    s_audio_state = AUDIO_STATE_STOPPING;

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
         * Timeout: AUDIO_STOP_TIMEOUT_MS - generous but bounded. Task should exit within
         * one tick cycle (2ms) plus time to complete current iteration. */
        if (s_engine_events != NULL) {
            EventBits_t bits = xEventGroupWaitBits(
                s_engine_events,
                ENGINE_STOPPED_BIT,
                pdTRUE,   /* clear bit on exit (cleanup for next start) */
                pdFALSE,  /* wait for this bit only */
                pdMS_TO_TICKS(AUDIO_STOP_TIMEOUT_MS)
            );

            if ((bits & ENGINE_STOPPED_BIT) == 0) {
                /* (RH-WR-02) Stop timeout: task didn't stop in time.
                 * Leave state FAULTED, retain handle, do NOT stop I2S
                 * underneath the live engine. */
                s_audio_state = AUDIO_STATE_FAULTED;
                ESP_LOGE(TAG, "audio engine stop timed out; state=FAULTED, task remains owned");
                return ESP_ERR_TIMEOUT;
            }

            ESP_LOGI(TAG, "audio_processor_stop: audio engine task stopped cleanly");

            /* Task has self-deleted; clear our handle reference */
            s_audio_engine_task_handle = NULL;
        }

        s_audio_engine_paused = false;  /* Reset pause state for next start */
    }
#endif

    esp_err_t err = i2s_manager_stop();
    if (err != ESP_OK) {
        s_audio_state = AUDIO_STATE_FAULTED;
        return err;
    }

    s_audio_state = AUDIO_STATE_STOPPED;
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

    /* (RH-WR-02) Reset lifecycle state on deinit */
    s_audio_state = AUDIO_STATE_STOPPED;
    /* (RH-WR-03) Reset startup error for retry safety */
    s_engine_start_error = ESP_OK;
    s_is_initialized = false;
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

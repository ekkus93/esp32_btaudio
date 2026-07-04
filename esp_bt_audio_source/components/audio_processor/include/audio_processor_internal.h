#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifndef UNIT_TEST
#include "freertos/event_groups.h"
#endif
#include "esp_log.h"
#ifndef UNIT_TEST
#include "esp_timer.h"
#endif
#include "audio_processor.h"
#include "audio_ringbuffer.h"
#include "beep_manager.h"
#include "i2s_manager.h"
#include "audio_util.h"
#include "util_safe.h"
#include "synth_manager.h"

/* Convenience aliases for safe memory functions */
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset

#define TAG "AUDIO_PROC"

/* Diagnostics gate for noisy logging */
extern volatile bool s_audio_diag_enabled;
#define AUDIO_DIAG_ENABLED() (s_audio_diag_enabled)
#define AUDIO_DIAG_PRINTF(...)            \
    do {                                 \
        if (AUDIO_DIAG_ENABLED()) {      \
            printf(__VA_ARGS__);         \
        }                                \
    } while (0)
#define AUDIO_PROC_LOG_ONCE()                                                 \
    do {                                                                     \
        static bool _logged = false;                                         \
        if (!_logged) {                                                      \
            ESP_LOGI(TAG, "audio_processor (main) entered %s", __func__);   \
            _logged = true;                                                  \
        }                                                                    \
    } while (0)

#define AUDIO_PROCESSING_STACK_SIZE  4096
#define AUDIO_BLOCK_SIZE              128
#ifdef CONFIG_BT_MOCK_TESTING
#define AUDIO_RESAMPLE_MAX_RATIO      6
#else
#define AUDIO_RESAMPLE_MAX_RATIO      8
#endif
#define AUDIO_WORK_BUFFER_BYTES ((size_t)AUDIO_BLOCK_SIZE * 8U * (size_t)AUDIO_RESAMPLE_MAX_RATIO)

/* Minimum work-buffer size guaranteed after audio_processor_init() succeeds.
 * On DRAM-only boards the allocator halves AUDIO_WORK_BUFFER_BYTES when no
 * PSRAM is detected; this floor asserts the minimum usable working size. */
#define AUDIO_WORK_BUFFER_DRAM_MIN_BYTES ((size_t)(AUDIO_WORK_BUFFER_BYTES / 2U))

/* Minimum audio ring-buffer capacity.  Production default is 32 KiB
 * (CONFIG_AUDIO_RB_CAPACITY_KB=32); host tests use the same fallback.
 * Tests should assert free bytes >= this value after init. */
#define AUDIO_MIN_RB_CAPACITY_BYTES ((size_t)(32U * 1024U))

#define BEEP_FADE_MS 50
#define I2S_RAW_POOL_DEFAULT_COUNT 8U
#define I2S_RAW_POOL_DRAM_COUNT    1U
#define I2S_DEFAULT_DMA_DESC_NUM 6U
#define I2S_DEFAULT_DMA_FRAME_NUM 32U
#define I2S_MAX_READ_BYTES ((size_t)4U * 1024U)
#define SYNTH_MIN_HEADROOM_BYTES  (AUDIO_WORK_BUFFER_BYTES)
#define SYNTH_THROTTLE_DELAY_MS   2
#define I2S_PROBE_MAX_ENTRIES 32U
#define I2S_FAILURE_THRESHOLD 20
#define I2S_FAILURE_LOG_THROTTLE 200
#define DIAG_DUMP_BYTES 64U

/* Audio engine task configuration (CODE_REVIEW6 Phase 2) */
#define AUDIO_ENGINE_TASK_STACK_SIZE  4096
#define AUDIO_ENGINE_TASK_PRIORITY    (configMAX_PRIORITIES - 2)  /* High priority, below BT */
#define AUDIO_ENGINE_TICK_MS          2   /* 2ms tick rate */
#define AUDIO_ENGINE_CHUNK_BYTES      1024  /* Produce 1KB chunks */

/* Chunks the engine may produce per wake-up. With CONFIG_FREERTOS_HZ=100
 * the 2 ms tick clamps to one 10 ms FreeRTOS tick, so a single chunk per
 * wake caps production at ~102 KB/s — below the 176.4 KB/s the A2DP
 * callback consumes at 44.1 kHz stereo, causing chronic ring underruns
 * (audible as silence gaps in real audio sources). Eight chunks per wake
 * lift the ceiling to ~819 KB/s; the in-loop high-watermark check still
 * bounds ring occupancy so this cannot overfill the ring. */
#define AUDIO_ENGINE_MAX_CHUNKS_PER_WAKE  8

/* I2S read timeout relationship (CODE_REVIEW 2602101453, A1)
 * 
 * DOCUMENTED HERE for architecture visibility, but DEFINED IN i2s_manager.c
 * to avoid circular include (this header includes i2s_manager.h).
 * 
 * RELATIONSHIP: I2S_READ_TIMEOUT_MS = AUDIO_ENGINE_TICK_MS - 1
 *               Current: 1ms timeout, 2ms tick → 1ms headroom for processing
 * 
 * WHY: I2S read timeout must be < engine tick period to prevent task overrun.
 *      If i2s_channel_read() consumes full tick + processing overhead exceeds
 *      tick rate, causing audio engine jitter and timing violations.
 * 
 * MAINTENANCE: When changing AUDIO_ENGINE_TICK_MS, must manually update
 *              I2S_READ_TIMEOUT_MS in i2s_manager.c to maintain relationship.
 * 
 * BEHAVIOR: Quick timeout ensures i2s_source_fill() returns promptly. On timeout,
 *           returns 0 (silence) allowing engine to proceed without blocking.
 */

/* Cooperative shutdown event bits (CODE_REVIEW 2602101453, P0.1.1) */
#define ENGINE_RUNNING_BIT   (1 << 0)  /* Set by task when running, cleared on stop request */
#define ENGINE_STOPPED_BIT   (1 << 1)  /* Set by task just before self-delete */

/* Ring buffer watermarks (Phase 2, Task 2.4) */
#define AUDIO_RB_LOW_WATERMARK   (8 * 1024)   /* Resume filling below 8KB used */
#define AUDIO_RB_HIGH_WATERMARK  (24 * 1024)  /* Stop filling above 24KB used */

typedef struct {
    int64_t t_before_us;
    int64_t t_after_us;
    uint32_t dur_us;
    size_t requested;
    size_t got;
    int err;
} i2s_probe_entry_t;

/* Shared state (defined in audio_processor_state.c) */

/* Ring buffer for audio engine architecture (CODE_REVIEW6 Phase 1) */
extern audio_rb_t *s_audio_ring;

/* Audio engine task (CODE_REVIEW6 Phase 2)
 * Only available in device builds (FreeRTOS required) */
#ifndef UNIT_TEST
extern TaskHandle_t s_audio_engine_task_handle;
extern bool s_audio_engine_paused;
extern uint32_t s_span_seq;  /* Span log sequence counter (CODE_REVIEW7 Priority 2) */

/* Cooperative shutdown infrastructure (CODE_REVIEW 2602101453, P0.1.1)
 * Event group for task lifecycle handshake - prevent deadlock/leaks from vTaskDelete() */
extern volatile bool s_engine_stop_requested;
extern EventGroupHandle_t s_engine_events;
#endif

extern bool s_is_initialized;
extern bool s_is_running;
extern bool s_force_synth;
extern bool s_keepalive_armed;
extern uint8_t s_volume_gain;
extern audio_config_t s_audio_config;

/* Volume NVS commit debounce timer (CODE_REVIEW8 Task D)
 * Only available in device builds (esp_timer required) */
#ifndef UNIT_TEST
extern esp_timer_handle_t s_volume_commit_timer;
#endif
extern audio_stats_t s_audio_stats;
extern portMUX_TYPE s_audio_stats_lock;
extern uint32_t s_tag_miss_count;
extern int64_t s_tag_recover_mute_until;
extern size_t s_runtime_work_bytes;
extern uint8_t s_audio_rb_residual[AUDIO_WORK_BUFFER_BYTES];
extern size_t s_audio_rb_residual_len;
extern size_t s_audio_rb_residual_pos;
extern uint8_t *s_work_block;
extern uint8_t *s_capture_buffer;
extern uint8_t *s_proc_buffer;
extern uint8_t *s_proc_buffer2;
extern TickType_t s_diag_next_log_tick;
extern size_t s_diag_last_conv_size;
extern size_t s_diag_last_frame_bytes;
extern int s_diag_last_src_rate;
extern int s_diag_last_dst_rate;
extern bool s_beep_prefill_active;
extern size_t s_beep_prefill_accum_bytes;
extern size_t s_beep_prefill_goal_bytes;
extern bool s_beep_restore_synth;
extern bool s_beep_restore_i2s;  /* F1.2: Restore I2S after beep if it was active */
extern volatile bool s_drop_ring_audio;  /* F1.4: Drain ring buffer on beep start */
extern bool s_trace_next_read_call;
extern bool s_trace_read_until_beep_done;
extern bool s_last_source_was_synth;
extern unsigned s_i2s_read_ops;
extern unsigned s_i2s_total_read_bytes;
extern unsigned s_i2s_timeout_count;
extern unsigned s_probe_captured;
extern unsigned s_probe_target;
extern bool s_dram_only_alloc;
/* WAV externs removed (play_manager deleted): s_wav_lock, s_wav_playback_active, s_wav_pending_bytes, s_wav_prev_* */
extern portMUX_TYPE s_beep_lock;
extern size_t s_beep_remaining_bytes;
extern bool s_dump_next_beep_diag;
#ifdef CONFIG_BT_MOCK_TESTING
extern int s_i2s_consecutive_failures;
extern int s_last_i2s_failure_log;
#endif
extern i2s_probe_entry_t s_probe_buf[I2S_PROBE_MAX_ENTRIES];
#ifdef UNIT_TEST
extern uint32_t s_last_beep_duration_ms;
extern double s_last_beep_freq_hz;
#endif

/* Shared helpers */
size_t audio_get_runtime_work_bytes(void);
int audio_bytes_per_sample(audio_bit_depth_t bit_depth);
void apply_volume(void* buffer, size_t size, uint8_t volume);

/* WAV helpers removed (play_manager deleted) */

/* Beep helpers */
void audio_processor_beep_reset(void);

/* Diagnostics */
void diag_dump_bytes(const void* data, size_t len, const char* tag);

/* External weak hook */
bool bt_manager_is_a2dp_connected(void);

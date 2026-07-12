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

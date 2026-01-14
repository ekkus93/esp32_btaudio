#include "audio_processor_internal.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
#include "esp_psram.h"
#endif

volatile bool s_audio_diag_enabled = false;
bool s_is_initialized = false;
bool s_is_running = false;
bool s_force_synth = false;
bool s_keepalive_armed = false;
uint8_t s_volume_gain = 100;
audio_config_t s_audio_config = {0};
audio_stats_t s_audio_stats = {0};
uint32_t s_tag_miss_count = 0;
int64_t s_tag_recover_mute_until = 0;
size_t s_runtime_work_bytes = 0;
uint8_t s_audio_rb_residual[AUDIO_WORK_BUFFER_BYTES] = {0};
size_t s_audio_rb_residual_len = 0;
size_t s_audio_rb_residual_pos = 0;
uint8_t *s_work_block = NULL;
uint8_t *s_capture_buffer = NULL;
uint8_t *s_proc_buffer = NULL;
uint8_t *s_proc_buffer2 = NULL;
TickType_t s_diag_next_log_tick = 0;
size_t s_diag_last_conv_size = SIZE_MAX;
size_t s_diag_last_frame_bytes = SIZE_MAX;
int s_diag_last_src_rate = -1;
int s_diag_last_dst_rate = -1;
bool s_beep_prefill_active = false;
size_t s_beep_prefill_accum_bytes = 0;
size_t s_beep_prefill_goal_bytes = 0;
bool s_beep_restore_synth = false;
bool s_trace_next_read_call = false;
bool s_last_source_was_synth = false;
unsigned s_i2s_read_ops = 0;
unsigned s_i2s_total_read_bytes = 0;
unsigned s_i2s_timeout_count = 0;
unsigned s_probe_captured = 0;
unsigned s_probe_target = 0;
bool s_dram_only_alloc = false;
portMUX_TYPE s_wav_lock = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_wav_playback_active = false;
volatile size_t s_wav_pending_bytes = 0;
bool s_wav_prev_valid = false;
bool s_wav_prev_force_synth = false;
#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
size_t s_test_queue_block_override = 0;
#endif
portMUX_TYPE s_beep_lock = portMUX_INITIALIZER_UNLOCKED;
size_t s_beep_remaining_bytes = 0;
bool s_dump_next_beep_diag = false;
#ifdef CONFIG_BT_MOCK_TESTING
int s_i2s_consecutive_failures = 0;
int s_last_i2s_failure_log = 0;
#endif
i2s_probe_entry_t s_probe_buf[I2S_PROBE_MAX_ENTRIES] = {0};
#ifdef UNIT_TEST
uint32_t s_last_beep_duration_ms = 0;
double s_last_beep_freq_hz = 0.0;
#endif

size_t audio_get_runtime_work_bytes(void)
{
    size_t bytes = s_runtime_work_bytes;
    if (bytes == 0U) {
        bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    }
    return bytes;
}

int audio_bytes_per_sample(audio_bit_depth_t bit_depth)
{
    switch (bit_depth) {
        case AUDIO_BIT_DEPTH_24:
        case AUDIO_BIT_DEPTH_32:
            return 4;
        case AUDIO_BIT_DEPTH_16:
        default:
            return 2;
    }
}

bool __attribute__((weak)) bt_manager_is_a2dp_connected(void)
{
    return true;
}
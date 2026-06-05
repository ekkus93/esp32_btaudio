#include "cmd_handlers.h"
#include "audio_processor_internal.h"
#include "audio_span_log.h"
#include <inttypes.h>
/* CODE_REVIEW5 Task 3.1: Need bt_get_streaming_info() but skip duplicate bt_device_t */
#define BT_SOURCE_SKIP_DEVICE_STRUCT 1
#include "bt_source.h"
#undef BT_SOURCE_SKIP_DEVICE_STRUCT

#if !defined(ESP_PLATFORM)
#if defined(__GNUC__)
extern const char *cmd_version_host_override(void) __attribute__((weak));
#else
extern const char *cmd_version_host_override(void);
#pragma weak cmd_version_host_override
#endif
#endif

static const struct
{
    const char *command;
    const char *params;
    const char *description;
} s_cmd_help_entries[] = {
    {"HELP", NULL, "Show this list"},
    {"STATUS", NULL, "Report system state"},
    {"VERSION", NULL, "Show firmware version"},
    {"SCAN", NULL, "Start Bluetooth device scan"},
    {"CONNECT", "<MAC>", "Connect by MAC address"},
    {"CONNECT_NAME", "<NAME...>", "Connect by device name"},
    {"DISCONNECT", NULL, "Disconnect the active link"},
    {"PAIR", "<MAC|NAME...>", "Initiate pairing"},
    {"CONFIRM_PIN", "[MAC] <ACCEPT|REJECT>", "Respond to SSP confirm"},
    {"ENTER_PIN", "[MAC] <PIN>", "Submit legacy PIN code"},
    {"SET_DEFAULT_PIN", "<PIN>", "Persist default PIN for pairing"},
    {"PAIRED", NULL, "List stored paired devices"},
    {"UNPAIR", "<MAC>", "Remove a paired device"},
    {"UNPAIR_ALL", NULL, "Erase all paired devices"},
    {"FILE", "<NAME>", "Report file size if present"},
    {"FILES", NULL, "List files stored on /spiffs"},
    {"PARTS", NULL, "List partitions visible to esp_partition API"},
    {"SET_NAME", "<NAME>", "Persist the local Bluetooth name"},
    {"START", NULL, "Start A2DP audio streaming"},
    {"STOP", NULL, "Stop A2DP audio streaming"},
    {"VOLUME", "<0-100>", "Set playback volume"},
    {"MUTE", NULL, "Mute audio output"},
    {"UNMUTE", NULL, "Unmute audio output"},
    {"SAMPLE_RATE", "<Hz>", "Apply I2S sample rate"},
    {"SYNTH", "ON|OFF", "Force synthetic audio source on/off (diagnostic)"},
    {"BEEP", NULL, "Play 10s middle-C tone when connected"},
    {"DIAG", NULL, "Report connection/stream/I2S/beep state"},
    {"AUDIO_STATUS", NULL, "Report audio engine stats and ring buffer state"},
    {"SPANLOG", "[N]", "Dump last N span log entries (default 10, max 100)"},
    {"DEBUG LOG", "<TAG> <LEVEL>", "Set log level for a tag at runtime"},
    {"I2S_CONFIG", "BCLK,WCLK,DOUT,DIN [RATE] [BIT_DEPTH] [CHANNELS]", "Configure I2S pins and format"},
    {"MEM", NULL, "Show free memory (DRAM/INTERNAL/8BIT/PSRAM)"},
    {"RESET", NULL, "Reboot the device"},
#ifdef ESP_PLATFORM
    {"DEBUG", "<SUBCMD>", "Developer diagnostics"},
#endif
};

cmd_status_t cmd_handle_help(const cmd_context_t *ctx)
{
    (void)ctx;
    const size_t count = sizeof(s_cmd_help_entries) / sizeof(s_cmd_help_entries[0]);
    char data[128];
    snprintf(data, sizeof(data), "%u commands available", (unsigned)count);
    cmd_send_response("INFO", "HELP", "SUMMARY", data);
    cmd_send_response("INFO", "HELP", "FORMAT", "COMMAND [ARGS] - DESCRIPTION");
    for (size_t i = 0; i < count; ++i)
    {
        const char *params = s_cmd_help_entries[i].params ? s_cmd_help_entries[i].params : "";
        char line[256];
        if (params[0] != '\0')
        {
            snprintf(line, sizeof(line), "%s %s - %s", s_cmd_help_entries[i].command, params, s_cmd_help_entries[i].description);
        }
        else
        {
            snprintf(line, sizeof(line), "%s - %s", s_cmd_help_entries[i].command, s_cmd_help_entries[i].description);
        }
        cmd_send_response("INFO", "HELP", "ENTRY", line);
    }
    cmd_send_response("OK", "HELP", "DONE", NULL);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_status(const cmd_context_t *ctx)
{
    (void)ctx;
    audio_status_t status = {0};
    int paired_count = 0;
    int mute_val = -1;
    int sample_rate_val = 0;
#ifdef ESP_PLATFORM
    if (audio_processor_get_status(&status) == ESP_OK)
    {
        mute_val = status.mute ? 1 : 0;
        sample_rate_val = (int)status.sample_rate;
    }
#else
    (void)status;
    if (audio_processor_get_status(&status) == ESP_OK)
    {
        mute_val = 0;
        sample_rate_val = 0;
    }
#endif
    (void)nvs_storage_get_paired_count(&paired_count);
    
    /* CODE_REVIEW5 Task 3.1: Include streaming stats in STATUS output */
    /* CODE_REVIEW8 Task B: Check bt_get_streaming_info return value */
    bt_streaming_info_t stream_info = {0};
    esp_err_t stream_info_err = bt_get_streaming_info(&stream_info);
    
    /* CODE_REVIEW5 Task 3.2: Calculate underrun rate */
    float underrun_rate = 0.0f;
    if (stream_info.total_callbacks > 0) {
        underrun_rate = ((float)stream_info.underrun_count / (float)stream_info.total_callbacks) * 100.0f;
    }
    
    char data[512];
    if (stream_info_err == ESP_OK) {
        snprintf(data, sizeof(data), 
                 "MUTE=%d,SAMPLE_RATE=%d,PAIRED_COUNT=%d,INIT=%d,RUN=%d,VOL=%d,"
                 "BYTES_REQ=%" PRIu64 ",BYTES_PROD=%" PRIu64 ",BYTES_SILENCE=%" PRIu64
                 ",PKTS=%lu,PKT_ERR=%lu,DUR=%lu,"
                 "UNDERRUNS=%lu,CALLBACKS=%lu,UNDERRUN_RATE=%.2f",
                 mute_val, sample_rate_val, paired_count, status.initialized, status.running, status.volume,
                 stream_info.bytes_requested, stream_info.bytes_produced,
                 stream_info.bytes_silence, (unsigned long)stream_info.packets_sent,
                 (unsigned long)stream_info.packet_errors, (unsigned long)stream_info.stream_duration,
                 (unsigned long)stream_info.underrun_count, (unsigned long)stream_info.total_callbacks,
                 underrun_rate);
    } else {
        /* Streaming info unavailable - report basic status only */
        snprintf(data, sizeof(data), 
                 "MUTE=%d,SAMPLE_RATE=%d,PAIRED_COUNT=%d,INIT=%d,RUN=%d,VOL=%d,"
                 "STREAM_INFO=UNAVAILABLE",
                 mute_val, sample_rate_val, paired_count, status.initialized, status.running, status.volume);
    }
    cmd_send_response("OK", "STATUS", "CURRENT", data);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_spanlog(const cmd_context_t *ctx)
{
#ifdef ESP_PLATFORM
    /* Parse count parameter (default: 10, max: 100) */
    size_t count = 10;  /* Default count */
    if (ctx->param_count > 0) {
        long parsed = strtol(ctx->params[0], NULL, 10);
        if (parsed > 0 && parsed <= 100) {
            count = (size_t)parsed;
        } else if (parsed > 100) {
            count = 100;  /* Clamp to max */
        } else {
            cmd_send_response("ERROR", "SPANLOG", "INVALID_PARAM", "Count must be 1-100");
            return CMD_ERROR_INVALID_PARAM;
        }
    }

    /* Allocate buffer for span entries */
    audio_rb_span_t *spans = (audio_rb_span_t *)malloc(count * sizeof(audio_rb_span_t));
    if (!spans) {
        cmd_send_response("ERROR", "SPANLOG", "NO_MEMORY", "Failed to allocate buffer");
        return CMD_ERROR_INIT_FAILED;
    }

    /* Retrieve span log entries */
    size_t actual = 0;
    if (!span_log_get_last_n(spans, count, &actual)) {
        free(spans);
        cmd_send_response("ERROR", "SPANLOG", "NOT_INITIALIZED", "Span log not initialized");
        return CMD_ERROR_NOT_INITIALIZED;
    }

    /* Send CSV header */
    cmd_send_response("OK", "SPANLOG", "", "seq,timestamp_ms,bytes,ring_used_kb,source,flags");

    /* Send each span entry as CSV line */
    for (size_t i = 0; i < actual; i++) {
        const audio_rb_span_t *s = &spans[i];
        
        /* Decode source enum to string */
        const char *source_str = "UNKNOWN";
        if (s->source == AUDIO_SPAN_SOURCE_I2S) {
            source_str = "I2S";
        } else if (s->source == AUDIO_SPAN_SOURCE_SYNTH) {
            source_str = "SYNTH";
        } else if (s->source == AUDIO_SPAN_SOURCE_SILENCE) {
            source_str = "SILENCE";
        }
        
        /* Format flags as hex */
        char data_line[256];
        snprintf(data_line, sizeof(data_line), "%lu,%lu,%zu,%u,%s,0x%02X",
                 (unsigned long)s->seq,
                 (unsigned long)s->timestamp_ms,
                 s->bytes,
                 (unsigned)s->ring_used_kb,
                 source_str,
                 (unsigned)s->flags);
        
        /* Send row as INFO response (not OK to avoid confusion with header) */
        cmd_send_response("INFO", "SPANLOG", "ENTRY", data_line);
    }

    free(spans);
#else
    (void)ctx;
    cmd_send_response("ERROR", "SPANLOG", "NOT_SUPPORTED", "Span log only available on ESP32");
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_mem(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    size_t free_default = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
    size_t free_psram = 0;
#endif
    char data[128];
    snprintf(data, sizeof(data), "DRAM=%zu,INTERNAL=%zu,8BIT=%zu,PSRAM=%zu", free_default, free_internal, free_8bit, free_psram);
    cmd_send_response("OK", "MEM", "STATS", data);
#else
    cmd_send_response("OK", "MEM", "MOCK", "DRAM=0,INTERNAL=0,8BIT=0,PSRAM=0");
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_audio_status(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    audio_stats_t stats = {0};
    esp_err_t err = audio_processor_get_stats(&stats);
    if (err != ESP_OK) {
        cmd_send_response("ERR", "AUDIO_STATUS", "FAILED", esp_err_to_name(err));
        return CMD_SUCCESS;
    }
    
    /* Query ring buffer state */
    extern audio_rb_t *s_audio_ring;  /* From audio_processor_internal.h */
    size_t ring_cap = 0;
    size_t ring_used = 0;
    size_t ring_free = 0;
    if (s_audio_ring != NULL) {
        ring_cap = audio_rb_capacity(s_audio_ring);
        ring_free = audio_rb_available_to_write(s_audio_ring);
        ring_used = ring_cap - ring_free;
    }
    
    /* Determine current source from stats (source with most recent bytes)
     * This is heuristic - actual source is only known in audio_engine_task */
    const char *source_name = "UNKNOWN";
    uint64_t max_bytes = 0;
    if (stats.bytes_by_source[0] > max_bytes) {  /* I2S */
        max_bytes = stats.bytes_by_source[0];
        source_name = "I2S";
    }
    if (stats.bytes_by_source[1] > max_bytes) {  /* SYNTH */
        max_bytes = stats.bytes_by_source[1];
        source_name = "SYNTH";
    }
    if (stats.bytes_by_source[2] > max_bytes) {  /* SILENCE */
        source_name = "SILENCE";
    }
    
    /* Check if beep is currently active */
    const char *beep_state = audio_processor_is_beep_active() ? "yes" : "no";
    
    /* Format response: CODE_REVIEW6 Phase 4, Task 4.3
     * RING_CAP, RING_USED, RING_FREE, RING_PEAK: Ring buffer metrics
     * SOURCE: Current/dominant audio source
     * BEEP: Beep overlay active
     * UNDERRUNS, UNDERRUN_BYTES: Consumer starvation stats
     * WAV_BYTES, I2S_BYTES, SYNTH_BYTES, SILENCE_BYTES: Per-source totals
     * SOURCE_SWITCHES: Source change count
     * BEEP_OVERLAYS: Beep mix count
     * ENGINE_WRITES, ENGINE_BYTES: Producer write stats
     * ENGINE_PAUSES: Watermark pause count */
    char data[512];
    snprintf(data, sizeof(data),
             "RING_CAP=%zu,RING_USED=%zu,RING_FREE=%zu,RING_PEAK=%zu,"
             "SOURCE=%s,BEEP=%s,"
             "UNDERRUNS=%lu,UNDERRUN_BYTES=%llu,"
             "WAV_BYTES=%llu,I2S_BYTES=%llu,SYNTH_BYTES=%llu,SILENCE_BYTES=%llu,"
             "SOURCE_SWITCHES=%lu,BEEP_OVERLAYS=%lu,BEEP_BYTES=%llu,"
             "ENGINE_WRITES=%lu,ENGINE_BYTES=%llu,ENGINE_PAUSES=%lu",
             ring_cap, ring_used, ring_free, stats.ring_peak_used,
             source_name, beep_state,
             (unsigned long)stats.buffer_underruns, (unsigned long long)stats.underrun_bytes,
             (unsigned long long)stats.bytes_by_source[0],
             (unsigned long long)stats.bytes_by_source[1],
             (unsigned long long)stats.bytes_by_source[2],
             (unsigned long long)stats.bytes_by_source[3],
             (unsigned long)stats.source_switch_count,
             (unsigned long)stats.beep_overlay_count,
             (unsigned long long)stats.beep_overlay_bytes,
             (unsigned long)stats.engine_write_calls,
             (unsigned long long)stats.engine_write_bytes,
             (unsigned long)stats.engine_pause_count);
    
    cmd_send_response("OK", "AUDIO_STATUS", "CURRENT", data);
#else
    cmd_send_response("OK", "AUDIO_STATUS", "MOCK", "RING_CAP=0,RING_USED=0,SOURCE=MOCK,BEEP=no");
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_version(const cmd_context_t *ctx)
{
    (void)ctx;
    char version[64] = {0};
    char metadata[128] = {0};

#ifdef ESP_PLATFORM
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc != NULL)
    {
        if (desc->version[0] != '\0')
        {
            snprintf(version, sizeof(version), "%s", desc->version);
        }
        if (desc->project_name[0] != '\0')
        {
            cmd_append_metadata(metadata, sizeof(metadata), "PROJECT", desc->project_name);
        }
        if (desc->date[0] != '\0' || desc->time[0] != '\0')
        {
            char build_info[64] = {0};
            if (desc->date[0] != '\0' && desc->time[0] != '\0')
            {
                snprintf(build_info, sizeof(build_info), "%s %s", desc->date, desc->time);
            }
            else if (desc->date[0] != '\0')
            {
                snprintf(build_info, sizeof(build_info), "%s", desc->date);
            }
            else if (desc->time[0] != '\0')
            {
                snprintf(build_info, sizeof(build_info), "%s", desc->time);
            }
            if (build_info[0] != '\0')
            {
                cmd_append_metadata(metadata, sizeof(metadata), "BUILD", build_info);
            }
        }
    }
#ifdef CONFIG_APP_PROJECT_VER
    if (version[0] == '\0' && strlen(CONFIG_APP_PROJECT_VER) > 0)
    {
        snprintf(version, sizeof(version), "%s", CONFIG_APP_PROJECT_VER);
    }
#endif
    if (version[0] == '\0')
    {
        snprintf(version, sizeof(version), "UNKNOWN");
    }
#else
    const char *override = NULL;
    if (cmd_version_host_override)
    {
        override = cmd_version_host_override();
    }
    if (override != NULL && override[0] != '\0')
    {
        snprintf(version, sizeof(version), "%s", override);
    }
    else
    {
        snprintf(version, sizeof(version), "HOST-MOCK");
    }
#endif

    cmd_send_response("OK", "VERSION", version, metadata[0] != '\0' ? metadata : NULL);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_reset(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    cmd_send_response("OK", "RESET", "REBOOTING", NULL);
    esp_restart();
#else
    cmd_send_response("OK", "RESET", "MOCK_REBOOT", NULL);
#endif
    return CMD_SUCCESS;
}

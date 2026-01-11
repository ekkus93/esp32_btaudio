#include "cmd_handlers.h"

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
    {"DEBUG LOG", "<TAG> <LEVEL>", "Set log level for a tag at runtime"},
    {"I2S_CONFIG", "BCLK,WCLK,DOUT,DIN", "Configure I2S pins"},
    {"PLAY", "<FILENAME>", "Play a WAV file from /spiffs (host-mode)"},
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
    char data[256];
    snprintf(data, sizeof(data), "MUTE=%d,SAMPLE_RATE=%d,PAIRED_COUNT=%d,INIT=%d,RUN=%d,VOL=%d",
             mute_val, sample_rate_val, paired_count, status.initialized, status.running, status.volume);
    cmd_send_response("OK", "STATUS", "CURRENT", data);
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

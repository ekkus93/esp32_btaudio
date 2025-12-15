#include "command_interface.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include "esp_log.h"
#if !defined(ESP_PLATFORM)
#include <sys/time.h>
#if defined(__GNUC__)
extern const char *cmd_files_host_mount_override(void) __attribute__((weak));
#else
extern const char *cmd_files_host_mount_override(void);
#pragma weak cmd_files_host_mount_override
#endif
#endif

#define CMD_FILES_WARN_NAME_MAX 80
#define CMD_FILES_ITEM_NAME_MAX 128
#define CMD_FILES_SUMMARY_ROOT_MAX 120
#define CMD_BEEP_DURATION_MS 10000U
#define CMD_BEEP_FREQ_HZ 261.63

#if !defined(ESP_PLATFORM)
/* Shared host-side log level state so tests and production code agree. */
int g_mock_log_level = ESP_LOG_INFO;
#endif

static void copy_truncated_identifier(const char *src, char *dst, size_t dst_size)
{
    if (dst_size == 0)
    {
        return;
    }
    if (src == NULL || src[0] == '\0')
    {
        dst[0] = '\0';
        return;
    }

    size_t max_copy = dst_size - 1;
    size_t i = 0;
    while (i < max_copy && src[i] != '\0')
    {
        dst[i] = src[i];
        ++i;
    }

    if (src[i] != '\0' && dst_size > 4)
    {
        size_t ellipsis_start = dst_size - 4;
        if (ellipsis_start > i)
        {
            ellipsis_start = i;
        }
        dst[ellipsis_start] = '.';
        dst[ellipsis_start + 1] = '.';
        dst[ellipsis_start + 2] = '.';
        dst[ellipsis_start + 3] = '\0';
    }
    else
    {
        dst[i] = '\0';
    }
}

static bool cmd_parse_log_level(const char *level_str, int *out_level)
{
    if (level_str == NULL || out_level == NULL)
    {
        return false;
    }

    /* Accept numeric levels 0-5 for convenience. */
    char *end = NULL;
    long numeric = strtol(level_str, &end, 10);
    if (end != level_str && *end == '\0')
    {
        if (numeric >= ESP_LOG_NONE && numeric <= ESP_LOG_VERBOSE)
        {
            *out_level = (int)numeric;
            return true;
        }
        return false;
    }

    /* Accept common level strings (case-insensitive). */
    if (strcasecmp(level_str, "NONE") == 0)
        *out_level = ESP_LOG_NONE;
    else if (strcasecmp(level_str, "ERROR") == 0 || strcasecmp(level_str, "ERR") == 0)
        *out_level = ESP_LOG_ERROR;
    else if (strcasecmp(level_str, "WARN") == 0 || strcasecmp(level_str, "WARNING") == 0)
        *out_level = ESP_LOG_WARN;
    else if (strcasecmp(level_str, "INFO") == 0)
        *out_level = ESP_LOG_INFO;
    else if (strcasecmp(level_str, "DEBUG") == 0 || strcasecmp(level_str, "DBG") == 0)
        *out_level = ESP_LOG_DEBUG;
    else if (strcasecmp(level_str, "VERBOSE") == 0 || strcasecmp(level_str, "TRACE") == 0)
        *out_level = ESP_LOG_VERBOSE;
    else
        return false;

    return true;
}

// Include platform headers and storage/bt mocks so host builds have prototypes
#include "audio_processor.h"
#include "nvs_storage.h"
#include "bt_manager.h"
// Ensure bt_get_connection_state is declared for device builds. The
// declaration usually lives in the bluetooth component headers, but some
// build configurations don't expose that include path to this component.
// Provide a local extern declaration to avoid implicit-function warnings
// and keep the production behavior unchanged.
#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
extern int bt_get_connection_state(void);
/* Also allow checking whether streaming is active; some connection paths
 * set a streaming-active flag even if the simple connected getter isn't
 * updated yet. Declare the integer helper used elsewhere in the code so
 * we can make a permissive check in the BEEP command handler. */
extern int bt_get_streaming_state_int(void);
#endif

/*
 * Ensure prototypes are visible to unit-test builds. In some host/unit-test
 * configurations the compatibility macros below remap bt_manager_* names to
 * other symbols; however the compiler still needs a prototype. The public
 * header `bt_manager.h` provides canonical prototypes (bt_disconnect,
 * bt_start_audio, bt_stop_audio). For legacy code that calls the
 * bt_manager_* names in unit tests we provide small forward declarations
 * that match the wrapper signatures used here (returning int on host).
 */
#if defined(UNIT_TEST) && !defined(ESP_PLATFORM)
/* forward-declare the wrappers used by the command layer during unit tests */
int bt_manager_disconnect(void);
int bt_manager_start_audio(void);
int bt_manager_stop_audio(void);
int bt_get_connection_state(void);
/* start/scan wrapper used by tests/compatibility layer */
int bt_manager_start_scan(void);
/* pairing start wrapper used by tests/compatibility layer */
int bt_manager_start_pair(const char *mac);
#endif

#if defined(ESP_PLATFORM)
#include "esp_gap_bt_api.h"
#else
#include "esp_bt.h"
#endif

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

// Provide a minimal UART abstraction for host/unit-test builds so the same
// symbols are available across build targets. On-device builds include the
// real driver header.
#if defined(UNIT_TEST) && !defined(ESP_PLATFORM)
#include "mock_uart.h"
#else
#include "driver/uart.h"
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#define TAG "CMD_IF"
/* Prefer the configured console UART if available so we don't call into an
 * uninstalled UART driver (which logs "uart driver error"). Fall back to
 * UART_NUM_1 for platforms that don't provide CONFIG_ESP_CONSOLE_UART_NUM. */
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
#define CMD_UART_NUM CONFIG_ESP_CONSOLE_UART_NUM
#else
#define CMD_UART_NUM UART_NUM_1
#endif

#ifdef ESP_PLATFORM
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_partition.h"
#endif
#define CMD_BUF_SIZE 256
#else
/* Host/unit tests should map CMD_UART_NUM to the mock UART port. The mock
 * implementation defines UART_NUM_1 or provides compatible symbols. If the
 * mock doesn't provide CMD_UART_NUM, default to UART_NUM_1 so host tests
 * behave like the device default. */
#ifndef CMD_UART_NUM
#define CMD_UART_NUM UART_NUM_1
#endif
#endif

// Minimal BT-success macro for compatibility
// Use esp_err_t / ESP_OK for result semantics instead of legacy BT_SUCCESS macro
#include "esp_err.h"

#if !defined(ESP_PLATFORM)
#if defined(__GNUC__)
extern const char *cmd_version_host_override(void) __attribute__((weak));
#else
extern const char *cmd_version_host_override(void);
#endif
#endif

// Compatibility mappings: older code used bt_manager_* names; map them
// to the currently exported bt_* functions in `bt_manager.h` when
// available. Provide a no-op fallback for set_name if the symbol is
// missing in the current API.
#ifdef ESP_PLATFORM
#ifndef bt_manager_start_scan
#define bt_manager_start_scan bt_start_scan
#endif
#ifndef bt_manager_connect
#define bt_manager_connect bt_connect
#endif
#ifndef bt_connect_by_name
/* bt_connect_by_name is expected from bt_manager; fall through if present */
#endif
#ifndef bt_manager_disconnect
#define bt_manager_disconnect bt_disconnect
#endif
#ifndef bt_manager_start_audio
#define bt_manager_start_audio bt_start_audio
#endif
#ifndef bt_manager_stop_audio
#define bt_manager_stop_audio bt_stop_audio
#endif
#ifndef bt_manager_pair
#define bt_manager_pair bt_pair
#endif
#ifndef bt_manager_set_name
static inline void bt_manager_set_name(const char *name) { (void)name; }
#endif
#endif

// Lightweight internal mock state used for deterministic host-mode pairing
static bool s_cmd_mock_enabled = false;
static char s_cmd_mock_pairing_addr[32] = {0};
static char s_cmd_mock_passkey[16] = {0};

// Sequence counter for pairing event ordering
static uint32_t s_event_sequence = 0;

typedef struct
{
    const char *command;
    const char *params;
    const char *description;
} cmd_help_entry_t;

static const cmd_help_entry_t s_cmd_help_entries[] = {
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
    {"DEBUG LOG", "<TAG> <LEVEL>", "Set log level for a tag at runtime"},
    {"I2S_CONFIG", "BCLK,WCLK,DOUT,DIN", "Configure I2S pins"},
    {"PLAY", "<FILENAME>", "Play a WAV file from /spiffs (host-mode)"},
    {"MEM", NULL, "Show free memory (DRAM/INTERNAL/8BIT/PSRAM)"},
    {"RESET", NULL, "Reboot the device"},
#ifdef ESP_PLATFORM
    {"DEBUG", "<SUBCMD>", "Developer diagnostics"},
#endif
};

static void cmd_help_emit_all(void)
{
    const size_t count = sizeof(s_cmd_help_entries) / sizeof(s_cmd_help_entries[0]);
    char data[128];
    snprintf(data, sizeof(data), "%u commands available", (unsigned)count);
    cmd_send_response("INFO", "HELP", "SUMMARY", data);
    cmd_send_response("INFO", "HELP", "FORMAT", "COMMAND [ARGS] - DESCRIPTION");
    for (size_t i = 0; i < count; ++i)
    {
        const cmd_help_entry_t *entry = &s_cmd_help_entries[i];
        const char *params = entry->params ? entry->params : "";
        char line[256];
        if (params[0] != '\0')
        {
            snprintf(line, sizeof(line), "%s %s - %s", entry->command, params, entry->description);
        }
        else
        {
            snprintf(line, sizeof(line), "%s - %s", entry->command, entry->description);
        }
        cmd_send_response("INFO", "HELP", "ENTRY", line);
    }
    cmd_send_response("OK", "HELP", "DONE", NULL);
}

static uint64_t cmd_get_timestamp_ms(void)
{
#ifdef ESP_PLATFORM
    /* esp_timer_get_time returns microseconds since boot */
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
    }
#endif
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
    {
        return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
    }
    /* Fallback monotonic counter in case time APIs are unavailable */
    static uint64_t fallback_ms;
    return ++fallback_ms;
#endif
}

static void cmd_append_metadata(char *buf, size_t buf_len, const char *key, const char *value)
{
    if (!buf || buf_len == 0 || !key || !value || value[0] == '\0')
    {
        return;
    }

    size_t used = strlen(buf);
    if (used >= buf_len - 1)
    {
        return;
    }

    int written = snprintf(buf + used, buf_len - used, "%s%s=%s",
                           (used > 0) ? "," : "", key, value);
    if (written < 0)
    {
        buf[0] = '\0';
    }
}

static void cmd_handle_version(void)
{
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
#if defined(__GNUC__)
    if (cmd_version_host_override)
    {
        override = cmd_version_host_override();
    }
#else
    override = cmd_version_host_override();
#endif
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
}

static const char *cmd_files_get_root(void)
{
#ifdef ESP_PLATFORM
    return "/spiffs";
#else
    const char *(*override_fn)(void) = cmd_files_host_mount_override;
    if (override_fn != NULL)
    {
        const char *override = override_fn();
        if (override != NULL && override[0] != '\0')
        {
            return override;
        }
    }
    return "/spiffs";
#endif
}

#if defined(UNIT_TEST)
static cmd_test_spiffs_mount_hook_t s_cmd_spiffs_mount_hook = NULL;

void cmd_test_install_spiffs_mount_hook(cmd_test_spiffs_mount_hook_t hook)
{
    s_cmd_spiffs_mount_hook = hook;
}

static inline void cmd_test_notify_spiffs_mount_hook(void)
{
    if (s_cmd_spiffs_mount_hook)
    {
        s_cmd_spiffs_mount_hook();
    }
}
#else
static inline void cmd_test_notify_spiffs_mount_hook(void)
{
}
#endif

#ifdef ESP_PLATFORM
static esp_err_t cmd_mount_spiffs_if_needed(void)
{
    cmd_test_notify_spiffs_mount_hook();
    if (esp_spiffs_mounted("spiffs"))
    {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t cfg = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&cfg);
    if (err == ESP_ERR_INVALID_STATE && esp_spiffs_mounted("spiffs"))
    {
        return ESP_OK;
    }
    return err;
}
#else
static esp_err_t cmd_mount_spiffs_if_needed(void)
{
    cmd_test_notify_spiffs_mount_hook();
    return ESP_OK;
}
#endif

// Test-only function to reset sequence counter
#if defined(UNIT_TEST) || defined(ESP_PLATFORM)
void cmd_reset_event_sequence(void)
{
    s_event_sequence = 0;
}
#endif

// Main command executor. Keeps behavior identical across ESP and host
// builds but uses #ifdef guards where device-specific APIs are required.
cmd_status_t cmd_execute(const cmd_context_t *ctx)
{
    if (ctx == NULL)
        return CMD_ERROR_NOT_INITIALIZED;

    switch (ctx->type)
    {
    case CMD_TYPE_STATUS:
    {
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
    }
    break;

    case CMD_TYPE_MEM:
    {
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
    }
    break;

    case CMD_TYPE_VERSION:
        cmd_handle_version();
        break;

    case CMD_TYPE_RESET:
#ifdef ESP_PLATFORM
        cmd_send_response("OK", "RESET", "REBOOTING", NULL);
        esp_restart();
#else
        cmd_send_response("OK", "RESET", "MOCK_REBOOT", NULL);
#endif
        break;

    case CMD_TYPE_SCAN:
#ifdef ESP_PLATFORM
        if (bt_manager_start_scan() == ESP_OK)
            cmd_send_response("OK", "SCAN", "STARTED", NULL);
        else
            cmd_send_response("ERR", "SCAN", "FAILED", NULL);
#elif defined(UNIT_TEST)
        /* In unit tests call into the manager so tests can observe the
         * interaction via test hooks/mocks. bt_manager_start_scan() returns
         * an esp_err_t-like value (ESP_OK==0) in our host-mode build. */
        if (bt_manager_start_scan() == ESP_OK)
            cmd_send_response("OK", "SCAN", "MOCK_STARTED", NULL);
        else
            cmd_send_response("ERR", "SCAN", "FAILED", NULL);
#else
        cmd_send_response("OK", "SCAN", "MOCK_STARTED", NULL);
#endif
        break;

    case CMD_TYPE_SYNTH:
    {
        /* Toggle synthetic audio generation used for diagnostics and to
         * isolate pipeline issues. Usage: SYNTH ON|OFF */
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "SYNTH", "MISSING_PARAM", NULL);
            break;
        }
        const char *p = ctx->params[0];
        if (strcasecmp(p, "ON") == 0 || strcmp(p, "1") == 0 || strcasecmp(p, "TRUE") == 0)
        {
#ifdef ESP_PLATFORM
            audio_processor_set_synth_mode(true);
#endif
            cmd_send_response("OK", "SYNTH", "ENABLED", NULL);
        }
        else if (strcasecmp(p, "OFF") == 0 || strcmp(p, "0") == 0 || strcasecmp(p, "FALSE") == 0)
        {
#ifdef ESP_PLATFORM
            audio_processor_set_synth_mode(false);
#endif
            cmd_send_response("OK", "SYNTH", "DISABLED", NULL);
        }
        else
        {
            cmd_send_response("ERR", "SYNTH", "BAD_PARAM", p);
        }
    }
    break;

    case CMD_TYPE_BEEP:
    {
        /* Emit a short audible beep when a BT device is connected or when
         * audio streaming is active. Some connection sequences (or race
         * conditions) update the streaming flag before the simple connected
         * getter; accept either path. */
#if 1
        int conn = 0;
#ifdef ESP_PLATFORM
        conn = bt_get_connection_state();
        int streaming = bt_get_streaming_state_int();
        /* Also check the bt_manager component's view of connection state.
         * In some builds the connection manager used by `bt_get_connection_state`
         * isn't registered for A2DP callbacks while `bt_manager` is the active
         * implementation. Consult the manager as a fallback so the BEEP
         * command isn't rejected when the global manager reports a connection. */
        int mgr_conn = 0;
#ifdef ESP_PLATFORM
        /* bt_manager.h exposes bt_manager_is_connected() implemented in
         * the bt_manager component. Call it when available. */
        extern int bt_manager_is_connected(void);
        mgr_conn = bt_manager_is_connected();
#endif
        /* Diagnostic logging: print both getters so on-device serial logs
         * show the exact runtime values when BEEP is attempted. This helps
         * determine whether the permissive check is being evaluated as false
         * even though other logs indicate a connection. */
        ESP_LOGI(TAG, "DIAG-BEEP: bt_get_connection_state=%d bt_get_streaming_state_int=%d bt_manager_conn=%d", conn, streaming, mgr_conn);
        /* Also emit an unconditionally printed line so consoles that don't
         * capture ESP_LOG output still show the diagnostic marker. Include
         * the manager-level value here as well so all three runtime fields
         * are visible in plain stdout logs. */
        printf("DIAG-BEEP: bt_get_connection_state=%d bt_get_streaming_state_int=%d bt_manager_conn=%d\n", conn, streaming, mgr_conn);
        if (!((conn == 1) || (streaming == 1) || (mgr_conn == 1)))
        {
            cmd_send_response("ERR", "BEEP", "NOT_CONNECTED", NULL);
            break;
        }
#else
        /* Host/unit tests mock bt_get_connection_state(), so call it there.
         * For non-ESP host builds fall back to the simple connected check. */
        if (bt_get_connection_state() != 1)
        {
            cmd_send_response("ERR", "BEEP", "NOT_CONNECTED", NULL);
            break;
        }
#endif
#endif
#ifdef ESP_PLATFORM
#ifdef CONFIG_BEEP_AUTOSTART_STREAMING
        /* If connected but not yet streaming, opportunistically kick off
         * A2DP so the sink pulls audio and the beep is audible. */
        if (streaming != 1)
        {
            (void)bt_manager_start_audio();
            /* Consider adding a simple throttle if allocator pressure appears. */
        }
#endif
#endif

        /* Play a 10s middle-C sine tone for an audible diagnostic. */
        if (audio_processor_beep_tone(CMD_BEEP_DURATION_MS, CMD_BEEP_FREQ_HZ) == ESP_OK)
            cmd_send_response("OK", "BEEP", "SENT", NULL);
        else
            cmd_send_response("ERR", "BEEP", "FAILED", NULL);
    }
    break;

    case CMD_TYPE_CONNECT:
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "CONNECT", "MISSING_PARAM", NULL);
            break;
        }
#ifdef ESP_PLATFORM
        if (bt_manager_connect(ctx->params[0]) == ESP_OK)
            cmd_send_response("OK", "CONNECT", "INITIATED", NULL);
        else
            cmd_send_response("ERR", "CONNECT", "FAILED", NULL);
#else
        cmd_send_response("OK", "CONNECT", "MOCK_CONNECTED", ctx->params[0]);
#endif
        break;

    case CMD_TYPE_DISCONNECT:
#ifdef ESP_PLATFORM
        if (bt_manager_disconnect() == ESP_OK)
            cmd_send_response("OK", "DISCONNECT", "DONE", NULL);
        else
            cmd_send_response("ERR", "DISCONNECT", "FAILED", NULL);
#elif defined(UNIT_TEST)
        /* In unit tests use the real manager wrapper so tests can simulate
         * failure via the bt_manager_test hooks. Preserve the MOCK_* result
         * string on success to remain compatible with existing expectations. */
        if (bt_manager_disconnect() == 0)
            cmd_send_response("OK", "DISCONNECT", "MOCK_DONE", NULL);
        else
            cmd_send_response("ERR", "DISCONNECT", "FAILED", NULL);
#else
        cmd_send_response("OK", "DISCONNECT", "MOCK_DONE", NULL);
#endif
        break;

    case CMD_TYPE_START:
#ifdef ESP_PLATFORM
        if (bt_manager_start_audio() == ESP_OK)
            cmd_send_response("OK", "START", "STARTED", NULL);
        else
            cmd_send_response("ERR", "START", "FAILED", NULL);
#elif defined(UNIT_TEST)
        if (bt_manager_start_audio() == 0)
            cmd_send_response("OK", "START", "MOCK_STARTED", NULL);
        else
            cmd_send_response("ERR", "START", "FAILED", NULL);
#else
        cmd_send_response("OK", "START", "MOCK_STARTED", NULL);
#endif
        break;

    case CMD_TYPE_PLAY:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "PLAY", "MISSING_PARAM", NULL);
            break;
        }
        /* Build file path: if caller provided an absolute path use it, otherwise
         * assume files live under /spiffs/ as a convenience for test flows. */
        char path[256];
        const char *p = ctx->params[0];
        if (p[0] == '/')
        {
            strncpy(path, p, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }
        else
        {
            snprintf(path, sizeof(path), "/spiffs/%s", p);
        }

        esp_err_t mount_err = cmd_mount_spiffs_if_needed();
#ifdef ESP_PLATFORM
        if (mount_err != ESP_OK)
        {
            cmd_send_response("ERR", "PLAY", "SPIFFS_MOUNT_FAILED", esp_err_to_name(mount_err));
            break;
        }

        /* Require A2DP to be present for PLAY. If a device is connected but
         * not currently streaming, attempt to start A2DP and fail if the
         * start call does not succeed. This prevents silently falling back
         * to the local I2S path when a Bluetooth sink is expected. */
        int conn = bt_get_connection_state();
        int streaming = bt_get_streaming_state_int();
        if (conn != 1)
        {
            /* No A2DP connection: return a parseable error and do not queue */
            cmd_send_response("ERR", "PLAY", "A2DP_NOT_CONNECTED", path);
            break;
        }
        if (streaming != 1)
        {
            esp_err_t sret = bt_manager_start_audio();
            if (sret != ESP_OK)
            {
                /* Starting A2DP failed (might be low-memory or other issue).
                 * Report the ESP error name so callers can diagnose and do
                 * not enqueue to the fallback I2S pipeline. */
                cmd_send_response("ERR", "PLAY", esp_err_to_name(sret), path);
                break;
            }
        }
        esp_err_t r = audio_processor_play_wav(path);
        if (r == ESP_OK)
            cmd_send_response("OK", "PLAY", "ENQUEUED", path);
        else
            cmd_send_response("ERR", "PLAY", esp_err_to_name(r), path);
#else
        (void)mount_err;
        (void)path;
        if (audio_processor_play_wav(path) == ESP_OK)
            cmd_send_response("OK", "PLAY", "MOCK_ENQUEUED", ctx->params[0]);
        else
            cmd_send_response("ERR", "PLAY", "MOCK_FAILED", ctx->params[0]);
#endif
    }
    break;

    case CMD_TYPE_STOP:
#ifdef ESP_PLATFORM
        if (bt_manager_stop_audio() == ESP_OK)
            cmd_send_response("OK", "STOP", "STOPPED", NULL);
        else
            cmd_send_response("ERR", "STOP", "FAILED", NULL);
#elif defined(UNIT_TEST)
        if (bt_manager_stop_audio() == 0)
            cmd_send_response("OK", "STOP", "MOCK_STOPPED", NULL);
        else
            cmd_send_response("ERR", "STOP", "FAILED", NULL);
#else
        cmd_send_response("OK", "STOP", "MOCK_STOPPED", NULL);
#endif
        break;

    case CMD_TYPE_FILE:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "FILE", "MISSING_PARAM", NULL);
            break;
        }

        const char *root = cmd_files_get_root();
        const char *requested = ctx->params[0];
        if ((requested == NULL || requested[0] == '\0'))
        {
            cmd_send_response("ERR", "FILE", "MISSING_PARAM", NULL);
            break;
        }

        char fullpath[256];
        if (requested[0] == '/')
        {
            strncpy(fullpath, requested, sizeof(fullpath) - 1);
            fullpath[sizeof(fullpath) - 1] = '\0';
        }
        else
        {
            if (root == NULL || root[0] == '\0')
            {
                cmd_send_response("ERR", "FILE", "NO_ROOT", NULL);
                break;
            }
            size_t root_len = strlen(root);
            const char *sep = (root_len > 0 && root[root_len - 1] == '/') ? "" : "/";
            int written = snprintf(fullpath, sizeof(fullpath), "%s%s%s", root, sep, requested);
            if (written < 0 || (size_t)written >= sizeof(fullpath))
            {
                cmd_send_response("ERR", "FILE", "PATH_TOO_LONG", requested);
                break;
            }
        }

        struct stat st = {0};
        if (stat(fullpath, &st) != 0)
        {
            int stat_err = errno;
#ifdef ESP_PLATFORM
            (void)stat_err;
#else
            (void)stat_err;
#endif
            cmd_send_response("ERR", "FILE", "NOT_FOUND", requested);
            break;
        }

        if (!S_ISREG(st.st_mode))
        {
            cmd_send_response("ERR", "FILE", "NOT_FILE", requested);
            break;
        }

        const char *display_name = requested;
        if (requested[0] == '/')
        {
            const char *slash = strrchr(requested, '/');
            if (slash && *(slash + 1) != '\0')
            {
                display_name = slash + 1;
            }
        }

        char data[160];
        snprintf(data, sizeof(data), "%s,%llu", display_name, (unsigned long long)st.st_size);
        cmd_send_response("OK", "FILE", "FOUND", data);
    }
    break;

    case CMD_TYPE_FILES:
    {
        if (ctx->param_count > 0)
        {
            cmd_send_response("ERR", "FILES", "UNEXPECTED_PARAM", ctx->params[0]);
            break;
        }
        const char *root = cmd_files_get_root();
        if (root == NULL || root[0] == '\0')
        {
            cmd_send_response("ERR", "FILES", "NO_ROOT", NULL);
            break;
        }

        cmd_send_response("INFO", "FILES", "ROOT", root);

        esp_err_t mount_err = cmd_mount_spiffs_if_needed();
#ifdef ESP_PLATFORM
        if (mount_err != ESP_OK)
        {
            char errbuf[64];
            snprintf(errbuf, sizeof(errbuf), "mount_err=%d", (int)mount_err);
            cmd_send_response("ERR", "FILES", "MOUNT_FAILED", errbuf);
            break;
        }
#else
        (void)mount_err;
#endif

        DIR *dir = opendir(root);
        if (dir == NULL)
        {
            char errbuf[96];
            int open_err = errno;
#ifndef ESP_PLATFORM
            const char *reason = strerror(open_err);
            snprintf(errbuf, sizeof(errbuf), "%d:%s", open_err, (reason != NULL) ? reason : "UNKNOWN");
#else
            snprintf(errbuf, sizeof(errbuf), "errno=%d", open_err);
#endif
            cmd_send_response("ERR", "FILES", "OPEN_FAILED", errbuf);
            break;
        }

        int file_count = 0;
        unsigned long long total_size = 0ULL;
        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL)
        {
            const char *name = entry->d_name;
            if (name == NULL || name[0] == '\0')
            {
                continue;
            }
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            {
                continue;
            }

            char fullpath[256];
            size_t root_len = strlen(root);
            const char *sep = (root_len > 0 && root[root_len - 1] == '/') ? "" : "/";
            int written = snprintf(fullpath, sizeof(fullpath), "%s%s%s", root, sep, name);
            if (written < 0 || (size_t)written >= sizeof(fullpath))
            {
                char warn_name[CMD_FILES_WARN_NAME_MAX];
                copy_truncated_identifier(name, warn_name, sizeof(warn_name));
                char warn[128];
                int warn_written = snprintf(warn, sizeof(warn), "SKIP_LONG_PATH,%s", warn_name);
                if (warn_written < 0 || warn_written >= (int)sizeof(warn))
                {
                    strncpy(warn, "SKIP_LONG_PATH,???", sizeof(warn));
                    warn[sizeof(warn) - 1] = '\0';
                }
                cmd_send_response("INFO", "FILES", "SKIP", warn);
                continue;
            }

            struct stat st = {0};
            if (stat(fullpath, &st) != 0)
            {
                char warn_name[CMD_FILES_WARN_NAME_MAX];
                copy_truncated_identifier(name, warn_name, sizeof(warn_name));
                char warn[160];
                int stat_err = errno;
#ifdef ESP_PLATFORM
                int warn_written = snprintf(warn, sizeof(warn), "STAT_FAILED,%s,errno=%d", warn_name, stat_err);
#else
                const char *reason = strerror(stat_err);
                int warn_written = snprintf(warn, sizeof(warn), "STAT_FAILED,%s,%d:%s", warn_name, stat_err, (reason != NULL) ? reason : "UNKNOWN");
#endif
                if (warn_written < 0 || warn_written >= (int)sizeof(warn))
                {
                    strncpy(warn, "STAT_FAILED,???,errno=?", sizeof(warn));
                    warn[sizeof(warn) - 1] = '\0';
                }
                cmd_send_response("INFO", "FILES", "SKIP", warn);
                continue;
            }

            if (!S_ISREG(st.st_mode))
            {
                char warn_name[CMD_FILES_WARN_NAME_MAX];
                copy_truncated_identifier(name, warn_name, sizeof(warn_name));
                char warn[128];
                int warn_written = snprintf(warn, sizeof(warn), "NON_FILE,%s", warn_name);
                if (warn_written < 0 || warn_written >= (int)sizeof(warn))
                {
                    strncpy(warn, "NON_FILE,???", sizeof(warn));
                    warn[sizeof(warn) - 1] = '\0';
                }
                cmd_send_response("INFO", "FILES", "SKIP", warn);
                continue;
            }

            char item_name[CMD_FILES_ITEM_NAME_MAX];
            copy_truncated_identifier(name, item_name, sizeof(item_name));
            char line[192];
            int line_written = snprintf(line, sizeof(line), "%s,%llu", item_name, (unsigned long long)st.st_size);
            if (line_written < 0 || line_written >= (int)sizeof(line))
            {
                strncpy(line, "???,0", sizeof(line));
                line[sizeof(line) - 1] = '\0';
            }
            cmd_send_response("INFO", "FILES", "ITEM", line);
            file_count++;
            total_size += (unsigned long long)st.st_size;
        }

        closedir(dir);

        char root_label[CMD_FILES_SUMMARY_ROOT_MAX];
        copy_truncated_identifier(root, root_label, sizeof(root_label));
        char summary[192];
        int summary_written = snprintf(summary, sizeof(summary), "ROOT=%s,COUNT=%d,TOTAL=%llu", root_label, file_count, total_size);
        if (summary_written < 0 || summary_written >= (int)sizeof(summary))
        {
            strncpy(summary, "ROOT=?,COUNT=?,TOTAL=?", sizeof(summary));
            summary[sizeof(summary) - 1] = '\0';
        }
        cmd_send_response("OK", "FILES", "SUMMARY", summary);
    }
    break;

        case CMD_TYPE_PARTS:
        {
    #ifdef ESP_PLATFORM
            esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
            if (it == NULL) {
                cmd_send_response("ERR", "PARTS", "NONE", NULL);
                break;
            }
            int count = 0;
            /* Iterate partitions using esp_partition_next(cur) which returns
             * the next iterator and internally releases the previous one.
             * Do not call esp_partition_iterator_release() inside the loop
             * to avoid double-freeing the iterator (which caused tlsf
             * assertion failures at runtime). */
            for (esp_partition_iterator_t cur = it; cur != NULL; cur = esp_partition_next(cur)) {
                const esp_partition_t *p = esp_partition_get(cur);
                if (p) {
                    char data[128];
                    const char *label = (p->label[0] != '\0') ? p->label : "<none>";
                    int type = (int)p->type;
                    int subtype = (int)p->subtype;
                    /* cast to unsigned for portable printf formatting */
                    snprintf(data, sizeof(data), "%s,type=%d,sub=%d,off=0x%08x,size=0x%08x", label, type, subtype, (unsigned)p->address, (unsigned)p->size);
                    cmd_send_response("INFO", "PARTS", "ITEM", data);
                    ++count;
                }
            }
            char summary[64];
            snprintf(summary, sizeof(summary), "COUNT=%d", count);
            cmd_send_response("OK", "PARTS", "SUMMARY", summary);
    #else
            cmd_send_response("ERR", "PARTS", "UNSUPPORTED", NULL);
    #endif
        }
        break;

    case CMD_TYPE_CONNECT_NAME:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "CONNECT_NAME", "MISSING_PARAM", NULL);
            break;
        }
        char name_buf[128] = {0};
        strncpy(name_buf, ctx->params[0], sizeof(name_buf) - 1);
        for (int i = 1; i < ctx->param_count; ++i)
        {
            strncat(name_buf, " ", sizeof(name_buf) - strlen(name_buf) - 1);
            strncat(name_buf, ctx->params[i], sizeof(name_buf) - strlen(name_buf) - 1);
        }
#ifdef ESP_PLATFORM
        if (bt_connect_by_name(name_buf) == ESP_OK)
            cmd_send_response("OK", "CONNECT_NAME", "INITIATED", NULL);
        else
            cmd_send_response("ERR", "CONNECT_NAME", "FAILED", NULL);
#else
        cmd_send_response("OK", "CONNECT_NAME", "MOCK_INITIATED", name_buf);
#endif
    }
    break;

    case CMD_TYPE_CONFIRM_PIN:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "CONFIRM_PIN", "MISSING_PARAM", NULL);
            break;
        }
        const char *mac_param = NULL;
        const char *decision_param = NULL;
        if (ctx->param_count >= 1)
        {
            if (strchr(ctx->params[0], ':') != NULL)
                mac_param = ctx->params[0];
            else
                decision_param = ctx->params[0];
        }
        if (ctx->param_count >= 2)
        {
            if (mac_param == NULL)
            {
                mac_param = ctx->params[0];
                decision_param = ctx->params[1];
            }
            else
            {
                decision_param = ctx->params[1];
            }
        }
        bool accept = true;
        if (decision_param != NULL)
        {
            if (strcasecmp(decision_param, "REJECT") == 0 || strcasecmp(decision_param, "NO") == 0 || strcmp(decision_param, "0") == 0)
                accept = false;
            else if (strcasecmp(decision_param, "ACCEPT") == 0 || strcasecmp(decision_param, "YES") == 0 || strcmp(decision_param, "1") == 0)
                accept = true;
            else if (decision_param[0] != '\0' && strchr(decision_param, ':') == NULL)
            {
                // Unknown keyword: treat as error to avoid silently accepting
                cmd_send_response("ERR", "CONFIRM_PIN", "BAD_PARAM", decision_param);
                break;
            }
        }
#ifdef ESP_PLATFORM
        bt_pairing_request_info_t pending = {0};
        if ((mac_param == NULL || mac_param[0] == '\0') && !bt_pairing_get_pending_request(&pending))
        {
            cmd_send_response("ERR", "CONFIRM_PIN", "NO_PENDING", NULL);
            break;
        }
        const char *target_mac = mac_param && mac_param[0] ? mac_param : pending.mac;
        if (target_mac == NULL || target_mac[0] == '\0')
        {
            cmd_send_response("ERR", "CONFIRM_PIN", "NO_MAC", NULL);
            break;
        }
        esp_err_t cerr = bt_pairing_confirm(target_mac, accept);
        if (cerr == ESP_OK)
        {
            cmd_send_response("OK", "CONFIRM_PIN", accept ? "ACCEPTED" : "REJECTED", target_mac);
        }
        else
        {
            cmd_send_response("ERR", "CONFIRM_PIN", esp_err_to_name(cerr), target_mac);
        }
#else
        const char *mac = mac_param;
        if (mac == NULL)
        {
            if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0)
                mac = s_cmd_mock_pairing_addr;
            else
            {
                cmd_send_response("ERR", "CONFIRM_PIN", "NO_MOCK", NULL);
                break;
            }
        }
        uint8_t bda[6] = {0};
        if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6)
        {
            esp_bt_gap_ssp_confirm_reply(bda, accept);
            cmd_send_response("OK", "CONFIRM_PIN", accept ? "MOCK_ACCEPTED" : "MOCK_REJECTED", mac);
            if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0 && strcmp(mac, s_cmd_mock_pairing_addr) == 0)
            {
                cmd_send_event_pair(accept ? "SUCCESS" : "FAILED", s_cmd_mock_pairing_addr);
                s_cmd_mock_enabled = false;
                s_cmd_mock_pairing_addr[0] = '\0';
                s_cmd_mock_passkey[0] = '\0';
            }
        }
        else
        {
            cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC", NULL);
        }
#endif
    }
    break;

    case CMD_TYPE_ENTER_PIN:
    {
        const char *mac_param = NULL;
        const char *pin_param = NULL;
        char default_pin[ESP_BT_PIN_CODE_LEN + 1] = {0};
        if (ctx->param_count >= 1)
        {
            mac_param = ctx->params[0];
        }
        if (ctx->param_count >= 2)
        {
            pin_param = ctx->params[1];
        }
#ifdef ESP_PLATFORM
        bt_pairing_request_info_t pending = {0};
        if (mac_param == NULL || mac_param[0] == '\0')
        {
            if (!bt_pairing_get_pending_request(&pending))
            {
                cmd_send_response("ERR", "ENTER_PIN", "NO_PENDING", NULL);
                break;
            }
            mac_param = pending.mac;
        }
        if (pin_param == NULL || pin_param[0] == '\0')
        {
            if (nvs_storage_get_default_pin(default_pin, sizeof(default_pin)) == ESP_OK && strlen(default_pin) > 0)
            {
                pin_param = default_pin;
            }
        }
        if (pin_param == NULL || pin_param[0] == '\0')
        {
            cmd_send_response("ERR", "ENTER_PIN", "MISSING_PIN", mac_param);
            break;
        }
        esp_err_t perr = bt_pairing_submit_pin(mac_param, pin_param);
        if (perr == ESP_OK)
        {
            cmd_send_response("OK", "ENTER_PIN", "SENT", mac_param);
        }
        else
        {
            cmd_send_response("ERR", "ENTER_PIN", esp_err_to_name(perr), mac_param);
        }
#else
        const char *mac = mac_param;
        if (mac == NULL || mac[0] == '\0')
        {
            if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0)
                mac = s_cmd_mock_pairing_addr;
            else
            {
                cmd_send_response("ERR", "ENTER_PIN", "NO_MOCK", NULL);
                break;
            }
        }
        if (pin_param == NULL || pin_param[0] == '\0')
        {
            if (nvs_storage_get_default_pin(default_pin, sizeof(default_pin)) == ESP_OK && strlen(default_pin) > 0)
                pin_param = default_pin;
        }
        if (pin_param == NULL || pin_param[0] == '\0')
        {
            cmd_send_response("ERR", "ENTER_PIN", "MISSING_PARAM", mac);
            break;
        }
        uint8_t bda[6] = {0};
        if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6)
        {
            uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
            size_t pin_len = strlen(pin_param);
            if (pin_len > ESP_BT_PIN_CODE_LEN)
                pin_len = ESP_BT_PIN_CODE_LEN;
            memcpy(pin_code, pin_param, pin_len);
            esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code);
            cmd_send_response("OK", "ENTER_PIN", "MOCK_SENT", mac);
            if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0 && strcmp(mac, s_cmd_mock_pairing_addr) == 0)
            {
                cmd_send_event_pair("SUCCESS", s_cmd_mock_pairing_addr);
                s_cmd_mock_enabled = false;
                s_cmd_mock_pairing_addr[0] = '\0';
                s_cmd_mock_passkey[0] = '\0';
            }
        }
        else
        {
            cmd_send_response("ERR", "ENTER_PIN", "INVALID_MAC", NULL);
        }
#endif
    }
    break;

    case CMD_TYPE_MUTE:
#ifdef ESP_PLATFORM
        if (audio_processor_set_mute(true) == ESP_OK)
            cmd_send_response("OK", "MUTE", "SET", NULL);
        else
            cmd_send_response("ERR", "MUTE", "FAILED", NULL);
#else
        cmd_send_response("OK", "MUTE", "MOCK_SET", NULL);
#endif
        break;

    case CMD_TYPE_UNMUTE:
#ifdef ESP_PLATFORM
        if (audio_processor_set_mute(false) == ESP_OK)
            cmd_send_response("OK", "UNMUTE", "CLEARED", NULL);
        else
            cmd_send_response("ERR", "UNMUTE", "FAILED", NULL);
#else
        cmd_send_response("OK", "UNMUTE", "MOCK_UNMUTED", NULL);
#endif
        break;

    case CMD_TYPE_VOLUME:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "VOLUME", "MISSING_PARAM", NULL);
            break;
        }
        int vol = atoi(ctx->params[0]);
        if (vol < 0 || vol > 100)
        {
            cmd_send_response("ERR", "VOLUME", "OUT_OF_RANGE", NULL);
            break;
        }
#ifdef ESP_PLATFORM
        if (audio_processor_set_volume((uint8_t)vol) == ESP_OK)
            cmd_send_response("OK", "VOLUME", "SET", ctx->params[0]);
        else
            cmd_send_response("ERR", "VOLUME", "FAILED", NULL);
#else
        cmd_send_response("OK", "VOLUME", "MOCK_SET", ctx->params[0]);
#endif
    }
    break;

    case CMD_TYPE_I2S_CONFIG:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "I2S_CONFIG", "MISSING_PARAM", NULL);
            break;
        }
        int pins[4] = {-1, -1, -1, -1};
        char param_copy[128];
        strncpy(param_copy, ctx->params[0], sizeof(param_copy) - 1);
        param_copy[sizeof(param_copy) - 1] = '\0';
        char *tok = strtok(param_copy, ",");
        int idx = 0;
        while (tok != NULL && idx < 4)
        {
            pins[idx++] = atoi(tok);
            tok = strtok(NULL, ",");
        }
#ifdef ESP_PLATFORM
        if (audio_processor_set_i2s_pins(pins[0], pins[1], pins[2], pins[3]) == ESP_OK)
            cmd_send_response("OK", "I2S_CONFIG", "APPLIED", ctx->params[0]);
        else
            cmd_send_response("ERR", "I2S_CONFIG", "FAILED", NULL);
#else
        cmd_send_response("OK", "I2S_CONFIG", "MOCK_APPLIED", ctx->params[0]);
#endif
    }
    break;

    case CMD_TYPE_PAIR:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "PAIR", "MISSING_PARAM", NULL);
            break;
        }
        // If the first param contains a colon, treat it as a MAC address. Otherwise
        // treat the whole param list as a device name (may contain spaces).
        const char *first = ctx->params[0];
        bool looks_like_mac = (strchr(first, ':') != NULL);
        if (looks_like_mac)
        {
#ifdef ESP_PLATFORM
            if (bt_manager_start_pair(first) == ESP_OK)
                cmd_send_response("OK", "PAIR", "INITIATED", first);
            else
                cmd_send_response("ERR", "PAIR", "FAILED", first);
#elif defined(UNIT_TEST)
            /* In unit tests call into the manager wrapper so tests can observe
             * the interaction via the weak hooks/mocks. The wrapper returns
             * ESP_OK (0) on success for compatibility with the device path. */
            if (bt_manager_start_pair(first) == ESP_OK)
                cmd_send_response("OK", "PAIR", "MOCK_INITIATED", first);
            else
                cmd_send_response("ERR", "PAIR", "FAILED", first);
#else
            cmd_send_response("OK", "PAIR", "MOCK_INITIATED", first);
#endif
        }
        else
        {
            // Join all params into a single name string
            char name_buf[128] = {0};
            strncpy(name_buf, first, sizeof(name_buf) - 1);
            for (int i = 1; i < ctx->param_count; ++i)
            {
                strncat(name_buf, " ", sizeof(name_buf) - strlen(name_buf) - 1);
                strncat(name_buf, ctx->params[i], sizeof(name_buf) - strlen(name_buf) - 1);
            }
#ifdef ESP_PLATFORM
            // Try connect-by-name which triggers pairing as needed
            if (bt_connect_by_name(name_buf) == ESP_OK)
                cmd_send_response("OK", "PAIR", "INITIATED_BY_NAME", name_buf);
            else
                cmd_send_response("ERR", "PAIR", "FAILED_BY_NAME", name_buf);
#else
            cmd_send_response("OK", "PAIR", "MOCK_INITIATED_BY_NAME", name_buf);
#endif
        }
    }
    break;

    case CMD_TYPE_PAIRED:
    {
        int count = 0;
        if (nvs_storage_get_paired_count(&count) == ESP_OK)
        {
            char data[64];
            snprintf(data, sizeof(data), "%d", count);
            cmd_send_response("OK", "PAIRED", "COUNT", data);
            for (int i = 0; i < count; ++i)
            {
                char mac[32];
                char name[64];
                if (nvs_storage_get_paired_device_by_index(i, mac, sizeof(mac), name, sizeof(name)) == ESP_OK)
                {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%s,%s", mac, name);
                    cmd_send_response("INFO", "PAIRED", "ITEM", buf);
                }
            }
        }
        else
        {
            cmd_send_response("ERR", "PAIRED", "READ_FAILED", NULL);
        }
    }
    break;

    case CMD_TYPE_SAMPLE_RATE:
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "SAMPLE_RATE", "MISSING_PARAM", NULL);
            break;
        }
        {
            int rate = atoi(ctx->params[0]);
            if (rate != 8000 && rate != 16000 && rate != 22050 && rate != 32000 && rate != 44100 && rate != 48000 && rate != 96000)
            {
                cmd_send_response("ERR", "SAMPLE_RATE", "INVALID_RATE", ctx->params[0]);
                break;
            }
        }
#ifdef ESP_PLATFORM
        if (audio_processor_set_sample_rate((audio_sample_rate_t)atoi(ctx->params[0])) == ESP_OK)
            cmd_send_response("OK", "SAMPLE_RATE", "APPLIED", ctx->params[0]);
        else
            cmd_send_response("ERR", "SAMPLE_RATE", "FAILED", NULL);
#else
        cmd_send_response("OK", "SAMPLE_RATE", "MOCK_APPLIED", ctx->params[0]);
#endif
        break;

    case CMD_TYPE_SET_NAME:
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "SET_NAME", "MISSING_PARAM", NULL);
            break;
        }
#ifdef ESP_PLATFORM
        if (nvs_storage_set_device_name(ctx->params[0]) == ESP_OK)
        {
            bt_manager_set_name(ctx->params[0]);
            cmd_send_response("OK", "SET_NAME", "SUCCESS", ctx->params[0]);
        }
        else
            cmd_send_response("ERR", "SET_NAME", "FAILED", NULL);
#else
        nvs_storage_set_device_name(ctx->params[0]);
        cmd_send_response("OK", "SET_NAME", "MOCK_SUCCESS", ctx->params[0]);
#endif
        break;

    case CMD_TYPE_DEBUG:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "DEBUG", "MISSING_PARAM", NULL);
            break;
        }
        /* Emit diagnostics via ESP logging on-device so they respect log
         * configuration, and fall back to printf for host/unit tests. */
#ifdef ESP_PLATFORM
        ESP_LOGI(TAG, "DIAG-DEBUG-ENTRY subcmd=%s param_count=%d", ctx->params[0], ctx->param_count);
#else
        printf("DIAG-DEBUG-ENTRY subcmd=%s param_count=%d\n", ctx->params[0], ctx->param_count);
#endif
        if (strcasecmp(ctx->params[0], "MOCK_ON") == 0)
        {
            s_cmd_mock_enabled = true;
            cmd_send_response("OK", "DEBUG", "MOCK_ON", NULL);
        }
        else if (strcasecmp(ctx->params[0], "MOCK_ADD") == 0)
        {
            if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0)
            {
                cmd_send_response("ERR", "DEBUG", "MOCK_ADD_MISSING", NULL);
            }
            else
            {
                /* Join remaining params with a comma so callers may provide
                 * either `MAC,PASS` or `MAC PASS` forms. This makes the
                 * test-friendly space-separated form accepted while remaining
                 * compatible with the original comma-separated syntax. */
                char payload[128];
                size_t pos = 0;
                for (int i = 1; i < ctx->param_count && pos + 1 < sizeof(payload); ++i)
                {
                    const char *p = ctx->params[i];
                    size_t l = strlen(p);
                    if (pos + l + 1 >= sizeof(payload))
                        break;
                    if (i > 1)
                        payload[pos++] = ',';
                    memcpy(&payload[pos], p, l);
                    pos += l;
                }
                payload[pos] = '\0';

                /* Accept either a single MAC (no comma) or MAC,extra form. */
                char *comma = strchr(payload, ',');
                const char *mac = payload;
                if (comma)
                {
                    *comma = '\0';
                }
                /* store the MAC we will use for the mock pairing */
                strncpy(s_cmd_mock_pairing_addr, mac, sizeof(s_cmd_mock_pairing_addr) - 1);
                s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr) - 1] = '\0';

                /* Diagnostic: indicate we've reached the MOCK_ADD path and
                 * show the MAC that will be used. This helps confirm the
                 * execution path is taken at runtime even if subsequent
                 * event delivery is missed. */
                printf("DIAG-MOCK-ADD: %s\n", s_cmd_mock_pairing_addr);
                /* Use ESP logging on-device to guarantee visibility even if
                 * stdout redirection changes, but still print to stdout for
                 * host tests. */
#ifdef ESP_PLATFORM
                ESP_LOGI(TAG, "DIAG-DEBUG-MOCK-ADD-BEFORE-SEND: mac=%s", s_cmd_mock_pairing_addr);
#else
                printf("DIAG-DEBUG-MOCK-ADD-BEFORE-SEND: mac=%s\n", s_cmd_mock_pairing_addr);
#endif
                /* Emit a pairing event so tests that call cmd_execute() and
                 * then test_capture_event() can observe that the mock was
                 * added. Previously only a response was emitted which
                 * caused test expectations to fail. */
                cmd_send_event_pair("ADDED", s_cmd_mock_pairing_addr);
                printf("DIAG-MOCK-ADD-AFTER-SEND\n");
                cmd_send_response("OK", "DEBUG", "MOCK_ADD", s_cmd_mock_pairing_addr);
            }
        }
        else if (strcasecmp(ctx->params[0], "MOCK_PAIR") == 0)
        {
            if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0)
            {
                cmd_send_response("ERR", "DEBUG", "MOCK_PAIR_MISSING", NULL);
            }
            else
            {
#ifdef ESP_PLATFORM
                s_cmd_mock_enabled = true;
                strncpy(s_cmd_mock_pairing_addr, ctx->params[1], sizeof(s_cmd_mock_pairing_addr) - 1);
                s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr) - 1] = '\0';
                char pass[16] = "000000";
                size_t maclen = strlen(s_cmd_mock_pairing_addr);
                if (maclen >= 2)
                {
                    const char *tail = s_cmd_mock_pairing_addr + (maclen > 5 ? maclen - 5 : 0);
                    /* Copy at most sizeof(pass)-1 characters from tail to avoid overflow */
                    strncpy(pass, tail, sizeof(pass) - 1);
                    pass[sizeof(pass) - 1] = '\0';
                }
                strncpy(s_cmd_mock_passkey, pass, sizeof(s_cmd_mock_passkey) - 1);
                s_cmd_mock_passkey[sizeof(s_cmd_mock_passkey) - 1] = '\0';
                char data[64];
                snprintf(data, sizeof(data), "%s,%s", s_cmd_mock_pairing_addr, s_cmd_mock_passkey);
                cmd_send_event_pair("CONFIRM", data);
                cmd_send_response("OK", "DEBUG", "MOCK_PAIR_STARTED", ctx->params[1]);
#else
                cmd_send_response("OK", "DEBUG", "MOCK_PAIR_MOCKED", ctx->params[1]);
#endif
            }
        }
        else if (strcasecmp(ctx->params[0], "BEEP_DIAG") == 0)
        {
#ifdef ESP_PLATFORM
            audio_processor_enable_next_beep_diag();
            cmd_send_response("OK", "DEBUG", "BEEP_DIAG_ARMED", NULL);
#else
            cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
        }
        else if (strcasecmp(ctx->params[0], "WORKER_DIAG") == 0)
        {
#ifdef ESP_PLATFORM
            if (audio_processor_emit_sync_worker_diag() == ESP_OK)
                cmd_send_response("OK", "DEBUG", "WORKER_DIAG_EMITTED", NULL);
            else
                cmd_send_response("ERR", "DEBUG", "WORKER_DIAG_FAILED", NULL);
#else
            cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
        }
        else if (strcasecmp(ctx->params[0], "AUDIO_DIAG") == 0)
        {
#ifdef ESP_PLATFORM
            if (ctx->param_count < 2)
            {
                cmd_send_response("ERR", "DEBUG", "AUDIO_DIAG_MISSING", NULL);
            }
            else
            {
                const char *p = ctx->params[1];
                bool enable = (strcasecmp(p, "ON") == 0) || (strcmp(p, "1") == 0) || (strcasecmp(p, "TRUE") == 0);
                bool disable = (strcasecmp(p, "OFF") == 0) || (strcmp(p, "0") == 0) || (strcasecmp(p, "FALSE") == 0);
                if (enable || disable)
                {
                    audio_processor_set_diag_enabled(enable);
                    cmd_send_response("OK", "DEBUG", enable ? "AUDIO_DIAG_ON" : "AUDIO_DIAG_OFF", NULL);
                }
                else
                {
                    cmd_send_response("ERR", "DEBUG", "AUDIO_DIAG_BAD_PARAM", p);
                }
            }
#else
            cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
        }
        else if (strcasecmp(ctx->params[0], "LOG") == 0)
        {
            if (ctx->param_count < 3)
            {
                cmd_send_response("ERR", "DEBUG", "LOG_MISSING", NULL);
            }
            else
            {
                int level = ESP_LOG_INFO;
                if (!cmd_parse_log_level(ctx->params[2], &level))
                {
                    cmd_send_response("ERR", "DEBUG", "LOG_BAD_LEVEL", ctx->params[2]);
                }
                else
                {
                    const char *tag = ctx->params[1];
                    esp_log_level_set(tag, level);
                    char payload[96];
                    snprintf(payload, sizeof(payload), "%s:%s", tag, ctx->params[2]);
                    cmd_send_response("OK", "DEBUG", "LOG_SET", payload);
                }
            }
        }
        /* Developer helper: force a beep regardless of BT connection state
         * Useful for on-device diagnostics when no sink is available. This
         * bypasses the BEEP command's permissive connection checks and calls
         * the audio_processor directly. Use cautiously in production. */
        else if (strcasecmp(ctx->params[0], "FORCE_BEEP") == 0)
        {
#ifdef ESP_PLATFORM
            if (audio_processor_beep(200) == ESP_OK)
                cmd_send_response("OK", "DEBUG", "FORCE_BEEP_SENT", NULL);
            else
                cmd_send_response("ERR", "DEBUG", "FORCE_BEEP_FAILED", NULL);
#else
            cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
        }
        else if (strcasecmp(ctx->params[0], "DRAIN_RING") == 0)
        {
#ifdef ESP_PLATFORM
            if (audio_processor_drain_ringbuffer() == ESP_OK)
                cmd_send_response("OK", "DEBUG", "DRAIN_RING_DONE", NULL);
            else
                cmd_send_response("ERR", "DEBUG", "DRAIN_RING_FAILED", NULL);
#else
            cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
        }
        else if (strcasecmp(ctx->params[0], "TAG_DUMP") == 0)
        {
#ifdef ESP_PLATFORM
            size_t max_items = 0;
            if (ctx->param_count > 1)
            {
                max_items = (size_t)strtoul(ctx->params[1], NULL, 10);
            }
            size_t captured = 0;
            esp_err_t err = audio_processor_dump_tag_ringbuffer(max_items, &captured);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", captured);
            if (err == ESP_OK)
                cmd_send_response("OK", "DEBUG", "TAG_DUMP", buf);
            else
                cmd_send_response("ERR", "DEBUG", "TAG_DUMP_FAIL", esp_err_to_name(err));
#else
            cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
        }
        else if (strcasecmp(ctx->params[0], "DRAM") == 0)
        {
            /* DEBUG DRAM ON|OFF - force DRAM-only allocations for audio */
            if (ctx->param_count < 2)
            {
                cmd_send_response("ERR", "DEBUG", "DRAM_MISSING_PARAM", NULL);
            }
            else
            {
                const char *p = ctx->params[1];
                if (strcasecmp(p, "ON") == 0 || strcmp(p, "1") == 0)
                {
#ifdef ESP_PLATFORM
                    audio_processor_set_dram_only(true);
                    cmd_send_response("OK", "DEBUG", "DRAM_ON", NULL);
#else
                    cmd_send_response("OK", "DEBUG", "DRAM_ON_MOCK", NULL);
#endif
                }
                else if (strcasecmp(p, "OFF") == 0 || strcmp(p, "0") == 0)
                {
#ifdef ESP_PLATFORM
                    audio_processor_set_dram_only(false);
                    cmd_send_response("OK", "DEBUG", "DRAM_OFF", NULL);
#else
                    cmd_send_response("OK", "DEBUG", "DRAM_OFF_MOCK", NULL);
#endif
                }
                else
                {
                    cmd_send_response("ERR", "DEBUG", "DRAM_BAD_PARAM", p);
                }
            }
        }
        else
            cmd_send_response("ERR", "DEBUG", "UNKNOWN_SUBCMD", ctx->params[0]);
    }
    break;

    case CMD_TYPE_SET_DEFAULT_PIN:
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "SET_DEFAULT_PIN", "MISSING_PARAM", NULL);
            break;
        }
#ifdef ESP_PLATFORM
        if (nvs_storage_set_default_pin(ctx->params[0]) == ESP_OK)
            cmd_send_response("OK", "SET_DEFAULT_PIN", "SUCCESS", ctx->params[0]);
        else
            cmd_send_response("ERR", "SET_DEFAULT_PIN", "FAILED", NULL);
#else
        nvs_storage_set_default_pin(ctx->params[0]);
        cmd_send_response("OK", "SET_DEFAULT_PIN", "MOCK_SUCCESS", ctx->params[0]);
#endif
        break;

    case CMD_TYPE_UNPAIR:
    {
        if (ctx->param_count < 1)
        {
            cmd_send_response("ERR", "UNPAIR", "MISSING_PARAM", NULL);
            break;
        }

        const char *target_mac = ctx->params[0];
        esp_err_t uerr = bt_unpair(target_mac);
#ifdef ESP_PLATFORM
        if (uerr == ESP_OK)
        {
            cmd_send_response("OK", "UNPAIR", "REMOVED", target_mac);
        }
        else
        {
            cmd_send_response("ERR", "UNPAIR", esp_err_to_name(uerr), target_mac);
        }
#else
        if (uerr == ESP_OK)
        {
            cmd_send_response("OK", "UNPAIR", "REMOVED", target_mac);
        }
        else if (uerr == ESP_ERR_NOT_FOUND)
        {
            cmd_send_response("ERR", "UNPAIR", "NOT_FOUND", target_mac);
        }
        else
        {
            cmd_send_response("ERR", "UNPAIR", "FAILED", target_mac);
        }
#endif
    }
    break;

    case CMD_TYPE_UNPAIR_ALL:
    {
        int cleared = 0;
        if (nvs_storage_get_paired_count(&cleared) != ESP_OK)
        {
            cleared = 0;
        }

        esp_err_t err = bt_unpair_all();
        if (err == ESP_OK)
        {
            char count_buf[16];
            snprintf(count_buf, sizeof(count_buf), "%d", cleared);
            cmd_send_response("OK", "UNPAIR_ALL", "SUCCESS", count_buf);
        }
        else
        {
#ifdef ESP_PLATFORM
            cmd_send_response("ERR", "UNPAIR_ALL", esp_err_to_name(err), NULL);
#else
            cmd_send_response("ERR", "UNPAIR_ALL", "FAILED", NULL);
#endif
        }
    }
    break;

    case CMD_TYPE_HELP:
        cmd_help_emit_all();
        break;

    default:
        cmd_send_response("INFO", "COMMAND", "RECEIVED", "Not implemented yet");
        break;
    }

    return CMD_SUCCESS;
}

// --- Public API wrappers and helpers (parsing / processing / response) ---

#ifndef CMD_BUF_SIZE
#define CMD_BUF_SIZE 256
#endif

// Simple init/deinit for host tests
cmd_status_t cmd_init(void) { return CMD_SUCCESS; }
cmd_status_t cmd_deinit(void) { return CMD_SUCCESS; }

// Emit a structured response on the serial console.
cmd_status_t cmd_send_response(const char *status, const char *command, const char *result, const char *data)
{
    char buf[512];
    const char *d = data ? data : "";
    int len = snprintf(buf, sizeof(buf), "%s|%s|%s|%s\r\n", status ? status : "", command ? command : "", result ? result : "", d);
#if defined(UNIT_TEST) || !defined(ESP_PLATFORM)
    /* For host/unit tests the mock UART maps UART_NUM_1 to a local buffer.
     * Use CMD_UART_NUM so tests can override which port is used. */
    uart_write_bytes(CMD_UART_NUM, buf, (size_t)len);
#else
    // On device, write to command UART (if available). If the UART driver
    // hasn't been installed for the console UART (common for UART0 used
    // as stdout), fall back to printf to avoid "uart driver error" logs.
    if (uart_is_driver_installed(CMD_UART_NUM))
    {
        uart_write_bytes(CMD_UART_NUM, buf, (size_t)len);
    }
    else
    {
        // Console is available via stdio; print the structured response there.
        printf("%s", buf);
    }
#endif
    return CMD_SUCCESS;
}

// Convenience wrapper for pairing events
cmd_status_t cmd_send_event_pair(const char *subtype, const char *data)
{
    /* Increment sequence for ordering */
    uint32_t seq = ++s_event_sequence;
    uint64_t ts_ms = cmd_get_timestamp_ms();

    /* Build the same EVENT line we emit on the console so tests can
     * optionally capture it via a weakly-linked hook. The hook is
     * provided by the test adapter and is intentionally weak so
     * production builds are unaffected. */
    const char *d = data ? data : "";
    char payload[256];
    if (d[0] != '\0')
    {
        snprintf(payload, sizeof(payload), "%s,SEQ=%lu,TS=%llu", d, (unsigned long)seq, (unsigned long long)ts_ms);
    }
    else
    {
        snprintf(payload, sizeof(payload), "SEQ=%lu,TS=%llu", (unsigned long)seq, (unsigned long long)ts_ms);
    }

    char buf[512];
    int n = snprintf(buf, sizeof(buf), "EVENT|PAIR|%s|%s", subtype ? subtype : "", payload);
    /* Emit on serial/console as usual */
    cmd_send_response("EVENT", "PAIR", subtype ? subtype : "", payload);

    /* Diagnostic: also print an explicit, easily-greppable line so
     * test runs and log collectors can detect emitted events even if
     * serial parsing/bridging is flaky. This must not change behavior
     * other than producing an extra log line. */
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "DIAG-EVENT: %s", buf);
#else
    /* Host/unit tests: print to stdout so the test log captures it. */
    printf("DIAG-EVENT: %s\n", buf);
#endif
    /* Also print via printf unconditionally so even if logging is disabled
     * the diagnostic marker is present in the serial console output. */
    printf("DIAG-EVENT: %s\n", buf);

    /* If a test-side receiver is linked, forward the same formatted
     * event string so `test_capture_event` can observe it without
     * relying on serial-bridge tooling. Insert a guaranteed printf
     * immediately before calling the weak hook so monitor logs will
     * show that we attempted the forwarding. This helps distinguish
     * between the code path not being reached and the hook not being
     * linked into the image. */
#if defined(__GNUC__)
    extern void test_push_event(const char *ev) __attribute__((weak));
#else
    extern void test_push_event(const char *ev);
#endif
    /* Guaranteed marker that's easy to grep in serial logs */
    printf("HOOK-DEBUG: about to call test_push_event (len=%d)\n", n);
    if (n > 0 && test_push_event)
    {
        printf("HOOK-DEBUG: test_push_event symbol present, forwarding event\n");
        test_push_event(buf);
    }
    else
    {
        printf("HOOK-DEBUG: test_push_event not present or n<=0\n");
    }

    return CMD_SUCCESS;
}

// Lightweight command parser used by host tests. Parses command name and up
// to CMD_MAX_PARAMS whitespace-separated parameters. Special-case
// CONNECT_NAME to keep the rest of the line as a single parameter.
cmd_status_t cmd_parse(const char *cmd_str, cmd_context_t *ctx)
{
    if (!cmd_str || !ctx)
        return CMD_ERROR_INVALID_PARAM;
    // Clear context
    memset(ctx, 0, sizeof(*ctx));

    // Copy and trim
    char buf[512];
    strncpy(buf, cmd_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    // Trim leading
    char *s = buf;
    while (*s && isspace((unsigned char)*s))
        ++s;
    // Trim trailing
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end))
    {
        *end = '\0';
        --end;
    }
    if (*s == '\0')
        return CMD_ERROR_UNKNOWN;

    // Extract command token
    char *save = NULL;
    char *token = strtok_r(s, " \t", &save);
    if (!token)
        return CMD_ERROR_UNKNOWN;
    printf("PARSE-DIAG: token='%s'\n", token);
    // Map command to type (case-insensitive)
    if (strcasecmp(token, "SCAN") == 0)
        ctx->type = CMD_TYPE_SCAN;
    else if (strcasecmp(token, "CONNECT") == 0)
        ctx->type = CMD_TYPE_CONNECT;
    else if (strcasecmp(token, "CONNECT_NAME") == 0)
        ctx->type = CMD_TYPE_CONNECT_NAME;
    else if (strcasecmp(token, "DISCONNECT") == 0)
        ctx->type = CMD_TYPE_DISCONNECT;
    else if (strcasecmp(token, "PAIRED") == 0)
        ctx->type = CMD_TYPE_PAIRED;
    else if (strcasecmp(token, "SET_NAME") == 0)
        ctx->type = CMD_TYPE_SET_NAME;
    else if (strcasecmp(token, "START") == 0)
        ctx->type = CMD_TYPE_START;
    else if (strcasecmp(token, "STOP") == 0)
        ctx->type = CMD_TYPE_STOP;
    else if (strcasecmp(token, "VOLUME") == 0)
        ctx->type = CMD_TYPE_VOLUME;
    else if (strcasecmp(token, "MUTE") == 0)
        ctx->type = CMD_TYPE_MUTE;
    else if (strcasecmp(token, "UNMUTE") == 0)
        ctx->type = CMD_TYPE_UNMUTE;
    else if (strcasecmp(token, "STATUS") == 0)
        ctx->type = CMD_TYPE_STATUS;
    else if (strcasecmp(token, "VERSION") == 0)
        ctx->type = CMD_TYPE_VERSION;
    else if (strcasecmp(token, "RESET") == 0)
        ctx->type = CMD_TYPE_RESET;
    else if (strcasecmp(token, "DEBUG") == 0)
        ctx->type = CMD_TYPE_DEBUG;
    else if (strcasecmp(token, "SAMPLE_RATE") == 0)
        ctx->type = CMD_TYPE_SAMPLE_RATE;
    else if (strcasecmp(token, "MEM") == 0)
        ctx->type = CMD_TYPE_MEM;
    else if (strcasecmp(token, "SYNTH") == 0)
        ctx->type = CMD_TYPE_SYNTH;
    else if (strcasecmp(token, "I2S_CONFIG") == 0)
        ctx->type = CMD_TYPE_I2S_CONFIG;
    else if (strcasecmp(token, "BEEP") == 0)
        ctx->type = CMD_TYPE_BEEP;
    else if (strcasecmp(token, "PLAY") == 0)
        ctx->type = CMD_TYPE_PLAY;
    else if (strcasecmp(token, "PAIR") == 0)
        ctx->type = CMD_TYPE_PAIR;
    else if (strcasecmp(token, "CONFIRM_PIN") == 0)
        ctx->type = CMD_TYPE_CONFIRM_PIN;
    else if (strcasecmp(token, "ENTER_PIN") == 0)
        ctx->type = CMD_TYPE_ENTER_PIN;
    else if (strcasecmp(token, "SET_DEFAULT_PIN") == 0)
        ctx->type = CMD_TYPE_SET_DEFAULT_PIN;
    else if (strcasecmp(token, "UNPAIR") == 0)
        ctx->type = CMD_TYPE_UNPAIR;
    else if (strcasecmp(token, "UNPAIR_ALL") == 0)
        ctx->type = CMD_TYPE_UNPAIR_ALL;
    else if (strcasecmp(token, "FILE") == 0)
        ctx->type = CMD_TYPE_FILE;
    else if (strcasecmp(token, "FILES") == 0)
        ctx->type = CMD_TYPE_FILES;
    else if (strcasecmp(token, "PARTS") == 0)
        ctx->type = CMD_TYPE_PARTS;
    else if (strcasecmp(token, "HELP") == 0)
        ctx->type = CMD_TYPE_HELP;
    else
        ctx->type = CMD_TYPE_UNKNOWN;

    // If CONNECT_NAME, keep rest of the line as single param
    if (ctx->type == CMD_TYPE_CONNECT_NAME)
    {
        char *rest = save;
        // skip leading whitespace
        while (rest && *rest && isspace((unsigned char)*rest))
            ++rest;
        if (rest && *rest)
        {
            strncpy(ctx->params[0], rest, CMD_MAX_PARAM_LEN - 1);
            ctx->params[0][CMD_MAX_PARAM_LEN - 1] = '\0';
            ctx->param_count = 1;
        }
        else
        {
            ctx->param_count = 0;
        }
        return CMD_SUCCESS;
    }

    // Otherwise split remaining tokens by whitespace
    int idx = 0;
    while ((token = strtok_r(NULL, " \t", &save)) != NULL && idx < CMD_MAX_PARAMS)
    {
        strncpy(ctx->params[idx], token, CMD_MAX_PARAM_LEN - 1);
        ctx->params[idx][CMD_MAX_PARAM_LEN - 1] = '\0';
        ++idx;
    }
    ctx->param_count = idx;

    return (ctx->type == CMD_TYPE_UNKNOWN) ? CMD_ERROR_UNKNOWN : CMD_SUCCESS;
}

// Read from UART and process one command line if available.
cmd_status_t cmd_process(void)
{
    /* Implement a small persistent line buffer so partial UART reads are
     * accumulated across calls to cmd_process(). This also allows a single
     * uart_read_bytes() to contain multiple newline-terminated commands. */
    static char line_buf[CMD_BUF_SIZE];
    static size_t line_len = 0;
    uint8_t read_buf[CMD_BUF_SIZE];

    // Only attempt to read if the driver is installed for the command UART.
    // For host/unit tests the mock UART is always available; skip the
    // runtime driver-installed check in that configuration.
    int read_uart = -1;
#if defined(UNIT_TEST) || !defined(ESP_PLATFORM)
    (void)CMD_UART_NUM;
    read_uart = CMD_UART_NUM;
#else
    // Prefer the configured command UART. If it's not installed, fall back
    // to the console UART (UART_NUM_0) if that driver is installed. This
    // helps boards that use the console driver for input but didn't install
    // a separate driver for the command UART.
    if (uart_is_driver_installed(CMD_UART_NUM))
    {
        read_uart = CMD_UART_NUM;
    }
    else if (uart_is_driver_installed(UART_NUM_0))
    {
#ifdef ESP_LOGW
        ESP_LOGW(TAG, "cmd_process: command UART %d not installed; falling back to console UART 0", CMD_UART_NUM);
#endif
        read_uart = UART_NUM_0;
    }
    else
    {
        // No available UART driver — nothing to read.
        return CMD_SUCCESS;
    }
#endif

    int r = uart_read_bytes(read_uart, read_buf, sizeof(read_buf) - 1, 0);
    if (r <= 0)
    {
        return CMD_SUCCESS;
    }

    // Append to persistent buffer (clip to available space)
    size_t to_copy = (size_t)r;
    if (line_len + to_copy >= sizeof(line_buf))
    {
        // Buffer overflow: reset to keep system responsive and log the event
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "cmd_process: line buffer overflow, resetting buffer");
#endif
        line_len = 0;
        to_copy = sizeof(line_buf) - 1;
    }
    memcpy(line_buf + line_len, read_buf, to_copy);
    line_len += to_copy;
    line_buf[line_len] = '\0';

    // Process complete lines (terminated by '\n' or '\r') one at a time
    char *start = line_buf;
    while (true)
    {
        char *nl = (char *)memchr(start, '\n', (size_t)(line_buf + line_len - start));
        char *cr = (char *)memchr(start, '\r', (size_t)(line_buf + line_len - start));
        char *term = nl ? nl : cr;
        if (!term)
            break;

        // Null-terminate the line and parse/execute
        *term = '\0';
        // Trim trailing spaces
        char *end = term - 1;
        while (end >= start && isspace((unsigned char)*end))
        {
            *end = '\0';
            --end;
        }

        cmd_context_t ctx;
        if (cmd_parse(start, &ctx) == CMD_SUCCESS)
        {
            cmd_execute(&ctx);
        }

        // Move to the next character after the terminator
        start = term + 1;
        while (start < line_buf + line_len && (*start == '\n' || *start == '\r'))
            ++start;
    }

    // Move any remaining bytes to the start of the buffer
    size_t remaining = (size_t)(line_buf + line_len - start);
    if (remaining > 0)
        memmove(line_buf, start, remaining);
    line_len = remaining;
    line_buf[line_len] = '\0';

    return CMD_SUCCESS;
}

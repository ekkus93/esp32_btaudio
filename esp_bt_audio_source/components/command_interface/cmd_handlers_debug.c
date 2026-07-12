/* cmd_handlers_debug.c — DEBUG subcommand handlers (mock pairing simulation and
 * audio/beep diagnostics).  Split out of cmd_handlers_bt.c; behavior unchanged.
 *
 * Each subcommand lives in its own static function.  cmd_handle_debug()
 * dispatches via a table rather than a long if-else chain so new subcommands
 * can be added by appending one entry.  Every handler has the same signature:
 *   static void handle_debug_<name>(const cmd_context_t *ctx)
 */
#include "cmd_handlers.h"
#include "cmd_handlers_internal.h"

static const char *TAG = "cmd";

static void handle_debug_mock_on(const cmd_context_t *ctx)
{
    (void)ctx;
    g_cmd_mock_enabled = true;
    cmd_send_response("OK", "DEBUG", "MOCK_ON", NULL);
}

static void handle_debug_mock_add(const cmd_context_t *ctx)
{
    if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0)
    {
        cmd_send_response("ERR", "DEBUG", "MOCK_ADD_MISSING", NULL);
        return;
    }
    char payload[128];
    size_t pos = 0;
    for (int i = 1; i < ctx->param_count && pos + 1 < sizeof(payload); ++i)
    {
        const char *p = ctx->params[i];
        size_t param_len = strlen(p);
        if (pos + param_len + 1 >= sizeof(payload)) {
            break;
        }
        if (i > 1) {
            payload[pos++] = ',';
        }
        memcpy(&payload[pos], p, param_len);
        pos += param_len;
    }
    payload[pos] = '\0';

    char *comma = strchr(payload, ',');
    const char *mac = payload;
    if (comma) {
        *comma = '\0';
    }
    cmd_safe_copy(g_cmd_mock_pairing_addr, sizeof(g_cmd_mock_pairing_addr), mac);
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "DIAG-DEBUG-MOCK-ADD-BEFORE-SEND: mac=%s", g_cmd_mock_pairing_addr);  // NOLINT(bugprone-branch-clone)
#else
    printf("DIAG-DEBUG-MOCK-ADD-BEFORE-SEND: mac=%s\n", g_cmd_mock_pairing_addr);
#endif
    cmd_send_event_pair("ADDED", g_cmd_mock_pairing_addr);
    printf("DIAG-MOCK-ADD-AFTER-SEND\n");
    cmd_send_response("OK", "DEBUG", "MOCK_ADD", g_cmd_mock_pairing_addr);
}

static void handle_debug_mock_pair(const cmd_context_t *ctx)
{
    if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0)
    {
        cmd_send_response("ERR", "DEBUG", "MOCK_PAIR_MISSING", NULL);
        return;
    }
#ifdef ESP_PLATFORM
    g_cmd_mock_enabled = true;
    cmd_safe_copy(g_cmd_mock_pairing_addr, sizeof(g_cmd_mock_pairing_addr), ctx->params[1]);
    char pass[16] = "000000";
    size_t maclen = strlen(g_cmd_mock_pairing_addr);
    if (maclen >= 2)
    {
        const char *tail = g_cmd_mock_pairing_addr + (maclen > 5 ? maclen - 5 : 0);
        cmd_safe_copy(pass, sizeof(pass), tail);
    }
    cmd_safe_copy(g_cmd_mock_passkey, sizeof(g_cmd_mock_passkey), pass);
    char data[64];
    snprintf(data, sizeof(data), "%s,%s", g_cmd_mock_pairing_addr, g_cmd_mock_passkey);
    cmd_send_event_pair("CONFIRM", data);
    cmd_send_response("OK", "DEBUG", "MOCK_PAIR_STARTED", ctx->params[1]);
#else
    cmd_send_response("OK", "DEBUG", "MOCK_PAIR_MOCKED", ctx->params[1]);
#endif
}

static void handle_debug_beep_diag(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    audio_processor_enable_next_beep_diag();
    cmd_send_response("OK", "DEBUG", "BEEP_DIAG_ARMED", NULL);
#else
    cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", "BEEP_DIAG");
#endif
}

static void handle_debug_worker_diag(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (audio_processor_emit_sync_worker_diag() == ESP_OK) {
        cmd_send_response("OK", "DEBUG", "WORKER_DIAG_EMITTED", NULL);
    } else {
        cmd_send_response("ERR", "DEBUG", "WORKER_DIAG_FAILED", NULL);
    }
#else
    cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", "WORKER_DIAG");
#endif
}

static void handle_debug_audio_diag(const cmd_context_t *ctx)
{
#ifdef ESP_PLATFORM
    if (ctx->param_count < 2)
    {
        cmd_send_response("ERR", "DEBUG", "AUDIO_DIAG_MISSING", NULL);
        return;
    }
    const char *p = ctx->params[1];
    bool enable  = (strcasecmp(p, "ON")   == 0) || (strcmp(p, "1") == 0) || (strcasecmp(p, "TRUE")  == 0);
    bool disable = (strcasecmp(p, "OFF")  == 0) || (strcmp(p, "0") == 0) || (strcasecmp(p, "FALSE") == 0);
    if (enable || disable) {
        audio_processor_set_diag_enabled(enable);
        cmd_send_response("OK", "DEBUG", enable ? "AUDIO_DIAG_ON" : "AUDIO_DIAG_OFF", NULL);
    } else {
        cmd_send_response("ERR", "DEBUG", "AUDIO_DIAG_BAD_PARAM", p);
    }
#else
    (void)ctx;
    cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", "AUDIO_DIAG");
#endif
}

static void handle_debug_audio_diag_summary(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    cmd_send_response("OK", "DEBUG", "AUDIO_DIAG_SUMMARY", NULL);
    if (audio_processor_emit_diag_summary() != ESP_OK) {
        ESP_LOGW(TAG, "audio_processor_emit_diag_summary() failed");  // NOLINT(bugprone-branch-clone)
    }
#else
    cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", "AUDIO_DIAG_SUMMARY");
#endif
}

static void handle_debug_audio_diag_probe(const cmd_context_t *ctx)
{
#ifdef ESP_PLATFORM
    if (ctx->param_count < 2) {
        cmd_send_response("ERR", "DEBUG", "AUDIO_DIAG_PROBE_USAGE", NULL);
    } else if (strcasecmp(ctx->params[1], "ARM") == 0) {
        unsigned probe_count = 0;
        if (ctx->param_count >= 3) {
            probe_count = (unsigned)strtoul(ctx->params[2], NULL, 10);
        }
        if (probe_count == 0) {
            probe_count = 16;
        }
        audio_processor_arm_probe((size_t)probe_count);
        cmd_send_response("OK", "DEBUG", "AUDIO_DIAG_PROBE_ARMED",
                          (ctx->param_count >= 3) ? ctx->params[2] : "16");
    } else if (strcasecmp(ctx->params[1], "DUMP") == 0) {
        cmd_send_response("OK", "DEBUG", "AUDIO_DIAG_PROBE_DUMP", NULL);
        if (audio_processor_emit_probe() != ESP_OK) {
            ESP_LOGW(TAG, "audio_processor_emit_probe() failed");  // NOLINT(bugprone-branch-clone)
        }
    } else {
        cmd_send_response("ERR", "DEBUG", "AUDIO_DIAG_PROBE_BAD_PARAM", ctx->params[1]);
    }
#else
    (void)ctx;
    cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", "AUDIO_DIAG_PROBE");
#endif
}

static void handle_debug_log(const cmd_context_t *ctx)
{
    if (ctx->param_count < 3) {
        cmd_send_response("ERR", "DEBUG", "LOG_MISSING", NULL);
        return;
    }
    int level = ESP_LOG_INFO;
    if (!cmd_parse_log_level(ctx->params[2], &level)) {
        cmd_send_response("ERR", "DEBUG", "LOG_BAD_LEVEL", ctx->params[2]);
        return;
    }
    const char *tag = ctx->params[1];
    esp_log_level_set(tag, level);
    char payload[96];
    snprintf(payload, sizeof(payload), "%s:%s", tag, ctx->params[2]);
    cmd_send_response("OK", "DEBUG", "LOG_SET", payload);
}

static void handle_debug_force_beep(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (audio_processor_beep(200) == ESP_OK) {
        cmd_send_response("OK", "DEBUG", "FORCE_BEEP_SENT", NULL);
    } else {
        cmd_send_response("ERR", "DEBUG", "FORCE_BEEP_FAILED", NULL);
    }
#else
    cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", "FORCE_BEEP");
#endif
}

static void handle_debug_drain_queue(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (audio_processor_drain_ring() == ESP_OK) {
        cmd_send_response("OK", "DEBUG", "DRAIN_QUEUE_DONE", NULL);
    } else {
        cmd_send_response("ERR", "DEBUG", "DRAIN_QUEUE_FAILED", NULL);
    }
#else
    cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", "DRAIN_QUEUE");
#endif
}

static void handle_debug_dram(const cmd_context_t *ctx)
{
    if (ctx->param_count < 2) {
        cmd_send_response("ERR", "DEBUG", "DRAM_MISSING_PARAM", NULL);
        return;
    }
    const char *p = ctx->params[1];
    if (strcasecmp(p, "ON") == 0 || strcmp(p, "1") == 0) {
#ifdef ESP_PLATFORM
        audio_processor_set_dram_only(true);
        cmd_send_response("OK", "DEBUG", "DRAM_ON", NULL);
#else
        cmd_send_response("OK", "DEBUG", "DRAM_ON_MOCK", NULL);
#endif
    } else if (strcasecmp(p, "OFF") == 0 || strcmp(p, "0") == 0) {
#ifdef ESP_PLATFORM
        audio_processor_set_dram_only(false);
        cmd_send_response("OK", "DEBUG", "DRAM_OFF", NULL);
#else
        cmd_send_response("OK", "DEBUG", "DRAM_OFF_MOCK", NULL);
#endif
    } else {
        cmd_send_response("ERR", "DEBUG", "DRAM_BAD_PARAM", p);
    }
}

/* Dispatch table entry */
typedef struct {
    const char *subcmd;
    void (*handler)(const cmd_context_t *ctx);
} debug_subcmd_entry_t;

static const debug_subcmd_entry_t s_debug_subcmds[] = {
    { "MOCK_ON",            handle_debug_mock_on            },
    { "MOCK_ADD",           handle_debug_mock_add           },
    { "MOCK_PAIR",          handle_debug_mock_pair          },
    { "BEEP_DIAG",          handle_debug_beep_diag          },
    { "WORKER_DIAG",        handle_debug_worker_diag        },
    { "AUDIO_DIAG",         handle_debug_audio_diag         },
    { "AUDIO_DIAG_SUMMARY", handle_debug_audio_diag_summary },
    { "AUDIO_DIAG_PROBE",   handle_debug_audio_diag_probe   },
    { "LOG",                handle_debug_log                },
    { "FORCE_BEEP",         handle_debug_force_beep         },
    { "DRAIN_QUEUE",        handle_debug_drain_queue        },
    { "DRAIN_RING",         handle_debug_drain_queue        },  /* alias */
    { "DRAM",               handle_debug_dram               },
};
#define NUM_DEBUG_SUBCMDS ((int)(sizeof(s_debug_subcmds) / sizeof(s_debug_subcmds[0])))

cmd_status_t cmd_handle_debug(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "DEBUG", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "DIAG-DEBUG-ENTRY subcmd=%s param_count=%d", ctx->params[0], ctx->param_count);  // NOLINT(bugprone-branch-clone)
#else
    printf("DIAG-DEBUG-ENTRY subcmd=%s param_count=%d\n", ctx->params[0], ctx->param_count);
#endif
    for (int i = 0; i < NUM_DEBUG_SUBCMDS; ++i)
    {
        if (strcasecmp(ctx->params[0], s_debug_subcmds[i].subcmd) == 0)
        {
            s_debug_subcmds[i].handler(ctx);
            return CMD_SUCCESS;
        }
    }
    cmd_send_response("ERR", "DEBUG", "UNKNOWN_SUBCMD", ctx->params[0]);
    return CMD_SUCCESS;
}

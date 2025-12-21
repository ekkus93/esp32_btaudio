#include "command_interface.h"
#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>

/* Declare the minimal prototype used from the audio processor so this
 * test-only component does not need to include the application's public
 * headers (which may live in the app's main/include directory). */
extern esp_err_t audio_processor_play_wav(const char* path);

static bool s_bt_connected = true;

bool bt_manager_is_a2dp_connected(void)
{
    return s_bt_connected;
}

void bt_manager_mock_connection_closed(const char* mac)
{
    (void)mac;
    s_bt_connected = false;
}

void bt_manager_mock_connection_opened(const char* mac)
{
    (void)mac;
    s_bt_connected = true;
}

// Very small parser that only understands PLAY <filename>
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx)
{
    if (!cmd_str || !ctx) return CMD_ERROR_INVALID_PARAM;
    memset(ctx, 0, sizeof(*ctx));

    // Copy and trim leading whitespace
    char buf[256];
    strncpy(buf, cmd_str, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    char* s = buf;
    while (*s && (*s == ' ' || *s == '\t')) ++s;
    if (*s == '\0') return CMD_ERROR_UNKNOWN;

    // Tokenize first token
    char* save = NULL;
    char* token = strtok_r(s, " \t", &save);
    if (!token) return CMD_ERROR_UNKNOWN;

    if (strcasecmp(token, "PLAY") == 0) {
        ctx->type = CMD_TYPE_PLAY;
        // next token is filename (if present)
        char* fn = strtok_r(NULL, " \t", &save);
        if (fn) {
            strncpy(ctx->params[0], fn, CMD_MAX_PARAM_LEN-1);
            ctx->params[0][CMD_MAX_PARAM_LEN-1] = '\0';
            ctx->param_count = 1;
        } else {
            ctx->param_count = 0;
        }
        return CMD_SUCCESS;
    }

    return CMD_ERROR_UNKNOWN;
}

cmd_status_t cmd_execute(const cmd_context_t* ctx)
{
    if (!ctx) return CMD_ERROR_NOT_INITIALIZED;
    if (ctx->type == CMD_TYPE_PLAY) {
        if (ctx->param_count < 1) return CMD_ERROR_INVALID_PARAM;
        char path[256];
        const char* p = ctx->params[0];
        if (p[0] == '/') {
            strncpy(path, p, sizeof(path)-1);
            path[sizeof(path)-1] = '\0';
        } else {
            snprintf(path, sizeof(path), "/spiffs/%s", p);
        }

        if (!bt_manager_is_a2dp_connected()) {
            ESP_LOGW("TEST_CMD_IF", "A2DP not connected; refusing PLAY for %s", path);
            printf("DIAG-PLAY-RET: -1 A2DP_NOT_CONNECTED %s\n", path);
            return CMD_ERROR_UNKNOWN;
        }

        esp_err_t r = audio_processor_play_wav(path);
        if (r != ESP_OK) {
            /* Emit both ESP_LOG and a plain printf so the test monitor
             * captures the diagnostic regardless of logging configuration. */
            ESP_LOGW("TEST_CMD_IF", "audio_processor_play_wav returned %d (%s) for path %s", (int)r, esp_err_to_name(r), path);
            printf("DIAG-PLAY-RET: %d %s %s\n", (int)r, esp_err_to_name(r), path);
            return CMD_ERROR_UNKNOWN;
        }
        return CMD_SUCCESS;
    }
    return CMD_ERROR_UNKNOWN;
}

#include "cmd_handlers.h"
#include "play_manager.h"
#include "bt_manager.h"

static const char *TAG = "cmd";

#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
extern int bt_get_connection_state(void);
extern int bt_get_streaming_state_int(void);
#else
int bt_get_connection_state(void);
int bt_get_streaming_state_int(void);
#endif

cmd_status_t cmd_handle_synth(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "SYNTH", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
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
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_diag(const cmd_context_t *ctx)
{
    if (ctx->param_count >= 1)
    {
        const char *sub = ctx->params[0];
        if (strcasecmp(sub, "I2S_STOP") == 0 || strcasecmp(sub, "STOP_I2S") == 0)
        {
            esp_err_t res = audio_processor_stop();
            if (res == ESP_OK)
            {
                cmd_send_response("OK", "DIAG", "I2S_STOPPED", NULL);
            }
            else
            {
                cmd_send_response("ERR", "DIAG", esp_err_to_name(res), "I2S_STOP");
            }
            return CMD_SUCCESS;
        }
        cmd_send_response("ERR", "DIAG", "BAD_PARAM", sub);
        return CMD_SUCCESS;
    }
    int conn = bt_get_connection_state();
    int streaming = bt_get_streaming_state_int();
    int mgr_conn = 0;
#ifdef ESP_PLATFORM
    mgr_conn = bt_manager_is_connected();
#else
    mgr_conn = bt_manager_is_connected();
#endif
    int i2s = audio_processor_is_i2s_active() ? 1 : 0;
    if (!i2s && audio_processor_is_running() && audio_processor_is_synth_mode_enabled())
    {
        i2s = 1;
    }
    int beep = audio_processor_is_beep_active() ? 1 : 0;
    char data[96];
    snprintf(data, sizeof(data), "CONN=%d,STREAM=%d,MGR=%d,I2S=%d,BEEP=%d", conn, streaming, mgr_conn, i2s, beep);
    cmd_send_response("OK", "DIAG", "STATE", data);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_beep(const cmd_context_t *ctx)
{
    (void)ctx;
    int conn = 0;
#ifdef ESP_PLATFORM
    conn = bt_get_connection_state();
    int streaming = bt_get_streaming_state_int();
    int mgr_conn = 0;
    mgr_conn = bt_manager_is_connected();
    ESP_LOGI(TAG, "DIAG-BEEP: bt_get_connection_state=%d bt_get_streaming_state_int=%d bt_manager_conn=%d", conn, streaming, mgr_conn);
    printf("DIAG-BEEP: bt_get_connection_state=%d bt_get_streaming_state_int=%d bt_manager_conn=%d\n", conn, streaming, mgr_conn);
    if (!((conn == 1) || (streaming == 1) || (mgr_conn == 1)))
    {
        cmd_send_response("ERR", "BEEP", "NOT_CONNECTED", NULL);
        return CMD_SUCCESS;
    }
    if (audio_processor_is_beep_active())
    {
        cmd_send_response("ERR", "BEEP", "BUSY", "BEEP_ACTIVE");
        return CMD_SUCCESS;
    }
    if (audio_processor_is_wav_active())
    {
        cmd_send_response("ERR", "BEEP", "BUSY", "WAV_ACTIVE");
        return CMD_SUCCESS;
    }
    if (play_manager_is_active())
    {
        cmd_send_response("ERR", "BEEP", "BUSY", "PLAY_ACTIVE");
        return CMD_SUCCESS;
    }
#else
    if (bt_get_connection_state() != 1)
    {
        cmd_send_response("ERR", "BEEP", "NOT_CONNECTED", NULL);
        return CMD_SUCCESS;
    }
    if (audio_processor_is_beep_active())
    {
        cmd_send_response("ERR", "BEEP", "BUSY", "BEEP_ACTIVE");
        return CMD_SUCCESS;
    }
    if (audio_processor_is_wav_active())
    {
        cmd_send_response("ERR", "BEEP", "BUSY", "WAV_ACTIVE");
        return CMD_SUCCESS;
    }
    if (play_manager_is_active())
    {
        cmd_send_response("ERR", "BEEP", "BUSY", "PLAY_ACTIVE");
        return CMD_SUCCESS;
    }
#endif
#ifdef ESP_PLATFORM
    if (streaming != 1)
    {
        int start_res = bt_manager_start_audio();
        if (start_res != 0)
        {
            ESP_LOGW(TAG, "BEEP: bt_manager_start_audio failed (%d)", start_res);
            cmd_send_response("ERR", "BEEP", "FAILED", "START_AUDIO");
            return CMD_SUCCESS;
        }
    }
#endif
#ifdef ESP_PLATFORM
    bool _prev_synth = audio_processor_is_synth_mode_enabled();
    audio_processor_set_synth_mode(true);
#endif
    esp_err_t _beep_res = audio_processor_beep_tone(CMD_BEEP_DURATION_MS, CMD_BEEP_FREQ_HZ);
#ifdef ESP_PLATFORM
    if (!_prev_synth) {
        audio_processor_set_synth_mode(false);
    }
#endif
    if (_beep_res == ESP_OK) {
        cmd_send_response("OK", "BEEP", "SENT", NULL);
    } else {
        ESP_LOGW(TAG, "BEEP: audio_processor_beep_tone() failed err=%d", (int)_beep_res);
        (void)audio_processor_dump_tag_queue(8, NULL);
        cmd_send_response("ERR", "BEEP", "FAILED", NULL);
    }
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_start(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (bt_manager_start_audio() != ESP_OK)
    {
        cmd_send_response("ERR", "START", "FAILED", NULL);
        return CMD_SUCCESS;
    }
    esp_err_t start_res = audio_processor_start();
    if (start_res != ESP_OK && start_res != ESP_ERR_INVALID_STATE)
    {
        cmd_send_response("ERR", "START", esp_err_to_name(start_res), "AUDIO_START");
        return CMD_SUCCESS;
    }
    cmd_send_response("OK", "START", "STARTED", NULL);
#elif defined(UNIT_TEST)
    esp_err_t start_res = audio_processor_start();
    int bt_res = bt_manager_start_audio();
    if (bt_res == 0 && (start_res == ESP_OK || start_res == ESP_ERR_INVALID_STATE)) {
        cmd_send_response("OK", "START", "MOCK_STARTED", NULL);
    } else {
        const char *err = (start_res != ESP_OK && start_res != ESP_ERR_INVALID_STATE)
                              ? esp_err_to_name(start_res)
                              : "FAILED";
        cmd_send_response("ERR", "START", err, NULL);
    }
#else
    cmd_send_response("OK", "START", "MOCK_STARTED", NULL);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_play(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "PLAY", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
    char path[256];
    const char *p = ctx->params[0];
    const char *root_for_path = cmd_files_get_root();
    if (root_for_path == NULL || root_for_path[0] == '\0')
    {
        root_for_path = "/spiffs";
    }

    int written = 0;
    if (p[0] == '/')
    {
        written = snprintf(path, sizeof(path), "%s", p);
    }
    else
    {
        size_t root_len = strlen(root_for_path);
        const char *sep = (root_len > 0 && root_for_path[root_len - 1] == '/') ? "" : "/";
        written = snprintf(path, sizeof(path), "%s%s%s", root_for_path, sep, p);
    }
    if (written < 0 || (size_t)written >= sizeof(path))
    {
        cmd_send_response("ERR", "PLAY", "PATH_TOO_LONG", ctx->params[0]);
        return CMD_SUCCESS;
    }

    esp_err_t mount_err = cmd_mount_spiffs_if_needed();
#ifdef ESP_PLATFORM
    if (mount_err != ESP_OK)
    {
        cmd_send_response("ERR", "PLAY", "SPIFFS_MOUNT_FAILED", esp_err_to_name(mount_err));
        return CMD_SUCCESS;
    }

    if (audio_processor_is_beep_active()) {
        cmd_send_response("ERR", "PLAY", "BUSY", "BEEP_ACTIVE");
        return CMD_SUCCESS;
    }
    if (audio_processor_is_wav_active()) {
        cmd_send_response("ERR", "PLAY", "BUSY", "WAV_ACTIVE");
        return CMD_SUCCESS;
    }

    int conn = bt_get_connection_state();
    int streaming = bt_get_streaming_state_int();
    if (conn != 1)
    {
        cmd_send_response("ERR", "PLAY", "A2DP_NOT_CONNECTED", path);
        return CMD_SUCCESS;
    }
    if (streaming != 1)
    {
        esp_err_t sret = bt_manager_start_audio();
        if (sret != ESP_OK)
        {
            cmd_send_response("ERR", "PLAY", esp_err_to_name(sret), path);
            return CMD_SUCCESS;
        }
    }
    esp_err_t r = audio_processor_play_wav(path);
    if (r == ESP_OK) {
        cmd_send_response("OK", "PLAY", "ENQUEUED", path);
    } else {
    	cmd_send_response("ERR", "PLAY", esp_err_to_name(r), path);
    }
#else
    (void)mount_err;
    if (audio_processor_is_beep_active()) {
        cmd_send_response("ERR", "PLAY", "BUSY", "BEEP_ACTIVE");
        return CMD_SUCCESS;
    }
    if (audio_processor_is_wav_active()) {
        cmd_send_response("ERR", "PLAY", "BUSY", "WAV_ACTIVE");
        return CMD_SUCCESS;
    }
    char host_path[sizeof(path)] = {0};
    const char *root = cmd_files_get_root();
    const char *play_path = path;

    if (ctx->params[0][0] == '/')
    {
        cmd_safe_copy(host_path, sizeof(host_path), ctx->params[0]);
        play_path = host_path;
    }
    else if (root != NULL && root[0] != '\0')
    {
        size_t root_len = strlen(root);
        const char *sep = (root_len > 0 && root[root_len - 1] == '/') ? "" : "/";
        int hw = snprintf(host_path, sizeof(host_path), "%s%s%s", root, sep, ctx->params[0]);
        if (hw < 0 || (size_t)hw >= sizeof(host_path))
        {
            cmd_send_response("ERR", "PLAY", "PATH_TOO_LONG", ctx->params[0]);
            return CMD_SUCCESS;
        }
        play_path = host_path;
    }
    else
    {
        cmd_safe_copy(host_path, sizeof(host_path), path);
        play_path = host_path;
    }

    if (audio_processor_play_wav(play_path) == ESP_OK)
        cmd_send_response("OK", "PLAY", "MOCK_ENQUEUED", ctx->params[0]);
    else
        cmd_send_response("ERR", "PLAY", "MOCK_FAILED", ctx->params[0]);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_stop(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (bt_manager_stop_audio() != ESP_OK)
    {
        cmd_send_response("ERR", "STOP", "FAILED", NULL);
        return CMD_SUCCESS;
    }
    esp_err_t stop_res = audio_processor_stop();
    if (stop_res != ESP_OK)
    {
        cmd_send_response("ERR", "STOP", esp_err_to_name(stop_res), "AUDIO_STOP");
        return CMD_SUCCESS;
    }
    cmd_send_response("OK", "STOP", "STOPPED", NULL);
#elif defined(UNIT_TEST)
    if (bt_manager_stop_audio() == 0)
        cmd_send_response("OK", "STOP", "MOCK_STOPPED", NULL);
    else
        cmd_send_response("ERR", "STOP", "FAILED", NULL);
#else
    cmd_send_response("OK", "STOP", "MOCK_STOPPED", NULL);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_mute(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (audio_processor_set_mute(true) == ESP_OK) {
        cmd_send_response("OK", "MUTE", "SET", NULL);
    } else {
    	cmd_send_response("ERR", "MUTE", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "MUTE", "MOCK_SET", NULL);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_unmute(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (audio_processor_set_mute(false) == ESP_OK) {
        cmd_send_response("OK", "UNMUTE", "CLEARED", NULL);
    } else {
    	cmd_send_response("ERR", "UNMUTE", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "UNMUTE", "MOCK_UNMUTED", NULL);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_volume(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "VOLUME", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
    int vol = atoi(ctx->params[0]);
    if (vol < 0 || vol > 100)
    {
        cmd_send_response("ERR", "VOLUME", "OUT_OF_RANGE", NULL);
        return CMD_SUCCESS;
    }
#ifdef ESP_PLATFORM
    if (audio_processor_set_volume((uint8_t)vol) == ESP_OK) {
        cmd_send_response("OK", "VOLUME", "SET", ctx->params[0]);
    } else {
    	cmd_send_response("ERR", "VOLUME", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "VOLUME", "MOCK_SET", ctx->params[0]);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_i2s_config(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "I2S_CONFIG", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
    int pins[4] = {-1, -1, -1, -1};
    char param_copy[128];
    cmd_safe_copy(param_copy, sizeof(param_copy), ctx->params[0]);
    char *tok = strtok(param_copy, ",");
    int idx = 0;
    while (tok != NULL && idx < 4)
    {
        pins[idx++] = atoi(tok);
        tok = strtok(NULL, ",");
    }
#ifdef ESP_PLATFORM
    if (audio_processor_set_i2s_pins(pins[0], pins[1], pins[2], pins[3]) == ESP_OK) {
        cmd_send_response("OK", "I2S_CONFIG", "APPLIED", ctx->params[0]);
    } else {
    	cmd_send_response("ERR", "I2S_CONFIG", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "I2S_CONFIG", "MOCK_APPLIED", ctx->params[0]);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_sample_rate(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "SAMPLE_RATE", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
    int rate = atoi(ctx->params[0]);
    if (rate != 8000 && rate != 16000 && rate != 22050 && rate != 32000 && rate != 44100 && rate != 48000 && rate != 96000)
    {
        cmd_send_response("ERR", "SAMPLE_RATE", "INVALID_RATE", ctx->params[0]);
        return CMD_SUCCESS;
    }
#ifdef ESP_PLATFORM
    if (audio_processor_set_sample_rate((audio_sample_rate_t)rate) == ESP_OK) {
        cmd_send_response("OK", "SAMPLE_RATE", "APPLIED", ctx->params[0]);
    } else {
    	cmd_send_response("ERR", "SAMPLE_RATE", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "SAMPLE_RATE", "MOCK_APPLIED", ctx->params[0]);
#endif
    return CMD_SUCCESS;
}
cmd_status_t cmd_handle_audio_autostart(const cmd_context_t *ctx)
{
    /* AUDIO_AUTOSTART [on|off|get]
     * Set or query audio autostart configuration in NVS.
     * When enabled (default), audio initializes automatically at boot.
     * When disabled, audio init is deferred until manual START command.
     */
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "AUDIO_AUTOSTART", "MISSING_PARAM", "Usage: AUDIO_AUTOSTART [on|off|get]");
        return CMD_SUCCESS;
    }

    const char *action = ctx->params[0];

    if (strcasecmp(action, "get") == 0)
    {
        /* Query current setting */
        uint8_t autostart = 1; /* default */
        esp_err_t err = nvs_storage_get_audio_autostart(&autostart);
        if (err == ESP_ERR_NOT_FOUND)
        {
            autostart = 1; /* default to enabled if not set */
        }
        const char *status = autostart ? "enabled" : "disabled";
        cmd_send_response("OK", "AUDIO_AUTOSTART", "STATUS", status);
        return CMD_SUCCESS;
    }
    if (strcasecmp(action, "on") == 0 || strcasecmp(action, "1") == 0)
    {
        /* Enable autostart */
        esp_err_t err = nvs_storage_set_audio_autostart(1);
        if (err == ESP_OK)
        {
            cmd_send_response("OK", "AUDIO_AUTOSTART", "ENABLED", "Restart required to apply");
        }
        else
        {
            cmd_send_response("ERR", "AUDIO_AUTOSTART", "WRITE_FAILED", NULL);
        }
        return CMD_SUCCESS;
    }
    if (strcasecmp(action, "off") == 0 || strcasecmp(action, "0") == 0)
    {
        /* Disable autostart */
        esp_err_t err = nvs_storage_set_audio_autostart(0);
        if (err == ESP_OK)
        {
            cmd_send_response("OK", "AUDIO_AUTOSTART", "DISABLED", "Restart required to apply");
        }
        else
        {
            cmd_send_response("ERR", "AUDIO_AUTOSTART", "WRITE_FAILED", NULL);
        }
        return CMD_SUCCESS;
    }
    
    cmd_send_response("ERR", "AUDIO_AUTOSTART", "INVALID_PARAM", "Use: on|off|get");
    return CMD_SUCCESS;
}
#include "cmd_handlers.h"

static const char *TAG = "cmd";

#ifdef ESP_PLATFORM
#include "esp_gap_bt_api.h"
#else
#include "esp_bt.h"
#endif

static bool s_cmd_mock_enabled = false;
static char s_cmd_mock_pairing_addr[32] = {0};
static char s_cmd_mock_passkey[16] = {0};

#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
extern int bt_get_connection_state(void);
extern int bt_get_streaming_state_int(void);
#endif

cmd_status_t cmd_handle_scan(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (bt_manager_start_scan() == ESP_OK) {
        cmd_send_response("OK", "SCAN", "STARTED", NULL);
    }
    else {
        cmd_send_response("ERR", "SCAN", "FAILED", NULL);
    }
#elif defined(UNIT_TEST)
    if (bt_manager_start_scan() == ESP_OK) {
        cmd_send_response("OK", "SCAN", "MOCK_STARTED", NULL);
    }
    else {
        cmd_send_response("ERR", "SCAN", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "SCAN", "MOCK_STARTED", NULL);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_connect(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "CONNECT", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
#ifdef ESP_PLATFORM
    if (bt_manager_connect(ctx->params[0]) == ESP_OK) {
        cmd_send_response("OK", "CONNECT", "INITIATED", NULL);
    }
    else {
        cmd_send_response("ERR", "CONNECT", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "CONNECT", "MOCK_CONNECTED", ctx->params[0]);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_connect_name(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "CONNECT_NAME", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
    char name_buf[128] = {0};
    cmd_safe_copy(name_buf, sizeof(name_buf), ctx->params[0]);
    for (int i = 1; i < ctx->param_count; ++i)
    {
        cmd_safe_append(name_buf, sizeof(name_buf), " ");
        cmd_safe_append(name_buf, sizeof(name_buf), ctx->params[i]);
    }
#ifdef ESP_PLATFORM
    if (bt_connect_by_name(name_buf) == ESP_OK) {
        cmd_send_response("OK", "CONNECT_NAME", "INITIATED", NULL);
    }
    else {
        cmd_send_response("ERR", "CONNECT_NAME", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "CONNECT_NAME", "MOCK_INITIATED", name_buf);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_disconnect(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    if (bt_manager_disconnect() == ESP_OK) {
        cmd_send_response("OK", "DISCONNECT", "DONE", NULL);
    }
    else {
        cmd_send_response("ERR", "DISCONNECT", "FAILED", NULL);
    }
#elif defined(UNIT_TEST)
    if (bt_manager_disconnect() == 0) {
        cmd_send_response("OK", "DISCONNECT", "MOCK_DONE", NULL);
    }
    else {
        cmd_send_response("ERR", "DISCONNECT", "FAILED", NULL);
    }
#else
    cmd_send_response("OK", "DISCONNECT", "MOCK_DONE", NULL);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_pair(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "PAIR", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
    const char *first = ctx->params[0];
    bool looks_like_mac = (strchr(first, ':') != NULL);
    if (looks_like_mac)
    {
#ifdef ESP_PLATFORM
        if (bt_manager_start_pair(first) == ESP_OK) {
            cmd_send_response("OK", "PAIR", "INITIATED", first);
        }
        else {
            cmd_send_response("ERR", "PAIR", "FAILED", first);
        }
#elif defined(UNIT_TEST)
        if (bt_manager_start_pair(first) == ESP_OK) {
            cmd_send_response("OK", "PAIR", "MOCK_INITIATED", first);
        }
        else {
            cmd_send_response("ERR", "PAIR", "FAILED", first);
        }
#else
        cmd_send_response("OK", "PAIR", "MOCK_INITIATED", first);
#endif
    }
    else
    {
        char name_buf[128] = {0};
        cmd_safe_copy(name_buf, sizeof(name_buf), first);
        for (int i = 1; i < ctx->param_count; ++i)
        {
            cmd_safe_append(name_buf, sizeof(name_buf), " ");
            cmd_safe_append(name_buf, sizeof(name_buf), ctx->params[i]);
        }
#ifdef ESP_PLATFORM
        if (bt_connect_by_name(name_buf) == ESP_OK) {
            cmd_send_response("OK", "PAIR", "INITIATED_BY_NAME", name_buf);
        }
        else {
            cmd_send_response("ERR", "PAIR", "FAILED_BY_NAME", name_buf);
        }
#else
        cmd_send_response("OK", "PAIR", "MOCK_INITIATED_BY_NAME", name_buf);
#endif
    }
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_paired(const cmd_context_t *ctx)
{
    (void)ctx;
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
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_confirm_pin(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "CONFIRM_PIN", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
    const char *mac_param = NULL;
    const char *decision_param = NULL;
    if (ctx->param_count >= 1)
    {
        if (strchr(ctx->params[0], ':') != NULL) {
            mac_param = ctx->params[0];
        }
        else {
            decision_param = ctx->params[0];
        }
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
        if (strcasecmp(decision_param, "REJECT") == 0 || strcasecmp(decision_param, "NO") == 0 || strcmp(decision_param, "0") == 0) {
            accept = false;
        }
        else if (strcasecmp(decision_param, "ACCEPT") == 0 || strcasecmp(decision_param, "YES") == 0 || strcmp(decision_param, "1") == 0) {
            accept = true;
        }
        else if (decision_param[0] != '\0' && strchr(decision_param, ':') == NULL)
        {
            cmd_send_response("ERR", "CONFIRM_PIN", "BAD_PARAM", decision_param);
            return CMD_SUCCESS;
        }
    }
#ifdef ESP_PLATFORM
    bt_pairing_request_info_t pending = {0};
    if ((mac_param == NULL || mac_param[0] == '\0') && !bt_pairing_get_pending_request(&pending))
    {
        cmd_send_response("ERR", "CONFIRM_PIN", "NO_PENDING", NULL);
        return CMD_SUCCESS;
    }
    const char *target_mac = mac_param && mac_param[0] ? mac_param : pending.mac;
    if (target_mac == NULL || target_mac[0] == '\0')
    {
        cmd_send_response("ERR", "CONFIRM_PIN", "NO_MAC", NULL);
        return CMD_SUCCESS;
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
        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0) {
            mac = s_cmd_mock_pairing_addr;
        }
        else
        {
            cmd_send_response("ERR", "CONFIRM_PIN", "NO_MOCK", NULL);
            return CMD_SUCCESS;
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
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_enter_pin(const cmd_context_t *ctx)
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
            return CMD_SUCCESS;
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
        return CMD_SUCCESS;
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
        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0) {
            mac = s_cmd_mock_pairing_addr;
        }
        else
        {
            cmd_send_response("ERR", "ENTER_PIN", "NO_MOCK", NULL);
            return CMD_SUCCESS;
        }
    }
    if (pin_param == NULL || pin_param[0] == '\0')
    {
        if (nvs_storage_get_default_pin(default_pin, sizeof(default_pin)) == ESP_OK && strlen(default_pin) > 0) {
            pin_param = default_pin;
        }
    }
    if (pin_param == NULL || pin_param[0] == '\0')
    {
        cmd_send_response("ERR", "ENTER_PIN", "MISSING_PARAM", mac);
        return CMD_SUCCESS;
    }
    uint8_t bda[6] = {0};
    if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6)
    {
        uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
        size_t pin_len = strlen(pin_param);
        if (pin_len > ESP_BT_PIN_CODE_LEN) {
            pin_len = ESP_BT_PIN_CODE_LEN;
        }
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
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_set_default_pin(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "SET_DEFAULT_PIN", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
#ifdef ESP_PLATFORM
    if (nvs_storage_set_default_pin(ctx->params[0]) == ESP_OK) {
        cmd_send_response("OK", "SET_DEFAULT_PIN", "SUCCESS", ctx->params[0]);
    }
    else {
        cmd_send_response("ERR", "SET_DEFAULT_PIN", "FAILED", NULL);
    }
#else
    nvs_storage_set_default_pin(ctx->params[0]);
    cmd_send_response("OK", "SET_DEFAULT_PIN", "MOCK_SUCCESS", ctx->params[0]);
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_unpair(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "UNPAIR", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
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
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_unpair_all(const cmd_context_t *ctx)
{
    (void)ctx;
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
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_set_name(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "SET_NAME", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }
#ifdef ESP_PLATFORM
    if (nvs_storage_set_device_name(ctx->params[0]) == ESP_OK)
    {
        bt_manager_set_name(ctx->params[0]);
        cmd_send_response("OK", "SET_NAME", "SUCCESS", ctx->params[0]);
    }
    else {
        cmd_send_response("ERR", "SET_NAME", "FAILED", NULL);
    }
#else
    nvs_storage_set_device_name(ctx->params[0]);
    cmd_send_response("OK", "SET_NAME", "MOCK_SUCCESS", ctx->params[0]);
#endif
    return CMD_SUCCESS;
}

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
            if (comma)
            {
                *comma = '\0';
            }
            cmd_safe_copy(s_cmd_mock_pairing_addr, sizeof(s_cmd_mock_pairing_addr), mac);
#ifdef ESP_PLATFORM
            ESP_LOGI(TAG, "DIAG-DEBUG-MOCK-ADD-BEFORE-SEND: mac=%s", s_cmd_mock_pairing_addr);  // NOLINT(bugprone-branch-clone)
#else
            printf("DIAG-DEBUG-MOCK-ADD-BEFORE-SEND: mac=%s\n", s_cmd_mock_pairing_addr);
#endif
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
            cmd_safe_copy(s_cmd_mock_pairing_addr, sizeof(s_cmd_mock_pairing_addr), ctx->params[1]);
            char pass[16] = "000000";
            size_t maclen = strlen(s_cmd_mock_pairing_addr);
            if (maclen >= 2)
            {
                const char *tail = s_cmd_mock_pairing_addr + (maclen > 5 ? maclen - 5 : 0);
                cmd_safe_copy(pass, sizeof(pass), tail);
            }
            cmd_safe_copy(s_cmd_mock_passkey, sizeof(s_cmd_mock_passkey), pass);
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
        if (audio_processor_emit_sync_worker_diag() == ESP_OK) {
            cmd_send_response("OK", "DEBUG", "WORKER_DIAG_EMITTED", NULL);
        }
        else {
            cmd_send_response("ERR", "DEBUG", "WORKER_DIAG_FAILED", NULL);
        }
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
    else if (strcasecmp(ctx->params[0], "AUDIO_DIAG_SUMMARY") == 0)
    {
#ifdef ESP_PLATFORM
        cmd_send_response("OK", "DEBUG", "AUDIO_DIAG_SUMMARY", NULL);
        if (audio_processor_emit_diag_summary() != ESP_OK)
        {
            ESP_LOGW(TAG, "audio_processor_emit_diag_summary() failed");  // NOLINT(bugprone-branch-clone)
        }
#else
        cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
    }
    else if (strcasecmp(ctx->params[0], "AUDIO_DIAG_PROBE") == 0)
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
            cmd_send_response("OK", "DEBUG", "AUDIO_DIAG_PROBE_ARMED", (ctx->param_count >= 3) ? ctx->params[2] : "16");
        } else if (strcasecmp(ctx->params[1], "DUMP") == 0) {
            cmd_send_response("OK", "DEBUG", "AUDIO_DIAG_PROBE_DUMP", NULL);
            if (audio_processor_emit_probe() != ESP_OK) {
                ESP_LOGW(TAG, "audio_processor_emit_probe() failed");  // NOLINT(bugprone-branch-clone)
            }
        } else {
            cmd_send_response("ERR", "DEBUG", "AUDIO_DIAG_PROBE_BAD_PARAM", ctx->params[1]);
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
    else if (strcasecmp(ctx->params[0], "FORCE_BEEP") == 0)
    {
#ifdef ESP_PLATFORM
        if (audio_processor_beep(200) == ESP_OK) {
            cmd_send_response("OK", "DEBUG", "FORCE_BEEP_SENT", NULL);
        }
        else {
            cmd_send_response("ERR", "DEBUG", "FORCE_BEEP_FAILED", NULL);
        }
#else
        cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
    }
    else if (strcasecmp(ctx->params[0], "DRAIN_QUEUE") == 0 || strcasecmp(ctx->params[0], "DRAIN_RING") == 0)
    {
#ifdef ESP_PLATFORM
        if (audio_processor_drain_ring() == ESP_OK) {
            cmd_send_response("OK", "DEBUG", "DRAIN_QUEUE_DONE", NULL);
        }
        else {
            cmd_send_response("ERR", "DEBUG", "DRAIN_QUEUE_FAILED", NULL);
        }
#else
        cmd_send_response("ERR", "DEBUG", "UNSUPPORTED", ctx->params[0]);
#endif
    }
    else if (strcasecmp(ctx->params[0], "DRAM") == 0)
    {
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
    {
        cmd_send_response("ERR", "DEBUG", "UNKNOWN_SUBCMD", ctx->params[0]);
    }
    return CMD_SUCCESS;
}

#include "cmd_handlers.h"
#include "cmd_handlers_internal.h"

#ifdef ESP_PLATFORM
#include "esp_gap_bt_api.h"
#else
#include "esp_bt.h"
#endif

/* Mock pairing state shared with the DEBUG handlers (cmd_handlers_debug.c):
 * declared in cmd_handlers_internal.h, defined once here. */
bool g_cmd_mock_enabled = false;
char g_cmd_mock_pairing_addr[32] = {0};
char g_cmd_mock_passkey[16] = {0};

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
    /* Clear in-memory reconnect target so explicit DISCONNECT does not
     * trigger the session auto-reconnect loop.  Only needed on the real
     * device; host/unit-test builds do not run the session reconnect timer. */
    bt_connection_manager_clear_reconnect_target();
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
    esp_err_t rc = nvs_storage_get_paired_count(&count);
    /* ESP_ERR_NOT_FOUND means the count key doesn't exist — valid empty state
     * after UNPAIR_ALL clears the NVS namespace.  Treat as count=0. */
    if (rc == ESP_OK || rc == ESP_ERR_NOT_FOUND)
    {
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
        char data[64];
        snprintf(data, sizeof(data), "%d", count);
        cmd_send_response("OK", "PAIRED", "COUNT", data);
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
        if (g_cmd_mock_enabled && strlen(g_cmd_mock_pairing_addr) > 0) {
            mac = g_cmd_mock_pairing_addr;
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
        if (g_cmd_mock_enabled && strlen(g_cmd_mock_pairing_addr) > 0 && strcmp(mac, g_cmd_mock_pairing_addr) == 0)
        {
            cmd_send_event_pair(accept ? "SUCCESS" : "FAILED", g_cmd_mock_pairing_addr);
            g_cmd_mock_enabled = false;
            g_cmd_mock_pairing_addr[0] = '\0';
            g_cmd_mock_passkey[0] = '\0';
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
        if (g_cmd_mock_enabled && strlen(g_cmd_mock_pairing_addr) > 0) {
            mac = g_cmd_mock_pairing_addr;
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
        if (g_cmd_mock_enabled && strlen(g_cmd_mock_pairing_addr) > 0 && strcmp(mac, g_cmd_mock_pairing_addr) == 0)
        {
            cmd_send_event_pair("SUCCESS", g_cmd_mock_pairing_addr);
            g_cmd_mock_enabled = false;
            g_cmd_mock_pairing_addr[0] = '\0';
            g_cmd_mock_passkey[0] = '\0';
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

cmd_status_t cmd_handle_last_mac(const cmd_context_t *ctx)
{
    /* LAST_MAC get   — return the most-recently-connected MAC (or NONE)
     * LAST_MAC clear — erase the stored MAC so auto-connect is disabled
     */
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "LAST_MAC", "MISSING_PARAM", "Usage: LAST_MAC [get|clear]");
        return CMD_SUCCESS;
    }

    const char *sub = ctx->params[0];

    if (strcasecmp(sub, "get") == 0)
    {
        char mac[18] = {0};
        esp_err_t err = nvs_storage_get_last_connected_mac(mac, sizeof(mac));
        if (err == ESP_OK && mac[0] != '\0') {
            cmd_send_response("OK", "LAST_MAC", mac, NULL);
        } else {
            cmd_send_response("OK", "LAST_MAC", "NONE", NULL);
        }
        return CMD_SUCCESS;
    }

    if (strcasecmp(sub, "clear") == 0)
    {
        esp_err_t err = nvs_storage_clear_last_connected_mac();
        if (err == ESP_OK) {
            cmd_send_response("OK", "LAST_MAC", "CLEARED", NULL);
        } else {
            cmd_send_response("ERR", "LAST_MAC", "NVS_ERROR", NULL);
        }
        return CMD_SUCCESS;
    }

    cmd_send_response("ERR", "LAST_MAC", "UNKNOWN_SUBCMD", sub);
    return CMD_SUCCESS;
}


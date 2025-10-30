#include "command_interface.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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
#define TAG "CMD_IF"
/* Prefer the configured console UART if available so we don't call into an
 * uninstalled UART driver (which logs "uart driver error"). Fall back to
 * UART_NUM_1 for platforms that don't provide CONFIG_ESP_CONSOLE_UART_NUM. */
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
#define CMD_UART_NUM CONFIG_ESP_CONSOLE_UART_NUM
#else
#define CMD_UART_NUM UART_NUM_1
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
static inline void bt_manager_set_name(const char* name) { (void)name; }
#endif
#endif

// Lightweight internal mock state used for deterministic host-mode pairing
static bool s_cmd_mock_enabled = false;
static char s_cmd_mock_pairing_addr[32] = {0};
static char s_cmd_mock_passkey[16] = {0};

// Sequence counter for pairing event ordering
static uint32_t s_event_sequence = 0;

// Test-only function to reset sequence counter
#if defined(UNIT_TEST) || defined(ESP_PLATFORM)
void cmd_reset_event_sequence(void) {
    s_event_sequence = 0;
}
#endif

// Main command executor. Keeps behavior identical across ESP and host
// builds but uses #ifdef guards where device-specific APIs are required.
cmd_status_t cmd_execute(const cmd_context_t* ctx)
{
    if (ctx == NULL) return CMD_ERROR_NOT_INITIALIZED;

    switch (ctx->type) {
    case CMD_TYPE_STATUS: {
    audio_status_t status = {0};
    int paired_count = 0;
    int mute_val = -1;
    int sample_rate_val = 0;
#ifdef ESP_PLATFORM
    if (audio_processor_get_status(&status) == ESP_OK) {
        mute_val = status.mute ? 1 : 0;
        sample_rate_val = (int)status.sample_rate;
    }
#else
    (void)status;
    if (audio_processor_get_status(&status) == ESP_OK) {
        mute_val = 0;
        sample_rate_val = 0;
    }
#endif
    (void)nvs_storage_get_paired_count(&paired_count);
    char data[256];
    snprintf(data, sizeof(data), "MUTE=%d,SAMPLE_RATE=%d,PAIRED_COUNT=%d,INIT=%d,RUN=%d,VOL=%d",
         mute_val, sample_rate_val, paired_count, status.initialized, status.running, status.volume);
    cmd_send_response("OK", "STATUS", "CURRENT", data);
    } break;

    case CMD_TYPE_VERSION:
    cmd_send_response("OK", "VERSION", "1.0.0", NULL);
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
#else
    cmd_send_response("OK", "SCAN", "MOCK_STARTED", NULL);
#endif
    break;

    case CMD_TYPE_BEEP: {
    /* Emit a short audible beep only when a BT device is connected. Use
     * bt_get_connection_state() (returns 1 when connected) for host tests
     * and the audio_processor_beep() API to request the tone. */
    if (bt_get_connection_state() != 1) { cmd_send_response("ERR", "BEEP", "NOT_CONNECTED", NULL); break; }
    /* Use a 200ms default beep duration; production implementation may
     * ignore or implement frequency/duration semantics. */
    if (audio_processor_beep(200) == ESP_OK) cmd_send_response("OK", "BEEP", "SENT", NULL);
    else cmd_send_response("ERR", "BEEP", "FAILED", NULL);
    } break;
    break;

    case CMD_TYPE_CONNECT:
    if (ctx->param_count < 1) { cmd_send_response("ERR", "CONNECT", "MISSING_PARAM", NULL); break; }
#ifdef ESP_PLATFORM
    if (bt_manager_connect(ctx->params[0]) == ESP_OK) cmd_send_response("OK", "CONNECT", "INITIATED", NULL);
    else cmd_send_response("ERR", "CONNECT", "FAILED", NULL);
#else
    cmd_send_response("OK", "CONNECT", "MOCK_CONNECTED", ctx->params[0]);
#endif
    break;

    case CMD_TYPE_DISCONNECT:
#ifdef ESP_PLATFORM
    if (bt_manager_disconnect() == ESP_OK) cmd_send_response("OK", "DISCONNECT", "DONE", NULL);
    else cmd_send_response("ERR", "DISCONNECT", "FAILED", NULL);
#elif defined(UNIT_TEST)
    /* In unit tests use the real manager wrapper so tests can simulate
     * failure via the bt_manager_test hooks. Preserve the MOCK_* result
     * string on success to remain compatible with existing expectations. */
    if (bt_manager_disconnect() == 0) cmd_send_response("OK", "DISCONNECT", "MOCK_DONE", NULL);
    else cmd_send_response("ERR", "DISCONNECT", "FAILED", NULL);
#else
    cmd_send_response("OK", "DISCONNECT", "MOCK_DONE", NULL);
#endif
    break;

    case CMD_TYPE_START:
#ifdef ESP_PLATFORM
    if (bt_manager_start_audio() == ESP_OK) cmd_send_response("OK", "START", "STARTED", NULL);
    else cmd_send_response("ERR", "START", "FAILED", NULL);
#elif defined(UNIT_TEST)
    if (bt_manager_start_audio() == 0) cmd_send_response("OK", "START", "MOCK_STARTED", NULL);
    else cmd_send_response("ERR", "START", "FAILED", NULL);
#else
    cmd_send_response("OK", "START", "MOCK_STARTED", NULL);
#endif
    break;

    case CMD_TYPE_STOP:
#ifdef ESP_PLATFORM
    if (bt_manager_stop_audio() == ESP_OK) cmd_send_response("OK", "STOP", "STOPPED", NULL);
    else cmd_send_response("ERR", "STOP", "FAILED", NULL);
#elif defined(UNIT_TEST)
    if (bt_manager_stop_audio() == 0) cmd_send_response("OK", "STOP", "MOCK_STOPPED", NULL);
    else cmd_send_response("ERR", "STOP", "FAILED", NULL);
#else
    cmd_send_response("OK", "STOP", "MOCK_STOPPED", NULL);
#endif
    break;

    case CMD_TYPE_CONNECT_NAME: {
    if (ctx->param_count < 1) { cmd_send_response("ERR", "CONNECT_NAME", "MISSING_PARAM", NULL); break; }
    char name_buf[128] = {0};
    strncpy(name_buf, ctx->params[0], sizeof(name_buf)-1);
    for (int i = 1; i < ctx->param_count; ++i) { strncat(name_buf, " ", sizeof(name_buf)-strlen(name_buf)-1); strncat(name_buf, ctx->params[i], sizeof(name_buf)-strlen(name_buf)-1); }
#ifdef ESP_PLATFORM
    if (bt_connect_by_name(name_buf) == ESP_OK) cmd_send_response("OK", "CONNECT_NAME", "INITIATED", NULL);
    else cmd_send_response("ERR", "CONNECT_NAME", "FAILED", NULL);
#else
    cmd_send_response("OK", "CONNECT_NAME", "MOCK_INITIATED", name_buf);
#endif
    } break;

    case CMD_TYPE_CONFIRM_PIN: {
    if (ctx->param_count < 1) { cmd_send_response("ERR", "CONFIRM_PIN", "MISSING_PARAM", NULL); break; }
    const char* mac_param = NULL;
    const char* decision_param = NULL;
    if (ctx->param_count >= 1) {
        if (strchr(ctx->params[0], ':') != NULL) mac_param = ctx->params[0]; else decision_param = ctx->params[0];
    }
    if (ctx->param_count >= 2) {
        if (mac_param == NULL) {
            mac_param = ctx->params[0];
            decision_param = ctx->params[1];
        } else {
            decision_param = ctx->params[1];
        }
    }
    bool accept = true;
    if (decision_param != NULL) {
        if (strcasecmp(decision_param, "REJECT") == 0 || strcasecmp(decision_param, "NO") == 0 || strcmp(decision_param, "0") == 0) accept = false;
        else if (strcasecmp(decision_param, "ACCEPT") == 0 || strcasecmp(decision_param, "YES") == 0 || strcmp(decision_param, "1") == 0) accept = true;
        else if (decision_param[0] != '\0' && strchr(decision_param, ':') == NULL) {
            // Unknown keyword: treat as error to avoid silently accepting
            cmd_send_response("ERR", "CONFIRM_PIN", "BAD_PARAM", decision_param);
            break;
        }
    }
#ifdef ESP_PLATFORM
    bt_pairing_request_info_t pending = {0};
    if ((mac_param == NULL || mac_param[0] == '\0') && !bt_pairing_get_pending_request(&pending)) {
        cmd_send_response("ERR", "CONFIRM_PIN", "NO_PENDING", NULL);
        break;
    }
    const char* target_mac = mac_param && mac_param[0] ? mac_param : pending.mac;
    if (target_mac == NULL || target_mac[0] == '\0') {
        cmd_send_response("ERR", "CONFIRM_PIN", "NO_MAC", NULL);
        break;
    }
    esp_err_t cerr = bt_pairing_confirm(target_mac, accept);
    if (cerr == ESP_OK) {
        cmd_send_response("OK", "CONFIRM_PIN", accept ? "ACCEPTED" : "REJECTED", target_mac);
    } else {
        cmd_send_response("ERR", "CONFIRM_PIN", esp_err_to_name(cerr), target_mac);
    }
#else
    const char* mac = mac_param;
    if (mac == NULL) {
        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0) mac = s_cmd_mock_pairing_addr;
        else { cmd_send_response("ERR", "CONFIRM_PIN", "NO_MOCK", NULL); break; }
    }
    uint8_t bda[6] = {0};
    if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
        esp_bt_gap_ssp_confirm_reply(bda, accept);
        cmd_send_response("OK", "CONFIRM_PIN", accept ? "MOCK_ACCEPTED" : "MOCK_REJECTED", mac);
        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0 && strcmp(mac, s_cmd_mock_pairing_addr) == 0) {
        cmd_send_event_pair(accept ? "SUCCESS" : "FAILED", s_cmd_mock_pairing_addr);
        s_cmd_mock_enabled = false; s_cmd_mock_pairing_addr[0] = '\0'; s_cmd_mock_passkey[0] = '\0';
        }
    } else {
        cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC", NULL);
    }
#endif
    } break;

    case CMD_TYPE_ENTER_PIN: {
    const char* mac_param = NULL;
    const char* pin_param = NULL;
    char default_pin[ESP_BT_PIN_CODE_LEN + 1] = {0};
    if (ctx->param_count >= 1) {
        mac_param = ctx->params[0];
    }
    if (ctx->param_count >= 2) {
        pin_param = ctx->params[1];
    }
#ifdef ESP_PLATFORM
    bt_pairing_request_info_t pending = {0};
    if (mac_param == NULL || mac_param[0] == '\0') {
        if (!bt_pairing_get_pending_request(&pending)) {
            cmd_send_response("ERR", "ENTER_PIN", "NO_PENDING", NULL);
            break;
        }
        mac_param = pending.mac;
    }
    if (pin_param == NULL || pin_param[0] == '\0') {
        if (nvs_storage_get_default_pin(default_pin, sizeof(default_pin)) == ESP_OK && strlen(default_pin) > 0) {
            pin_param = default_pin;
        }
    }
    if (pin_param == NULL || pin_param[0] == '\0') {
        cmd_send_response("ERR", "ENTER_PIN", "MISSING_PIN", mac_param);
        break;
    }
    esp_err_t perr = bt_pairing_submit_pin(mac_param, pin_param);
    if (perr == ESP_OK) {
        cmd_send_response("OK", "ENTER_PIN", "SENT", mac_param);
    } else {
        cmd_send_response("ERR", "ENTER_PIN", esp_err_to_name(perr), mac_param);
    }
#else
    const char* mac = mac_param;
    if (mac == NULL || mac[0] == '\0') {
        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0) mac = s_cmd_mock_pairing_addr;
        else { cmd_send_response("ERR", "ENTER_PIN", "NO_MOCK", NULL); break; }
    }
    if (pin_param == NULL || pin_param[0] == '\0') {
        if (nvs_storage_get_default_pin(default_pin, sizeof(default_pin)) == ESP_OK && strlen(default_pin) > 0) pin_param = default_pin;
    }
    if (pin_param == NULL || pin_param[0] == '\0') { cmd_send_response("ERR", "ENTER_PIN", "MISSING_PARAM", mac); break; }
    uint8_t bda[6] = {0};
    if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
        uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0}; size_t pin_len = strlen(pin_param); if (pin_len > ESP_BT_PIN_CODE_LEN) pin_len = ESP_BT_PIN_CODE_LEN; memcpy(pin_code, pin_param, pin_len);
        esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code);
        cmd_send_response("OK", "ENTER_PIN", "MOCK_SENT", mac);
        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0 && strcmp(mac, s_cmd_mock_pairing_addr) == 0) {
        cmd_send_event_pair("SUCCESS", s_cmd_mock_pairing_addr); s_cmd_mock_enabled = false; s_cmd_mock_pairing_addr[0] = '\0'; s_cmd_mock_passkey[0] = '\0';
        }
    } else {
        cmd_send_response("ERR", "ENTER_PIN", "INVALID_MAC", NULL);
    }
#endif
    } break;

    case CMD_TYPE_MUTE:
#ifdef ESP_PLATFORM
    if (audio_processor_set_mute(true) == ESP_OK) cmd_send_response("OK", "MUTE", "SET", NULL);
    else cmd_send_response("ERR", "MUTE", "FAILED", NULL);
#else
    cmd_send_response("OK", "MUTE", "MOCK_SET", NULL);
#endif
    break;

    case CMD_TYPE_UNMUTE:
#ifdef ESP_PLATFORM
    if (audio_processor_set_mute(false) == ESP_OK) cmd_send_response("OK", "UNMUTE", "CLEARED", NULL);
    else cmd_send_response("ERR", "UNMUTE", "FAILED", NULL);
#else
    cmd_send_response("OK", "UNMUTE", "MOCK_UNMUTED", NULL);
#endif
    break;

    case CMD_TYPE_VOLUME: {
    if (ctx->param_count < 1) { cmd_send_response("ERR", "VOLUME", "MISSING_PARAM", NULL); break; }
    int vol = atoi(ctx->params[0]); if (vol < 0 || vol > 100) { cmd_send_response("ERR", "VOLUME", "OUT_OF_RANGE", NULL); break; }
#ifdef ESP_PLATFORM
    if (audio_processor_set_volume((uint8_t)vol) == ESP_OK) cmd_send_response("OK", "VOLUME", "SET", ctx->params[0]); else cmd_send_response("ERR", "VOLUME", "FAILED", NULL);
#else
    cmd_send_response("OK", "VOLUME", "MOCK_SET", ctx->params[0]);
#endif
    } break;

    case CMD_TYPE_I2S_CONFIG: {
    if (ctx->param_count < 1) { cmd_send_response("ERR", "I2S_CONFIG", "MISSING_PARAM", NULL); break; }
    int pins[4] = { -1, -1, -1, -1 };
    char param_copy[128]; strncpy(param_copy, ctx->params[0], sizeof(param_copy)-1); param_copy[sizeof(param_copy)-1] = '\0';
    char* tok = strtok(param_copy, ","); int idx = 0; while (tok != NULL && idx < 4) { pins[idx++] = atoi(tok); tok = strtok(NULL, ","); }
#ifdef ESP_PLATFORM
    if (audio_processor_set_i2s_pins(pins[0], pins[1], pins[2], pins[3]) == ESP_OK) cmd_send_response("OK", "I2S_CONFIG", "APPLIED", ctx->params[0]); else cmd_send_response("ERR", "I2S_CONFIG", "FAILED", NULL);
#else
    cmd_send_response("OK", "I2S_CONFIG", "MOCK_APPLIED", ctx->params[0]);
#endif
    } break;

    case CMD_TYPE_PAIR: {
    if (ctx->param_count < 1) { cmd_send_response("ERR", "PAIR", "MISSING_PARAM", NULL); break; }
    // If the first param contains a colon, treat it as a MAC address. Otherwise
    // treat the whole param list as a device name (may contain spaces).
    const char* first = ctx->params[0];
    bool looks_like_mac = (strchr(first, ':') != NULL);
    if (looks_like_mac) {
#ifdef ESP_PLATFORM
        if (bt_manager_pair(first) == ESP_OK) cmd_send_response("OK", "PAIR", "INITIATED", first);
        else cmd_send_response("ERR", "PAIR", "FAILED", first);
#else
        cmd_send_response("OK", "PAIR", "MOCK_INITIATED", first);
#endif
    } else {
        // Join all params into a single name string
        char name_buf[128] = {0};
        strncpy(name_buf, first, sizeof(name_buf)-1);
        for (int i = 1; i < ctx->param_count; ++i) {
            strncat(name_buf, " ", sizeof(name_buf)-strlen(name_buf)-1);
            strncat(name_buf, ctx->params[i], sizeof(name_buf)-strlen(name_buf)-1);
        }
#ifdef ESP_PLATFORM
        // Try connect-by-name which triggers pairing as needed
        if (bt_connect_by_name(name_buf) == ESP_OK) cmd_send_response("OK", "PAIR", "INITIATED_BY_NAME", name_buf);
        else cmd_send_response("ERR", "PAIR", "FAILED_BY_NAME", name_buf);
#else
        cmd_send_response("OK", "PAIR", "MOCK_INITIATED_BY_NAME", name_buf);
#endif
    }
    } break;

    case CMD_TYPE_PAIRED: {
    int count = 0;
    if (nvs_storage_get_paired_count(&count) == ESP_OK) {
        char data[64]; snprintf(data, sizeof(data), "%d", count); cmd_send_response("OK", "PAIRED", "COUNT", data);
        for (int i = 0; i < count; ++i) { char mac[32]; char name[64]; if (nvs_storage_get_paired_device_by_index(i, mac, sizeof(mac), name, sizeof(name)) == ESP_OK) { char buf[128]; snprintf(buf, sizeof(buf), "%s,%s", mac, name); cmd_send_response("INFO", "PAIRED", "ITEM", buf); } }
    } else {
        cmd_send_response("ERR", "PAIRED", "READ_FAILED", NULL);
    }
    } break;

    case CMD_TYPE_SAMPLE_RATE:
    if (ctx->param_count < 1) { cmd_send_response("ERR", "SAMPLE_RATE", "MISSING_PARAM", NULL); break; }
    { int rate = atoi(ctx->params[0]); if (rate != 8000 && rate != 16000 && rate != 22050 && rate != 32000 && rate != 44100 && rate != 48000 && rate != 96000) { cmd_send_response("ERR", "SAMPLE_RATE", "INVALID_RATE", ctx->params[0]); break; } }
#ifdef ESP_PLATFORM
    if (audio_processor_set_sample_rate((audio_sample_rate_t)atoi(ctx->params[0])) == ESP_OK) cmd_send_response("OK", "SAMPLE_RATE", "APPLIED", ctx->params[0]); else cmd_send_response("ERR", "SAMPLE_RATE", "FAILED", NULL);
#else
    cmd_send_response("OK", "SAMPLE_RATE", "MOCK_APPLIED", ctx->params[0]);
#endif
    break;

    case CMD_TYPE_SET_NAME:
    if (ctx->param_count < 1) { cmd_send_response("ERR", "SET_NAME", "MISSING_PARAM", NULL); break; }
#ifdef ESP_PLATFORM
    if (nvs_storage_set_device_name(ctx->params[0]) == ESP_OK) { bt_manager_set_name(ctx->params[0]); cmd_send_response("OK", "SET_NAME", "SUCCESS", ctx->params[0]); }
    else cmd_send_response("ERR", "SET_NAME", "FAILED", NULL);
#else
    nvs_storage_set_device_name(ctx->params[0]); cmd_send_response("OK", "SET_NAME", "MOCK_SUCCESS", ctx->params[0]);
#endif
    break;

    case CMD_TYPE_DEBUG: {
    if (ctx->param_count < 1) { cmd_send_response("ERR", "DEBUG", "MISSING_PARAM", NULL); break; }
    /* Emit diagnostics via ESP logging on-device so they respect log
     * configuration, and fall back to printf for host/unit tests. */
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "DIAG-DEBUG-ENTRY subcmd=%s param_count=%d", ctx->params[0], ctx->param_count);
#else
    printf("DIAG-DEBUG-ENTRY subcmd=%s param_count=%d\n", ctx->params[0], ctx->param_count);
#endif
    if (strcasecmp(ctx->params[0], "MOCK_ON") == 0) { s_cmd_mock_enabled = true; cmd_send_response("OK", "DEBUG", "MOCK_ON", NULL); }
    else if (strcasecmp(ctx->params[0], "MOCK_ADD") == 0) {
        if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0) {
            cmd_send_response("ERR", "DEBUG", "MOCK_ADD_MISSING", NULL);
        } else {
            /* Join remaining params with a comma so callers may provide
             * either `MAC,PASS` or `MAC PASS` forms. This makes the
             * test-friendly space-separated form accepted while remaining
             * compatible with the original comma-separated syntax. */
            char payload[128];
            size_t pos = 0;
            for (int i = 1; i < ctx->param_count && pos + 1 < sizeof(payload); ++i) {
                const char* p = ctx->params[i];
                size_t l = strlen(p);
                if (pos + l + 1 >= sizeof(payload)) break;
                if (i > 1) payload[pos++] = ',';
                memcpy(&payload[pos], p, l);
                pos += l;
            }
            payload[pos] = '\0';

            /* Accept either a single MAC (no comma) or MAC,extra form. */
            char* comma = strchr(payload, ',');
            const char* mac = payload;
            if (comma) {
                *comma = '\0';
            }
            /* store the MAC we will use for the mock pairing */
            strncpy(s_cmd_mock_pairing_addr, mac, sizeof(s_cmd_mock_pairing_addr)-1);
            s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr)-1] = '\0';

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
    else if (strcasecmp(ctx->params[0], "MOCK_PAIR") == 0) { if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0) { cmd_send_response("ERR", "DEBUG", "MOCK_PAIR_MISSING", NULL); } else {
#ifdef ESP_PLATFORM
            s_cmd_mock_enabled = true;
            strncpy(s_cmd_mock_pairing_addr, ctx->params[1], sizeof(s_cmd_mock_pairing_addr)-1);
            s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr)-1] = '\0';
            char pass[16] = "000000";
            size_t maclen = strlen(s_cmd_mock_pairing_addr);
            if (maclen >= 2) {
                const char* tail = s_cmd_mock_pairing_addr + (maclen > 5 ? maclen - 5 : 0);
                /* Copy at most sizeof(pass)-1 characters from tail to avoid overflow */
                strncpy(pass, tail, sizeof(pass)-1);
                pass[sizeof(pass)-1] = '\0';
            }
            strncpy(s_cmd_mock_passkey, pass, sizeof(s_cmd_mock_passkey)-1);
            s_cmd_mock_passkey[sizeof(s_cmd_mock_passkey)-1] = '\0';
            char data[64];
            snprintf(data, sizeof(data), "%s,%s", s_cmd_mock_pairing_addr, s_cmd_mock_passkey);
            cmd_send_event_pair("CONFIRM", data);
            cmd_send_response("OK", "DEBUG", "MOCK_PAIR_STARTED", ctx->params[1]);
#else
            cmd_send_response("OK", "DEBUG", "MOCK_PAIR_MOCKED", ctx->params[1]);
#endif
            } }
    else cmd_send_response("ERR", "DEBUG", "UNKNOWN_SUBCMD", ctx->params[0]);
    } break;

    case CMD_TYPE_SET_DEFAULT_PIN:
    if (ctx->param_count < 1) { cmd_send_response("ERR", "SET_DEFAULT_PIN", "MISSING_PARAM", NULL); break; }
#ifdef ESP_PLATFORM
    if (nvs_storage_set_default_pin(ctx->params[0]) == ESP_OK) cmd_send_response("OK", "SET_DEFAULT_PIN", "SUCCESS", ctx->params[0]); else cmd_send_response("ERR", "SET_DEFAULT_PIN", "FAILED", NULL);
#else
    nvs_storage_set_default_pin(ctx->params[0]); cmd_send_response("OK", "SET_DEFAULT_PIN", "MOCK_SUCCESS", ctx->params[0]);
#endif
    break;

    case CMD_TYPE_UNPAIR:
#ifdef ESP_PLATFORM
    if (ctx->param_count < 1) { cmd_send_response("ERR", "UNPAIR", "MISSING_PARAM", NULL); break; }
    if (nvs_storage_remove_paired_device(ctx->params[0]) == ESP_OK) cmd_send_response("OK", "UNPAIR", "REMOVED", ctx->params[0]); else cmd_send_response("ERR", "UNPAIR", "FAILED", NULL);
#else
    if (ctx->param_count < 1) { cmd_send_response("ERR", "UNPAIR", "MISSING_PARAM", NULL); break; }
    if (nvs_storage_remove_paired_device(ctx->params[0]) == ESP_OK) cmd_send_response("OK", "UNPAIR", "MOCK_REMOVED", ctx->params[0]); else cmd_send_response("ERR", "UNPAIR", "FAILED", NULL);
#endif
    break;

    case CMD_TYPE_UNPAIR_ALL:
#ifdef ESP_PLATFORM
    if (nvs_storage_clear_paired_devices() == ESP_OK) cmd_send_response("OK", "UNPAIR_ALL", "CLEARED", NULL); else cmd_send_response("ERR", "UNPAIR_ALL", "FAILED", NULL);
#else
    nvs_storage_clear_paired_devices(); cmd_send_response("OK", "UNPAIR_ALL", "MOCK_CLEARED", NULL);
#endif
    break;

    case CMD_TYPE_HELP: {
    cmd_send_response("INFO", "HELP", "ALL", "See detailed help in device docs");
    } break;

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
cmd_status_t cmd_send_response(const char* status, const char* command, const char* result, const char* data)
{
    char buf[512];
    const char* d = data ? data : "";
    int len = snprintf(buf, sizeof(buf), "%s|%s|%s|%s\r\n", status ? status : "", command ? command : "", result ? result : "", d);
#if defined(UNIT_TEST) || !defined(ESP_PLATFORM)
    /* For host/unit tests the mock UART maps UART_NUM_1 to a local buffer.
     * Use CMD_UART_NUM so tests can override which port is used. */
    uart_write_bytes(CMD_UART_NUM, buf, (size_t)len);
#else
    // On device, write to command UART (if available). If the UART driver
    // hasn't been installed for the console UART (common for UART0 used
    // as stdout), fall back to printf to avoid "uart driver error" logs.
    if (uart_is_driver_installed(CMD_UART_NUM)) {
        uart_write_bytes(CMD_UART_NUM, buf, (size_t)len);
    } else {
        // Console is available via stdio; print the structured response there.
        printf("%s", buf);
    }
#endif
    return CMD_SUCCESS;
}

// Convenience wrapper for pairing events
cmd_status_t cmd_send_event_pair(const char* subtype, const char* data)
{
    /* Increment sequence for ordering */
    s_event_sequence++;

    /* Build the same EVENT line we emit on the console so tests can
     * optionally capture it via a weakly-linked hook. The hook is
     * provided by the test adapter and is intentionally weak so
     * production builds are unaffected. */
    char buf[512];
    const char* d = data ? data : "";
    int n = snprintf(buf, sizeof(buf), "EVENT|PAIR|%s|seq=%lu,%s", subtype ? subtype : "", (unsigned long)s_event_sequence, d);
    /* Emit on serial/console as usual */
    char data_with_seq[256];
    snprintf(data_with_seq, sizeof(data_with_seq), "seq=%lu,%s", (unsigned long)s_event_sequence, d);
    cmd_send_response("EVENT", "PAIR", subtype ? subtype : "", data_with_seq);

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
    extern void test_push_event(const char* ev) __attribute__((weak));
#else
    extern void test_push_event(const char* ev);
#endif
    /* Guaranteed marker that's easy to grep in serial logs */
    printf("HOOK-DEBUG: about to call test_push_event (len=%d)\n", n);
    if (n > 0 && test_push_event) {
        printf("HOOK-DEBUG: test_push_event symbol present, forwarding event\n");
        test_push_event(buf);
    } else {
        printf("HOOK-DEBUG: test_push_event not present or n<=0\n");
    }

    return CMD_SUCCESS;
}

// Lightweight command parser used by host tests. Parses command name and up
// to CMD_MAX_PARAMS whitespace-separated parameters. Special-case
// CONNECT_NAME to keep the rest of the line as a single parameter.
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx)
{
    if (!cmd_str || !ctx) return CMD_ERROR_INVALID_PARAM;
    // Clear context
    memset(ctx, 0, sizeof(*ctx));

    // Copy and trim
    char buf[512];
    strncpy(buf, cmd_str, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    // Trim leading
    char* s = buf; while (*s && isspace((unsigned char)*s)) ++s;
    // Trim trailing
    char* end = s + strlen(s) - 1; while (end >= s && isspace((unsigned char)*end)) { *end = '\0'; --end; }
    if (*s == '\0') return CMD_ERROR_UNKNOWN;

    // Extract command token
    char* save = NULL;
    char* token = strtok_r(s, " \t", &save);
    if (!token) return CMD_ERROR_UNKNOWN;
    printf("PARSE-DIAG: token='%s'\n", token);
    // Map command to type (case-insensitive)
    if (strcasecmp(token, "SCAN") == 0) ctx->type = CMD_TYPE_SCAN;
    else if (strcasecmp(token, "CONNECT") == 0) ctx->type = CMD_TYPE_CONNECT;
    else if (strcasecmp(token, "CONNECT_NAME") == 0) ctx->type = CMD_TYPE_CONNECT_NAME;
    else if (strcasecmp(token, "DISCONNECT") == 0) ctx->type = CMD_TYPE_DISCONNECT;
    else if (strcasecmp(token, "PAIRED") == 0) ctx->type = CMD_TYPE_PAIRED;
    else if (strcasecmp(token, "SET_NAME") == 0) ctx->type = CMD_TYPE_SET_NAME;
    else if (strcasecmp(token, "START") == 0) ctx->type = CMD_TYPE_START;
    else if (strcasecmp(token, "STOP") == 0) ctx->type = CMD_TYPE_STOP;
    else if (strcasecmp(token, "VOLUME") == 0) ctx->type = CMD_TYPE_VOLUME;
    else if (strcasecmp(token, "MUTE") == 0) ctx->type = CMD_TYPE_MUTE;
    else if (strcasecmp(token, "UNMUTE") == 0) ctx->type = CMD_TYPE_UNMUTE;
    else if (strcasecmp(token, "STATUS") == 0) ctx->type = CMD_TYPE_STATUS;
    else if (strcasecmp(token, "VERSION") == 0) ctx->type = CMD_TYPE_VERSION;
    else if (strcasecmp(token, "RESET") == 0) ctx->type = CMD_TYPE_RESET;
    else if (strcasecmp(token, "DEBUG") == 0) ctx->type = CMD_TYPE_DEBUG;
    else if (strcasecmp(token, "SAMPLE_RATE") == 0) ctx->type = CMD_TYPE_SAMPLE_RATE;
    else if (strcasecmp(token, "I2S_CONFIG") == 0) ctx->type = CMD_TYPE_I2S_CONFIG;
    else if (strcasecmp(token, "BEEP") == 0) ctx->type = CMD_TYPE_BEEP;
    else if (strcasecmp(token, "PAIR") == 0) ctx->type = CMD_TYPE_PAIR;
    else if (strcasecmp(token, "CONFIRM_PIN") == 0) ctx->type = CMD_TYPE_CONFIRM_PIN;
    else if (strcasecmp(token, "ENTER_PIN") == 0) ctx->type = CMD_TYPE_ENTER_PIN;
    else if (strcasecmp(token, "SET_DEFAULT_PIN") == 0) ctx->type = CMD_TYPE_SET_DEFAULT_PIN;
    else if (strcasecmp(token, "UNPAIR") == 0) ctx->type = CMD_TYPE_UNPAIR;
    else if (strcasecmp(token, "UNPAIR_ALL") == 0) ctx->type = CMD_TYPE_UNPAIR_ALL;
    else if (strcasecmp(token, "HELP") == 0) ctx->type = CMD_TYPE_HELP;
    else ctx->type = CMD_TYPE_UNKNOWN;

    // If CONNECT_NAME, keep rest of the line as single param
    if (ctx->type == CMD_TYPE_CONNECT_NAME) {
        char* rest = save;
        // skip leading whitespace
        while (rest && *rest && isspace((unsigned char)*rest)) ++rest;
        if (rest && *rest) {
            strncpy(ctx->params[0], rest, CMD_MAX_PARAM_LEN-1);
            ctx->params[0][CMD_MAX_PARAM_LEN-1] = '\0';
            ctx->param_count = 1;
        } else {
            ctx->param_count = 0;
        }
        return CMD_SUCCESS;
    }

    // Otherwise split remaining tokens by whitespace
    int idx = 0;
    while ((token = strtok_r(NULL, " \t", &save)) != NULL && idx < CMD_MAX_PARAMS) {
        strncpy(ctx->params[idx], token, CMD_MAX_PARAM_LEN-1);
        ctx->params[idx][CMD_MAX_PARAM_LEN-1] = '\0';
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
    if (uart_is_driver_installed(CMD_UART_NUM)) {
        read_uart = CMD_UART_NUM;
    } else if (uart_is_driver_installed(UART_NUM_0)) {
#ifdef ESP_LOGW
        ESP_LOGW(TAG, "cmd_process: command UART %d not installed; falling back to console UART 0", CMD_UART_NUM);
#endif
        read_uart = UART_NUM_0;
    } else {
        // No available UART driver — nothing to read.
        return CMD_SUCCESS;
    }
#endif

    int r = uart_read_bytes(read_uart, read_buf, sizeof(read_buf)-1, 0);
    if (r <= 0) {
        return CMD_SUCCESS;
    }

    // Append to persistent buffer (clip to available space)
    size_t to_copy = (size_t)r;
    if (line_len + to_copy >= sizeof(line_buf)) {
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
    char* start = line_buf;
    while (true) {
        char* nl = (char*)memchr(start, '\n', (size_t)(line_buf + line_len - start));
        char* cr = (char*)memchr(start, '\r', (size_t)(line_buf + line_len - start));
        char* term = nl ? nl : cr;
        if (!term) break;

        // Null-terminate the line and parse/execute
        *term = '\0';
        // Trim trailing spaces
        char* end = term - 1; while (end >= start && isspace((unsigned char)*end)) { *end = '\0'; --end; }

        cmd_context_t ctx;
        if (cmd_parse(start, &ctx) == CMD_SUCCESS) {
            cmd_execute(&ctx);
        }

        // Move to the next character after the terminator
        start = term + 1;
        while (start < line_buf + line_len && (*start == '\n' || *start == '\r')) ++start;
    }

    // Move any remaining bytes to the start of the buffer
    size_t remaining = (size_t)(line_buf + line_len - start);
    if (remaining > 0) memmove(line_buf, start, remaining);
    line_len = remaining;
    line_buf[line_len] = '\0';

    return CMD_SUCCESS;
}

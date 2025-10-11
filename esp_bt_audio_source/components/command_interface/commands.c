#include "command_interface.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
// Audio processor APIs
#ifdef ESP_PLATFORM
#include "audio_processor.h"
#else
// In host tests these symbols may be mocked; include a local header if available
#include "audio_processor.h"
#endif

// Some static analysis/build setups may not pick up the project's include
// directories. As a fallback ensure the concrete header is available via a
// relative include so the audio_status_t type is defined for this TU.
#ifndef _AUDIO_PROCESSOR_H_
#include "../../main/include/audio_processor.h"
#endif

// Ensure audio status type is visible when this translation unit references
// audio_status_t in cmd_execute(). The header above defines audio_status_t for
// both ESP and host test builds.

// Define this for ESP32 builds
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "driver/uart.h"
#define TAG "CMD_IF"
#define CMD_UART_NUM UART_NUM_1
#define CMD_BUF_SIZE 256
#include "nvs_storage.h"
#include "esp_gap_bt_api.h"

// Avoid including bt_manager.h / bt_registry.h / bt_mock_devices.h here because
// those headers define overlapping types (bt_device_t, bt_interface_t, etc.)
// which conflict with types from other components when combined in this
// translation unit. Instead, forward-declare the limited set of symbols used by
// the command interface to keep compile units independent and avoid duplicate
// type definitions.

// Basic return code used by bt_manager APIs (treat 0 as success)
#ifndef BT_SUCCESS
#define BT_SUCCESS 0
#endif

// Forward declarations for small subset of BT helper APIs used by commands.c
extern int bt_set_name(const char* name);
extern int bt_pair(const char* mac);
extern int bt_unpair(const char* mac);
extern int bt_start_scan(void);
extern int bt_connect(const char* mac);
extern int bt_disconnect(void);
extern int bt_start_audio(void);
extern int bt_stop_audio(void);

// Internal lightweight mock-mode state used by the command interface to
// simulate pairing events for host E2E tests without pulling in the
// full bt_mock component (keeps binary size smaller).
static bool s_cmd_mock_enabled = false;
static char s_cmd_mock_pairing_addr[18] = {0};
static char s_cmd_mock_passkey[16] = {0};

// If the BT device type macros are not available, define the audio type used by
// the debug mock API as a fallback value.
#ifndef BT_DEVICE_TYPE_AUDIO
#define BT_DEVICE_TYPE_AUDIO 1
#endif
#else
// Include mock UART header for host-based testing
#include "mock_uart.h"
#include "nvs_storage.h"
// Include mock Bluetooth types for host tests (esp_bt_pin_code_t, etc.)
#include "esp_bt.h"
// Internal lightweight mock-mode state used by the command interface to
// simulate pairing events for host E2E tests without pulling in the
// full bt_mock component (keeps binary size smaller).
static bool s_cmd_mock_enabled = false;
static char s_cmd_mock_pairing_addr[18] = {0};
static char s_cmd_mock_passkey[16] = {0};
#endif

// Private data
static struct {
    bool initialized;
    char cmd_buffer[256];
    int cmd_buffer_idx;
} cmd_ctx = {
    .initialized = false,
    .cmd_buffer_idx = 0
};

// String helpers
static void trim_string(char* str) {
    if (!str) return;
    
    // Trim leading whitespace
    char* start = str;
    while (isspace((unsigned char)*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

// Initialize command interface
cmd_status_t cmd_init(void) {
    if (cmd_ctx.initialized) {
        return CMD_SUCCESS; // Already initialized
    }
    
    memset(cmd_ctx.cmd_buffer, 0, sizeof(cmd_ctx.cmd_buffer));
    cmd_ctx.cmd_buffer_idx = 0;

#ifdef ESP_PLATFORM
    // Configure UART1 for command interface
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
    };
    
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(CMD_UART_NUM, &uart_config));
    
    // Set UART pins (using UART1)
    ESP_ERROR_CHECK(uart_set_pin(CMD_UART_NUM, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    // Install UART driver (rx and tx buffers allocated)
    ESP_ERROR_CHECK(uart_driver_install(CMD_UART_NUM, CMD_BUF_SIZE * 2, CMD_BUF_SIZE * 2, 0, NULL, 0));
    
    // Also ensure the USB console UART (UART0) has a driver installed so
    // the command interface can read commands sent to /dev/ttyUSB0 (the
    // USB CDC serial). This avoids uart_read_bytes() returning errors when
    // attempting to read from UART0. Installing a small buffer is sufficient
    // for command-line usage.
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, CMD_BUF_SIZE, CMD_BUF_SIZE, 0, NULL, 0));

    ESP_LOGI(TAG, "Command interface initialized on UART1 (TX:17, RX:16)");
#endif
    
    cmd_ctx.initialized = true;
    return CMD_SUCCESS;
}

// Deinitialize command interface
cmd_status_t cmd_deinit(void) {
    if (!cmd_ctx.initialized) {
        return CMD_SUCCESS; // Not initialized
    }
    
#ifdef ESP_PLATFORM
    // Delete the UART driver for the command UART and the USB console
    uart_driver_delete(CMD_UART_NUM);
    uart_driver_delete(UART_NUM_0);
#endif
    
    cmd_ctx.initialized = false;
    return CMD_SUCCESS;
}

// Parse a command string
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx) {
    if (!cmd_ctx.initialized) {
        return CMD_ERROR_NOT_INITIALIZED;
    }
    
    if (cmd_str == NULL || ctx == NULL) {
        return CMD_ERROR_INVALID_PARAM;
    }
    
    // Clear context
    memset(ctx, 0, sizeof(cmd_context_t));
    
    // Make a copy for tokenization
    char cmd_copy[256];
    strncpy(cmd_copy, cmd_str, sizeof(cmd_copy) - 1);
    trim_string(cmd_copy);

    // Accept optional envelope prefix 'CMD|' used by host scripts (e.g. "CMD|DEBUG ...")
    if (strncmp(cmd_copy, "CMD|", 4) == 0) {
        size_t l = strlen(cmd_copy + 4) + 1;
        memmove(cmd_copy, cmd_copy + 4, l);
    }
    // Accept '|' as an alternative separator (host uses pipes in some messages)
    for (char* p = cmd_copy; *p; ++p) {
        if (*p == '|') *p = ' ';
    }
    
    // Extract command name
    char* token = strtok(cmd_copy, " ");
    if (token == NULL) {
        return CMD_ERROR_INVALID_PARAM;
    }
    
    // Determine command type
    if (strcmp(token, "SCAN") == 0) {
        ctx->type = CMD_TYPE_SCAN;
    } else if (strcmp(token, "CONNECT") == 0) {
        ctx->type = CMD_TYPE_CONNECT;
    } else if (strcmp(token, "CONNECT_NAME") == 0) {
        ctx->type = CMD_TYPE_CONNECT_NAME;
    } else if (strcmp(token, "DISCONNECT") == 0) {
        ctx->type = CMD_TYPE_DISCONNECT;
    } else if (strcmp(token, "PAIRED") == 0) {
        ctx->type = CMD_TYPE_PAIRED;
    } else if (strcmp(token, "SET_NAME") == 0) {
        ctx->type = CMD_TYPE_SET_NAME;
    } else if (strcmp(token, "START") == 0) {
        ctx->type = CMD_TYPE_START;
    } else if (strcmp(token, "STOP") == 0) {
        ctx->type = CMD_TYPE_STOP;
    } else if (strcmp(token, "VOLUME") == 0) {
        ctx->type = CMD_TYPE_VOLUME;
    } else if (strcmp(token, "MUTE") == 0) {
        ctx->type = CMD_TYPE_MUTE;
    } else if (strcmp(token, "UNMUTE") == 0) {
        ctx->type = CMD_TYPE_UNMUTE;
    } else if (strcmp(token, "STATUS") == 0) {
        ctx->type = CMD_TYPE_STATUS;
    } else if (strcmp(token, "VERSION") == 0) {
        ctx->type = CMD_TYPE_VERSION;
    } else if (strcmp(token, "RESET") == 0) {
        ctx->type = CMD_TYPE_RESET;
    } else if (strcmp(token, "DEBUG") == 0) {
        ctx->type = CMD_TYPE_DEBUG;
    } else if (strcmp(token, "SAMPLE_RATE") == 0) {
        ctx->type = CMD_TYPE_SAMPLE_RATE;
    } else if (strcmp(token, "I2S_CONFIG") == 0) {
        ctx->type = CMD_TYPE_I2S_CONFIG;
    } else if (strcmp(token, "PAIR") == 0) {
        ctx->type = CMD_TYPE_PAIR;
    } else if (strcmp(token, "CONFIRM_PIN") == 0) {
        ctx->type = CMD_TYPE_CONFIRM_PIN;
    } else if (strcmp(token, "ENTER_PIN") == 0) {
        ctx->type = CMD_TYPE_ENTER_PIN;
    } else if (strcmp(token, "SET_DEFAULT_PIN") == 0) {
        ctx->type = CMD_TYPE_SET_DEFAULT_PIN;
    } else if (strcmp(token, "UNPAIR") == 0) {
        ctx->type = CMD_TYPE_UNPAIR;
    } else if (strcmp(token, "UNPAIR_ALL") == 0) {
        ctx->type = CMD_TYPE_UNPAIR_ALL;
    } else if (strcmp(token, "HELP") == 0) {
        ctx->type = CMD_TYPE_HELP;
    } else {
        ctx->type = CMD_TYPE_UNKNOWN;
        return CMD_ERROR_UNKNOWN;
    }
    
    // Extract parameters
    token = strtok(NULL, " ");
    while (token != NULL && ctx->param_count < CMD_MAX_PARAMS) {
        strncpy(ctx->params[ctx->param_count], token, CMD_MAX_PARAM_LEN - 1);
        ctx->param_count++;
        token = strtok(NULL, " ");
    }
    
    return CMD_SUCCESS;
}

// Send a response
cmd_status_t cmd_send_response(const char* status, const char* command, 
                             const char* result, const char* data) {
    if (!cmd_ctx.initialized) {
        return CMD_ERROR_NOT_INITIALIZED;
    }
    
    char response[512];
    int len;
    
    if (data != NULL) {
        len = snprintf(response, sizeof(response), "%s|%s|%s|%s\r\n", 
                 status, command, result, data);
    } else {
        len = snprintf(response, sizeof(response), "%s|%s|%s\r\n", 
                 status, command, result);
    }
    
#ifdef ESP_PLATFORM
    // Write to command UART (UART1)
    uart_write_bytes(CMD_UART_NUM, response, len);
    // Also mirror responses to the USB serial (UART0) so host-side
    // tooling which listens on the console sees events even if the
    // command UART isn't connected to the host directly.
    uart_write_bytes(UART_NUM_0, response, len);
#else
    // For unit/host tests without ESP-IDF UART0 availability we still use
    // the mocked UART write helper (UART_NUM_1).
    uart_write_bytes(UART_NUM_1, response, len);
#endif
    
    return CMD_SUCCESS;
}

// Helper to emit pairing events with standard envelope: EVENT|PAIR|<SUBTYPE>|<DATA>\r\n
cmd_status_t cmd_send_event_pair(const char* subtype, const char* data) {
    if (!cmd_ctx.initialized) {
        return CMD_ERROR_NOT_INITIALIZED;
    }
    // Reuse cmd_send_response formatting
    return cmd_send_response("EVENT", "PAIR", subtype, data);
}

// Process incoming data from UART and handle complete commands
cmd_status_t cmd_process(void) {
    if (!cmd_ctx.initialized) {
        return CMD_ERROR_NOT_INITIALIZED;
    }
    
    // Simple mock implementation for testing
    // Read a command from UART and process it
    uint8_t buf[128];
    int len = 0;
#ifdef ESP_PLATFORM
    /* Prefer reading from the dedicated command UART (UART1). If there's
     * no data there, also check the USB console (UART0) so host scripts
     * that talk to /dev/ttyUSB0 (monitor) can send commands during tests.
     */
    len = uart_read_bytes(CMD_UART_NUM, buf, sizeof(buf), 20 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "cmd_process: read %d bytes from CMD_UART (UART1)", len);
    if (len <= 0) {
        len = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), 20 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "cmd_process: read %d bytes from USB console (UART0)", len);
    }
#else
    /* Reuse the previously-declared 'len' variable instead of re-declaring it
     * (avoids compiler shadowing or redeclaration warnings when building for
     * host tests where the earlier 'int len = 0;' is present). */
    len = uart_read_bytes(UART_NUM_1, buf, sizeof(buf), 0);
#endif
    
    if (len > 0) {
        // Null terminate the command
        if (len < sizeof(buf)) {
            buf[len] = '\0';
        } else {
            buf[sizeof(buf) - 1] = '\0';
        }
        
        // Echo raw input for debugging so host-side tests can see that the
        // device actually received the line. This helps distinguish whether
        // the TX from the host reached the CMD UART or not.
        // Send as INFO|RAW|RECV|<data>\r\n
        {
            // Truncate to reasonable size
            char raw[128];
            int rlen = len < (int)sizeof(raw)-1 ? len : (int)sizeof(raw)-1;
            memcpy(raw, buf, rlen);
            raw[rlen] = '\0';
            // Trim trailing newlines for nicer output
            for (int i = rlen-1; i >= 0; --i) {
                if (raw[i] == '\r' || raw[i] == '\n') raw[i] = '\0'; else break;
            }
            cmd_send_response("INFO", "RAW", "RECV", raw);
        }
        // Quick-path: handle common shorthand envelopes used by host tests
        // like "CMD|CONFIRM_PIN|1" or "CMD|ENTER_PIN|1234". Handling
        // them here avoids depending on tokenization differences and
        // ensures our lightweight mock-mode responds deterministically.
        {
            char* line = (char*)buf;
            trim_string(line);
            // Accept optional CMD| prefix
            char* p = line;
            if (strncmp(p, "CMD|", 4) == 0) p += 4;
            // CONFIRM_PIN|1 shorthand
            if (strncmp(p, "CONFIRM_PIN|", 12) == 0) {
                char* val = p + 12;
                trim_string(val);
                if ((strcmp(val, "1") == 0 || strcmp(val, "0") == 0)) {
                    bool accept = (strcmp(val, "1") == 0);
                    if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0) {
#ifdef ESP_PLATFORM
                        esp_bd_addr_t bda;
                        if (sscanf(s_cmd_mock_pairing_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                                   &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
                            esp_bt_gap_ssp_confirm_reply(bda, accept);
                        }
#else
                        // Host tests: call mock gap reply with addr parsed
                        uint8_t bda[6] = {0};
                        sscanf(s_cmd_mock_pairing_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]);
                        esp_bt_gap_ssp_confirm_reply(bda, accept);
#endif
                        cmd_send_response("OK", "CONFIRM_PIN", accept ? "MOCK_ACCEPTED" : "MOCK_REJECTED", s_cmd_mock_pairing_addr);
                        cmd_send_event_pair(accept ? "SUCCESS" : "FAILED", s_cmd_mock_pairing_addr);
                        s_cmd_mock_enabled = false;
                        s_cmd_mock_pairing_addr[0] = '\0';
                        s_cmd_mock_passkey[0] = '\0';
                        return CMD_SUCCESS;
                    } else {
                        cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC_OR_NO_MOCK", NULL);
                        return CMD_SUCCESS;
                    }
                }
            }
            // ENTER_PIN|<PIN> shorthand - accept and apply to current mock
            if (strncmp(p, "ENTER_PIN|", 10) == 0) {
                char* val = p + 10;
                trim_string(val);
                if (strlen(val) > 0) {
                    if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0) {
#ifdef ESP_PLATFORM
                        esp_bd_addr_t bda;
                        if (sscanf(s_cmd_mock_pairing_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                                   &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
                            esp_bt_pin_code_t pin_code = {0};
                            size_t pin_len = strlen(val);
                            if (pin_len > ESP_BT_PIN_CODE_LEN) pin_len = ESP_BT_PIN_CODE_LEN;
                            memcpy(pin_code, val, pin_len);
                            esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code);
                        }
#else
                        uint8_t bda[6] = {0};
                        sscanf(s_cmd_mock_pairing_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]);
                        uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
                        size_t pin_len = strlen(val);
                        if (pin_len > ESP_BT_PIN_CODE_LEN) pin_len = ESP_BT_PIN_CODE_LEN;
                        memcpy(pin_code, val, pin_len);
                        esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code);
#endif
                        cmd_send_response("OK", "ENTER_PIN", "MOCK_SENT", s_cmd_mock_pairing_addr);
                        cmd_send_event_pair("SUCCESS", s_cmd_mock_pairing_addr);
                        s_cmd_mock_enabled = false;
                        s_cmd_mock_pairing_addr[0] = '\0';
                        s_cmd_mock_passkey[0] = '\0';
                        return CMD_SUCCESS;
                    } else {
                        cmd_send_response("ERR", "ENTER_PIN", "INVALID_MAC_OR_NO_MOCK", NULL);
                        return CMD_SUCCESS;
                    }
                }
            }
        }
        // Process command (simple test implementation)
        cmd_context_t ctx;
        if (cmd_parse((const char*)buf, &ctx) == CMD_SUCCESS) {
            // Minimal inline handlers for a small set of commands used by
            // host E2E tests. We avoid calling cmd_execute() to prevent
            // pulling many BT symbols into this object (keeps binary size down).
            if (ctx.type == CMD_TYPE_DEBUG) {
                if (ctx.param_count < 1) {
                    cmd_send_response("ERR", "DEBUG", "MISSING_PARAM", NULL);
                } else if (strcasecmp(ctx.params[0], "MOCK_ON") == 0) {
                    // Activate lightweight internal mock mode for command-interface tests
                    s_cmd_mock_enabled = true;
                    cmd_send_response("OK", "DEBUG", "MOCK_ON", NULL);
                } else if (strcasecmp(ctx.params[0], "MOCK_ADD") == 0) {
                    if (ctx.param_count < 2 || strlen(ctx.params[1]) == 0) {
                        cmd_send_response("ERR", "DEBUG", "MOCK_ADD_MISSING", NULL);
                    } else {
                        char payload[128];
                        strncpy(payload, ctx.params[1], sizeof(payload)-1);
                        payload[sizeof(payload)-1] = '\0';
                        char* comma = strchr(payload, ',');
                        if (!comma) {
                            cmd_send_response("ERR", "DEBUG", "MOCK_ADD_INVALID", ctx.params[1]);
                        } else {
                            *comma = '\0';
                            const char* mac = payload;
                            // Lightweight on-device mock: record known mock device for
                            // debug/testing without pulling in the full bt_mock component.
                            // We simply acknowledge the addition and store the last
                            // address in our local mock state for deterministic pairing.
                            strncpy(s_cmd_mock_pairing_addr, mac, sizeof(s_cmd_mock_pairing_addr)-1);
                            s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr)-1] = '\0';
                            cmd_send_response("OK", "DEBUG", "MOCK_ADD", ctx.params[1]);
                        }
                    }
                } else if (strcasecmp(ctx.params[0], "MOCK_PAIR") == 0) {
                    if (ctx.param_count < 2 || strlen(ctx.params[1]) == 0) {
                        cmd_send_response("ERR", "DEBUG", "MOCK_PAIR_MISSING", NULL);
                    } else {
#ifdef ESP_PLATFORM
                        // Enable our lightweight mock-mode and begin a deterministic
                        // mock pairing sequence: generate a predictable passkey and
                        // emit a CONFIRM event to the host. This avoids linking the
                        // full bt_mock component into the command interface.
                        s_cmd_mock_enabled = true;
                        strncpy(s_cmd_mock_pairing_addr, ctx.params[1], sizeof(s_cmd_mock_pairing_addr)-1);
                        s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr)-1] = '\0';
                        // Deterministic passkey (for testing): use last 6 hex digits
                        // of the MAC address or a fixed value if parsing fails.
                        {
                            const char* mac = s_cmd_mock_pairing_addr;
                            char pass[16] = "000000";
                            size_t maclen = strlen(mac);
                            if (maclen >= 2) {
                                // copy last two bytes (4 hex chars) + pad
                                const char* tail = mac + (maclen > 5 ? maclen - 5 : 0);
                                snprintf(pass, sizeof(pass), "%s", tail);
                            }
                            strncpy(s_cmd_mock_passkey, pass, sizeof(s_cmd_mock_passkey)-1);
                            s_cmd_mock_passkey[sizeof(s_cmd_mock_passkey)-1] = '\0';
                        }
                        // Emit CONFIRM event: "<MAC>,<PASSKEY>"
                        char data[64];
                        snprintf(data, sizeof(data), "%s,%s", s_cmd_mock_pairing_addr, s_cmd_mock_passkey);
                        cmd_send_event_pair("CONFIRM", data);
                        cmd_send_response("OK", "DEBUG", "MOCK_PAIR_STARTED", ctx.params[1]);
#else
                        cmd_send_response("OK", "DEBUG", "MOCK_PAIR_MOCKED", ctx.params[1]);
#endif
                    }
                } else {
                    cmd_send_response("ERR", "DEBUG", "UNKNOWN_SUBCMD", ctx.params[0]);
                }
            } else if (ctx.type == CMD_TYPE_CONFIRM_PIN) {
                // Expect params: <MAC> [ACCEPT|REJECT]
                // Support shorthand from host tests: CMD|CONFIRM_PIN|1 (accept)
                if (ctx.param_count < 1) {
                    cmd_send_response("ERR", "CONFIRM_PIN", "MISSING_PARAM", NULL);
                } else {
                    const char* mac = NULL;
                    bool accept = true;
                    // If only a single param and it's 1/0, use mock pairing addr
                    if (ctx.param_count == 1 && (strcmp(ctx.params[0], "1") == 0 || strcmp(ctx.params[0], "0") == 0)) {
                        accept = (strcmp(ctx.params[0], "1") == 0);
                        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0) {
                            mac = s_cmd_mock_pairing_addr;
                        }
                    } else {
                        mac = ctx.params[0];
                        if (ctx.param_count >= 2) {
                            const char* p = ctx.params[1];
                            if (p == NULL) accept = true;
                            else if (strcasecmp(p, "REJECT") == 0 || strcasecmp(p, "NO") == 0 || strcmp(p, "0") == 0) accept = false;
                            else accept = true;
                        }
                    }
                    if (mac == NULL) {
                        cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC_OR_NO_MOCK", NULL);
                    } else {
#ifdef ESP_PLATFORM
                        esp_bd_addr_t bda;
                        if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                                   &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
                            cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC", NULL);
                        } else if (esp_bt_gap_ssp_confirm_reply(bda, accept) == ESP_OK) {
                            cmd_send_response("OK", "CONFIRM_PIN", accept ? "ACCEPTED" : "REJECTED", mac);
                        } else {
                            cmd_send_response("ERR", "CONFIRM_PIN", "FAILED", NULL);
                        }
#else
                        uint8_t bda[6] = {0};
                        if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                                   &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
                            esp_bt_gap_ssp_confirm_reply(bda, accept);
                            cmd_send_response("OK", "CONFIRM_PIN", accept ? "MOCK_ACCEPTED" : "MOCK_REJECTED", mac);
                        } else {
                            cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC", NULL);
                        }
#endif
                        // If this was for our internal mock pairing, emit SUCCESS/FAILED
                        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0 && mac != NULL && strcmp(mac, s_cmd_mock_pairing_addr) == 0) {
                            cmd_send_event_pair(accept ? "SUCCESS" : "FAILED", s_cmd_mock_pairing_addr);
                            s_cmd_mock_enabled = false;
                            s_cmd_mock_pairing_addr[0] = '\0';
                            s_cmd_mock_passkey[0] = '\0';
                        }
                    }
                }
            } else if (ctx.type == CMD_TYPE_ENTER_PIN) {
                // Expect params: <MAC> <PIN>
                if (ctx.param_count < 1) {
                    cmd_send_response("ERR", "ENTER_PIN", "MISSING_PARAM", NULL);
                } else {
                    const char* mac = ctx.params[0];
                    const char* pin = NULL;
                    char default_pin[ESP_BT_PIN_CODE_LEN + 1] = {0};
                    if (ctx.param_count >= 2 && strlen(ctx.params[1]) > 0) {
                        pin = ctx.params[1];
                    } else {
                        if (nvs_storage_get_default_pin(default_pin, sizeof(default_pin)) == ESP_OK && strlen(default_pin) > 0) {
                            pin = default_pin;
                        }
                    }
                    if (pin == NULL) {
                        cmd_send_response("ERR", "ENTER_PIN", "MISSING_PIN", NULL);
                    } else {
#ifdef ESP_PLATFORM
                        esp_bd_addr_t bda;
                        if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                                   &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
                            cmd_send_response("ERR", "ENTER_PIN", "INVALID_MAC", NULL);
                        } else {
                            esp_bt_pin_code_t pin_code = {0};
                            size_t pin_len = strlen(pin);
                            if (pin_len > ESP_BT_PIN_CODE_LEN) pin_len = ESP_BT_PIN_CODE_LEN;
                            memcpy(pin_code, pin, pin_len);
                            if (esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code) == ESP_OK) {
                                cmd_send_response("OK", "ENTER_PIN", "SENT", mac);
                            } else {
                                cmd_send_response("ERR", "ENTER_PIN", "FAILED", NULL);
                            }
                        }
#else
                        uint8_t bda[6] = {0};
                        if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                                   &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
                            uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
                            size_t pin_len = strlen(pin);
                            if (pin_len > ESP_BT_PIN_CODE_LEN) pin_len = ESP_BT_PIN_CODE_LEN;
                            memcpy(pin_code, pin, pin_len);
                            esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code);
                            cmd_send_response("OK", "ENTER_PIN", "MOCK_SENT", mac);
                            if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0 && strcmp(mac, s_cmd_mock_pairing_addr) == 0) {
                                cmd_send_event_pair("SUCCESS", s_cmd_mock_pairing_addr);
                                s_cmd_mock_enabled = false;
                                s_cmd_mock_pairing_addr[0] = '\0';
                                s_cmd_mock_passkey[0] = '\0';
                            }
                        } else {
                            cmd_send_response("ERR", "ENTER_PIN", "INVALID_MAC", NULL);
                        }
#endif
                    }
                }
            }
        }
    }
    
    return CMD_SUCCESS;
}

// Simple implementation for cmd_execute - will be expanded in production code
cmd_status_t cmd_execute(const cmd_context_t* ctx) {
    if (!cmd_ctx.initialized || ctx == NULL) {
        return CMD_ERROR_NOT_INITIALIZED;
    }

    // Just a placeholder for now
    // Handlers for each command type will be implemented here
    // Example:
    switch (ctx->type) {
        case CMD_TYPE_STATUS: {
            audio_status_t status;
            if (audio_processor_get_status(&status) == ESP_OK) {
                char data[128];
                snprintf(data, sizeof(data), "init=%d,run=%d,vol=%d",
                         status.initialized, status.running, status.volume);
                cmd_send_response("OK", "STATUS", "CURRENT", data);
            } else {
                cmd_send_response("ERR", "STATUS", "NO_AUDIO", NULL);
            }
        } break;

        case CMD_TYPE_VERSION:
            cmd_send_response("OK", "VERSION", "1.0.0", NULL);
            break;

        case CMD_TYPE_SCAN: {
#ifdef ESP_PLATFORM
            if (bt_start_scan() == BT_SUCCESS) {
                cmd_send_response("OK", "SCAN", "STARTED", NULL);
            } else {
                cmd_send_response("ERR", "SCAN", "FAILED", NULL);
            }
#else
            cmd_send_response("OK", "SCAN", "MOCK_STARTED", NULL);
#endif
        } break;

        case CMD_TYPE_CONNECT: {
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "CONNECT", "MISSING_PARAM", NULL);
                break;
            }
#ifdef ESP_PLATFORM
            if (bt_connect(ctx->params[0]) == BT_SUCCESS) {
                cmd_send_response("OK", "CONNECT", "INITIATED", NULL);
            } else {
                cmd_send_response("ERR", "CONNECT", "FAILED", NULL);
            }
#else
            // Host test: simulate connect
            cmd_send_response("OK", "CONNECT", "MOCK_CONNECTED", ctx->params[0]);
#endif
        } break;

        case CMD_TYPE_DISCONNECT: {
#ifdef ESP_PLATFORM
            if (bt_disconnect() == BT_SUCCESS) {
                cmd_send_response("OK", "DISCONNECT", "INITIATED", NULL);
            } else {
                cmd_send_response("ERR", "DISCONNECT", "FAILED", NULL);
            }
#else
            cmd_send_response("OK", "DISCONNECT", "MOCK_DISCONNECTED", NULL);
#endif
        } break;

        case CMD_TYPE_START: {
#ifdef ESP_PLATFORM
            if (bt_start_audio() == BT_SUCCESS) {
                cmd_send_response("OK", "START", "AUDIO_STARTED", NULL);
            } else {
                cmd_send_response("ERR", "START", "FAILED", NULL);
            }
#else
            cmd_send_response("OK", "START", "MOCK_STARTED", NULL);
#endif
        } break;

        case CMD_TYPE_STOP: {
#ifdef ESP_PLATFORM
            if (bt_stop_audio() == BT_SUCCESS) {
                cmd_send_response("OK", "STOP", "AUDIO_STOPPED", NULL);
            } else {
                cmd_send_response("ERR", "STOP", "FAILED", NULL);
            }
#else
            cmd_send_response("OK", "STOP", "MOCK_STOPPED", NULL);
#endif
        } break;

        case CMD_TYPE_VOLUME: {
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "VOLUME", "MISSING_PARAM", NULL);
                break;
            }
            int vol = atoi(ctx->params[0]);
            if (vol < 0 || vol > 100) {
                cmd_send_response("ERR", "VOLUME", "OUT_OF_RANGE", NULL);
                break;
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
        } break;

        case CMD_TYPE_I2S_CONFIG: {
            // Expect params: bclk,wclk,din[,dout]
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "I2S_CONFIG", "MISSING_PARAM", NULL);
                break;
            }
            // Parse comma-separated pins
            int pins[4] = { -1, -1, -1, -1 };
            char param_copy[128];
            strncpy(param_copy, ctx->params[0], sizeof(param_copy)-1);
            param_copy[sizeof(param_copy)-1] = '\0';
            char* tok = strtok(param_copy, ",");
            int idx = 0;
            while (tok != NULL && idx < 4) {
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
        } break;

        case CMD_TYPE_PAIR: {
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "PAIR", "MISSING_PARAM", NULL);
                break;
            }
#ifdef ESP_PLATFORM
            if (bt_pair(ctx->params[0]) == BT_SUCCESS) {
                cmd_send_response("OK", "PAIR", "INITIATED", NULL);
            } else {
                cmd_send_response("ERR", "PAIR", "FAILED", NULL);
            }
#else
            cmd_send_response("OK", "PAIR", "MOCK_INITIATED", ctx->params[0]);
#endif
        } break;

        case CMD_TYPE_CONFIRM_PIN: {
            // Expect params: <MAC> [ACCEPT|REJECT]
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "CONFIRM_PIN", "MISSING_PARAM", NULL);
                break;
            }
            const char* mac = ctx->params[0];
            bool accept = true; // default to accept if not specified
            if (ctx->param_count >= 2) {
                const char* p = ctx->params[1];
                if (p == NULL) accept = true;
                else if (strcasecmp(p, "REJECT") == 0 || strcasecmp(p, "NO") == 0 || strcmp(p, "0") == 0) accept = false;
                else accept = true;
            }
#ifdef ESP_PLATFORM
            esp_bd_addr_t bda;
            if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
                       &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
                cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC", NULL);
                break;
            }
            if (esp_bt_gap_ssp_confirm_reply(bda, accept) == ESP_OK) {
                cmd_send_response("OK", "CONFIRM_PIN", accept ? "ACCEPTED" : "REJECTED", mac);
            } else {
                cmd_send_response("ERR", "CONFIRM_PIN", "FAILED", NULL);
            }
#else
            // Host test: call mock GAP API so tests can observe parameters
            {
                uint8_t bda[6] = {0};
                    if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
                        esp_bt_gap_ssp_confirm_reply(bda, accept);
                        cmd_send_response("OK", "CONFIRM_PIN", accept ? "MOCK_ACCEPTED" : "MOCK_REJECTED", mac);
                        // If internal lightweight mock-mode is active and this
                        // confirmation targets the pending mock pairing, emit
                        // a SUCCESS event so host tests complete deterministically.
                        if (s_cmd_mock_enabled && strlen(s_cmd_mock_pairing_addr) > 0 && strcmp(mac, s_cmd_mock_pairing_addr) == 0) {
                            cmd_send_event_pair(accept ? "SUCCESS" : "FAILED", s_cmd_mock_pairing_addr);
                            s_cmd_mock_enabled = false;
                            s_cmd_mock_pairing_addr[0] = '\0';
                            s_cmd_mock_passkey[0] = '\0';
                        }
                    } else {
                        cmd_send_response("ERR", "CONFIRM_PIN", "INVALID_MAC", NULL);
                    }
            }
#endif
        } break;

        case CMD_TYPE_ENTER_PIN: {
            // Expect params: <MAC> <PIN>
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "ENTER_PIN", "MISSING_PARAM", NULL);
                break;
            }
            const char* mac = ctx->params[0];
            const char* pin = NULL;
            char default_pin[ESP_BT_PIN_CODE_LEN + 1] = {0};
            if (ctx->param_count >= 2 && strlen(ctx->params[1]) > 0) {
                pin = ctx->params[1];
            } else {
                // Try to use default PIN from NVS
                if (nvs_storage_get_default_pin(default_pin, sizeof(default_pin)) == ESP_OK && strlen(default_pin) > 0) {
                    pin = default_pin;
                }
            }
            if (pin == NULL) {
                cmd_send_response("ERR", "ENTER_PIN", "MISSING_PIN", NULL);
                break;
            }
#ifdef ESP_PLATFORM
            esp_bd_addr_t bda;
            if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
                       &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
                cmd_send_response("ERR", "ENTER_PIN", "INVALID_MAC", NULL);
                break;
            }
            esp_bt_pin_code_t pin_code = {0};
            size_t pin_len = strlen(pin);
            if (pin_len > ESP_BT_PIN_CODE_LEN) pin_len = ESP_BT_PIN_CODE_LEN;
            memcpy(pin_code, pin, pin_len);
            if (esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code) == ESP_OK) {
                cmd_send_response("OK", "ENTER_PIN", "SENT", mac);
            } else {
                cmd_send_response("ERR", "ENTER_PIN", "FAILED", NULL);
            }
#else
            // Host test: call mock GAP API so tests can observe parameters
            {
                uint8_t bda[6] = {0};
                if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
                           &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) == 6) {
                    uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
                    size_t pin_len = strlen(pin);
                    if (pin_len > ESP_BT_PIN_CODE_LEN) pin_len = ESP_BT_PIN_CODE_LEN;
                    memcpy(pin_code, pin, pin_len);
                    esp_bt_gap_pin_reply(bda, true, (uint8_t)pin_len, pin_code);
                    cmd_send_response("OK", "ENTER_PIN", "MOCK_SENT", mac);
                } else {
                    cmd_send_response("ERR", "ENTER_PIN", "INVALID_MAC", NULL);
                }
            }
#endif
        } break;

        case CMD_TYPE_SET_NAME: {
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "SET_NAME", "MISSING_PARAM", NULL);
                break;
            }
            // Persist the device name and set in the Bluetooth stack if available
#ifdef ESP_PLATFORM
            if (nvs_storage_set_device_name(ctx->params[0]) == ESP_OK) {
                // Try to set the name in bt_manager if provided
                bt_set_name(ctx->params[0]);
                cmd_send_response("OK", "SET_NAME", "SUCCESS", ctx->params[0]);
            } else {
                cmd_send_response("ERR", "SET_NAME", "FAILED", NULL);
            }
#else
            nvs_storage_set_device_name(ctx->params[0]);
            cmd_send_response("OK", "SET_NAME", "MOCK_SUCCESS", ctx->params[0]);
#endif
        } break;

        case CMD_TYPE_DEBUG: {
            // Support quick debug commands for development/testing
            // Usage: DEBUG MOCK_ON
            //        DEBUG MOCK_ADD <MAC>,<NAME>
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "DEBUG", "MISSING_PARAM", NULL);
                break;
            }
            if (strcasecmp(ctx->params[0], "MOCK_ON") == 0) {
                // Turn on internal lightweight mock mode for command-interface-only
                // testing. This does not enable the full bt_mock component, but
                // allows deterministic on-device pairing flows for E2E tests.
                s_cmd_mock_enabled = true;
                cmd_send_response("OK", "DEBUG", "MOCK_ON", NULL);
            } else if (strcasecmp(ctx->params[0], "MOCK_ADD") == 0) {
                // params[1] should contain 'MAC,NAME'
                if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0) {
                    cmd_send_response("ERR", "DEBUG", "MOCK_ADD_MISSING", NULL);
                    break;
                }
                char payload[128];
                strncpy(payload, ctx->params[1], sizeof(payload)-1);
                payload[sizeof(payload)-1] = '\0';
                char* comma = strchr(payload, ',');
                if (!comma) {
                    cmd_send_response("ERR", "DEBUG", "MOCK_ADD_INVALID", ctx->params[1]);
                    break;
                }
                *comma = '\0';
                const char* mac = payload;
                // Record last mock device address in our lightweight mock state.
                strncpy(s_cmd_mock_pairing_addr, mac, sizeof(s_cmd_mock_pairing_addr)-1);
                s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr)-1] = '\0';
                cmd_send_response("OK", "DEBUG", "MOCK_ADD", ctx->params[1]);
            } else {
                cmd_send_response("ERR", "DEBUG", "UNKNOWN_SUBCMD", ctx->params[0]);
            }
            // Support MOCK_PAIR: DEBUG MOCK_PAIR <MAC>
            if (strcasecmp(ctx->params[0], "MOCK_PAIR") == 0) {
                if (ctx->param_count < 2 || strlen(ctx->params[1]) == 0) {
                    cmd_send_response("ERR", "DEBUG", "MOCK_PAIR_MISSING", NULL);
                } else {
#ifdef ESP_PLATFORM
                    // Start deterministic internal mock pairing flow.
                    s_cmd_mock_enabled = true;
                    strncpy(s_cmd_mock_pairing_addr, ctx->params[1], sizeof(s_cmd_mock_pairing_addr)-1);
                    s_cmd_mock_pairing_addr[sizeof(s_cmd_mock_pairing_addr)-1] = '\0';
                    // Deterministic passkey generation
                    {
                        const char* mac = s_cmd_mock_pairing_addr;
                        char pass[16] = "000000";
                        size_t maclen = strlen(mac);
                        if (maclen >= 2) {
                            const char* tail = mac + (maclen > 5 ? maclen - 5 : 0);
                            snprintf(pass, sizeof(pass), "%s", tail);
                        }
                        strncpy(s_cmd_mock_passkey, pass, sizeof(s_cmd_mock_passkey)-1);
                        s_cmd_mock_passkey[sizeof(s_cmd_mock_passkey)-1] = '\0';
                    }
                    char data[64];
                    snprintf(data, sizeof(data), "%s,%s", s_cmd_mock_pairing_addr, s_cmd_mock_passkey);
                    cmd_send_event_pair("CONFIRM", data);
                    cmd_send_response("OK", "DEBUG", "MOCK_PAIR_STARTED", ctx->params[1]);
#else
                    cmd_send_response("OK", "DEBUG", "MOCK_PAIR_MOCKED", ctx->params[1]);
#endif
                }
            }
        } break;

        

        case CMD_TYPE_SET_DEFAULT_PIN: {
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "SET_DEFAULT_PIN", "MISSING_PARAM", NULL);
                break;
            }
#ifdef ESP_PLATFORM
            if (nvs_storage_set_default_pin(ctx->params[0]) == ESP_OK) {
                cmd_send_response("OK", "SET_DEFAULT_PIN", "SUCCESS", ctx->params[0]);
            } else {
                cmd_send_response("ERR", "SET_DEFAULT_PIN", "FAILED", NULL);
            }
#else
            nvs_storage_set_default_pin(ctx->params[0]);
            cmd_send_response("OK", "SET_DEFAULT_PIN", "MOCK_SUCCESS", ctx->params[0]);
#endif
        } break;

        case CMD_TYPE_UNPAIR: {
            if (ctx->param_count < 1) {
                cmd_send_response("ERR", "UNPAIR", "MISSING_PARAM", NULL);
                break;
            }
#ifdef ESP_PLATFORM
            if (bt_unpair(ctx->params[0]) == BT_SUCCESS) {
                cmd_send_response("OK", "UNPAIR", "REMOVED", NULL);
            } else {
                cmd_send_response("ERR", "UNPAIR", "FAILED", NULL);
            }
#else
            cmd_send_response("OK", "UNPAIR", "MOCK_REMOVED", ctx->params[0]);
#endif
        } break;

        case CMD_TYPE_HELP: {
            // Emit a concise list of supported commands with syntax and brief descriptions
            cmd_send_response("INFO", "HELP", "SCAN", "Start Bluetooth device scan (example: SCAN)");
            cmd_send_response("INFO", "HELP", "CONNECT <MAC>", "Connect by MAC (example: CONNECT AA:BB:CC:DD:EE:FF)");
            cmd_send_response("INFO", "HELP", "CONNECT_NAME <NAME>", "Connect by name (example: CONNECT_NAME \"MySpeaker\")");
            cmd_send_response("INFO", "HELP", "DISCONNECT", "Disconnect active device (example: DISCONNECT)");
            cmd_send_response("INFO", "HELP", "PAIRED", "List paired devices (example: PAIRED)");
            cmd_send_response("INFO", "HELP", "PAIR <MAC>", "Initiate pairing (example: PAIR AA:BB:CC:DD:EE:FF)");
            cmd_send_response("INFO", "HELP", "CONFIRM_PIN <MAC> [ACCEPT|REJECT]", "Confirm pairing; examples: CONFIRM_PIN AA:BB:CC:DD:EE:FF ACCEPT or shorthand: CMD|CONFIRM_PIN|1");
            cmd_send_response("INFO", "HELP", "ENTER_PIN <MAC> <PIN>", "Enter PIN for pairing; example: ENTER_PIN AA:BB:CC:DD:EE:FF 1234 (or use default with ENTER_PIN AA:BB:...)");
            cmd_send_response("INFO", "HELP", "SET_DEFAULT_PIN <PIN>", "Store default PIN in NVS; example: SET_DEFAULT_PIN 0000");
            cmd_send_response("INFO", "HELP", "SET_NAME <NAME>", "Set device Bluetooth name; example: SET_NAME \"MySpeaker\"");
            cmd_send_response("INFO", "HELP", "START", "Start audio streaming (example: START)");
            cmd_send_response("INFO", "HELP", "STOP", "Stop audio streaming (example: STOP)");
            cmd_send_response("INFO", "HELP", "VOLUME <0-100>", "Set volume; example: VOLUME 75");
            cmd_send_response("INFO", "HELP", "MUTE", "Mute audio output (example: MUTE)");
            cmd_send_response("INFO", "HELP", "UNMUTE", "Unmute audio output (example: UNMUTE)");
            cmd_send_response("INFO", "HELP", "STATUS", "Show audio processor status (example: STATUS)");
            cmd_send_response("INFO", "HELP", "VERSION", "Show firmware/tool version (example: VERSION)");
            cmd_send_response("INFO", "HELP", "RESET", "Perform a soft reset (example: RESET)");
            cmd_send_response("INFO", "HELP", "DEBUG <SUBCMD>", "Debug: MOCK_ON; MOCK_ADD <MAC,NAME> (example: DEBUG MOCK_ADD AA:BB:CC:DD:EE:FF,MockDevice); MOCK_PAIR <MAC> (example: DEBUG MOCK_PAIR AA:BB:...)");
            cmd_send_response("INFO", "HELP", "I2S_CONFIG <bclk,wclk,din[,dout]>", "Configure I2S pins; example: I2S_CONFIG 26,25,22,0");
            cmd_send_response("INFO", "HELP", "SAMPLE_RATE <HZ>", "Set sample rate; example: SAMPLE_RATE 48000");
            cmd_send_response("INFO", "HELP", "UNPAIR <MAC>", "Remove paired device; example: UNPAIR AA:BB:CC:DD:EE:FF");
            cmd_send_response("INFO", "HELP", "UNPAIR_ALL", "Remove all paired devices (example: UNPAIR_ALL)");
            // Compact machine-friendly help envelope: semicolon-separated COMMAND:example
            cmd_send_response("INFO", "HELP", "ALL",
                "SCAN:SCAN;CONNECT:CONNECT AA:BB:CC:DD:EE:FF;CONNECT_NAME:CONNECT_NAME \"MySpeaker\";PAIR:PAIR AA:BB:CC:DD:EE:FF;CONFIRM_PIN:CMD|CONFIRM_PIN|1 (or CONFIRM_PIN MAC ACCEPT);ENTER_PIN:ENTER_PIN AA:BB:CC:DD:EE:FF 1234;SET_DEFAULT_PIN:SET_DEFAULT_PIN 0000;SET_NAME:SET_NAME \"MySpeaker\";START:START;STOP:STOP;VOLUME:VOLUME 75;MUTE:MUTE;UNMUTE:UNMUTE;STATUS:STATUS;VERSION:VERSION;RESET:RESET;DEBUG:DEBUG MOCK_ADD AA:BB:CC:DD:EE:FF,MockDevice;I2S_CONFIG:I2S_CONFIG 26,25,22,0;SAMPLE_RATE:SAMPLE_RATE 48000;UNPAIR:UNPAIR AA:BB:CC:DD:EE:FF;UNPAIR_ALL:UNPAIR_ALL");
        } break;

        default:
            cmd_send_response("INFO", "COMMAND", "RECEIVED", "Not implemented yet");
            break;
    }
    
    return CMD_SUCCESS;
}

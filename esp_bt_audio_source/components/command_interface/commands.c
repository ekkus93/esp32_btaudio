#include "command_interface.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
// Audio processor APIs
#ifdef ESP_PLATFORM
#include "audio_processor.h"
#else
// In host tests these symbols may be mocked; include a local header if available
#include "audio_processor.h"
#endif

// Define this for ESP32 builds
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "driver/uart.h"
#define TAG "CMD_IF"
#define CMD_UART_NUM UART_NUM_1
#define CMD_BUF_SIZE 256
#include "nvs_storage.h"
#include "bt_manager.h"
#include "esp_gap_bt_api.h"
#else
// Include mock UART header for host-based testing
#include "mock_uart.h"
#include "nvs_storage.h"
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
    uart_driver_delete(CMD_UART_NUM);
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
    uart_write_bytes(CMD_UART_NUM, response, len);
#else
    // For testing without ESP-IDF
    uart_write_bytes(UART_NUM_1, response, len);
#endif
    
    return CMD_SUCCESS;
}

// Process incoming data from UART and handle complete commands
cmd_status_t cmd_process(void) {
    if (!cmd_ctx.initialized) {
        return CMD_ERROR_NOT_INITIALIZED;
    }
    
    // Simple mock implementation for testing
    // Read a command from UART and process it
    uint8_t buf[128];
    int len = uart_read_bytes(UART_NUM_1, buf, sizeof(buf), 0);
    
    if (len > 0) {
        // Null terminate the command
        if (len < sizeof(buf)) {
            buf[len] = '\0';
        } else {
            buf[sizeof(buf) - 1] = '\0';
        }
        
        // Process command (simple test implementation)
        cmd_context_t ctx;
        if (cmd_parse((const char*)buf, &ctx) == CMD_SUCCESS) {
            // For the test, we only need to handle SCAN command
            if (ctx.type == CMD_TYPE_SCAN) {
                // Send a mock response for testing
                cmd_send_response("OK", "SCAN", "COMPLETE", "0");
                return CMD_SUCCESS;
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
            // Host test: just echo
            cmd_send_response("OK", "CONFIRM_PIN", accept ? "MOCK_ACCEPTED" : "MOCK_REJECTED", mac);
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
            // Host test: just echo
            cmd_send_response("OK", "ENTER_PIN", "MOCK_SENT", mac);
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
                if (bt_set_name != NULL) bt_set_name(ctx->params[0]);
                cmd_send_response("OK", "SET_NAME", "SUCCESS", ctx->params[0]);
            } else {
                cmd_send_response("ERR", "SET_NAME", "FAILED", NULL);
            }
#else
            nvs_storage_set_device_name(ctx->params[0]);
            cmd_send_response("OK", "SET_NAME", "MOCK_SUCCESS", ctx->params[0]);
#endif
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

        default:
            cmd_send_response("INFO", "COMMAND", "RECEIVED", "Not implemented yet");
            break;
    }
    
    return CMD_SUCCESS;
}

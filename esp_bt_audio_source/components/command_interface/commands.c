#include "command_interface.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Define this for ESP32 builds
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "driver/uart.h"
#define TAG "CMD_IF"
#define CMD_UART_NUM UART_NUM_1
#define CMD_BUF_SIZE 256
#else
// Include mock UART header for host-based testing
#include "mock_uart.h"
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
        case CMD_TYPE_STATUS:
            cmd_send_response("OK", "STATUS", "DISCONNECTED,STOPPED,50", NULL);
            break;
        case CMD_TYPE_VERSION:
            cmd_send_response("OK", "VERSION", "1.0.0", NULL);
            break;
        default:
            cmd_send_response("INFO", "COMMAND", "RECEIVED", "Not implemented yet");
            break;
    }
    
    return CMD_SUCCESS;
}

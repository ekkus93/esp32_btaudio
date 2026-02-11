#ifndef COMMAND_INTERFACE_MOCK_H
#define COMMAND_INTERFACE_MOCK_H

#include "esp_err.h"

/**
 * @brief Command status codes
 */
typedef enum {
    CMD_SUCCESS = 0,
    CMD_INVALID_SYNTAX,
    CMD_UNKNOWN_COMMAND,
    CMD_EXECUTION_ERROR,
    CMD_INVALID_PARAMETER
} cmd_status_t;

/**
 * @brief Command types
 */
typedef enum {
    CMD_TYPE_NONE = 0,
    CMD_TYPE_SCAN,
    CMD_TYPE_CONNECT,
    CMD_TYPE_DISCONNECT,
    CMD_TYPE_STATUS,
    CMD_TYPE_START,
    CMD_TYPE_STOP,
    CMD_TYPE_VOLUME,
    CMD_TYPE_PAIRED,
    CMD_TYPE_HELP
} cmd_type_t;

/**
 * @brief Command context structure
 */
typedef struct {
    cmd_type_t type;
    char params[5][32];  // Up to 5 parameters, each max 32 chars
    int param_count;
} cmd_context_t;

/**
 * @brief Parse a command string
 */
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx);

/**
 * @brief Execute a parsed command
 */
cmd_status_t cmd_execute(const cmd_context_t* ctx);

#endif /* COMMAND_INTERFACE_MOCK_H */
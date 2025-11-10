#ifndef COMMAND_INTERFACE_H
#define COMMAND_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Command Interface - Handles serial command protocol
 */

// Status codes
typedef enum {
    CMD_SUCCESS = 0,
    CMD_ERROR_INIT_FAILED,
    CMD_ERROR_INVALID_PARAM,
    CMD_ERROR_UNKNOWN,
    CMD_ERROR_NOT_INITIALIZED,
    CMD_ERROR_TOO_MANY_PARAMS
} cmd_status_t;

// Command types
typedef enum {
    CMD_TYPE_SCAN = 0,
    CMD_TYPE_CONNECT,
    CMD_TYPE_CONNECT_NAME,
    CMD_TYPE_DISCONNECT,
    CMD_TYPE_PAIRED,
    CMD_TYPE_SET_NAME,
    CMD_TYPE_START,
    CMD_TYPE_STOP,
    CMD_TYPE_VOLUME,
    CMD_TYPE_MUTE,
    CMD_TYPE_UNMUTE,
    CMD_TYPE_STATUS,
    CMD_TYPE_MEM,
    CMD_TYPE_VERSION,
    CMD_TYPE_RESET,
    CMD_TYPE_DEBUG,
    CMD_TYPE_SAMPLE_RATE,
    CMD_TYPE_I2S_CONFIG,
    CMD_TYPE_SYNTH,
    CMD_TYPE_PAIR,
    CMD_TYPE_BEEP,
    CMD_TYPE_CONFIRM_PIN,
    CMD_TYPE_ENTER_PIN,
    CMD_TYPE_SET_DEFAULT_PIN,
    CMD_TYPE_UNPAIR,
    CMD_TYPE_UNPAIR_ALL,
    CMD_TYPE_HELP,
    // Add new command types here
    CMD_TYPE_PLAY,
    CMD_TYPE_UNKNOWN
} cmd_type_t;

// Maximum parameter count and length
#define CMD_MAX_PARAMS 5
#define CMD_MAX_PARAM_LEN 32

// Command context
typedef struct {
    cmd_type_t type;
    int param_count;
    char params[CMD_MAX_PARAMS][CMD_MAX_PARAM_LEN];
} cmd_context_t;

/**
 * Initialize command interface
 *
 * @return CMD_SUCCESS if successful
 */
cmd_status_t cmd_init(void);

/**
 * Deinitialize command interface
 *
 * @return CMD_SUCCESS if successful
 */
cmd_status_t cmd_deinit(void);

/**
 * Parse a command string
 *
 * @param cmd_str Command string to parse
 * @param ctx Pointer to command context (result)
 * @return CMD_SUCCESS if successful
 */
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx);

/**
 * Execute a command
 *
 * @param ctx Command context
 * @return CMD_SUCCESS if successful
 */
cmd_status_t cmd_execute(const cmd_context_t* ctx);

/**
 * Send a response
 *
 * @param status Status (OK, ERROR, INFO)
 * @param command Command name
 * @param result Result string
 * @param data Additional data (or NULL)
 * @return CMD_SUCCESS if successful
 */
cmd_status_t cmd_send_response(const char* status, const char* command, 
                              const char* result, const char* data);

/**
 * Process incoming serial data
 * This should be called regularly to handle commands
 *
 * @return CMD_SUCCESS if successful
 */
cmd_status_t cmd_process(void);

/**
 * Convenience helper to emit pairing-related events to the serial interface.
 * This wraps the standard response format using status=EVENT and command=PAIR.
 *
 * @param subtype Event subtype (e.g. "PIN_REQUEST", "CONFIRM", "SUCCESS", "FAILED")
 * @param data Optional data string (MAC, PIN, or comma-separated values)
 * @return CMD_SUCCESS if the event was emitted
 */
cmd_status_t cmd_send_event_pair(const char* subtype, const char* data);

#endif // COMMAND_INTERFACE_H

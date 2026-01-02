// Minimal command_interface.h for test_app_audio (test-only)
#ifndef TEST_COMMAND_INTERFACE_H
#define TEST_COMMAND_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CMD_SUCCESS = 0,
    CMD_ERROR_INIT_FAILED,
    CMD_ERROR_INVALID_PARAM,
    CMD_ERROR_UNKNOWN,
    CMD_ERROR_NOT_INITIALIZED,
    CMD_ERROR_TOO_MANY_PARAMS
} cmd_status_t;

typedef enum {
    CMD_TYPE_UNKNOWN = -1,
    CMD_TYPE_PLAY = 0,
    CMD_TYPE_STOP = 1,
    CMD_TYPE_BEEP = 2,
} cmd_type_t;

#define CMD_MAX_PARAMS 5
#define CMD_MAX_PARAM_LEN 32

typedef struct {
    cmd_type_t type;
    int param_count;
    char params[CMD_MAX_PARAMS][CMD_MAX_PARAM_LEN];
} cmd_context_t;

cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx);
cmd_status_t cmd_execute(const cmd_context_t* ctx);

#endif // TEST_COMMAND_INTERFACE_H

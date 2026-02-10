#include "command_interface.h"

const char* cmd_status_to_name(cmd_status_t status) {
    switch (status) {
        case CMD_SUCCESS:                return "CMD_SUCCESS";
        case CMD_ERROR_INIT_FAILED:      return "CMD_ERROR_INIT_FAILED";
        case CMD_ERROR_INVALID_PARAM:    return "CMD_ERROR_INVALID_PARAM";
        case CMD_ERROR_UNKNOWN:          return "CMD_ERROR_UNKNOWN";
        case CMD_ERROR_NOT_INITIALIZED:  return "CMD_ERROR_NOT_INITIALIZED";
        case CMD_ERROR_TOO_MANY_PARAMS:  return "CMD_ERROR_TOO_MANY_PARAMS";
        default:                         return "CMD_ERROR_UNKNOWN_CODE";
    }
}

/* Host/unit-test builds (ESP_PLATFORM undefined) may not link the full
 * command implementation. Provide weak stubs that return ERROR to catch
 * missing mocks/implementations at test time rather than silently succeeding.
 * Tests must explicitly link `commands.c` or provide their own mocks.
 * When building for the real ESP platform, rely on the strong definitions
 * in `commands.c` to avoid accidentally pulling in these error stubs. */
#ifndef ESP_PLATFORM
__attribute__((weak)) cmd_status_t cmd_init(void) { return CMD_ERROR_NOT_INITIALIZED; }
__attribute__((weak)) cmd_status_t cmd_deinit(void) { return CMD_ERROR_NOT_INITIALIZED; }
__attribute__((weak)) cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx) { (void)cmd_str; (void)ctx; return CMD_ERROR_NOT_INITIALIZED; }
__attribute__((weak)) cmd_status_t cmd_execute(const cmd_context_t* ctx) { (void)ctx; return CMD_ERROR_NOT_INITIALIZED; }
__attribute__((weak)) cmd_status_t cmd_send_response(const char* status, const char* command, const char* result, const char* data) { (void)status; (void)command; (void)result; (void)data; return CMD_ERROR_NOT_INITIALIZED; }
__attribute__((weak)) cmd_status_t cmd_process(void) { return CMD_ERROR_NOT_INITIALIZED; }
#endif

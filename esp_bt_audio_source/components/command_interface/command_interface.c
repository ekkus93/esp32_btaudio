#include "command_interface.h"

// Provide stub implementations for linker
cmd_status_t cmd_init(void) { return CMD_SUCCESS; }
cmd_status_t cmd_deinit(void) { return CMD_SUCCESS; }
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx) { return CMD_SUCCESS; }
cmd_status_t cmd_execute(const cmd_context_t* ctx) { return CMD_SUCCESS; }
cmd_status_t cmd_send_response(const char* status, const char* command, const char* result, const char* data) { return CMD_SUCCESS; }
cmd_status_t cmd_process(void) { return CMD_SUCCESS; }

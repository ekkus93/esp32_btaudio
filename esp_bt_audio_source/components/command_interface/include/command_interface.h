#ifndef COMMAND_INTERFACE_H
#define COMMAND_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

#define CMD_STATUS_OK    "OK"
#define CMD_STATUS_ERR   "ERR"
#define CMD_STATUS_INFO  "INFO"
#define CMD_STATUS_EVENT "EVENT"

typedef enum {
    CMD_SUCCESS = 0,
    CMD_ERROR_INIT_FAILED,
    CMD_ERROR_INVALID_PARAM,
    CMD_ERROR_UNKNOWN,
    CMD_ERROR_NOT_INITIALIZED,
    CMD_ERROR_TOO_MANY_PARAMS
} cmd_status_t;

const char* cmd_status_to_name(cmd_status_t status);

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
    CMD_TYPE_DIAG,
    CMD_TYPE_CONFIRM_PIN,
    CMD_TYPE_ENTER_PIN,
    CMD_TYPE_SET_DEFAULT_PIN,
    CMD_TYPE_UNPAIR,
    CMD_TYPE_UNPAIR_ALL,
    CMD_TYPE_PARTS,
    CMD_TYPE_HELP,
    CMD_TYPE_AUDIO_AUTOSTART,
    CMD_TYPE_AUDIO_STATUS,
    CMD_TYPE_SPANLOG,
    CMD_TYPE_LAST_MAC,
    CMD_TYPE_UARTAUDIO,
    CMD_TYPE_I2S_PROBE,
    CMD_TYPE_I2S_RXTEST,
    CMD_TYPE_I2S_CLKGEN,
    CMD_TYPE_HFP,
    CMD_TYPE_UNKNOWN
} cmd_type_t;

#define CMD_MAX_PARAMS 5
#define CMD_MAX_PARAM_LEN 64

typedef struct {
    cmd_type_t type;
    int param_count;
    char params[CMD_MAX_PARAMS][CMD_MAX_PARAM_LEN];
} cmd_context_t;

cmd_status_t cmd_init(void);
cmd_status_t cmd_deinit(void);
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx);
cmd_status_t cmd_execute(const cmd_context_t* ctx);
cmd_status_t cmd_send_response(const char* status, const char* command,
                               const char* result, const char* data);
cmd_status_t cmd_send_response_all(const char* status, const char* command,
                                   const char* result, const char* data);
cmd_status_t cmd_process(void);
cmd_status_t cmd_send_event_pair(const char* subtype, const char* data);

#if defined(UNIT_TEST)
void cmd_test_reset_cmd_process_state(void);
#endif

#endif

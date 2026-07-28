#include "bt_hfp_event_command_stub.h"

#include <stdio.h>
#include <string.h>

#define EVENT_STUB_MAX_LINES 32U
#define EVENT_STUB_LINE_LEN 256U

static cmd_status_t s_status = CMD_SUCCESS;
static size_t s_count;
static char s_lines[EVENT_STUB_MAX_LINES][EVENT_STUB_LINE_LEN];

void bt_hfp_event_command_stub_reset(void)
{
    s_status = CMD_SUCCESS;
    s_count = 0U;
    memset(s_lines, 0, sizeof(s_lines));
}

void bt_hfp_event_command_stub_set_status(cmd_status_t status)
{
    s_status = status;
}

size_t bt_hfp_event_command_stub_count(void)
{
    return s_count;
}

const char *bt_hfp_event_command_stub_line(size_t index)
{
    return index < s_count ? s_lines[index] : NULL;
}

cmd_status_t cmd_send_response(const char *status,
                               const char *command,
                               const char *result,
                               const char *data)
{
    if (s_count >= EVENT_STUB_MAX_LINES) {
        return CMD_ERROR_TOO_MANY_PARAMS;
    }
    {
        const char *safe_status = status != NULL ? status : "";
        const char *safe_command = command != NULL ? command : "";
        const char *safe_result = result != NULL ? result : "";
        const char *safe_data = data != NULL ? data : "";
        int written = snprintf(s_lines[s_count], EVENT_STUB_LINE_LEN,
                               "%s|%s|%s|%s",
                               safe_status, safe_command,
                               safe_result, safe_data);
        if (written < 0 || (size_t)written >= EVENT_STUB_LINE_LEN) {
            return CMD_ERROR_TOO_MANY_PARAMS;
        }
        s_count++;
    }
    return s_status;
}

#pragma once

#include <stddef.h>

#include "command_interface.h"

void bt_hfp_event_command_stub_reset(void);
void bt_hfp_event_command_stub_set_status(cmd_status_t status);
size_t bt_hfp_event_command_stub_count(void);
const char *bt_hfp_event_command_stub_line(size_t index);

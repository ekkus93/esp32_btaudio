#pragma once

#include "bt_mock.h"

// Function declarations
void bt_mock_set_scanning(bool scanning);
void bt_mock_set_connected(bool connected, const char* addr, const char* name);
void bt_mock_set_streaming(bool streaming);
void bt_mock_set_connection_state(bt_connection_state_t state);
void bt_mock_set_streaming_state(bt_streaming_state_t state);

// Implement these functions if they don't exist

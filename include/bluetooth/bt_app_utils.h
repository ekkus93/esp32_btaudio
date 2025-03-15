#ifndef BT_APP_UTILS_H
#define BT_APP_UTILS_H

#include <stdbool.h>
#include <stdint.h>

// Function declarations
bool is_valid_mac(const char *mac_str);

// Add the new function declaration
void bt_wait_for_stack_ready(uint32_t min_time_ms);

#endif // BT_APP_UTILS_H
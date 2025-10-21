#pragma once
#include <stdbool.h>
#include <stddef.h>

// Minimal stub used by test pairing commands when run under the host-test
// harness. The real test app provides richer test utilities; for the purposes
// of the adapter runner we only need these prototypes which are implemented in
// `test_pairing_adapters.c`.

void test_utils_reset_state(void);
bool test_send_serial_cmd(const char *cmd);
bool test_capture_event(char *out_buf, size_t out_len);

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "esp_err.h"

/* Test helper to initialize the bt_manager for test-app suites. This wraps
 * bt_manager_init() with a stable device name and marks the source mock
 * initialized so tests relying on bt_init() semantics continue to work.
 */
esp_err_t test_bt_manager_init(void);

#endif // TEST_HELPERS_H

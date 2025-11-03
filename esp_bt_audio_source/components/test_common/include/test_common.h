// Shared test-only prototypes used by test helpers across test apps.
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdbool.h>

/* Test-only function hooks used by tests and test helpers. These are small
 * prototypes only — implementations remain in test apps or test-only
 * components to avoid duplicate definitions across different test targets.
 */
void bt_manager_force_initialized(bool value);
void bt_source_mock_set_initialized(bool initialized);

#endif // TEST_COMMON_H

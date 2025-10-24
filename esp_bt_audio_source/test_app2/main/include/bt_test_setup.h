#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bt_source.h"
#include "bt_mock.h"
#include "test_config.h"

/**
 * @brief Set up common test environment for BT tests
 * - Initializes BT mock system
 * - Adds test devices
 */
void bt_mock_setup_common(void);

/**
 * @brief Set up mock devices for testing
 * - Adds standard test devices with predefined addresses
 */
void bt_mock_setup_devices(void);

/**
 * @brief Set up paired devices for testing
 * - Adds paired test devices
 */
void bt_mock_setup_paired_devices(void);

/**
 * @brief Ensure the Bluetooth manager is initialized for tests
 *
 * Calls bt_manager_init with a deterministic configuration so that
 * manager-backed APIs used by the test suite do not early-out when the
 * manager hasn't been started.
 */
void bt_manager_test_setup(void);

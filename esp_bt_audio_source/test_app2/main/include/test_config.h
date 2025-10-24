/**
 * @file test_config.h
 * @brief Configuration and declarations for the BT test suite
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bt_source.h"
#include "bt_mock.h"

/**
 * @brief Test configuration settings
 */
typedef struct {
    bool use_mock_bt;        /* Use Bluetooth mocks instead of real implementation */
    bool skip_audio_tests;   /* Skip running audio hardware tests */
    bool run_bt_tests;       /* Run Bluetooth tests */
    int test_duration;       /* Duration of tests in seconds */
    char test_device_name[64]; /* Target test device name if specified */
} test_config_t;

// Test constants for Bluetooth testing
#define TEST_DEVICE_ADDR       "11:22:33:44:55:66"
#define TEST_DEVICE_NAME       "Test_BT_Speaker"
#define TEST_SCAN_TIMEOUT      3  // Scan timeout in seconds

// Function declarations for common test setup routines
void bt_test_setup_common(void);
void setup_mock_devices(void);
void bt_test_setup_paired_devices(void);

/**
 * @brief Initialize test configuration with default values
 * 
 * @param config Pointer to configuration struct to initialize
 * @return ESP_OK on success
 */
esp_err_t test_config_init(test_config_t *config);

/**
 * @brief Set test configuration parameters
 * 
 * @param config Pointer to configuration with new values
 * @return ESP_OK on success
 */
esp_err_t test_config_set(const test_config_t *config);

/**
 * @brief Get current test configuration
 * 
 * @param config Pointer to configuration struct to fill
 * @return ESP_OK on success
 */
esp_err_t test_config_get(test_config_t *config);

/**
 * @brief Check if a specific test should be run
 * 
 * @param test_name Name of the test to check
 * @return true if test should run, false if it should be skipped
 */
bool test_config_should_run_test(const char *test_name);

/**
 * @brief Get the test device name if set
 * 
 * @return Pointer to device name string, or NULL if not set
 */
const char* test_config_get_device_name(void);

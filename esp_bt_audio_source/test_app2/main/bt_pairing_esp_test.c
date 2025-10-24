/**
 * @file bt_pairing_esp_test.c
 * @brief Tests for the Bluetooth pairing functionality on ESP32
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "unity.h"
#include "unity_fixture.h"  // Add this include for TEST_CASE macro
#include "bt_source.h"
#include "unity_test_utils.h"

/* Log tag for this file */
static const char *TAG = "BT_PAIRING_ESP32_TEST";

/* Default PIN used for testing */
static const char *DEFAULT_TEST_PIN = "1234";

// Define test group
TEST_GROUP(bt_pairing);

// Setup function - runs before each test
TEST_SETUP(bt_pairing)
{
    // Setup for each test
}

// Teardown function - runs after each test
TEST_TEAR_DOWN(bt_pairing)
{
    // Cleanup after each test
}

/**
 * @brief Test that the default PIN can be retrieved
 */
TEST(bt_pairing, DefaultPINCanBeRetrieved)
{
    char pin[8] = {0};
    esp_err_t ret = bt_get_default_pin(pin, sizeof(pin));
    
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(DEFAULT_TEST_PIN, pin);
}

/**
 * @brief Test that a custom PIN can be set
 */
TEST(bt_pairing, CustomPINCanBeSet)
{
    const char *custom_pin = "5678";
    char pin[8] = {0};
    
    /* Set a custom PIN */
    esp_err_t ret = bt_set_default_pin(custom_pin);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    /* Retrieve the PIN and verify it matches */
    ret = bt_get_default_pin(pin, sizeof(pin));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(custom_pin, pin);
    
    /* Reset to default PIN for other tests */
    bt_set_default_pin(DEFAULT_TEST_PIN);
}

// Register the tests
TEST_GROUP_RUNNER(bt_pairing)
{
    RUN_TEST_CASE(bt_pairing, DefaultPINCanBeRetrieved);
    RUN_TEST_CASE(bt_pairing, CustomPINCanBeSet);
}

// This can be called from the main test runner
void run_bt_pairing_tests(void)
{
    RUN_TEST_GROUP(bt_pairing);
}

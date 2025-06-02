/**
 * Small, quick tests for the Bluetooth pairing functionality
 * These test the real implementation on actual hardware
 */
#include <stdio.h>
#include "unity.h"
#include "bt_source.h"
#include "esp_log.h"

static const char *TAG = "BT_PAIRING_ESP32_TEST";

// Test fixtures
void setUp(void) {
    // Initialize Bluetooth for each test
    bt_init();
}

void tearDown(void) {
    // Clean up after each test
    bt_disconnect();
}

// Test that we can get the default PIN
TEST_CASE("Default PIN can be retrieved", "[bluetooth][pairing]")
{
    char pin[16];
    esp_err_t ret = bt_get_default_pin(pin, sizeof(pin));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_EQUAL(0, strlen(pin));
}

// Test that we can set custom PIN
TEST_CASE("Custom PIN can be set", "[bluetooth][pairing]")
{
    esp_err_t ret = bt_set_default_pin("9876");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    char pin[16];
    ret = bt_get_default_pin(pin, sizeof(pin));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("9876", pin);
}

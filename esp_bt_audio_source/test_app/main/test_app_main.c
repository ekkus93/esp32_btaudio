#include "unity.h"
#include "bt_source.h"
#include "command_interface.h"

void setUp(void) {
    // This function is called before each test
}

void tearDown(void) {
    // This function is called after each test
}

// Test: Bluetooth stack initializes successfully
TEST_CASE("Bluetooth stack initializes successfully", "[bluetooth]") {
    esp_err_t err = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

// Test: Parse SCAN command
TEST_CASE("Parse SCAN command", "[commands]") {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SCAN", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_SCAN, ctx.type);
}

// Test: Parse CONNECT command
TEST_CASE("Parse CONNECT command", "[commands]") {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT, ctx.type);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", ctx.params[0]);
}

void app_main(void) {
    unity_run_menu();
}
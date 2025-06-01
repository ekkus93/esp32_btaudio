#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "command_interface.h"
#include "mock_uart.h"

// Test fixture
void setUp(void) {
    // Initialize before each test
    mock_uart_init(115200);
    cmd_init();
}

void tearDown(void) {
    // Clean up after each test
    cmd_deinit();
}

// Test basic command parsing
void test_parse_scan_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SCAN", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_SCAN, ctx.type);
    TEST_ASSERT_EQUAL(0, ctx.param_count);
}

// Test command with parameter
void test_parse_connect_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", ctx.params[0]);
}

// Test multiple parameters
void test_parse_i2s_config_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_I2S_CONFIG, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("26,25,22,21", ctx.params[0]);
}

// Test invalid command
void test_parse_invalid_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("INVALID_COMMAND", &ctx));
}

// Test command with whitespace
void test_parse_command_with_whitespace(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("  VOLUME  75  ", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_VOLUME, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("75", ctx.params[0]);
}

// Test response generation
void test_send_response(void) {
    mock_uart_reset_tx();
    
    cmd_send_response("OK", "SCAN", "COMPLETE", "2");
    
    // Check that the correct response was sent
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    TEST_ASSERT_EQUAL_STRING("OK|SCAN|COMPLETE|2\r\n", tx_data);
}

// Test command processing from UART
void test_command_processing(void) {
    // Inject command into mock UART
    mock_uart_inject_rx_data("SCAN\r\n", 6);
    
    // Process the command
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    
    // Verify response
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    
    // Note: In a real implementation, this would return scan results
    // For the test, we're just checking it sent some response
    TEST_ASSERT_TRUE(strstr(tx_data, "SCAN") != NULL);
}

// Main test runner
int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_parse_scan_command);
    RUN_TEST(test_parse_connect_command);
    RUN_TEST(test_parse_i2s_config_command);
    RUN_TEST(test_parse_invalid_command);
    RUN_TEST(test_parse_command_with_whitespace);
    RUN_TEST(test_send_response);
    RUN_TEST(test_command_processing);
    
    return UNITY_END();
}

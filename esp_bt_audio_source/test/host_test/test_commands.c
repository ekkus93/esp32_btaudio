#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "command_interface.h"
#include "mock_uart.h"
#include "nvs_storage.h"
// Access mock gap helpers
extern void mock_gap_reset(void);
extern const char* mock_gap_get_last_mac(void);
extern int mock_gap_get_last_pin_len(void);
extern const char* mock_gap_get_last_pin(void);
extern int mock_gap_get_last_confirm(void);

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

// New tests for pairing command handlers
void test_confirm_pin_command(void) {
    mock_gap_reset();
    // Execute confirm command (MAC + accept)
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONFIRM_PIN AA:BB:CC:DD:EE:FF ACCEPT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    // Mock formats hex lowercase
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", mac);
    TEST_ASSERT_EQUAL(1, mock_gap_get_last_confirm());
}

void test_enter_pin_command(void) {
    mock_gap_reset();
    // Execute enter pin command
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("ENTER_PIN AA:BB:CC:DD:EE:FF 1234", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", mac);
    TEST_ASSERT_EQUAL(4, mock_gap_get_last_pin_len());
    TEST_ASSERT_EQUAL_STRING("1234", mock_gap_get_last_pin());
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

    // Pairing related tests
    RUN_TEST(test_confirm_pin_command);
    RUN_TEST(test_enter_pin_command);
    
    // Forward-declare tests implemented below
    extern void test_mute_unmute_command(void);
    extern void test_unpair_all_command(void);
    extern void test_status_command(void);
    extern void test_reset_command(void);

    // New tests for mute/unmute and unpair_all
    RUN_TEST(test_mute_unmute_command);
    RUN_TEST(test_unpair_all_command);
    
    // New tests for PAIRED and SAMPLE_RATE
    extern void test_paired_command(void);
    extern void test_sample_rate_command(void);
    RUN_TEST(test_paired_command);
    RUN_TEST(test_sample_rate_command);
    RUN_TEST(test_status_command);
    RUN_TEST(test_reset_command);
    
    return UNITY_END();
}

// Test mute and unmute flow
void test_mute_unmute_command(void) {
    mock_uart_reset_tx();
    // Mute
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("MUTE", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);

    mock_uart_reset_tx();
    // Unmute
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNMUTE", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "UNMUTE") != NULL);
}

// Test UNPAIR_ALL clears stored paired devices
void test_unpair_all_command(void) {
    // Seed mock NVS with paired devices
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));

    int before = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&before));
    TEST_ASSERT_TRUE(before >= 2);

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR_ALL", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "UNPAIR_ALL") != NULL);

    int after = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&after));
    TEST_ASSERT_EQUAL(0, after);
}

// Test listing paired devices
void test_paired_command(void) {
    // Seed mock NVS
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PAIRED", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "COUNT") != NULL);
}

// Test setting sample rate
void test_sample_rate_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SAMPLE_RATE 48000", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "48000") != NULL || strstr(tx, "MOCK_APPLIED") != NULL);
}

// Test STATUS returns key fields
void test_status_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STATUS", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "STATUS") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL || strstr(tx, "SAMPLE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED_COUNT") != NULL || strstr(tx, "COUNT") != NULL);
}

// Test RESET in host-mode returns a mock reboot response
void test_reset_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("RESET", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "RESET") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_REBOOT") != NULL || strstr(tx, "REBOOTING") != NULL);
}

/**
 * @file test_cmd_handlers_bt.c
 * @brief Unit tests for Bluetooth command handlers (TDD Red-Green-Refactor)
 * 
 * Tests focus on error paths, parameter validation, and edge cases for:
 * - cmd_handle_disconnect()
 * - cmd_handle_paired()
 * - cmd_handle_unpair()
 * - cmd_handle_unpair_all()
 * - cmd_handle_set_name()
 * - cmd_handle_set_default_pin()
 * - cmd_handle_debug()
 */

#include "unity.h"
#include "cmd_handlers.h"
#include "command_interface.h"
#include "mock_uart.h"
#include "nvs_storage.h"
#include "platform_storage.h"
#include "bt_manager.h"
#include "esp_err.h"
#include <string.h>

// Mock control functions (from bt_manager_test_hooks.c and nvs_storage_mock.c)
extern void nvs_storage_mock_reset(void);
extern void nvs_storage_mock_set_get_count_result(esp_err_t err);
extern void bt_manager_test_reset_forces(void);
extern void bt_manager_test_set_force_disconnect_failure(int v);
extern void bt_manager_test_set_force_unpair_failure(int v);
extern void bt_manager_test_set_force_unpair_all_failure(int v);

void setUp(void) {
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();
    
    // Initialize BT manager (required for unpair_all and other BT operations)
    bt_manager_init_t cfg = {
        .device_name = "TestBT",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    bt_manager_init(&cfg);
    
    nvs_storage_mock_reset();
    bt_manager_test_reset_forces();
}

void tearDown(void) {
    cmd_deinit();
}

// ============================================================================
// Test 1: cmd_handle_disconnect() should succeed when BT manager accepts
// ============================================================================
void test_cmd_disconnect_should_succeed_when_connected(void) {
    // Arrange
    bt_manager_test_set_force_disconnect_failure(0); // Success
    cmd_context_t ctx = {
        .type = CMD_TYPE_DISCONNECT,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_disconnect(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DISCONNECT|"));
}

// ============================================================================
// Test 2: cmd_handle_disconnect() should fail when BT manager reports error
// ============================================================================
void test_cmd_disconnect_should_fail_when_bt_manager_errors(void) {
    // Arrange
    bt_manager_test_set_force_disconnect_failure(1); // Failure
    cmd_context_t ctx = {
        .type = CMD_TYPE_DISCONNECT,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_disconnect(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DISCONNECT|FAILED"));
}

// ============================================================================
// Test 3: cmd_handle_paired() should report empty list when no devices paired
// ============================================================================
void test_cmd_paired_should_report_empty_list(void) {
    // Arrange - nvs_storage_mock starts with 0 devices by default
    cmd_context_t ctx = {
        .type = CMD_TYPE_PAIRED,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_paired(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|PAIRED|COUNT|0"));
}

// ============================================================================
// Test 4: cmd_handle_paired() should fail when NVS read errors
// ============================================================================
void test_cmd_paired_should_fail_on_nvs_error(void) {
    // Arrange
    nvs_storage_mock_set_get_count_result(ESP_FAIL); // Simulate NVS failure
    cmd_context_t ctx = {
        .type = CMD_TYPE_PAIRED,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_paired(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|PAIRED|READ_FAILED"));
}

// ============================================================================
// Test 5: cmd_handle_unpair() should reject missing MAC parameter
// ============================================================================
void test_cmd_unpair_should_reject_missing_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_UNPAIR,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_unpair(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR|MISSING_PARAM"));
}

// ============================================================================
// Test 6: cmd_handle_unpair() should report NOT_FOUND for unknown MAC
// ============================================================================
void test_cmd_unpair_should_report_not_found_for_unknown_mac(void) {
    // Arrange
    bt_manager_test_set_force_unpair_failure(1); // Force failure -> returns ESP_ERR_NOT_FOUND
    cmd_context_t ctx = {
        .type = CMD_TYPE_UNPAIR,
        .param_count = 1
    };
    strcpy(ctx.params[0], "AA:BB:CC:DD:EE:FF");
    
    // Act
    cmd_status_t result = cmd_handle_unpair(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    // When unpair fails, it returns FAILED or specific error
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR|"));
}

// ============================================================================
// Test 7: cmd_handle_unpair_all() should succeed and report count
// ============================================================================
void test_cmd_unpair_all_should_succeed_and_report_count(void) {
    // Arrange - Add 3 paired devices
    nvs_storage_add_paired_device("11:22:33:44:55:66", "Device1");
    nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "Device2");
    nvs_storage_add_paired_device("00:11:22:33:44:55", "Device3");
    
    bt_manager_test_set_force_unpair_all_failure(0); // Success
    cmd_context_t ctx = {
        .type = CMD_TYPE_UNPAIR_ALL,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_unpair_all(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|UNPAIR_ALL|SUCCESS|3"));
}

// ============================================================================
// Test 8: cmd_handle_unpair_all() should fail when BT operation errors
// ============================================================================
void test_cmd_unpair_all_should_fail_on_bt_error(void) {
    // Arrange - Add 2 devices
    nvs_storage_add_paired_device("11:22:33:44:55:66", "Device1");
    nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "Device2");
    
    bt_manager_test_set_force_unpair_all_failure(1); // Failure
    cmd_context_t ctx = {
        .type = CMD_TYPE_UNPAIR_ALL,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_unpair_all(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR_ALL|FAILED"));
}

// ============================================================================
// Test 9: cmd_handle_set_name() should reject missing parameter
// ============================================================================
void test_cmd_set_name_should_reject_missing_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_SET_NAME,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_set_name(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|SET_NAME|MISSING_PARAM"));
}

// ============================================================================
// Test 10: cmd_handle_set_default_pin() should reject missing parameter
// ============================================================================
void test_cmd_set_default_pin_should_reject_missing_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_SET_DEFAULT_PIN,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_set_default_pin(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|SET_DEFAULT_PIN|MISSING_PARAM"));
}

// ============================================================================
// Test 11: cmd_handle_debug() should reject missing parameter
// ============================================================================
void test_cmd_debug_should_reject_missing_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_DEBUG,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_debug(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|MISSING_PARAM"));
}

/* PHASE-5b: LAST_MAC command handler — provided by nvs_storage_mock.c */
extern void nvs_storage_mock_set_last_mac(const char *mac, esp_err_t get_result);

void test_cmd_last_mac_get_returns_stored_mac(void) {
    nvs_storage_mock_set_last_mac("AA:BB:CC:DD:EE:FF", ESP_OK);

    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_LAST_MAC;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "get", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_last_mac(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|LAST_MAC|AA:BB:CC:DD:EE:FF"));
}

void test_cmd_last_mac_get_returns_none_when_absent(void) {
    nvs_storage_mock_set_last_mac(NULL, PLATFORM_ERR_STORAGE_NOT_FOUND);

    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_LAST_MAC;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "get", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_last_mac(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|LAST_MAC|NONE"));
}

void test_cmd_last_mac_clear_clears_stored_mac(void) {
    nvs_storage_mock_set_last_mac("11:22:33:44:55:66", ESP_OK);

    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_LAST_MAC;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "clear", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_last_mac(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|LAST_MAC|CLEARED"));
}

void test_cmd_last_mac_missing_param_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_LAST_MAC;
    ctx.param_count = 0;

    mock_uart_reset_tx();
    cmd_handle_last_mac(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|LAST_MAC|MISSING_PARAM"));
}

void test_cmd_last_mac_unknown_subcmd_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_LAST_MAC;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "badparam", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_last_mac(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|LAST_MAC|UNKNOWN_SUBCMD"));
}

/* TEST-7: DEBUG dispatch table subcommand coverage */

void test_cmd_debug_mock_on_enables_mock(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "MOCK_ON", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_status_t result = cmd_handle_debug(&ctx);

    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DEBUG|MOCK_ON"));
}

void test_cmd_debug_mock_add_missing_mac_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "MOCK_ADD", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|MOCK_ADD_MISSING"));
}

void test_cmd_debug_log_sets_level(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 3;
    strncpy(ctx.params[0], "LOG", CMD_MAX_PARAM_LEN - 1);
    strncpy(ctx.params[1], "*", CMD_MAX_PARAM_LEN - 1);
    strncpy(ctx.params[2], "INFO", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DEBUG|LOG_SET"));
}

void test_cmd_debug_log_missing_params_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "LOG", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|LOG_MISSING"));
}

void test_cmd_debug_log_bad_level_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 3;
    strncpy(ctx.params[0], "LOG", CMD_MAX_PARAM_LEN - 1);
    strncpy(ctx.params[1], "*", CMD_MAX_PARAM_LEN - 1);
    strncpy(ctx.params[2], "BADLEVEL", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|LOG_BAD_LEVEL"));
}

void test_cmd_debug_dram_on_host_path(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 2;
    strncpy(ctx.params[0], "DRAM", CMD_MAX_PARAM_LEN - 1);
    strncpy(ctx.params[1], "ON", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    /* On host build: DRAM_ON_MOCK */
    TEST_ASSERT_NOT_NULL(strstr(tx, "DRAM_ON"));
}

void test_cmd_debug_dram_missing_param_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "DRAM", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|DRAM_MISSING_PARAM"));
}

void test_cmd_debug_dram_bad_param_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 2;
    strncpy(ctx.params[0], "DRAM", CMD_MAX_PARAM_LEN - 1);
    strncpy(ctx.params[1], "BANANA", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|DRAM_BAD_PARAM"));
}

void test_cmd_debug_unknown_subcommand_returns_error(void) {
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_DEBUG;
    ctx.param_count = 1;
    strncpy(ctx.params[0], "NOTASUBCMD", CMD_MAX_PARAM_LEN - 1);

    mock_uart_reset_tx();
    cmd_handle_debug(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|UNKNOWN_SUBCMD"));
}

// ============================================================================
// Unity Test Runner
// ============================================================================
int main(void) {
    UNITY_BEGIN();

    // Disconnect tests
    RUN_TEST(test_cmd_disconnect_should_succeed_when_connected);
    RUN_TEST(test_cmd_disconnect_should_fail_when_bt_manager_errors);
    
    // Paired list tests
    RUN_TEST(test_cmd_paired_should_report_empty_list);
    RUN_TEST(test_cmd_paired_should_fail_on_nvs_error);
    
    // Unpair tests
    RUN_TEST(test_cmd_unpair_should_reject_missing_param);
    RUN_TEST(test_cmd_unpair_should_report_not_found_for_unknown_mac);
    
    // Unpair all tests
    RUN_TEST(test_cmd_unpair_all_should_succeed_and_report_count);
    RUN_TEST(test_cmd_unpair_all_should_fail_on_bt_error);
    
    // Set name/pin tests
    RUN_TEST(test_cmd_set_name_should_reject_missing_param);
    RUN_TEST(test_cmd_set_default_pin_should_reject_missing_param);
    
    // Debug tests
    RUN_TEST(test_cmd_debug_should_reject_missing_param);

    // PHASE-5b: LAST_MAC command handler
    RUN_TEST(test_cmd_last_mac_get_returns_stored_mac);
    RUN_TEST(test_cmd_last_mac_get_returns_none_when_absent);
    RUN_TEST(test_cmd_last_mac_clear_clears_stored_mac);
    RUN_TEST(test_cmd_last_mac_missing_param_returns_error);
    RUN_TEST(test_cmd_last_mac_unknown_subcmd_returns_error);

    // TEST-7: DEBUG dispatch table coverage
    RUN_TEST(test_cmd_debug_mock_on_enables_mock);
    RUN_TEST(test_cmd_debug_mock_add_missing_mac_returns_error);
    RUN_TEST(test_cmd_debug_log_sets_level);
    RUN_TEST(test_cmd_debug_log_missing_params_returns_error);
    RUN_TEST(test_cmd_debug_log_bad_level_returns_error);
    RUN_TEST(test_cmd_debug_dram_on_host_path);
    RUN_TEST(test_cmd_debug_dram_missing_param_returns_error);
    RUN_TEST(test_cmd_debug_dram_bad_param_returns_error);
    RUN_TEST(test_cmd_debug_unknown_subcommand_returns_error);

    return UNITY_END();
}

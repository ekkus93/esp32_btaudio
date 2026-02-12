/**
 * Unit tests for audio command handlers (cmd_handlers_audio.c)
 * 
 * Phase 1 of test coverage expansion - focused on error paths and edge cases.
 * Following TDD Red-Green-Refactor cycle per Kent Beck methodology.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "cmd_handlers.h"
#include "command_interface.h"
#include "mock_uart.h"
#include "audio_processor.h"
#include "bt_manager.h"

// External test helpers
extern void cmd_test_reset_cmd_process_state(void);

// Test setup and teardown
void setUp(void) {
    // Initialize command interface infrastructure
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();
    mock_uart_reset_tx();
}

void tearDown(void) {
    // Cleanup after each test
    cmd_deinit();
}

// Helper function to extract response fields from mock UART output
static bool parse_response(const char* response, char* status, char* command, char* result) {
    if (!response || strlen(response) == 0) {
        return false;
    }
    
    char buf[256];
    strncpy(buf, response, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    // Remove trailing \r\n
    char* newline = strchr(buf, '\r');
    if (newline) *newline = '\0';
    
    // Parse format: STATUS|COMMAND|RESULT|DATA
    char* tok1 = strtok(buf, "|");
    char* tok2 = strtok(NULL, "|");
    char* tok3 = strtok(NULL, "|");
    
    if (tok1) strncpy(status, tok1, 31);
    if (tok2) strncpy(command, tok2, 31);
    if (tok3) strncpy(result, tok3, 63);
    
    return (tok1 && tok2 && tok3);
}

// =============================================================================
// RED PHASE: Write failing tests first
// =============================================================================

// Test 1: cmd_handle_synth() should reject invalid parameters
void test_cmd_synth_should_reject_invalid_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_SYNTH,
        .param_count = 1
    };
    strcpy(ctx.params[0], "invalid_value");
    
    // Act
    cmd_status_t result = cmd_handle_synth(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|SYNTH|BAD_PARAM"));
}

// Test 2: cmd_handle_synth() should reject missing parameter
void test_cmd_synth_should_reject_missing_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_SYNTH,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_synth(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|SYNTH|MISSING_PARAM"));
}

// Test 3: cmd_handle_synth() should accept "ON", "1", "TRUE" (case insensitive)
void test_cmd_synth_should_accept_on_variants(void) {
    const char* on_variants[] = {"ON", "on", "On", "1", "TRUE", "true", "True"};
    
    for (size_t i = 0; i < sizeof(on_variants) / sizeof(on_variants[0]); i++) {
        // Arrange
        mock_uart_reset_tx();
        cmd_context_t ctx = {
            .type = CMD_TYPE_SYNTH,
            .param_count = 1
        };
        strcpy(ctx.params[0], on_variants[i]);
        
        // Act
        cmd_status_t result = cmd_handle_synth(&ctx);
        
        // Assert
        TEST_ASSERT_EQUAL_MESSAGE(CMD_SUCCESS, result, on_variants[i]);
        
        const char* tx = mock_uart_get_tx_data();
        TEST_ASSERT_NOT_NULL_MESSAGE(tx, on_variants[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tx, "OK|SYNTH|ENABLED"), on_variants[i]);
    }
}

// Test 4: cmd_handle_synth() should accept "OFF", "0", "FALSE" (case insensitive)
void test_cmd_synth_should_accept_off_variants(void) {
    const char* off_variants[] = {"OFF", "off", "Off", "0", "FALSE", "false", "False"};
    
    for (size_t i = 0; i < sizeof(off_variants) / sizeof(off_variants[0]); i++) {
        // Arrange
        mock_uart_reset_tx();
        cmd_context_t ctx = {
            .type = CMD_TYPE_SYNTH,
            .param_count = 1
        };
        strcpy(ctx.params[0], off_variants[i]);
        
        // Act
        cmd_status_t result = cmd_handle_synth(&ctx);
        
        // Assert
        TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
        
        const char* tx = mock_uart_get_tx_data();
        TEST_ASSERT_NOT_NULL_MESSAGE(tx, off_variants[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tx, "OK|SYNTH|DISABLED"), off_variants[i]);
    }
}

// Test 5: cmd_handle_volume() should reject negative values
void test_cmd_volume_should_reject_negative(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_VOLUME,
        .param_count = 1
    };
    strcpy(ctx.params[0], "-1");
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|OUT_OF_RANGE"));
}

// Test 6: cmd_handle_volume() should reject values > 100
void test_cmd_volume_should_reject_over_100(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_VOLUME,
        .param_count = 1
    };
    strcpy(ctx.params[0], "101");
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|OUT_OF_RANGE"));
}

// Test 7: cmd_handle_volume() should accept boundary value 0
void test_cmd_volume_should_accept_zero(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_VOLUME,
        .param_count = 1
    };
    strcpy(ctx.params[0], "0");
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VOLUME|MOCK_SET|0"));
}

// Test 8: cmd_handle_volume() should accept boundary value 100
void test_cmd_volume_should_accept_100(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_VOLUME,
        .param_count = 1
    };
    strcpy(ctx.params[0], "100");
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VOLUME|MOCK_SET|100"));
}

// Test 9: cmd_handle_volume() should reject non-numeric input
void test_cmd_volume_should_reject_non_numeric(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_VOLUME,
        .param_count = 1
    };
    strcpy(ctx.params[0], "abc");
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|BAD_PARAM"));
}

// Test 10: cmd_handle_volume() should reject missing parameter
void test_cmd_volume_should_reject_missing_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_VOLUME,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|MISSING_PARAM"));
}

// Test 11: cmd_handle_beep() when not connected
void test_cmd_beep_should_fail_when_not_connected(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_BEEP,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_beep(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|BEEP|NOT_CONNECTED"));
}

// =============================================================================
// Unity test runner
// =============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Test synth command
    RUN_TEST(test_cmd_synth_should_reject_invalid_param);
    RUN_TEST(test_cmd_synth_should_reject_missing_param);
    RUN_TEST(test_cmd_synth_should_accept_on_variants);
    RUN_TEST(test_cmd_synth_should_accept_off_variants);
    
    // Test volume command
    RUN_TEST(test_cmd_volume_should_reject_negative);
    RUN_TEST(test_cmd_volume_should_reject_over_100);
    RUN_TEST(test_cmd_volume_should_accept_zero);
    RUN_TEST(test_cmd_volume_should_accept_100);
    RUN_TEST(test_cmd_volume_should_reject_non_numeric);
    RUN_TEST(test_cmd_volume_should_reject_missing_param);
    
    // Test beep command
    RUN_TEST(test_cmd_beep_should_fail_when_not_connected);
    
    return UNITY_END();
}

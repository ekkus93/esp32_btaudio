// Host-side tests for edge cases in pairing command handlers
#include "unity.h"
#include "command_interface.h"
#include "mock_uart.h"
#include <string.h>

// mock_gap helpers
extern void mock_gap_reset(void);
extern const char* mock_gap_get_last_mac(void);

void setUp(void) {
    mock_gap_reset();
    mock_uart_init(115200);
}
void tearDown(void) {
    mock_gap_reset();
}

// CONFIRM_PIN without a pending mock should return NO_MOCK error
void test_confirm_pin_no_pending_returns_no_mock(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONFIRM_PIN ACCEPT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "ERR|CONFIRM_PIN|NO_MOCK") != NULL);
}

// ENTER_PIN without a pending mock should return NO_MOCK error
void test_enter_pin_no_pending_returns_no_mock(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    // Call without params; host path should report NO_MOCK when no pending pairing
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("ENTER_PIN", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "ERR|ENTER_PIN|NO_MOCK") != NULL);
}

// CONFIRM_PIN with an invalid MAC should return INVALID_MAC
void test_confirm_pin_invalid_mac_returns_invalid(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    // include invalid hex characters to force parse failure
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONFIRM_PIN AA:BB:CC:GG:HH:II ACCEPT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "ERR|CONFIRM_PIN|INVALID_MAC") != NULL);
}

// ENTER_PIN with an invalid MAC should return INVALID_MAC
void test_enter_pin_invalid_mac_returns_invalid(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("ENTER_PIN AA:BB:CC:GG:HH:II 0000", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "ERR|ENTER_PIN|INVALID_MAC") != NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_confirm_pin_no_pending_returns_no_mock);
    RUN_TEST(test_enter_pin_no_pending_returns_no_mock);
    RUN_TEST(test_confirm_pin_invalid_mac_returns_invalid);
    RUN_TEST(test_enter_pin_invalid_mac_returns_invalid);
    return UNITY_END();
}

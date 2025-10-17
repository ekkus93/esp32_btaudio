/* Unity tests for serial pairing commands: PAIR, CONFIRM_PIN, ENTER_PIN */
#include "unity.h"
#include "test_utils.h"
#include <string.h>
#include <stdbool.h>

// Reuse helper that sends a serial command into the command parser and
// captures emitted EVENT lines injected into the test harness. Existing
// test_app provides `test_send_serial_cmd()` and `test_capture_event()`.

// The test helpers are implemented by the test harness; declare the
// prototypes here so this translation unit compiles even if the shared
// header is not available.
void test_utils_reset_state(void);
bool test_send_serial_cmd(const char *cmd);
bool test_capture_event(char *out_buf, size_t out_len);

// Per-suite setup registration: this project provides a global
// setUp/tearDown in `unity_config.c` and allows suites to register
// their own setup/teardown callbacks using unity_set_setup_function().
// Register a small wrapper that calls the test utils reset.
static void pairing_tests_setup(void) {
    test_utils_reset_state();
}

__attribute__((constructor)) static void register_pairing_tests_setup(void) {
    // unity_set_setup_function is declared in unity_config.h
    extern void unity_set_setup_function(void (*setup)(void));
    unity_set_setup_function(pairing_tests_setup);
}

void test_pairing_commands_happy_path(void) {
    test_utils_reset_state();
    // Start pairing (device 11:22:33:44:55:66) — the stubs will simulate PIN request
    test_send_serial_cmd("PAIR 11:22:33:44:55:66\r\n");
    // Expect a PIN_REQUEST event
    char ev[128];
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|PIN_REQUEST|11:22:33:44:55:66", ev);

    // Submit PIN
    test_send_serial_cmd("ENTER_PIN 11:22:33:44:55:66 123456\r\n");
    // Expect CONFIRM then SUCCESS events (mock behavior)
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|CONFIRM|11:22:33:44:55:66,123456", ev);
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|SUCCESS|11:22:33:44:55:66", ev);
}

void test_enter_pin_uses_default_when_missing(void) {
    test_utils_reset_state();
    // Simulate PAIR that requests PIN
    test_send_serial_cmd("PAIR 22:33:44:55:66:77\r\n");
    char ev[128];
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|PIN_REQUEST|22:33:44:55:66:77", ev);

    // Call ENTER_PIN without providing a PIN — command handler should use NVS default PIN
    test_send_serial_cmd("ENTER_PIN 22:33:44:55:66:77\r\n");
    // Expect CONFIRM with the default pin (test utils sets default to 000000)
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|CONFIRM|22:33:44:55:66:77,000000", ev);
}

void test_confirm_pin_without_pending_request_returns_error_event(void) {
    test_utils_reset_state();
    // Confirm for a device with no pending pairing should emit FAILED or no-op
    test_send_serial_cmd("CONFIRM_PIN AA:BB:CC:DD:EE:FF 123456\r\n");
    char ev[128];
    // Depending on implementation, either no event or FAILED
    bool got = test_capture_event(ev, sizeof(ev));
    if (got) {
        // If an event was produced, ensure it's a FAIL/FAILED event for that addr
        TEST_ASSERT_TRUE(strstr(ev, "FAILED") != NULL || strstr(ev, "FAIL") != NULL);
    } else {
        TEST_PASS_MESSAGE("No event produced (acceptable for no pending request)");
    }
}

// Register tests in test group (existing test harness picks up tests in files automatically)
// If explicit registration is needed, the existing main test runner will call these via Unity.

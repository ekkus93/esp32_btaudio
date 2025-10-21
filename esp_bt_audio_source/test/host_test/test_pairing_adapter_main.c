#include "unity.h"

// Provide setUp/tearDown used by Unity
void setUp(void) {}
void tearDown(void) {}

// Provide a no-op unity_set_setup_function so the constructor in
// test_pairing_commands.c can call it during static init on host.
void unity_set_setup_function(void (*setup)(void)) { (void)setup; }

// Extern declarations of tests defined in test_pairing_commands.c
extern void test_pairing_commands_happy_path(void);
extern void test_enter_pin_uses_default_when_missing(void);
extern void test_confirm_pin_without_pending_request_returns_error_event(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pairing_commands_happy_path);
    RUN_TEST(test_enter_pin_uses_default_when_missing);
    RUN_TEST(test_confirm_pin_without_pending_request_returns_error_event);
    return UNITY_END();
}

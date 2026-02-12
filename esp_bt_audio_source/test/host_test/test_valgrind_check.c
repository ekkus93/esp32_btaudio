/**
 * test_valgrind_check.c - Memory error detection verification
 * 
 * This test verifies that memory error detection tools (Valgrind, AddressSanitizer)
 * work correctly when enabled via run_all_tests.py flags.
 * 
 * Expected behavior:
 * - Normal run (no flags): All enabled tests pass
 * - With --valgrind: Detects memory leaks (exit code 1 if leak tests uncommented)
 * - With --asan: Detects memory leaks and buffer overflows (runtime abort if error tests uncommented)
 * 
 * Tool comparison:
 * - Valgrind: Slower (10-30x), most thorough, no recompilation needed
 * - AddressSanitizer: Faster (2-3x), good detection, requires rebuild with -fsanitize=address
 */

#include "unity.h"
#include <stdlib.h>
#include <string.h>

void setUp(void) {
    // Unity setUp - runs before each test
}

void tearDown(void) {
    // Unity tearDown - runs after each test
}

/**
 * Test: Normal behavior (no leaks)
 * This test should pass with or without Valgrind
 */
void test_no_leak(void) {
    char *buffer = malloc(100);
    TEST_ASSERT_NOT_NULL(buffer);
    strcpy(buffer, "Hello, Valgrind!");
    free(buffer);  // Properly freed - no leak
}

/**
 * Test: Deliberate memory leak (DISABLED for normal runs)
 * This test should:
 * - Pass without --valgrind or --asan (Unity doesn't detect leaks)
 * - Fail with --valgrind (Valgrind detects leak and returns exit code 1)
 * - Fail with --asan (AddressSanitizer detects leak and aborts)
 * 
 * IMPORTANT: This test is commented out in main() to avoid false failures.
 * Uncomment it only for manual memory error detection testing.
 */
void test_intentional_leak(void) {
    char *leaked = malloc(42);
    TEST_ASSERT_NOT_NULL(leaked);
    strcpy(leaked, "This memory is intentionally leaked");
    // Deliberate leak: 'leaked' is never freed
    // Both Valgrind and AddressSanitizer should detect this
}

/**
 * Test: Buffer overflow (DISABLED for normal runs)
 * This test should:
 * - May crash or succeed unpredictably without sanitizers
 * - Fail with --asan (AddressSanitizer detects overflow immediately)
 * - May or may not be detected by Valgrind (depends on timing)
 * 
 * IMPORTANT: This test is commented out in main() to avoid false failures.
 * Uncomment it only for manual AddressSanitizer testing.
 */
void test_intentional_buffer_overflow(void) {
    char *buffer = malloc(10);
    TEST_ASSERT_NOT_NULL(buffer);
    // Deliberate overflow: write past the end of buffer
    strcpy(buffer, "This string is way too long for a 10-byte buffer!");
    free(buffer);
    // AddressSanitizer should catch the overflow before we get here
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_no_leak);
    
    // Intentional error tests are disabled for normal runs
    // Uncomment the lines below ONLY for manual testing:
    // 
    // For Valgrind or ASan leak detection:
    // RUN_TEST(test_intentional_leak);
    // 
    // For AddressSanitizer buffer overflow detection:
    // RUN_TEST(test_intentional_buffer_overflow);
    
    return UNITY_END();
}

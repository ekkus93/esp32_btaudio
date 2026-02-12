/**
 * test_valgrind_check.c - Quick test to verify Valgrind integration
 * 
 * This test deliberately leaks memory to verify that Valgrind correctly
 * detects and reports memory leaks when the --valgrind flag is used.
 * 
 * Expected behavior:
 * - Without --valgrind: Test passes (memory leak not detected)
 * - With --valgrind: Test fails with exit code 1 (memory leak detected)
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
 * - Pass without --valgrind (Unity doesn't detect leaks)
 * - Fail with --valgrind (Valgrind detects leak and returns exit code 1)
 * 
 * IMPORTANT: This test is commented out in main() to avoid false failures.
 * Uncomment it only for manual Valgrind integration testing.
 */
void test_intentional_leak_for_valgrind_check(void) {
    char *leaked = malloc(42);
    TEST_ASSERT_NOT_NULL(leaked);
    strcpy(leaked, "This memory is intentionally leaked");
    // Deliberate leak: 'leaked' is never freed
    // Valgrind should detect this and fail the test
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_no_leak);
    
    // Intentional leak test is disabled for normal runs
    // Uncomment the line below ONLY for manual Valgrind integration testing:
    // RUN_TEST(test_intentional_leak_for_valgrind_check);
    
    return UNITY_END();
}

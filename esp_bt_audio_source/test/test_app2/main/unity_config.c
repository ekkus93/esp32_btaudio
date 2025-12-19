#include "unity_config.h"
#include <stddef.h>  // Add this to get NULL definition
#include "unity.h"
#include "esp_log.h"

// Global function pointers that can be modified by test suites
void (*unity_setup_function)(void) = NULL;
void (*unity_teardown_function)(void) = NULL;

void unity_set_setup_function(void (*setup)(void)) {
    unity_setup_function = setup;
}

void unity_set_teardown_function(void (*teardown)(void)) {
    unity_teardown_function = teardown;
}

// These functions are called by the Unity framework before and after each test
void setUp(void) {
    // Call the registered setup function if there is one
    if (unity_setup_function != NULL) {
        unity_setup_function();
    }
}

void tearDown(void) {
    // Call the registered teardown function if there is one
    if (unity_teardown_function != NULL) {
        unity_teardown_function();
    }
}

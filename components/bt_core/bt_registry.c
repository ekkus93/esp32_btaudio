#include "bt_interface.h"
#include "bt_registry.h"

// Declarations for the implementations
extern bt_interface_t bt_real_implementation;
extern bt_interface_t bt_mock_implementation;

// Current active implementation
static bt_interface_t* current_implementation = NULL;

// Get current implementation
bt_interface_t* bt_get_implementation(void) {
    // Default to real implementation if none set
    if (current_implementation == NULL) {
        bt_use_real_implementation();
    }
    return current_implementation;
}

// Register custom implementation
void bt_register_implementation(bt_interface_t* implementation) {
    if (implementation != NULL) {
        current_implementation = implementation;
    }
}

// Use mock implementation
void bt_use_mock_implementation(void) {
    current_implementation = &bt_mock_implementation;
}

// Use real implementation
void bt_use_real_implementation(void) {
    current_implementation = &bt_real_implementation;
}

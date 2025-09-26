#include "test_config.h"
#include "unity.h"
#include "bt_source.h"

// Fix the implementation - returning the correct type and using only valid members
bt_interface_t mock_interface = {
    .priv = NULL,
};

// Fix function to return correct type
bt_interface_t* get_bt_implementation(void) {
    return &mock_interface;
}

// Other functions unchanged

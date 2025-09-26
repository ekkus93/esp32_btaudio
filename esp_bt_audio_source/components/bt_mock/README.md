# Bluetooth Mock Component

## Overview

The `bt_mock` component provides a mock implementation of Bluetooth functionality for testing ESP32 Bluetooth audio applications. It allows testing of Bluetooth-dependent code without requiring actual Bluetooth hardware or connections.

## API Reference

### Initialization and Cleanup

```c
// Initialize the mock system
esp_err_t bt_mock_init(void);

// Clean up resources
void bt_mock_cleanup(void);

// Reset all state - THIS FUNCTION IS NOW PROPERLY NAMED
void bt_mock_reset(void);
```

### Device Management

```c
// Add a test device with specific properties
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type);

// Add a device as paired
esp_err_t bt_mock_add_paired_device(bt_device_t* device);

// Get number of mock devices
int bt_mock_devices_count(void);

// Get information about a specific device
esp_err_t bt_mock_get_device(int index, bt_device_t* device);

// Get count of paired devices
uint16_t bt_mock_get_paired_device_count(void);

// Get paired devices
esp_err_t bt_mock_get_paired_devices(bt_device_t *devices, uint16_t max_count, uint16_t *actual_count);
```

### Pairing Simulation

```c
// Configure whether SSP is supported
void bt_mock_set_ssp_supported(bool supported);

// Simulate PIN-based pairing failure
void bt_mock_simulate_pin_failure(void);

// Simulate pairing timeout
void bt_mock_simulate_pairing_timeout(void);

// Confirm SSP pairing - CORRECT FUNCTION NAME
esp_err_t bt_ssp_confirm(bool confirm);
```

### Searching and Filtering

```c
// Check if devices match a filter - CORRECT FUNCTION NAME
bool bt_filter_has_matches(int timeout);

// Start scanning with timeout
esp_err_t bt_scan(uint32_t duration_s);
```

## Using the Mock Component in Tests

The bt_mock component is designed to integrate with unit tests. Here's how to use it:

1. Include the component in your test project
2. Include the required headers in your test files
3. Initialize the mock system at the beginning of your test
4. Add test devices as needed
5. Call your Bluetooth functions which will now use the mock implementation
6. Check the results against expected values
7. Clean up the mock system after testing

### Example Test Case

```c
#include "unity.h"
#include "bt_mock.h"
#include "bt_source.h"

TEST_CASE("Test BT device scanning", "[bluetooth][scan]")
{
    // Initialize mock system
    bt_mock_init();
    
    // Add test devices
    bt_mock_add_test_device("11:22:33:44:55:66", "TestSpeaker", BT_DEVICE_TYPE_AUDIO);
    bt_mock_add_test_device("AA:BB:CC:DD:EE:FF", "TestHeadphones", BT_DEVICE_TYPE_AUDIO);
    
    // Run scanning function that will use mock implementation
    esp_err_t ret = bt_scan(5);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check if filter finds matches - CORRECT FUNCTION CALL
    bool has_matches = bt_filter_has_matches(5);
    TEST_ASSERT_TRUE(has_matches);
    
    // Clean up
    bt_mock_cleanup();
}
```

## Pairing Tests Example

```c
TEST_CASE("Test SSP confirmation", "[bluetooth][pairing]")
{
    bt_mock_init();
    
    // Add a device
    bt_mock_add_test_device("11:22:33:44:55:66", "TestDevice", BT_DEVICE_TYPE_AUDIO);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Confirm SSP pairing request - USING CORRECT FUNCTION NAME
    ret = bt_ssp_confirm(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify device is paired
    TEST_ASSERT_TRUE(bt_is_device_paired("11:22:33:44:55:66"));
    
    bt_mock_cleanup();
}
```

## Implementation Details

The mock implementation includes:

1. Device discovery simulation
2. Connection state management
3. Pairing simulation (PIN, SSP, Just Works)
4. Audio streaming state tracking
5. Error condition simulation

## Recent Changes and Fixes

The following issues were fixed in the component:

1. **Function Name Consistency**: Standardized function names to use the `bt_mock_` prefix for all mock-specific functions, with some exceptions for functions that must match the real API exactly (`bt_filter_has_matches`, `bt_ssp_confirm`).

2. **Return Type Consistency**: Fixed `bt_mock_add_device` to return `esp_err_t` instead of `void` to match its declaration in the header file.

3. **Implementation Completeness**: Implemented previously missing functions:
   - `bt_mock_reset()`
   - `bt_filter_has_matches()`
   - `bt_ssp_confirm()`  
   - `bt_mock_add_paired_device()`
   - `bt_mock_get_paired_device_count()`

4. **Documentation Updates**: Updated this README with correct function names and usage patterns.

## Why Mock Functions Have Been Confusing

Several factors have contributed to the confusion with mock functions:

1. **Inconsistent Naming Convention**: Some functions used `bt_mock_` prefix while others used `bt_test_`, `bt_`, or other prefixes. This made it hard to know what functions belonged to the mock implementation.

2. **Duplicate Implementations**: The same functionality was sometimes implemented in multiple places, leading to function redefinition errors.

3. **Interface Mismatch**: Some functions were declared with one signature but implemented with another (e.g., return types not matching).

4. **API Boundary Confusion**: The boundary between the mock interface and the API it was mocking wasn't clearly defined, causing some functions to appear in both.

5. **Missing Documentation**: Without complete documentation, it wasn't clear which functions were available or how they should be used.

These issues are now addressed with consistent naming, proper implementation, and updated documentation.

## Naming Conventions

To maintain clarity in the codebase, the following naming conventions are used:

1. **Mock-specific functions**: All functions that are part of the mock implementation use the `bt_mock_` prefix.
   Examples: `bt_mock_init()`, `bt_mock_reset()`, `bt_mock_add_test_device()`

2. **Emulated API functions**: Functions that emulate the real Bluetooth API for testing use the same names 
   as the real API to serve as drop-in replacements.
   Examples: `bt_filter_has_matches()`, `bt_ssp_confirm()`

3. **Backward Compatibility**: For compatibility with existing tests, some aliases are provided:
   Example: `bt_test_reset` → `bt_mock_reset`

Always use the standard `bt_mock_` prefix for new functions to maintain consistency.

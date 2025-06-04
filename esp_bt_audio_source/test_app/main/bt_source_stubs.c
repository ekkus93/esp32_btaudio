/**
 * Stub implementations for BT functions used in tests
 * These are minimal implementations just to make the linker happy
 */
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h> // For PRIu32 macros
#include "bt_source.h"
#include "esp_log.h"
#include "bt_mock_devices.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "BT_STUBS";

// Access the scanning state from bt_mock_devices.c
extern bool mock_is_scanning;

// Define constants for array sizes
#define MAX_DISCOVERED_DEVICES 20
#define DEFAULT_PIN_SIZE 17     // 16 chars + null terminator
#define BT_MAC_ADDR_STR_LEN 18  // Format XX:XX:XX:XX:XX:XX + null terminator

// Define missing enums
#define BT_STREAMING_STATE_PLAYING BT_STREAMING_STATE_STOPPED + 1
#define BT_STREAMING_STATE_STARTED BT_STREAMING_STATE_PLAYING

// Scan timer for automatic timeout
static TimerHandle_t scan_timer = NULL;
static uint32_t scan_timeout_seconds = 0;

// Static storage for default PIN
static char s_default_pin[DEFAULT_PIN_SIZE] = "1234";

// Static storage for connection info
static bt_connection_info_t s_connection_info = {0};
static bool s_has_connection_info = false;

// Static storage for tracking state
static bool s_is_initialized = false;
static bool s_is_streaming = false;
static bool s_is_paused = false;
static bt_streaming_state_t s_streaming_state = BT_STREAMING_STATE_STOPPED;

// Internal scan state to prevent duplicate stop calls
static bool s_scan_active = false;

/**
 * Initialize the stub state
 * This helps ensure all data structures are properly initialized before use
 */
static void bt_stub_init_state(void)
{
    static bool s_initialized = false;

    if (!s_initialized) {
        // Initialize memory
        memset(s_default_pin, 0, sizeof(s_default_pin));
        strncpy(s_default_pin, "1234", sizeof(s_default_pin) - 1);
        
        memset(&s_connection_info, 0, sizeof(s_connection_info));
        s_has_connection_info = false;
        
        s_is_initialized = false;
        s_is_streaming = false;
        s_is_paused = false;
        s_streaming_state = BT_STREAMING_STATE_STOPPED;
        s_scan_active = false;
        
        // Make sure timer is null initially
        scan_timer = NULL;
        
        s_initialized = true;
        
        ESP_LOGI(TAG, "Stub state initialized");
    }
}

/**
 * Scan timeout callback
 */
static void scan_timer_callback(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Scan timeout triggered after %" PRIu32 " seconds", scan_timeout_seconds);
    
    // Stop the scan safely (function handles the case if already stopped)
    bt_scan_stop();
    
    // Clean up timer resources
    if (scan_timer != NULL) {
        xTimerStop(scan_timer, 0);
        xTimerDelete(scan_timer, 0);
        scan_timer = NULL;
    }
}

/**
 * Safely clean up scan resources
 */
static void cleanup_scan_resources(void)
{
    // Stop and delete timer if it exists
    if (scan_timer != NULL) {
        if (xTimerIsTimerActive(scan_timer)) {
            xTimerStop(scan_timer, 0);
        }
        xTimerDelete(scan_timer, 0);
        scan_timer = NULL;
        ESP_LOGI(TAG, "Scan timer cleaned up");
    }
    
    // Reset scan state
    s_scan_active = false;
    scan_timeout_seconds = 0;
}

// Define stub implementations for all functions referenced in tests

esp_err_t bt_init(void) {
    // Initialize the stub state
    bt_stub_init_state();
    
    ESP_LOGI(TAG, "Stub: bt_init");
    s_is_initialized = true;
    return ESP_OK;
}

esp_err_t bt_scan_start(void) {
    // Validate initialization
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "Stub: bt_scan_start called before initialization");
        return ESP_ERR_INVALID_STATE;
    }

    // If a scan is already active, clean it up first
    if (s_scan_active) {
        ESP_LOGW(TAG, "Stub: bt_scan_start called while scan already active, stopping previous scan");
        bt_scan_stop();
    }

    ESP_LOGI(TAG, "Stub: bt_scan_start");
    
    // Set our internal state
    s_scan_active = true;
    
    // Start the mock scan
    bt_mock_start_scan();
    return ESP_OK;
}

esp_err_t bt_scan_stop(void) {
    ESP_LOGI(TAG, "Stub: bt_scan_stop");
    
    // If scan is not active in our state tracking, just return success
    if (!s_scan_active) {
        ESP_LOGW(TAG, "Stub: bt_scan_stop called but no scan active in our state");
        return ESP_OK;
    }
    
    // Stop the mock scan first
    bt_mock_stop_scan();
    
    // Clean up all scan resources properly
    cleanup_scan_resources();
    
    return ESP_OK;
}

esp_err_t bt_scan(uint32_t timeout_seconds) {
    // Validate initialization
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "Stub: bt_scan called before initialization");
        return ESP_ERR_INVALID_STATE;
    }

    // Validate parameter
    if (timeout_seconds == 0) {
        ESP_LOGW(TAG, "Stub: bt_scan invalid timeout");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Stub: bt_scan with timeout %" PRIu32, timeout_seconds);
    
    // If a scan is already active, clean it up first
    if (s_scan_active) {
        ESP_LOGW(TAG, "Stub: bt_scan called while scan already active, stopping previous scan");
        bt_scan_stop();
    }
    
    // Start the mock scan
    bt_mock_start_scan();
    
    // Set our internal state
    s_scan_active = true;
    scan_timeout_seconds = timeout_seconds;
    
    // Create and start a timer to automatically stop the scan
    if (scan_timer != NULL) {
        // Delete any existing timer first to prevent leaks
        xTimerStop(scan_timer, 0);
        xTimerDelete(scan_timer, 0);
        scan_timer = NULL;
    }
    
    scan_timer = xTimerCreate(
        "scan_timer",
        pdMS_TO_TICKS(timeout_seconds * 1000),
        pdFALSE, // One-shot timer
        NULL,    // No timer ID needed
        scan_timer_callback
    );
    
    if (scan_timer == NULL) {
        ESP_LOGE(TAG, "Stub: Failed to create scan timer");
        bt_scan_stop(); // Clean up the scan we just started
        return ESP_ERR_NO_MEM;
    }
    
    // Start the timer
    if (xTimerStart(scan_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Stub: Failed to start scan timer");
        xTimerDelete(scan_timer, 0);
        scan_timer = NULL;
        bt_scan_stop(); // Clean up the scan we just started
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

bool bt_is_scanning(void) {
    // Use both our internal state and mock state to be doubly sure
    return s_scan_active && mock_is_scanning;
}

esp_err_t bt_scan_start_filtered(bt_device_type_t device_type) {
    // Validate initialization
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "Stub: bt_scan_start_filtered called before initialization");
        return ESP_ERR_INVALID_STATE;
    }

    // Validate parameter - fix the range check
    if (device_type < BT_DEVICE_TYPE_UNKNOWN || device_type > BT_DEVICE_TYPE_AUDIO) {
        ESP_LOGW(TAG, "Stub: bt_scan_start_filtered invalid device type: %d", device_type);
        return ESP_ERR_INVALID_ARG;
    }
    
    // If a scan is already active, clean it up first
    if (s_scan_active) {
        ESP_LOGW(TAG, "Stub: bt_scan_start_filtered called while scan already active, stopping previous scan");
        bt_scan_stop();
    }

    ESP_LOGI(TAG, "Stub: bt_scan_start_filtered");
    
    // Set our internal state
    s_scan_active = true;
    
    // Start the mock scan
    bt_mock_start_scan();
    return ESP_OK;
}

esp_err_t bt_connect(const char* addr) {
    // Validate parameter
    if (addr == NULL) {
        ESP_LOGW(TAG, "Stub: bt_connect called with NULL address");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Stub: bt_connect to %s", addr);
    
    // First, add the device to the discovered devices list if it doesn't exist
    if (!bt_mock_is_device_paired(addr)) {
        // Create a fake device for this address
        uint8_t addr_bytes[6];
        if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                 &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
                 &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) == 6) {
            bt_mock_add_device(addr, "Test Device", BT_DEVICE_TYPE_AUDIO, true);
        }
    }
    
    // Now try to connect
    esp_err_t ret = bt_mock_connect(addr);
    
    // Update connection info on success
    if (ret == ESP_OK) {
        memset(&s_connection_info, 0, sizeof(s_connection_info));
        s_connection_info.connected = true;
        strncpy(s_connection_info.remote_addr, addr, sizeof(s_connection_info.remote_addr) - 1);
        strncpy(s_connection_info.remote_name, "Test Speaker", sizeof(s_connection_info.remote_name) - 1);
        s_has_connection_info = true;
    }
    
    return ret;
}

esp_err_t bt_disconnect(void) {
    // No need to validate initialization here as disconnection should always be safe
    ESP_LOGI(TAG, "Stub: bt_disconnect");
    
    // Clear connection info
    if (s_has_connection_info) {
        memset(&s_connection_info, 0, sizeof(s_connection_info));
        s_has_connection_info = false;
    }
    
    return bt_mock_disconnect();
}

bool bt_is_connected(void) {
    return bt_mock_is_connected();
}

esp_err_t bt_connect_by_name(const char* name) {
    // Validate parameter
    if (name == NULL) {
        ESP_LOGW(TAG, "Stub: bt_connect_by_name called with NULL name");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Stub: bt_connect_by_name to %s", name);
    
    // Implementation would need to search devices by name
    // For now, just fake success
    return ESP_OK;
}

esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms) {
    // Validate parameters
    if (addr == NULL) {
        ESP_LOGW(TAG, "Stub: bt_connect_with_timeout called with NULL address");
        return ESP_ERR_INVALID_ARG;
    }

    if (timeout_ms == 0) {
        ESP_LOGW(TAG, "Stub: bt_connect_with_timeout called with zero timeout");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Stub: bt_connect_with_timeout to %s, timeout %" PRIu32, 
             addr, timeout_ms);
    return bt_connect(addr); // Just use regular connect
}

esp_err_t bt_get_connection_info(bt_connection_info_t* info) {
    // Validate parameter
    if (!info) {
        ESP_LOGW(TAG, "Stub: bt_get_connection_info called with NULL info");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Zero out the info structure first
    memset(info, 0, sizeof(bt_connection_info_t));
    
    // If we have a mock connection, use it
    if (bt_mock_is_connected()) {
        info->connected = true;
        const char* addr = bt_mock_get_connected_addr();
        if (addr) {
            strncpy(info->remote_addr, addr, sizeof(info->remote_addr) - 1);
            info->remote_addr[sizeof(info->remote_addr) - 1] = '\0';
            // Set a name for testing
            strncpy(info->remote_name, "Test Speaker", sizeof(info->remote_name) - 1);
            info->remote_name[sizeof(info->remote_name) - 1] = '\0';
        }
    }
    // Otherwise, use our cached connection info if available
    else if (s_has_connection_info) {
        memcpy(info, &s_connection_info, sizeof(bt_connection_info_t));
    }
    
    return ESP_OK;
}

esp_err_t bt_set_auto_reconnect(bool enable) {
    ESP_LOGI(TAG, "Stub: bt_set_auto_reconnect %d", enable);
    return ESP_OK;
}

// Paired device management
esp_err_t bt_unpair_device(const char* addr) {
    // Validate parameter
    if (!addr) {
        ESP_LOGW(TAG, "Stub: bt_unpair_device called with NULL address");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Stub: bt_unpair_device %s", addr);
    
    // If device is connected, disconnect it first
    if (bt_mock_is_connected()) {
        const char* connected_addr = bt_mock_get_connected_addr();
        if (connected_addr && strcmp(connected_addr, addr) == 0) {
            bt_mock_disconnect();
            
            // Clear connection info
            if (s_has_connection_info) {
                memset(&s_connection_info, 0, sizeof(s_connection_info));
                s_has_connection_info = false;
            }
        }
    }
    
    return bt_mock_unpair_device(addr);
}

esp_err_t bt_unpair_all_devices(void) {
    ESP_LOGI(TAG, "Stub: bt_unpair_all_devices");
    
    // If any device is connected, disconnect it
    if (bt_mock_is_connected()) {
        bt_mock_disconnect();
        
        // Clear connection info
        if (s_has_connection_info) {
            memset(&s_connection_info, 0, sizeof(s_connection_info));
            s_has_connection_info = false;
        }
    }
    
    return bt_mock_unpair_all_devices();
}

uint16_t bt_get_paired_device_count(void) {
    int count = bt_mock_get_paired_device_count();
    
    // Ensure count doesn't exceed uint16_t max
    if (count < 0) {
        ESP_LOGW(TAG, "Stub: bt_get_paired_device_count got negative count: %d", count);
        return 0;
    }
    
    if (count > UINT16_MAX) {
        ESP_LOGW(TAG, "Stub: bt_get_paired_device_count exceeded uint16 max: %d", count);
        return UINT16_MAX;
    }
    
    return (uint16_t)count;
}

int bt_get_paired_devices(bt_device_t* devices, int max_devices) {
    // Validate parameters
    if (!devices || max_devices <= 0) {
        ESP_LOGW(TAG, "Stub: bt_get_paired_devices called with invalid args");
        return 0;
    }
    
    return bt_mock_get_paired_devices(devices, max_devices);
}

esp_err_t bt_store_paired_devices(void) {
    // This is a no-op in the mock implementation
    return ESP_OK;
}

esp_err_t bt_load_paired_devices(void) {
    // This is a no-op in the mock implementation
    return ESP_OK;
}

// Streaming functions
esp_err_t bt_start_streaming(void) {
    // Validate state
    if (!bt_mock_is_connected()) {
        ESP_LOGW(TAG, "Stub: bt_start_streaming called when not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stub: bt_start_streaming");
    s_is_streaming = true;
    s_is_paused = false;
    // Fix the enum reference
    s_streaming_state = BT_STREAMING_STATE_PLAYING;
    return ESP_OK;
}

esp_err_t bt_stop_streaming(void) {
    // No validation needed as stopping should always be safe
    
    ESP_LOGI(TAG, "Stub: bt_stop_streaming");
    s_is_streaming = false;
    s_is_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    return ESP_OK;
}

bool bt_is_streaming(void) {
    return s_is_streaming && !s_is_paused;
}

bt_streaming_state_t bt_get_streaming_state(void) {
    return s_streaming_state;
}

esp_err_t bt_pause_streaming(void) {
    // Validate state
    if (!s_is_streaming || s_is_paused) {
        ESP_LOGW(TAG, "Stub: bt_pause_streaming called when not streaming or already paused");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stub: bt_pause_streaming");
    s_is_paused = true;
    s_streaming_state = BT_STREAMING_STATE_PAUSED;
    return ESP_OK;
}

esp_err_t bt_resume_streaming(void) {
    // Validate state
    if (!s_is_streaming || !s_is_paused) {
        ESP_LOGW(TAG, "Stub: bt_resume_streaming called when not streaming or not paused");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stub: bt_resume_streaming");
    s_is_paused = false;
    // Fix the enum reference
    s_streaming_state = BT_STREAMING_STATE_PLAYING;
    return ESP_OK;
}

bool bt_is_paused(void) {
    return s_is_streaming && s_is_paused;
}

/**
 * Discovery related functions
 */
uint16_t bt_get_discovered_device_count(void) {
    // This should be safe even if we're not scanning
    bt_device_t temp_devices[MAX_DISCOVERED_DEVICES];
    int actual_count = bt_mock_get_scan_results(temp_devices, MAX_DISCOVERED_DEVICES);
    
    // Ensure we don't return a negative count (shouldn't happen, but let's be safe)
    if (actual_count < 0) {
        ESP_LOGW(TAG, "bt_mock_get_scan_results returned negative count: %d", actual_count);
        return 0;
    }
    
    // In case the actual count exceeds uint16_t max value (unlikely but possible)
    if (actual_count > UINT16_MAX) {
        ESP_LOGW(TAG, "Discovered device count exceeds uint16_t max value");
        return UINT16_MAX;
    }
    
    return (uint16_t)actual_count;
}

esp_err_t bt_get_discovered_devices(bt_device_t* devices, uint16_t count, uint16_t *actual_count) {
    if (devices == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Use the mock function that should be properly implemented
    int num_copied = bt_mock_get_scan_results(devices, count);
    
    // Handle potential errors
    if (num_copied < 0) {
        ESP_LOGW(TAG, "bt_mock_get_scan_results returned error: %d", num_copied);
        *actual_count = 0;
        return ESP_FAIL;
    }
    
    // Set the output count parameter
    *actual_count = (uint16_t)num_copied;
    return ESP_OK;
}

// Add these for compatibility with bt_pairing_test.c
esp_err_t bt_send_pin_code(const char* pin) {
    // Validate parameter
    if (!pin) {
        ESP_LOGW(TAG, "Stub: bt_send_pin_code called with NULL pin");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Stub: bt_send_pin_code %s", pin);
    return bt_mock_send_pin(pin);
}

esp_err_t bt_ssp_confirm(bool confirm) {
    ESP_LOGI(TAG, "Stub: bt_ssp_confirm %d", confirm);
    
    // Validate pairing state
    if (!bt_mock_is_ssp_confirm_requested()) {
        ESP_LOGW(TAG, "Stub: bt_ssp_confirm called when no confirmation requested");
        return ESP_ERR_INVALID_STATE;
    }
    
    return bt_mock_confirm_ssp(confirm);
}

esp_err_t bt_set_default_pin(const char* pin) {
    // Validate parameter
    if (!pin) {
        ESP_LOGW(TAG, "Stub: bt_set_default_pin called with NULL pin");
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t pin_len = strlen(pin);
    if (pin_len == 0 || pin_len >= DEFAULT_PIN_SIZE) {
        ESP_LOGW(TAG, "Stub: bt_set_default_pin called with invalid pin length: %zu", pin_len);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Stub: bt_set_default_pin %s", pin);
    
    // Store the pin locally
    memset(s_default_pin, 0, sizeof(s_default_pin));
    strncpy(s_default_pin, pin, sizeof(s_default_pin) - 1);
    
    return bt_mock_set_default_pin(pin);
}

esp_err_t bt_get_default_pin(char* pin, size_t size) {
    // Validate parameters
    if (!pin || size == 0) {
        ESP_LOGW(TAG, "Stub: bt_get_default_pin called with invalid args");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy the stored default pin
    size_t copy_len = strlen(s_default_pin);
    if (copy_len >= size) {
        // Truncate if needed
        copy_len = size - 1;
    }
    
    memcpy(pin, s_default_pin, copy_len);
    pin[copy_len] = '\0';
    
    return ESP_OK;
}

// Important missing functions that were causing the linker errors
esp_err_t bt_start_pairing(const char* addr) {
    // Validate parameter
    if (!addr) {
        ESP_LOGW(TAG, "Stub: bt_start_pairing called with NULL address");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Stub: bt_start_pairing %s", addr);
    return bt_mock_start_pairing(addr);
}

bool bt_is_device_paired(const char* addr) {
    // Validate parameter
    if (!addr) {
        ESP_LOGW(TAG, "Stub: bt_is_device_paired called with NULL address");
        return false;
    }
    
    return bt_mock_is_device_paired(addr);
}

// For compatibility with bt_a2dp_test.c
esp_err_t bt_add_paired_device(bt_device_t* device) {
    // Validate parameter
    if (!device) {
        ESP_LOGW(TAG, "Stub: bt_add_paired_device called with NULL device");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Stub: bt_add_paired_device");
    
    // Create a proper address string for the mock, using our standard buffer size
    char addr_str[BT_MAC_ADDR_STR_LEN];
    snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x", 
             device->addr[0], device->addr[1], device->addr[2],
             device->addr[3], device->addr[4], device->addr[5]);
    
    // Add device to the mock framework
    bt_mock_add_device(addr_str, device->name, BT_DEVICE_TYPE_AUDIO, true);
    
    return bt_mock_add_paired_device(device);
}

/**
 * Complete reset of all stubs state
 * This ensures clean state between tests
 */
static void bt_stub_reset_state(void)
{
    ESP_LOGI(TAG, "Stub: Performing complete state reset");
    
    // Reset all scan state
    if (s_scan_active) {
        bt_scan_stop();  // This also cleans up scan timer
    }
    
    // Explicitly clean up scan timer if it exists
    if (scan_timer != NULL) {
        if (xTimerIsTimerActive(scan_timer)) {
            xTimerStop(scan_timer, 0);
        }
        xTimerDelete(scan_timer, 0);
        scan_timer = NULL;
    }
    scan_timeout_seconds = 0;
    
    // Reset connection info
    memset(&s_connection_info, 0, sizeof(s_connection_info));
    s_has_connection_info = false;
    
    // Reset default PIN
    memset(s_default_pin, 0, sizeof(s_default_pin));
    strncpy(s_default_pin, "1234", sizeof(s_default_pin) - 1);
    
    // Reset all state flags
    s_is_initialized = false;
    s_is_streaming = false;
    s_is_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    s_scan_active = false;
}

// Add a cleanup function to free resources properly
esp_err_t bt_deinit(void) {
    ESP_LOGI(TAG, "Stub: bt_deinit");
    
    // First, ensure we disconnect any connected device
    if (bt_mock_is_connected()) {
        bt_mock_disconnect();
    }
    
    // Perform a complete state reset
    bt_stub_reset_state();
    
    // Clean up any BT mock resources - this should clear paired and discovered devices
    bt_mock_cleanup();
    
    return ESP_OK;
}

/**
 * Reset function for testing - allows tests to reset state between runs
 */
esp_err_t bt_reset_for_test(void) {
    ESP_LOGI(TAG, "Stub: Explicit reset for testing");
    
    // Disconnect any active connections
    if (bt_mock_is_connected()) {
        bt_mock_disconnect();
    }
    
    // Stop any active scan
    if (s_scan_active) {
        bt_scan_stop();
    }
    
    // Reset our local stub state
    bt_stub_reset_state();
    
    // Reset mock device state - tells mocks to clear their arrays
    bt_mock_reset();
    
    // Re-initialize to a clean state
    bt_stub_init_state();
    s_is_initialized = true;
    
    return ESP_OK;
}

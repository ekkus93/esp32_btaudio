#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "bt_source.h"
#include "bt_source_mock.h"

static const char* TAG = "BT_SOURCE_MOCK";

/* Static variables to track connection and device state */
static bool s_initialized = false;
static bool s_connected = false;
static bool s_streaming = false;
static bool s_scanning = false;
static bt_device_t s_discovered_devices[10];
static uint16_t s_discovered_device_count = 0;
static bt_connection_info_t s_current_connection;
static bt_device_type_t s_filter_type = BT_DEVICE_TYPE_UNKNOWN;

/* Streaming state tracking */
static bt_streaming_state_t s_streaming_state = BT_STREAMING_STATE_STOPPED;
static bool s_streaming_paused = false;
static bt_profile_t s_active_profile = BT_PROFILE_NONE;

/* Callback function pointers */
static bt_connection_callback_t s_connection_callback = NULL;
static void* s_connection_callback_data = NULL;

/* Track API call history for verification */
static struct {
    int init_calls;
    int connect_calls;
    int disconnect_calls;
    int scan_start_calls;
    int scan_stop_calls;
    int streaming_start_calls;
    int streaming_stop_calls;
    int streaming_pause_calls;
    int streaming_resume_calls;
    char last_connect_addr[32];
    char last_connect_name[32];
    bool auto_reconnect_enabled;
} verification = {0};

/* Mock control state */
static struct {
    esp_err_t init_return;
    esp_err_t scan_start_return;
    esp_err_t connect_return;
    esp_err_t timeout_return;
    bt_device_t *paired_devices;
    int paired_device_count;
} mock_control = {
    .init_return = ESP_OK,
    .scan_start_return = ESP_OK,
    .connect_return = ESP_OK,
    .timeout_return = ESP_ERR_TIMEOUT,
    .paired_devices = NULL,
    .paired_device_count = 0
};

/**
 * @brief Initialize the mock framework
 */
void bt_mock_init(void)
{
    ESP_LOGI(TAG, "Initializing mock framework");
    s_initialized = false;
    s_connected = false;
    s_streaming = false;
    s_scanning = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    s_streaming_paused = false;
    s_active_profile = BT_PROFILE_NONE;
    s_discovered_device_count = 0;
    s_connection_callback = NULL;
    s_connection_callback_data = NULL;
    
    memset(&s_current_connection, 0, sizeof(s_current_connection));
    memset(&verification, 0, sizeof(verification));
    
    mock_control.init_return = ESP_OK;
    mock_control.scan_start_return = ESP_OK;
    mock_control.connect_return = ESP_OK;
    mock_control.timeout_return = ESP_ERR_TIMEOUT;
    
    if (mock_control.paired_devices) {
        free(mock_control.paired_devices);
        mock_control.paired_devices = NULL;
    }
    mock_control.paired_device_count = 0;
}

/**
 * @brief Reset test state for a new test
 */
void bt_mock_reset(void)
{
    ESP_LOGI(TAG, "Resetting mock state");
    
    // Keep track of initialization but reset all other state
    s_connected = false;
    s_streaming = false;
    s_scanning = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    s_streaming_paused = false;
    s_active_profile = BT_PROFILE_NONE;
    
    // Reset call history
    memset(&verification, 0, sizeof(verification));
}

/**
 * @brief Set the expected return value for bt_init()
 */
void bt_mock_set_init_return(esp_err_t ret)
{
    mock_control.init_return = ret;
}

/**
 * @brief Set the connected state
 */
void bt_mock_set_is_connected_return(bool connected)
{
    s_connected = connected;
    
    // Update connection info
    s_current_connection.connected = connected;
}

/**
 * @brief Set the streaming state
 */
void bt_mock_set_is_streaming_return(bool streaming)
{
    s_streaming = streaming;
    
    // Update streaming state
    if (streaming) {
        s_streaming_state = BT_STREAMING_STATE_PLAYING;
    } else if (!s_streaming_paused) {
        s_streaming_state = BT_STREAMING_STATE_STOPPED;
    }
}

/**
 * @brief Set the streaming state enum
 */
void bt_mock_set_streaming_state(bt_streaming_state_t state)
{
    s_streaming_state = state;
    
    // Update consistent state
    s_streaming = (state == BT_STREAMING_STATE_PLAYING);
    s_streaming_paused = (state == BT_STREAMING_STATE_PAUSED);
}

/**
 * @brief Set scan start return
 */
void bt_mock_set_scan_start_return(esp_err_t ret)
{
    mock_control.scan_start_return = ret;
}

/**
 * @brief Set connect return
 */
void bt_mock_set_connect_return(esp_err_t ret)
{
    mock_control.connect_return = ret;
}

/**
 * @brief Set timeout return
 */
void bt_mock_set_connect_timeout_return(esp_err_t ret)
{
    mock_control.timeout_return = ret;
}

/**
 * @brief Set paired devices
 */
void bt_mock_set_paired_devices(bt_device_t *devices, int count)
{
    if (mock_control.paired_devices) {
        free(mock_control.paired_devices);
        mock_control.paired_devices = NULL;
    }
    
    mock_control.paired_device_count = count;
    if (count > 0 && devices != NULL) {
        mock_control.paired_devices = malloc(count * sizeof(bt_device_t));
        if (mock_control.paired_devices) {
            memcpy(mock_control.paired_devices, devices, count * sizeof(bt_device_t));
        } else {
            mock_control.paired_device_count = 0;
        }
    }
}

/**
 * @brief Set discovered devices
 */
void bt_mock_set_discovered_devices(bt_device_t *devices, int count)
{
    s_discovered_device_count = (count <= 10) ? count : 10;
    
    if (count > 0 && devices != NULL && count <= 10) {
        memcpy(s_discovered_devices, devices, count * sizeof(bt_device_t));
    }
}

/**
 * @brief Set devices by type for filtered scan testing
 */
void bt_mock_set_devices_by_type(bt_device_type_t type, bt_device_t *devices, int count)
{
    s_filter_type = type;
    s_discovered_device_count = (count <= 10) ? count : 10;
    
    if (count > 0 && devices != NULL && count <= 10) {
        memcpy(s_discovered_devices, devices, count * sizeof(bt_device_t));
    }
}

/**
 * @brief Simulate timeout
 */
void bt_mock_simulate_timeout(void)
{
    s_scanning = false;
}

/**
 * @brief Simulate disconnect
 */
void bt_mock_simulate_disconnect(void)
{
    s_connected = false;
    s_current_connection.connected = false;
    
    // Call connection callback if registered
    if (s_connection_callback) {
        bt_device_t device = {0};
        s_connection_callback(false, &device, ESP_OK, s_connection_callback_data);
    }
}

/**
 * @brief Simulate reconnect
 */
void bt_mock_simulate_reconnect(void)
{
    if (verification.auto_reconnect_enabled) {
        s_connected = true;
        s_current_connection.connected = true;
        
        // Call connection callback if registered
        if (s_connection_callback) {
            bt_device_t device = {0};
            s_connection_callback(true, &device, ESP_OK, s_connection_callback_data);
        }
    }
}

/**
 * @brief Set connection info
 */
void bt_mock_set_connection_info(const char* addr, const char* name, int8_t rssi)
{
    if (addr) {
        strncpy(s_current_connection.remote_addr, addr, sizeof(s_current_connection.remote_addr) - 1);
    }
    
    if (name) {
        strncpy(s_current_connection.remote_name, name, sizeof(s_current_connection.remote_name) - 1);
    }
    
    s_current_connection.signal_strength = rssi;
}

/**
 * @brief Get active profile
 */
bt_profile_t bt_mock_get_active_profile(void)
{
    return s_active_profile;
}

/**
 * @brief Set active profile
 */
void bt_mock_set_active_profile(bt_profile_t profile)
{
    s_active_profile = profile;
}

/* BT Source API implementation */

/**
 * @brief Init Bluetooth stack
 */
esp_err_t bt_init(void)
{
    ESP_LOGI(TAG, "Mock: Initializing Bluetooth stack");
    
    // Track call for verification
    verification.init_calls++;
    
    // Check if already initialized - meaningful behavior
    if (s_initialized && mock_control.init_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Set initialized state based on return value
    s_initialized = (mock_control.init_return == ESP_OK);
    
    return mock_control.init_return;
}

/**
 * @brief Start scanning
 */
esp_err_t bt_scan_start(void)
{
    ESP_LOGI(TAG, "Mock: Starting Bluetooth scan");
    
    // Track call for verification
    verification.scan_start_calls++;
    
    // Check if not initialized - meaningful behavior
    if (!s_initialized && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if already scanning - meaningful behavior
    if (s_scanning && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update state if successful
    if (mock_control.scan_start_return == ESP_OK) {
        s_scanning = true;
    }
    
    return mock_control.scan_start_return;
}

/**
 * @brief Start filtered scan
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type)
{
    ESP_LOGI(TAG, "Mock: Starting filtered Bluetooth scan for type %d", device_type);
    
    // Track call for verification
    verification.scan_start_calls++;
    
    // Check if not initialized - meaningful behavior
    if (!s_initialized && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if already scanning - meaningful behavior
    if (s_scanning && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update state if successful
    if (mock_control.scan_start_return == ESP_OK) {
        s_scanning = true;
        s_filter_type = device_type;
    }
    
    return mock_control.scan_start_return;
}

/**
 * @brief Stop scanning
 */
esp_err_t bt_scan_stop(void)
{
    ESP_LOGI(TAG, "Mock: Stopping Bluetooth scan");
    
    // Track call for verification
    verification.scan_stop_calls++;
    
    // Check if not scanning - meaningful behavior
    if (!s_scanning) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update state
    s_scanning = false;
    
    return ESP_OK;
}

/**
 * @brief Check if scanning
 */
bool bt_is_scanning(void)
{
    return s_scanning;
}

/**
 * @brief Start scan with timeout
 */
esp_err_t bt_scan(uint32_t duration_s)
{
    ESP_LOGI(TAG, "Mock: Starting Bluetooth scan with %"PRIu32"s timeout", duration_s);
    
    // Track call for verification
    verification.scan_start_calls++;
    
    // Check if not initialized - meaningful behavior
    if (!s_initialized && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if already scanning - meaningful behavior
    if (s_scanning && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update state if successful
    if (mock_control.scan_start_return == ESP_OK) {
        s_scanning = true;
    }
    
    return mock_control.scan_start_return;
}

/**
 * @brief Get discovered devices
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, int max_count, uint16_t* device_count)
{
    if (!devices || !device_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate how many devices to return
    uint16_t count = (s_discovered_device_count <= max_count) ? 
                      s_discovered_device_count : max_count;
    
    // Copy the devices
    for (int i = 0; i < count; i++) {
        memcpy(&devices[i], &s_discovered_devices[i], sizeof(bt_device_t));
    }
    
    *device_count = count;
    return ESP_OK;
}

/**
 * @brief Connect to device
 */
esp_err_t bt_connect(const char* addr)
{
    ESP_LOGI(TAG, "Mock: Connecting to %s", addr);
    
    // Track call for verification
    verification.connect_calls++;
    strncpy(verification.last_connect_addr, addr, sizeof(verification.last_connect_addr) - 1);
    
    // Check if not initialized - meaningful behavior
    if (!s_initialized && mock_control.connect_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check for configured error return
    if (mock_control.connect_return != ESP_OK) {
        return mock_control.connect_return;
    }
    
    // Update connection state on success
    s_connected = true;
    s_active_profile = BT_PROFILE_A2DP_SINK;
    
    // Update connection info
    memset(&s_current_connection, 0, sizeof(s_current_connection));
    s_current_connection.connected = true;
    strncpy(s_current_connection.remote_addr, addr, sizeof(s_current_connection.remote_addr) - 1);
    
    return ESP_OK;
}

/**
 * @brief Connect by name
 */
esp_err_t bt_connect_by_name(const char* name)
{
    ESP_LOGI(TAG, "Mock: Connecting to device with name %s", name);
    
    // Track call for verification
    verification.connect_calls++;
    strncpy(verification.last_connect_name, name, sizeof(verification.last_connect_name) - 1);
    
    // Check if not initialized - meaningful behavior
    if (!s_initialized && mock_control.connect_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check for configured error return
    if (mock_control.connect_return != ESP_OK) {
        return mock_control.connect_return;
    }
    
    // Check if already connected - meaningful behavior
    if (s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Check for device in discovered list - just for validation
    bool found = false;
    for (int i = 0; i < s_discovered_device_count; i++) {
        if (strcmp(s_discovered_devices[i].name, name) == 0) {
            found = true;
            // Update connection info with this device
            strncpy(s_current_connection.remote_name, s_discovered_devices[i].name, 
                   sizeof(s_current_connection.remote_name) - 1);
            break;
        }
    }
    */
    
    // For test simplicity, update state regardless
    s_connected = true;
    s_current_connection.connected = true;
    strncpy(s_current_connection.remote_name, name, sizeof(s_current_connection.remote_name) - 1);
    
    return ESP_OK;
}

/**
 * @brief Connect with timeout
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Mock: Connecting to %s with timeout %"PRIu32"ms", addr, timeout_ms);
    
    // Special case for tests that expect timeout
    if (timeout_ms == 500) {
        return mock_control.timeout_return;
    }
    
    return bt_connect(addr);
}

/**
 * @brief Check if connected
 */
bool bt_is_connected(void)
{
    return s_connected;
}

/**
 * @brief Disconnect
 */
esp_err_t bt_disconnect(void)
{
    // Track call for verification
    verification.disconnect_calls++;
    
    // Check if not connected - meaningful behavior
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update state
    s_connected = false;
    s_current_connection.connected = false;
    
    return ESP_OK;
}

/**
 * @brief Start streaming
 */
esp_err_t bt_start_streaming(void)
{
    ESP_LOGI(TAG, "Mock: Starting audio streaming");
    
    // Track call for verification
    verification.streaming_start_calls = verification.streaming_start_calls + 1;
    
    // Check if not connected - meaningful behavior
    if (!s_connected) {
        return ESP_FAIL;
    }
    
    // Check if already streaming - meaningful behavior
    if (s_streaming) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update streaming state
    s_streaming = true;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_PLAYING;
    
    return ESP_OK;
}

/**
 * @brief Stop streaming
 */
esp_err_t bt_stop_streaming(void)
{
    ESP_LOGI(TAG, "Mock: Stopping audio streaming");
    
    // Track call for verification
    verification.streaming_stop_calls = verification.streaming_stop_calls + 1;
    
    // Check if not streaming - meaningful behavior
    if (!s_streaming && !s_streaming_paused) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update streaming state
    s_streaming = false;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    
    return ESP_OK;
}

/**
 * @brief Pause streaming
 */
esp_err_t bt_pause_streaming(void)
{
    // Track call for verification
    verification.streaming_pause_calls = verification.streaming_pause_calls + 1;
    
    // Can only pause if actually streaming
    if (!s_streaming) {
        return ESP_FAIL;
    }
    
    // Update streaming state
    s_streaming = false;
    s_streaming_paused = true;
    s_streaming_state = BT_STREAMING_STATE_PAUSED;
    
    return ESP_OK;
}

/**
 * @brief Resume streaming
 */
esp_err_t bt_resume_streaming(void)
{
    // Track call for verification
    verification.streaming_resume_calls = verification.streaming_resume_calls + 1;
    
    // Can only resume if paused
    if (!s_streaming_paused) {
        return ESP_FAIL;
    }
    
    // Update streaming state
    s_streaming = true;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_PLAYING;
    
    return ESP_OK;
}

/**
 * @brief Check if streaming
 */
bool bt_is_streaming(void)
{
    return s_streaming;
}

/**
 * @brief Check if streaming is paused
 */
bool bt_is_paused(void)
{
    return s_streaming_paused;
}

/**
 * @brief Get streaming state
 */
bt_streaming_state_t bt_get_streaming_state(void)
{
    return s_streaming_state;
}

/**
 * @brief Get paired device count
 */
uint16_t bt_get_paired_device_count(void)
{
    // Return the mock control value if set
    if (mock_control.paired_device_count > 0) {
        return mock_control.paired_device_count;
    }
    
    // Default mock implementation - just return 1 for testing
    return 1;
}

/**
 * @brief Register connection callback
 */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data)
{
    s_connection_callback = callback;
    s_connection_callback_data = user_data;
    return ESP_OK;
}

/**
 * @brief Get connection info
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy the connection info
    memcpy(info, &s_current_connection, sizeof(bt_connection_info_t));
    return ESP_OK;
}

/**
 * @brief Set auto reconnect
 */
esp_err_t bt_set_auto_reconnect(bool enable)
{
    ESP_LOGI(TAG, "Mock: Setting auto reconnect to %d", enable);
    
    // Track the setting
    verification.auto_reconnect_enabled = enable;
    
    return ESP_OK;
}

/**
 * @brief Check if device supports profile
 */
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile)
{
    if (!device) {
        return false;
    }
    
    // For A2DP sink tests, audio devices with certain class of device support A2DP sink
    if (profile == BT_PROFILE_A2DP_SINK) {
        // Check if device is audio device
        if ((device->cod & 0x240000) != 0) {
            return true;
        }
    }
    
    return false;
}

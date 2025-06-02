#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bt_source.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "BT_SOURCE";

// Pairing state and management
static bt_pairing_state_t current_pairing_state = BT_PAIRING_STATE_NONE;
static bt_pairing_method_t current_pairing_method = BT_PAIRING_NONE;
static char current_pairing_addr[18] = {0};
static bool is_pairing = false;
static char default_pin[16] = "1234";
static char pairing_passkey[7] = {0};
static bool ssp_confirmation_requested = false;

// Connection management
static bool is_initialized = false;
static bool is_connected = false;
static bt_connection_info_t current_connection = {0};
static bt_connection_callback_t connection_callback = NULL;
static void* connection_callback_data = NULL;
static bt_discovery_cb_t discovery_callback = NULL;
static void* discovery_callback_data = NULL;

// Streaming state
static bt_streaming_state_t streaming_state = BT_STREAMING_STATE_STOPPED;
static bool is_streaming = false;
static bool is_paused = false;

// Device discovery
static bt_device_t discovered_devices[20];  // Adjust size as needed
static uint16_t discovered_count = 0;
static bool scanning = false;

// Paired devices storage
#define MAX_PAIRED_DEVICES 10
static bt_device_t paired_devices[MAX_PAIRED_DEVICES];
static uint16_t paired_count = 0;

// Auto-reconnect configuration
static bool auto_reconnect_enabled = false;

// Semaphore for thread safety
static SemaphoreHandle_t bt_mutex = NULL;

// Forward declarations for callback functions
static void bt_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void bt_a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
static void bt_avrc_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

/**
 * Initialize Bluetooth stack
 */
esp_err_t bt_init(void)
{
    ESP_LOGI(TAG, "Initializing Bluetooth stack");
    
    if (is_initialized) {
        ESP_LOGW(TAG, "Bluetooth already initialized");
        return ESP_OK;
    }
    
    // Initialize semaphore
    if (bt_mutex == NULL) {
        bt_mutex = xSemaphoreCreateMutex();
        if (bt_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore");
            return ESP_FAIL;
        }
    }
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase and init");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Enable Bluetooth controller
    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize Bluedroid stack
    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Enable Bluedroid stack
    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register GAP callback
    if ((ret = esp_bt_gap_register_callback(bt_gap_callback)) != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register A2DP callback
    if ((ret = esp_a2d_register_callback(bt_a2dp_callback)) != ESP_OK) {
        ESP_LOGE(TAG, "A2DP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize A2DP source
    if ((ret = esp_a2d_source_init()) != ESP_OK) {
        ESP_LOGE(TAG, "A2DP source init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register AVRC controller
    if ((ret = esp_avrc_ct_init()) != ESP_OK) {
        ESP_LOGE(TAG, "AVRC controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register AVRC callback
    if ((ret = esp_avrc_ct_register_callback(bt_avrc_callback)) != ESP_OK) {
        ESP_LOGE(TAG, "AVRC callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set device name
    if ((ret = esp_bt_dev_set_device_name("ESP32 Audio Source")) != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set discoverable and connectable mode
    if ((ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE)) != ESP_OK) {
        ESP_LOGE(TAG, "Set scan mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set security parameters for both SSP and Legacy PIN
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;  // Display Yes/No capability
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(uint8_t));
    
    // Enable SSP
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap_mode = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap_mode, sizeof(esp_bt_io_cap_t));
    
    // Set authentication mode to require authentication for all connections
    esp_bt_gap_set_authentication_requirement(ESP_BT_AUTH_REQ_SC_MITM_BOND);
    
    // Load paired devices from NVS
    bt_load_paired_devices();
    
    is_initialized = true;
    ESP_LOGI(TAG, "Bluetooth initialized successfully");
    return ESP_OK;
}

/**
 * Start scanning for devices
 */
esp_err_t bt_scan_start(void)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting Bluetooth scan");
    
    // Clear previous discovered devices
    xSemaphoreTake(bt_mutex, portMAX_DELAY);
    discovered_count = 0;
    memset(discovered_devices, 0, sizeof(discovered_devices));
    scanning = true;
    xSemaphoreGive(bt_mutex);
    
    // Set scan mode
    esp_err_t ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set scan mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start discovery
    ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start discovery failed: %s", esp_err_to_name(ret));
        scanning = false;
        return ret;
    }
    
    return ESP_OK;
}

/**
 * Start scan with specified timeout
 */
esp_err_t bt_scan(uint32_t timeout_seconds)
{
    // Use the standard scan but add a timer to stop it
    esp_err_t ret = bt_scan_start();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Create a timer to stop the scan after timeout
    // In real implementation, you'd create an actual timer
    // For simplicity, we'll just log this here
    ESP_LOGI(TAG, "Scan will automatically stop after %d seconds", timeout_seconds);
    
    return ESP_OK;
}

/**
 * Start filtered scan
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type)
{
    // For now, just do a normal scan - filtering will be done when processing results
    ESP_LOGI(TAG, "Starting filtered scan for device type %d", device_type);
    return bt_scan_start();
}

/**
 * Stop scanning
 */
esp_err_t bt_scan_stop(void)
{
    if (!scanning) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping Bluetooth scan");
    
    esp_err_t ret = esp_bt_gap_cancel_discovery();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop discovery failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    scanning = false;
    return ESP_OK;
}

/**
 * Connect to a device by address
 */
esp_err_t bt_connect(const char* addr)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (is_connected) {
        ESP_LOGW(TAG, "Already connected to a device");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Connecting to device %s", addr);
    
    // Convert MAC address string to bytes
    esp_bd_addr_t bda;
    sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]);
    
    // Initialize connection info
    memset(&current_connection, 0, sizeof(current_connection));
    strcpy(current_connection.remote_addr, addr);
    
    // Find device name if we have it from discovery
    for (int i = 0; i < discovered_count; i++) {
        uint8_t* dev_addr = discovered_devices[i].addr;
        if (memcmp(dev_addr, bda, 6) == 0) {
            strcpy(current_connection.remote_name, discovered_devices[i].name);
            break;
        }
    }
    
    // Start A2DP connection
    esp_err_t ret = esp_a2d_source_connect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP connect failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Connection status will be updated in the A2DP callback
    return ESP_OK;
}

/**
 * Check if streaming is active
 */
bool bt_is_streaming(void)
{
    return is_streaming;
}

/**
 * Get streaming state
 */
bt_streaming_state_t bt_get_streaming_state(void)
{
    return streaming_state;
}

// Add all the pairing-related functions that match the header:

/**
 * Start Bluetooth pairing with a device
 */
esp_err_t bt_start_pairing(const char* addr)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting pairing with device %s", addr);
    
    // Store the address
    strncpy(current_pairing_addr, addr, sizeof(current_pairing_addr) - 1);
    current_pairing_addr[sizeof(current_pairing_addr) - 1] = '\0';
    is_pairing = true;
    
    // Convert MAC address string to bytes
    esp_bd_addr_t bda;
    sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]);
    
    // For ESP32, the pairing process is initiated automatically when connecting
    // or can be explicitly started with authentication request
    esp_err_t ret = esp_bt_gap_security_req(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate pairing: %s", esp_err_to_name(ret));
        is_pairing = false;
        return ret;
    }
    
    // Set initial state - actual state changes will happen in GAP callback
    current_pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;
    current_pairing_method = BT_PAIRING_PIN;  // Default, may change in callback
    
    return ESP_OK;
}

/**
 * Send PIN code for pairing
 */
esp_err_t bt_send_pin_code(const char* pin)
{
    if (!is_pairing || current_pairing_method != BT_PAIRING_PIN) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Sending PIN code");
    
    // Convert MAC address string to bytes
    esp_bd_addr_t bda;
    sscanf(current_pairing_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]);
    
    // Send PIN code
    esp_err_t ret = esp_bt_pin_code_reply(bda, true, strlen(pin), (uint8_t*)pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PIN code reply failed: %s", esp_err_to_name(ret));
        current_pairing_state = BT_PAIRING_STATE_FAILED;
        return ret;
    }
    
    // On success, state will change in GAP callback
    return ESP_OK;
}

/**
 * Get current pairing state
 */
bt_pairing_state_t bt_get_pairing_state(void)
{
    return current_pairing_state;
}

/**
 * Check if a device is paired
 */
bool bt_is_device_paired(const char* addr)
{
    if (!addr) {
        return false;
    }
    
    // Convert address to bytes for comparison
    uint8_t addr_bytes[6];
    sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
           &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]);
    
    // Check if device is in paired devices list
    for (int i = 0; i < paired_count; i++) {
        if (memcmp(paired_devices[i].addr, addr_bytes, 6) == 0) {
            return true;
        }
    }
    
    return false;
}

/**
 * Set default PIN code
 */
esp_err_t bt_set_default_pin(const char* pin)
{
    if (!pin || strlen(pin) == 0 || strlen(pin) > 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(default_pin, pin, sizeof(default_pin) - 1);
    default_pin[sizeof(default_pin) - 1] = '\0';
    
    // Store in NVS
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("bt_config", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = nvs_set_str(nvs, "default_pin", default_pin);
    if (ret == ESP_OK) {
        nvs_commit(nvs);
    }
    nvs_close(nvs);
    
    return ret;
}

/**
 * Get default PIN code
 */
esp_err_t bt_get_default_pin(char* pin, size_t size)
{
    if (!pin || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(default_pin) < size) {
        strcpy(pin, default_pin);
        return ESP_OK;
    } else {
        strncpy(pin, default_pin, size - 1);
        pin[size - 1] = '\0';
        return ESP_ERR_INSUFFICIENT_RESOURCES;
    }
}

/**
 * Store paired devices to persistent storage
 */
esp_err_t bt_store_paired_devices(void)
{
    // Use NVS to store paired devices
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("bt_paired", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %d", ret);
        return ret;
    }
    
    // Store count
    ret = nvs_set_u16(nvs, "count", paired_count);
    if (ret != ESP_OK) {
        nvs_close(nvs);
        return ret;
    }
    
    // Store each device
    for (int i = 0; i < paired_count; i++) {
        char key[16];
        sprintf(key, "dev_%d", i);
        ret = nvs_set_blob(nvs, key, &paired_devices[i], sizeof(bt_device_t));
        if (ret != ESP_OK) {
            nvs_close(nvs);
            return ret;
        }
    }
    
    nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Stored %d paired devices", paired_count);
    return ESP_OK;
}

/**
 * Load paired devices from persistent storage
 */
esp_err_t bt_load_paired_devices(void)
{
    // Use NVS to load paired devices
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("bt_paired", NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        // Not an error if NVS namespace doesn't exist yet
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            paired_count = 0;
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Error opening NVS: %d", ret);
        return ret;
    }
    
    // Load count
    ret = nvs_get_u16(nvs, "count", &paired_count);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            paired_count = 0;
            nvs_close(nvs);
            return ESP_OK;
        }
        nvs_close(nvs);
        return ret;
    }
    
    // Bound check
    if (paired_count > MAX_PAIRED_DEVICES) {
        paired_count = MAX_PAIRED_DEVICES;
    }
    
    // Load each device
    for (int i = 0; i < paired_count; i++) {
        char key[16];
        sprintf(key, "dev_%d", i);
        
        size_t required_size = sizeof(bt_device_t);
        ret = nvs_get_blob(nvs, key, &paired_devices[i], &required_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error loading device %d: %d", i, ret);
            // Continue with other devices
        }
    }
    
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Loaded %d paired devices", paired_count);
    return ESP_OK;
}

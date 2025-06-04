/**
 * Abstract interface for Bluetooth functionality
 */
typedef struct {
    // Initialization
    esp_err_t (*init)(void);
    esp_err_t (*deinit)(void);
    
    // Device discovery
    esp_err_t (*scan_start)(void);
    esp_err_t (*scan_start_filtered)(bt_device_type_t filter);
    esp_err_t (*scan_stop)(void);
    bool (*is_scanning)(void);
    uint16_t (*get_discovered_count)(void);
    esp_err_t (*get_discovered_devices)(bt_device_t* devices, uint16_t max_count, uint16_t* actual_count);
    
    // Connection management
    esp_err_t (*connect)(const char* addr);
    esp_err_t (*disconnect)(void);
    bool (*is_connected)(void);
    
    // Pairing
    esp_err_t (*start_pairing)(const char* addr);
    esp_err_t (*send_pin_code)(const char* pin);
    esp_err_t (*ssp_confirm)(bool confirm);
    bt_pairing_state_t (*get_pairing_state)(void);
    
    // Paired devices
    bool (*is_device_paired)(const char* addr);
    esp_err_t (*add_paired_device)(const bt_device_t* device);
    esp_err_t (*unpair_device)(const char* addr);
    esp_err_t (*unpair_all_devices)(void);
    int (*get_paired_device_count)(void);
    int (*get_paired_devices)(bt_device_t* devices, int max_count);
    
    // Streaming
    esp_err_t (*start_streaming)(void);
    esp_err_t (*stop_streaming)(void);
    bool (*is_streaming)(void);
    bt_streaming_state_t (*get_streaming_state)(void);
} bt_interface_t;

/**
 * @file bt_mock.c
 * @brief Implementation of Bluetooth mock functionality for testing
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "bt_mock.h"
#include "bt_mock_devices.h"
#include "util_safe.h"

static const char *TAG = "BT_MOCK";

#define safe_vsnprintf util_safe_vsnprintf
#define safe_snprintf util_safe_snprintf
#define safe_memcpy util_safe_memcpy

/* Optional hook implemented by the test-app mock to keep its local caches in
 * sync when the authoritative component-level mock mutates the paired device
 * list. Guarded through a weak declaration so production builds that link this
 * component without the test harness do not require the symbol.
 */
extern void bt_source_mock_cache_paired_device(const bt_device_t* device)
    __attribute__((weak));

// Static variables to track mock state
static bool s_mock_initialized = false;
static bool s_mock_ssp_supported = true;
static bool s_mock_pin_failure = false;
static bool s_mock_pairing_timeout = false;

esp_err_t bt_mock_init(void)
{
    if (s_mock_initialized) {
        ESP_LOGW(TAG, "BT mock already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing BT mock system");
    bt_mock_devices_init();
    s_mock_initialized = true;
    return ESP_OK;
}

void bt_mock_cleanup(void)
{
    if (!s_mock_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Cleaning up BT mock system");
    bt_mock_devices_cleanup();
    s_mock_initialized = false;
}

// Implement bt_mock_reset function - this is the one we need to match the call in the tests
void bt_mock_reset(void)
{
    ESP_LOGI(TAG, "Resetting Bluetooth mock state");
    // Reset device counters and state
    bt_mock_devices_reset();
    
    // Reset other internal state
    s_mock_ssp_supported = true;
    s_mock_pin_failure = false;
    s_mock_pairing_timeout = false;
    
    ESP_LOGI(TAG, "Mock state reset complete");
}

// Update to use bt_device_type_t instead of int
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type)
{
    ESP_LOGI(TAG, "Adding test device: %s (%s)", name, addr_str);
    // Adding false for paired parameter (test devices start unpaired)
    bt_mock_add_device(addr_str, name, type, false);
}

// bt_mock_set_ssp_supported and bt_mock_simulate_pin_failure are implemented
// in bt_mock_devices.c. Do not duplicate them here to avoid multiple
// definition/linker conflicts.

void bt_mock_simulate_pairing_timeout(void)
{
    s_mock_pairing_timeout = true;
    ESP_LOGI(TAG, "Pairing timeout simulation enabled");
    /* Also inform device-level mock so that bt_mock_get_pairing_state()
     * (the authoritative source used in tests) transitions to TIMEOUT.
     */
    bt_mock_devices_simulate_pairing_timeout();
}

// Implement bt_filter_has_matches function for compatibility with API calls
bool bt_filter_has_matches(int timeout)
{
    ESP_LOGI(TAG, "Check for filter matches with timeout %d seconds", timeout);
    // In mock implementation, return true if we have any devices
    return bt_mock_devices_count() > 0;
}

// Implement bt_ssp_confirm function for compatibility with API calls
esp_err_t bt_ssp_confirm(bool confirm)
{
    ESP_LOGI(TAG, "SSP confirmation: %s", confirm ? "confirmed" : "rejected");
    /* Delegate to the device-level mock implementation which updates pairing
     * state and the paired-device list (bt_mock_confirm_ssp is implemented in
     * bt_mock_devices.c). This keeps authoritative state in one place.
     */
    return bt_mock_confirm_ssp(confirm);
}

// Implement bt_mock_add_paired_device
esp_err_t bt_mock_add_paired_device(bt_device_t* device)
{
    if (!device) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Adding paired device: %s", device->name);
    device->paired = true;

    /* Convert binary address to string "AA:BB:CC:DD:EE:FF" expected by
     * bt_mock_add_device() and derive device type from COD field.
     */
    char addr_str[18] = {0};
    safe_snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                  device->addr[0], device->addr[1], device->addr[2],
                  device->addr[3], device->addr[4], device->addr[5]);

    bt_device_type_t type = BT_DEVICE_TYPE_OTHER;
    if ((device->cod & 0x200000) != 0) {
        type = BT_DEVICE_TYPE_AUDIO;
    }

    esp_err_t add_res = bt_mock_add_device(addr_str, device->name, type, true);
    if (add_res == ESP_OK && bt_source_mock_cache_paired_device) {
        int idx = -1;
        bt_device_t cached = {0};

        if (bt_mock_find_device(addr_str, &idx) && idx >= 0) {
            if (bt_mock_get_device(idx, &cached) != ESP_OK) {
                cached = *device;
            }
        } else {
            cached = *device;
        }

        cached.paired = true;
        bt_source_mock_cache_paired_device(&cached);
    }

    return add_res;
}

// Implement bt_mock_get_paired_device_count
// bt_mock_get_paired_device_count is implemented in bt_mock_devices.c; keep
// the authoritative implementation there to avoid duplicate symbols.

// Implement bt_mock_get_paired_devices if needed
esp_err_t bt_mock_get_paired_devices(bt_device_t *devices, uint16_t max_count, uint16_t *actual_count)
{
    if (!devices || !actual_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *actual_count = 0;
    int total = bt_mock_devices_count();
    
    for (int i = 0; i < total && *actual_count < max_count; i++) {
        bt_device_t device;
        if (bt_mock_get_device(i, &device) == ESP_OK && device.paired) {
            safe_memcpy(&devices[*actual_count], sizeof(bt_device_t), &device, sizeof(bt_device_t));
            (*actual_count)++;
        }
    }
    
    return ESP_OK;
}

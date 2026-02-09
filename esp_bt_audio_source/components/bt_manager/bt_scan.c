/**
 * bt_scan.c - Bluetooth device discovery and scanning implementation
 * 
 * This module was extracted from bt_manager.c to improve modularity and
 * maintainability. It handles all aspects of Bluetooth device discovery
 * including starting/stopping scans and processing discovery results.
 */

#include "bt_scan.h"
#include "bt_manager_internal.h"
#include "util_safe.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#define TAG "BT_SCAN"
#else
#include "esp_log.h"
#define TAG "BT_SCAN"
#endif

/* Test hooks - weak symbols overridden by tests */
#ifdef UNIT_TEST
__attribute__((weak)) void bt_manager_test_record_scan_start(void) { }
#endif

/* Public API implementation */

bt_err_t bt_start_scan(void)
{
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
    if (bt_ctx.scanning) {
        return ESP_OK; // Already scanning
    }
    
    // Clear previous discovered devices
    safe_memset(&bt_ctx.discovered_devices, sizeof(bt_ctx.discovered_devices), 
                0, sizeof(bt_ctx.discovered_devices));
    
#ifdef ESP_PLATFORM
    // Start discovery
    if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Start device discovery failed");  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Started Bluetooth device scanning");  // NOLINT(bugprone-branch-clone)
#endif
    
    bt_ctx.scanning = true;
    
    /* Unit-test hook: notify test shim that a scan started so host tests can
     * assert the command layer invoked the manager. The test shim provides
     * bt_manager_test_record_scan_start() in the mocks; declare as weak
     * and call it here so only unit-test builds attempt the symbol. */
#ifdef UNIT_TEST
    /* The weak symbol is declared at file scope (above); call it here so
     * host-mode tests can observe that a scan was initiated. Using an
     * extern declaration avoids redeclaring the weak attribute inside the
     * function (which some toolchains reject). */
    extern void bt_manager_test_record_scan_start(void);
    bt_manager_test_record_scan_start();
#endif

    return ESP_OK;
}

bt_err_t bt_stop_scan(void)
{
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
    if (!bt_ctx.scanning) {
        return ESP_OK; // Not scanning
    }
    
#ifdef ESP_PLATFORM
    // Stop discovery
    if (esp_bt_gap_cancel_discovery() != ESP_OK) {
        ESP_LOGE(TAG, "Stop device discovery failed");  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Stopped Bluetooth device scanning");  // NOLINT(bugprone-branch-clone)
#endif
    
    bt_ctx.scanning = false;
    return ESP_OK;
}

/* Internal API for GAP callback */

void bt_scan_handle_discovery_result(const esp_bd_addr_t bda, 
                                     int num_prop,
                                     esp_bt_gap_dev_prop_t *prop)
{
    char bda_str[18];
    char name[32] = {0};
    uint32_t cod = 0;
    int8_t rssi = 0;
    
    // Get device address
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    
    // Get device name and properties
    for (int i = 0; i < num_prop; i++) {
        if (prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            safe_memcpy(name, sizeof(name), prop[i].val, prop[i].len);
            name[sizeof(name) - 1] = '\0';
        } else if (prop[i].type == ESP_BT_GAP_DEV_PROP_COD) {
            safe_memcpy(&cod, sizeof(cod), prop[i].val, sizeof(uint32_t));
        } else if (prop[i].type == ESP_BT_GAP_DEV_PROP_RSSI) {
            safe_memcpy(&rssi, sizeof(rssi), prop[i].val, sizeof(int8_t));
        }
    }
    
    ESP_LOGI(TAG, "Device found: %s, name: %s, RSSI: %d", bda_str, name, rssi);  // NOLINT(bugprone-branch-clone)
    
    // Add to discovered devices list
    if (bt_ctx.discovered_devices.count < 20) {
        int idx = bt_ctx.discovered_devices.count;
        safe_copy_str(bt_ctx.discovered_devices.devices[idx].mac,
                      sizeof(bt_ctx.discovered_devices.devices[idx].mac), bda_str);
        safe_copy_str(bt_ctx.discovered_devices.devices[idx].name,
                      sizeof(bt_ctx.discovered_devices.devices[idx].name), name);
        bt_ctx.discovered_devices.devices[idx].rssi = (int)(unsigned char)rssi;
        bt_ctx.discovered_devices.count++;
    }
}

void bt_scan_handle_state_change(esp_bt_gap_discovery_state_t state)
{
    if (state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        ESP_LOGI(TAG, "Device discovery stopped");  // NOLINT(bugprone-branch-clone)
        bt_ctx.scanning = false;
    } else if (state == ESP_BT_GAP_DISCOVERY_STARTED) {
        ESP_LOGI(TAG, "Device discovery started");  // NOLINT(bugprone-branch-clone)
        bt_ctx.scanning = true;
    }
}

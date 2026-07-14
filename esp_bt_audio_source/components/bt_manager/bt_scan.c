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
#include "command_interface.h"
#include <stdio.h>
#define TAG "BT_SCAN"
#else
#include "esp_log.h"
#define TAG "BT_SCAN"
#endif

#ifdef UNIT_TEST
/* Weak stub for test hook - host tests can override with strong symbol */
MAYBE_WEAK void bt_manager_test_record_scan_start(void) {
    /* No-op default implementation */
}
#endif

/* Public API implementation */

bt_err_t bt_start_scan(void)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_FAIL;
    }

    if (bt_ctx.scanning) {
        bt_ctx_unlock();
        return ESP_OK; // Already scanning
    }

    // Clear previous discovered devices
    safe_memset(&bt_ctx.discovered_devices, sizeof(bt_ctx.discovered_devices),
                0, sizeof(bt_ctx.discovered_devices));

    bt_ctx.scanning = true;
    bt_ctx_unlock();

#ifdef ESP_PLATFORM
    err = esp_bt_gap_start_discovery(ESP_BT_DEVICE_DISCOVERY_ALL_MODE,
                                     0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Start device discovery failed: %s", esp_err_to_name(err));  // NOLINT(bugprone-branch-clone)
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) {
            return ESP_FAIL;
        }
        bt_ctx.scanning = false;
        bt_ctx_unlock();
        return ESP_FAIL;
    }
#endif

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

    /* In host tests, call esp_bt_gap_start_discovery() so the mock can
     * control success/failure. Check the result and roll back if it fails. */
    esp_err_t gap_err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                                   0, 0);
    if (gap_err != ESP_OK) {
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) {
            return ESP_FAIL;
        }
        bt_ctx.scanning = false;
        bt_ctx_unlock();
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

bt_err_t bt_stop_scan(void)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_FAIL;
    }

    if (!bt_ctx.scanning) {
        bt_ctx_unlock();
        return ESP_OK; // Not scanning
    }

    bool was_scanning = true;
    bt_ctx.scanning = false;
    bt_ctx_unlock();

    if (!was_scanning) {
        return ESP_OK;
    }

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
    // Stop discovery (ESP_PLATFORM for device, UNIT_TEST for host testing)
    if (esp_bt_gap_cancel_discovery() != ESP_OK) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Stop device discovery failed");  // NOLINT(bugprone-branch-clone)
#endif
        return ESP_FAIL;
    }

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Stopped Bluetooth device scanning");  // NOLINT(bugprone-branch-clone)
#endif
#endif  // ESP_PLATFORM || UNIT_TEST

    return ESP_OK;
}

#ifdef ESP_PLATFORM
/* Tracks how many remote name requests are still pending.  When it reaches
 * zero, bt_scan_emit_results() fires and sends all discovered devices over
 * the serial command interface. */
static int s_pending_name_requests = 0;

static void bt_scan_emit_results(void)
{
    /* Copy discovered device list under lock, emit outside lock. */
    int count;
    bt_device_t devices[BT_MAX_DISCOVERED_DEVICES];

    if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) {
        return;
    }

    count = bt_ctx.discovered_devices.count;
    for (int i = 0; i < count; i++) {
        safe_copy_str(devices[i].mac, sizeof(devices[i].mac),
                      bt_ctx.discovered_devices.devices[i].mac);
        safe_copy_str(devices[i].name, sizeof(devices[i].name),
                      bt_ctx.discovered_devices.devices[i].name);
        devices[i].rssi = bt_ctx.discovered_devices.devices[i].rssi;
    }

    bt_ctx_unlock();

    for (int i = 0; i < count; i++) {
        char result_data[64];
        safe_snprintf(result_data, sizeof(result_data), "%s,%s",
                      devices[i].mac, devices[i].name);
        /* Broadcast: these fire asynchronously after the SCAN command's
         * cmd_process() cycle, so route to every port (incl. the bt_link UART
         * the S3 requested the scan on), not just the reset-to-primary one. */
        cmd_send_response_all("INFO", "SCAN", "RESULT", result_data);
    }
    char count_str[16];
    safe_snprintf(count_str, sizeof(count_str), "count=%d", count);
    cmd_send_response_all("OK", "SCAN", "DONE", count_str);
}
#endif

/* Internal API for GAP callback - also available for UNIT_TEST */
#if defined(ESP_PLATFORM) || defined(UNIT_TEST)

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

    /* BT_MAX_DISCOVERED_DEVICES = 20 (size of bt_device_list_t.devices array) */
#define BT_MAX_DISCOVERED_DEVICES 20
    if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) {
        return;
    }

    if (bt_ctx.discovered_devices.count < BT_MAX_DISCOVERED_DEVICES) {
        int idx = bt_ctx.discovered_devices.count;
        /* Check if this MAC is already in the list (second callback may bring name). */
        int existing = -1;
        for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
            if (strcmp(bt_ctx.discovered_devices.devices[i].mac, bda_str) == 0) {
                existing = i;
                break;
            }
        }
        if (existing >= 0) {
            /* Update name if the new callback has a non-empty name. */
            if (name[0] != '\0') {
                safe_copy_str(bt_ctx.discovered_devices.devices[existing].name,
                              sizeof(bt_ctx.discovered_devices.devices[existing].name),
                              name);
            }
        } else {
            safe_copy_str(bt_ctx.discovered_devices.devices[idx].mac,
                          sizeof(bt_ctx.discovered_devices.devices[idx].mac), bda_str);
            safe_copy_str(bt_ctx.discovered_devices.devices[idx].name,
                          sizeof(bt_ctx.discovered_devices.devices[idx].name), name);
            bt_ctx.discovered_devices.devices[idx].rssi = (int)rssi;  // Direct cast from int8_t to int
            bt_ctx.discovered_devices.count++;
        }
    } else {
        ESP_LOGW(TAG, "Discovery buffer full (%d devices); dropping %s (%s)",
                 BT_MAX_DISCOVERED_DEVICES, name, bda_str);
    }

    bt_ctx_unlock();
}

void bt_scan_handle_state_change(esp_bt_gap_discovery_state_t state)
{
    if (state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        ESP_LOGI(TAG, "Device discovery stopped");  // NOLINT(bugprone-branch-clone)

        /* Snapshot devices for remote-name lookup, then clear scanning flag. */
        bool any_empty_names = false;
        int device_count = 0;
        esp_bd_addr_t pending_bdas[BT_MAX_DISCOVERED_DEVICES] = {{0}};

        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) {
            return;
        }

        bt_ctx.scanning = false;
        device_count = bt_ctx.discovered_devices.count;

        for (int i = 0; i < device_count; i++) {
            if (bt_ctx.discovered_devices.devices[i].name[0] == '\0') {
                any_empty_names = true;
                /* NOLINTNEXTLINE(cert-err34-c) */
                sscanf(bt_ctx.discovered_devices.devices[i].mac,
                       "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                       &pending_bdas[i][0], &pending_bdas[i][1],
                       &pending_bdas[i][2], &pending_bdas[i][3],
                       &pending_bdas[i][4], &pending_bdas[i][5]);
            }
        }

        bt_ctx_unlock();

#ifdef ESP_PLATFORM
        /* For devices whose name was not in EIR, issue an explicit remote name
         * request.  Results are emitted only after all requests complete (or
         * if there are none to issue, emit immediately). */
        s_pending_name_requests = 0;
        for (int i = 0; i < device_count; i++) {
            if (pending_bdas[i][0] || pending_bdas[i][1] || pending_bdas[i][2]) {
                if (esp_bt_gap_read_remote_name(pending_bdas[i]) == ESP_OK) {
                    s_pending_name_requests++;
                }
            }
        }
        if (s_pending_name_requests == 0) {
            bt_scan_emit_results();
        }
#endif
    } else if (state == ESP_BT_GAP_DISCOVERY_STARTED) {
        ESP_LOGI(TAG, "Device discovery started");  // NOLINT(bugprone-branch-clone)
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            bt_ctx.scanning = true;
            bt_ctx_unlock();
        }
    }
}

#ifdef ESP_PLATFORM
void bt_scan_handle_remote_name_evt(const esp_bd_addr_t bda,
                                    esp_bt_status_t stat,
                                    const uint8_t *rmt_name)
{
    char bda_str[18];
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    if (stat == ESP_BT_STATUS_SUCCESS) {
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
                if (strcmp(bt_ctx.discovered_devices.devices[i].mac, bda_str) == 0) {
                    safe_copy_str(bt_ctx.discovered_devices.devices[i].name,
                                  sizeof(bt_ctx.discovered_devices.devices[i].name),
                                  (const char *)rmt_name);
                    ESP_LOGI(TAG, "Remote name resolved: %s -> %s", bda_str,
                             bt_ctx.discovered_devices.devices[i].name);  // NOLINT(bugprone-branch-clone)
                    break;
                }
            }
            bt_ctx_unlock();
        }
    } else {
        ESP_LOGW(TAG, "Remote name request failed for %s (stat=%d)", bda_str, (int)stat);  // NOLINT(bugprone-branch-clone)
    }

    if (s_pending_name_requests > 0) {
        s_pending_name_requests--;
    }
    if (s_pending_name_requests == 0) {
        bt_scan_emit_results();
    }
}
#endif  // ESP_PLATFORM

#endif  // ESP_PLATFORM || UNIT_TEST

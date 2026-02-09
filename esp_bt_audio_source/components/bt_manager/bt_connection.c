/**
 * bt_connection.c - Bluetooth connection initiation and termination implementation
 * 
 * This module was extracted from bt_manager.c to improve modularity and
 * maintainability. It handles connection lifecycle management including
 * initiating connections and disconnecting.
 */

#include "bt_connection.h"
#include "bt_manager_internal.h"
#include "util_safe.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_a2dp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define TAG "BT_CONN"
#else
#include "esp_log.h"
#define TAG "BT_CONN"
#endif

/* Public API implementation */

bt_err_t bt_connect(const char* mac)
{
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
    if (mac == NULL) {
        return ESP_FAIL;
    }
    
    if (bt_ctx.connected) {
        return ESP_FAIL;
    }
    
#ifdef ESP_PLATFORM
    // Convert MAC string to address
    esp_bd_addr_t bda;
    if (!parse_mac_bytes(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }
    
    // Connect to device
    if (esp_a2d_source_connect(bda) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to device: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Connecting to device: %s", mac);  // NOLINT(bugprone-branch-clone)
    safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), mac);
#endif
    
    return ESP_OK;
}

bt_err_t bt_connect_by_name(const char* name)
{
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find device in discovered devices
    for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
        if (strcmp(bt_ctx.discovered_devices.devices[i].name, name) == 0) {
            return bt_connect(bt_ctx.discovered_devices.devices[i].mac);
        }
    }
    
    // Not found in discovered list — try paired devices (persisted)
    for (int i = 0; i < bt_ctx.paired_devices.count; i++) {
        if (strcmp(bt_ctx.paired_devices.devices[i].name, name) == 0) {
            return bt_connect(bt_ctx.paired_devices.devices[i].mac);
        }
    }
    
    return ESP_FAIL;
}

#if defined(UNIT_TEST)
// Allow test code to override the manager disconnect implementation by
// marking this symbol weak in unit test builds. This keeps production
// behavior unchanged while allowing test-only mocks/stubs in
// `test_app` to take precedence during linking.
__attribute__((weak))
bt_err_t bt_disconnect(void)
#else
bt_err_t bt_disconnect(void)
#endif
{
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    const char *task_name = pcTaskGetName(NULL);
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect entry task=\"%s\" initialized=%d connected=%d audio_playing=%d mac=\"%s\"",
             task_name ? task_name : "<unknown>",
             bt_ctx.initialized,
             bt_ctx.connected,
             bt_ctx.audio_playing,
             bt_ctx.connected ? bt_ctx.connected_mac : "<none>");
#endif

    if (!bt_ctx.initialized) {
        /* Treat disconnect when manager is not initialized as a no-op
         * returning ESP_OK so higher-level C wrappers that convert
         * bt_err_t to int (0 == success) do not leak non-canonical
         * positive values into tests. This makes disconnect idempotent
         * across initialization state.
         */
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect short-circuit (manager not initialized)");
#endif
        return ESP_OK;
    }
    
    if (!bt_ctx.connected) {
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect short-circuit (already disconnected)");
#endif
        return ESP_OK; // Already disconnected
    }
    
#ifdef ESP_PLATFORM
    // Stop audio if playing
    if (bt_ctx.audio_playing) {
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect stopping audio before disconnect");
#endif
        // Forward declaration - bt_stop_audio is in bt_manager.c
        extern bt_err_t bt_stop_audio(void);
        bt_stop_audio();
    }
    
    // Convert MAC string to address
    esp_bd_addr_t bda;
    if (!parse_mac_bytes(bt_ctx.connected_mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC format in stored address: %s", bt_ctx.connected_mac);  // NOLINT(bugprone-branch-clone)
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGE(TAG, "DIAG: mgr_bt_disconnect aborting due to invalid MAC string");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    // Disconnect device
    if (esp_a2d_source_disconnect(bda) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect from device: %s", bt_ctx.connected_mac);  // NOLINT(bugprone-branch-clone)
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGE(TAG, "DIAG: mgr_bt_disconnect esp_a2d_source_disconnect() returned error");
#endif
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Disconnecting from device: %s", bt_ctx.connected_mac);  // NOLINT(bugprone-branch-clone)
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect invoked esp_a2d_source_disconnect(), bt_ctx.connected=%d",
             bt_ctx.connected);
#endif
#else
    // For testing without ESP-IDF
    bt_ctx.connected = false;
    
    if (bt_ctx.disconnected_callback != NULL) {
        bt_ctx.disconnected_callback(bt_ctx.connected_mac);
    }
#if defined(CONFIG_BT_MOCK_TESTING)
    printf("DIAG: mgr_bt_disconnect (mock build) set connected flag to %d\n", bt_ctx.connected);
#endif
#endif
    
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect exit connected=%d audio_playing=%d mac=\"%s\"",
             bt_ctx.connected,
             bt_ctx.audio_playing,
             bt_ctx.connected ? bt_ctx.connected_mac : "<none>");
#endif
    return ESP_OK;
}

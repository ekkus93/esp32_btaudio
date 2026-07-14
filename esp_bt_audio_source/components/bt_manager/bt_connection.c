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
    if (mac == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_FAIL;
    }

    if (bt_ctx.connected) {
        bt_ctx_unlock();
        return ESP_FAIL;
    }

    // Convert MAC string to address - validate in all builds
    esp_bd_addr_t bda;
    if (!parse_mac_bytes(mac, bda)) {
        bt_ctx_unlock();
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), mac);
    bt_ctx.connecting = true;
    bt_ctx_unlock();

    // Connect to device - call A2DP in all builds
    esp_err_t result = esp_a2d_source_connect(bda);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to device: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to device: %s", mac);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

bt_err_t bt_connect_by_name(const char* name)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char mac[18] = {0};

    esp_err_t err = bt_ctx_lock(100);
    if (err != ESP_OK) {
        return err;
    }

    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    // Find device in discovered devices
    int found = -1;
    for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
        if (strcmp(bt_ctx.discovered_devices.devices[i].name, name) == 0) {
            safe_copy_str(mac, sizeof(mac), bt_ctx.discovered_devices.devices[i].mac);
            found = i;
            break;
        }
    }

    /* Not found in discovered list — try paired devices (persisted) */
    if (found < 0) {
        for (int i = 0; i < bt_ctx.paired_devices.count; i++) {
            if (strcmp(bt_ctx.paired_devices.devices[i].name, name) == 0) {
                safe_copy_str(mac, sizeof(mac), bt_ctx.paired_devices.devices[i].mac);
                found = i;
                break;
            }
        }
    }

    bt_ctx_unlock();

    if (found < 0) {
        return ESP_FAIL;
    }

    return bt_connect(mac);
}

// Allow test code to override the manager disconnect implementation by
// marking this symbol weak in unit test builds. This keeps production
// behavior unchanged while allowing test-only mocks/stubs in
// `test_app` to take precedence during linking.
MAYBE_WEAK bt_err_t bt_disconnect(void)
{
    /* Snapshot current state under lock. */
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    bool initialized = bt_ctx.initialized;
    bool connected = bt_ctx.connected;
    bool connecting = bt_ctx.connecting;
    bool audio_playing = bt_ctx.audio_playing;
    char mac[18] = {0};
    safe_copy_str(mac, sizeof(mac), bt_ctx.connected_mac);
    bt_ctx_unlock();

#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    const char *task_name = pcTaskGetName(NULL);
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect entry task=\"%s\" initialized=%d connected=%d audio_playing=%d mac=\"%s\"",
             task_name ? task_name : "<unknown>",
             initialized, connected, audio_playing,
             mac[0] ? mac : "<none>");
#endif

    if (!initialized) {
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

    if (!connected && !connecting) {
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect short-circuit (already disconnected, not connecting)");
#endif
        return ESP_OK; // Already disconnected and no connection in progress
    }

#ifdef ESP_PLATFORM
    // Stop audio if playing
    if (audio_playing) {
#if defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect stopping audio before disconnect");
#endif
        // Forward declaration - bt_stop_audio is in bt_manager.c
        extern bt_err_t bt_stop_audio(void);
        bt_stop_audio();
    }
#endif

    // Convert MAC string to address
    esp_bd_addr_t bda;
    if (!parse_mac_bytes(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC format in stored address: %s", mac);  // NOLINT(bugprone-branch-clone)
#if defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGE(TAG, "DIAG: mgr_bt_disconnect aborting due to invalid MAC string");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    // Disconnect device - call in both ESP_PLATFORM and UNIT_TEST builds
    esp_err_t result = esp_a2d_source_disconnect(bda);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect from device: %s", mac);  // NOLINT(bugprone-branch-clone)
#if defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect esp_a2d_source_disconnect() returned error");
#endif
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Disconnecting from device: %s", mac);  // NOLINT(bugprone-branch-clone)

#ifdef ESP_PLATFORM
#if defined(CONFIG_BT_MOCK_TESTING)
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect invoked esp_a2d_source_disconnect(), bt_ctx.connected=%d",
             connected);
#endif
#else
    // For testing without ESP-IDF, manually update state
    bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    bt_ctx.connected = false;
    bt_ctx.connecting = false;
    bt_disconnected_cb disconnected_cb = bt_ctx.disconnected_callback;
    bt_ctx_unlock();

    if (disconnected_cb) {
        disconnected_cb(mac);
    }
#if defined(CONFIG_BT_MOCK_TESTING)
    printf("DIAG: mgr_bt_disconnect (mock build) set connected flag to %d\n", connected);
#endif
#endif

#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect exit connected=%d audio_playing=%d mac=\"%s\"",
             connected, audio_playing, mac[0] ? mac : "<none>");
#endif
    return ESP_OK;
}

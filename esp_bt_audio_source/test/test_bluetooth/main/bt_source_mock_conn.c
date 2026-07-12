/* bt_source_mock_conn.c — connection lifecycle + disconnect mocks.
 * Split out of bt_source_mock.c; shares bt_source_mock_internal.h. */
#include "bt_source_mock_internal.h"

static const char *TAG = "BT_SOURCE_MOCK";


/**
 * Connect to a Bluetooth device by address
 */
esp_err_t bt_connect_device(const char* addr)  // Changed from bt_connect to bt_connect_device
{
    if (!addr) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if already connected - now using the properly defined variable
    if (s_connection_state == BT_CONNECTION_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* If component mock provides connect helper, delegate to it so the
     * authoritative connection state is updated. Otherwise, fall back to
     * local behavior.
     */
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    ESP_LOGI(TAG, "DIAG_SEQ: mock_delegate_before #%u addr=%s", (unsigned int)++s_diag_seq_mock, addr ? addr : "<null>");
    esp_err_t err = bt_mock_connect(addr);
    ESP_LOGI(TAG, "DIAG_SEQ: mock_delegate_after #%u addr=%s err=%d connected=%d", (unsigned int)s_diag_seq_mock, addr ? addr : "<null>", (int)err, (int)bt_mock_is_connected());
    if (err == ESP_OK) {
        strncpy(s_current_connection.addr, addr, sizeof(s_current_connection.addr) - 1);
        s_current_connection.addr[sizeof(s_current_connection.addr) - 1] = '\0';
        s_current_connection.connected = true;
        s_current_connection.state = BT_CONNECTION_STATE_CONNECTED;
        s_connection_state = BT_CONNECTION_STATE_CONNECTED;
        s_defer_disconnect_visibility = false;
        s_connected = true;

        s_current_connection.name[0] = '\0';
        int device_idx = -1;
        if (bt_mock_find_device(addr, &device_idx) && device_idx >= 0) {
            bt_device_t device = {0};
            if (bt_mock_get_device(device_idx, &device) == ESP_OK) {
                strncpy(s_current_connection.name, device.name, sizeof(s_current_connection.name) - 1);
                s_current_connection.name[sizeof(s_current_connection.name) - 1] = '\0';
            }
        }

        bt_source_stub_sync_connected_state(true,
                                            s_current_connection.addr,
                                            (s_current_connection.name[0] != '\0') ? s_current_connection.name : NULL);
        return ESP_OK;
    }

    s_connection_state = BT_CONNECTION_STATE_FAILED;
    s_current_connection.connected = false;
    s_current_connection.state = BT_CONNECTION_STATE_FAILED;
    s_current_connection.name[0] = '\0';
    s_current_connection.addr[0] = '\0';
    s_connected = false;
    bt_source_stub_sync_connected_state(false, NULL, NULL);
    /*
     * Tests expect the connect API to return ESP_OK and report failures via
     * callbacks/state rather than a non-zero error code. Preserve the
     * component-level error in logs but return ESP_OK to match the test
     * contract used throughout the suite.
     */
    return ESP_OK;
#else
    strncpy(s_current_connection.addr, addr, sizeof(s_current_connection.addr) - 1);
    s_current_connection.addr[sizeof(s_current_connection.addr) - 1] = '\0';
    s_current_connection.connected = true;
    s_current_connection.state = BT_CONNECTION_STATE_CONNECTED;
    s_connection_state = BT_CONNECTION_STATE_CONNECTED;
    s_connected = true;
    bt_source_stub_sync_connected_state(true, s_current_connection.addr,
                                        (s_current_connection.name[0] != '\0') ? s_current_connection.name : NULL);
    return mock_control.connect_return;
#endif
}

/**
 * Connect to a Bluetooth device by name
 */
esp_err_t bt_connect_device_by_name(const char* name)  // Changed from bt_connect_by_name
{
    ESP_LOGI(TAG, "Mock: Connecting to device by name: %s", name);
    
    if (!s_initialized && mock_control.connect_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component-level mock which may have a connect-by-name hook.
     * The component returns ESP_OK on success. Update local state to match.
     */
    if (bt_mock_hook_connect_by_name(name) == ESP_OK) {
        s_connected = true;
        s_current_connection.connected = true;
        s_current_connection.state = BT_CONNECTION_STATE_CONNECTED;
        s_connection_state = BT_CONNECTION_STATE_CONNECTED;

        /* copy reported name; if the component reports a different canonical
         * name we will overwrite it below when we fetch the device entry. */
        strncpy(s_current_connection.name, name, sizeof(s_current_connection.name) - 1);
        s_current_connection.name[sizeof(s_current_connection.name) - 1] = '\0';

        char conn_addr[18] = {0};
        if (bt_mock_get_connected_addr(conn_addr, sizeof(conn_addr)) == ESP_OK) {
            strncpy(s_current_connection.addr, conn_addr, sizeof(s_current_connection.addr) - 1);
            s_current_connection.addr[sizeof(s_current_connection.addr) - 1] = '\0';
        } else {
            s_current_connection.addr[0] = '\0';
        }

        int device_idx = -1;
        if (s_current_connection.addr[0] != '\0' && bt_mock_find_device(s_current_connection.addr, &device_idx) &&
            device_idx >= 0) {
            bt_device_t device;
            if (bt_mock_get_device(device_idx, &device) == ESP_OK) {
                strncpy(s_current_connection.name, device.name, sizeof(s_current_connection.name) - 1);
                s_current_connection.name[sizeof(s_current_connection.name) - 1] = '\0';
            }
        }

        bt_source_stub_sync_connected_state(true,
                                            (s_current_connection.addr[0] != '\0') ? s_current_connection.addr : NULL,
                                            (s_current_connection.name[0] != '\0') ? s_current_connection.name : NULL);
        return ESP_OK;
    }

    s_connected = false;
    s_current_connection.connected = false;
    s_current_connection.state = BT_CONNECTION_STATE_FAILED;
    s_connection_state = BT_CONNECTION_STATE_FAILED;
    bt_source_stub_sync_connected_state(false, NULL, NULL);
    return ESP_FAIL;
#else
    s_connected = true;
    sprintf(s_current_connection.name, "%s", name);  // Changed from remote_name to name
    sprintf(s_current_connection.addr, "00:11:22:33:44:55");  // Changed from remote_addr to addr
    s_current_connection.connected = true;
    s_current_connection.state = BT_CONNECTION_STATE_CONNECTED;
    s_connection_state = BT_CONNECTION_STATE_CONNECTED;
    bt_source_stub_sync_connected_state(true, s_current_connection.addr, s_current_connection.name);
    return mock_control.connect_return;
#endif
}

/**
 * Connect to a Bluetooth device with timeout
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Mock: Connecting to %s with timeout %" PRIu32 "ms", addr, timeout_ms);
    
    if (timeout_ms > 0) {
        return mock_control.timeout_return;
    } else {
        return bt_connect_device(addr);
    }
}

/* ── API catch-up layer (2026-07) ─────────────────────────────────────────
 * command_interface and the merged test files grew references to newer
 * bt_manager-era APIs. Providing them here (backed by the mock's own
 * state) keeps the real bt_manager/bt_connection_manager objects out of
 * this fully-mocked link — any symbol resolved from libbt_manager.a
 * drags in objects whose definitions collide with this mock.
 * Wrappers that need bt_manager.h types live in bt_manager_api_mock.c
 * (bt_manager.h's bt_device_t conflicts with bt_source.h's). */

bt_err_t bt_connect_by_name(const char* name)
{
    return bt_connect_device_by_name(name);
}

int bt_get_connection_quality(const bt_connection_info_t* info)
{
    /* simple mock heuristic: any live connection reports good quality */
    return (info != NULL && info->connected) ? 80 : 0;
}

void bt_connection_shim_publish_info(const bt_connection_info_t *info)
{
    if (info != NULL) {
        bt_source_stub_sync_connected_state(info->connected, info->addr, info->name);
    }
}

/* Check if connected */
bool bt_is_connected(void)
{
    bool reported = s_defer_disconnect_visibility ? true : s_connected;
#ifdef DIAG_LOG
    ESP_LOGI(TAG,
             "DIAG: bt_is_connected() -> reported=%d (mock_connected=%d deferred=%d)",
             reported,
             s_connected,
             s_defer_disconnect_visibility);
#endif
    return reported;
}

/* Disconnect */
esp_err_t bt_disconnect(void)
{
    // If not connected, make this call idempotent and return ESP_OK.
    // Some tests expect calling disconnect when already disconnected to be
    // a no-op returning success; returning an esp_err_t negative/positive
    // mismatch led to observed numeric values in on-device logs. Normalize
    // to ESP_OK here to keep behavior deterministic for tests.
    if (!s_connected) {
        /* Ensure authoritative mock state is also cleared so future tests
         * observe a consistent disconnected view even if local state thinks
         * we're idle. The component mock treats redundant disconnects as
         * idempotent ESP_OK, so ignore the return value in this path. */
        (void)bt_mock_disconnect();
        ESP_LOGW(TAG, "bt_disconnect called when not connected - treating as success for tests");
        return ESP_OK;
    }

    esp_err_t mock_err = bt_mock_disconnect();
    if (mock_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "bt_mock_disconnect returned %d (0x%08x); continuing to clear local state",
                 (int)mock_err,
                 (unsigned int)mock_err);
    }

    // Update state
    s_connected = false;
    s_defer_disconnect_visibility = true;
#ifdef DIAG_LOG
    ESP_LOGI(TAG,
             "DIAG: deferring disconnect visibility until explicit release (mock path)");
    ESP_LOGI(TAG, "Mock: bt_disconnect invoked while connected; proceeding with disconnect simulation");
#endif
    s_current_connection.connected = false;
    s_current_connection.state = BT_CONNECTION_STATE_DISCONNECTED;
    s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    s_current_connection.name[0] = '\0';
    s_current_connection.addr[0] = '\0';

    bt_source_stub_sync_connected_state(false, NULL, NULL);

    /* After clearing authoritative state, double-check that the mock reports
     * disconnected so wait_for_authoritative_connected_state() can succeed. */
    if (bt_mock_is_connected()) {
        ESP_LOGW(TAG, "Authoritative mock still reports connected after disconnect");
    }

    return ESP_OK;
}

void bt_mock_release_disconnect_visibility(void)
{
#ifdef DIAG_LOG
    ESP_LOGI(TAG,
             "DIAG: clearing deferred disconnect visibility (defer=%d)",
             s_defer_disconnect_visibility);
#endif
    s_defer_disconnect_visibility = false;
}

/* Register connection callback */
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
 * Simulate a device disconnect
 */
esp_err_t bt_simulate_disconnect(void) {
    ESP_LOGI(TAG, "Simulating device disconnect");
    
    if (!s_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Keep a safe copy of the current connection info (string address already formatted)
    bt_connection_info_t prev_info;
    bool was_connected = s_connected;
    memcpy(&prev_info, &s_current_connection, sizeof(prev_info));

    // Ensure the authoritative device-level mock drops its connection too.
    esp_err_t mock_disc_err = bt_mock_force_disconnect();
    ESP_LOGI(TAG, "DIAG: bt_mock_force_disconnect -> %d", (int)mock_disc_err);
    if (mock_disc_err != ESP_OK) {
        ESP_LOGW(TAG, "bt_mock_disconnect returned %d", (int)mock_disc_err);
    }
    
    // Simulate disconnect
    s_connected = false;
    s_streaming = false;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;  // Changed from BT_STREAM_STATE_IDLE
    s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    s_defer_disconnect_visibility = false;
    /* Clear the public connection view so bt_get_connection_info() reports the
     * drop immediately when auto-reconnect is disabled. prev_device already
     * holds the authoritative pre-disconnect details used for reconnection. */
    s_current_connection.connected = false;
    s_current_connection.state = BT_CONNECTION_STATE_DISCONNECTED;
    s_current_connection.name[0] = '\0';
    s_current_connection.addr[0] = '\0';

    /* Propagate the disconnected state to the stub so authoritative and
     * stub-visible views stay aligned before any auto-reconnect attempt. */
    bt_source_stub_sync_connected_state(false, NULL, NULL);
    
    // IMPORTANT: Ensure prev_info has valid strings with proper null termination
    prev_info.name[sizeof(prev_info.name) - 1] = '\0';
    prev_info.addr[sizeof(prev_info.addr) - 1] = '\0';
    
    // If auto reconnect is enabled, reconnect with proper validation
    if (s_auto_reconnect_config.auto_reconnect_enabled && was_connected) {
        if (strlen(prev_info.name) > 0 && strlen(prev_info.addr) > 0) {
            uint16_t max_attempts = (s_auto_reconnect_config.retry_count == 0U) ? 1U : s_auto_reconnect_config.retry_count;
#if CONFIG_BT_MOCK_TESTING
            uint32_t delay_ms = s_test_reconnect_delay_overridden ? s_test_reconnect_delay_ms : s_auto_reconnect_config.retry_interval_ms;
#else
            uint32_t delay_ms = s_auto_reconnect_config.retry_interval_ms;
#endif
            bool reconnected = false;

            s_reconnect_attempts = 0;
            while (s_reconnect_attempts < max_attempts) {
                if (delay_ms > 0U) {
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                }

                esp_err_t attempt_res;
#if CONFIG_BT_MOCK_TESTING
                if (s_test_reconnect_results_len > 0U && s_test_reconnect_results_idx < s_test_reconnect_results_len) {
                    attempt_res = s_test_reconnect_results[s_test_reconnect_results_idx++];
                    if (attempt_res == ESP_OK) {
                        /* Keep the authoritative mock in sync when we short-circuit
                         * results for deterministic testing. */
                        esp_err_t sync_res = bt_mock_connect(prev_info.addr);
                        if (sync_res != ESP_OK) {
                            ESP_LOGW(TAG, "bt_mock_connect returned %d during forced reconnect", (int)sync_res);
                        }
                    }
                } else {
                    attempt_res = bt_mock_connect(prev_info.addr);
                }
#else
                attempt_res = bt_mock_connect(prev_info.addr);
#endif

                s_reconnect_attempts++;
                s_current_connection.retry_count = s_reconnect_attempts;

                if (attempt_res == ESP_OK) {
                    ESP_LOGI(TAG, "Auto reconnecting to %s (attempt %u)", prev_info.name, (unsigned)s_reconnect_attempts);

                    s_connected = true;
                    s_current_connection.connected = true;
                    s_current_connection.state = BT_CONNECTION_STATE_CONNECTED;
                    s_connection_state = BT_CONNECTION_STATE_CONNECTED;
                    s_defer_disconnect_visibility = false;

                    strncpy(s_current_connection.name, prev_info.name, sizeof(s_current_connection.name) - 1);
                    s_current_connection.name[sizeof(s_current_connection.name) - 1] = '\0';
                    strncpy(s_current_connection.addr, prev_info.addr, sizeof(s_current_connection.addr) - 1);
                    s_current_connection.addr[sizeof(s_current_connection.addr) - 1] = '\0';

                    bt_source_stub_sync_connected_state(true,
                                                        s_current_connection.addr,
                                                        (s_current_connection.name[0] != '\0') ? s_current_connection.name : NULL);

                    /* Reset retry counter after successful reconnection to mirror production behavior. */
                    s_current_connection.retry_count = 0;
                    s_reconnect_attempts = 0;
                    reconnected = true;
                    break;
                }
            }

            if (!reconnected) {
                ESP_LOGW(TAG, "Auto reconnect failed for %s after %u attempt(s)", prev_info.name, (unsigned)s_reconnect_attempts);
                s_connection_state = BT_CONNECTION_STATE_FAILED;
                s_current_connection.state = BT_CONNECTION_STATE_FAILED;
                s_current_connection.connected = false;
            }
        } else {
            ESP_LOGW(TAG, "Cannot auto-reconnect: device name/address invalid");
        }
    }
    
    return ESP_OK;
}

#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "platform_memory.h"
/* CODE_REVIEW8 P2: Need bt_manager_status_t but skip duplicate bt_device_t */
#define BT_SOURCE_SKIP_DEVICE_STRUCT 1
#include "bt_source.h"      /* For bt_manager_status_t (CODE_REVIEW8 P2) */
#undef BT_SOURCE_SKIP_DEVICE_STRUCT
#include "bt_app_core.h"    /* For bt_app_send_mgr_request() (CODE_REVIEW8 P2) */
#include "bt_api.h"
#include "bt_pairing_store.h"
#include "bt_scan.h"
#include "bt_connection.h"
#include "bt_events_gap.h"
#include "bt_events_a2dp.h"
#include "bt_events_avrc.h"
#include "nvs_storage.h"
#include "esp_bt.h"
#include "util_safe.h"
#include "audio_processor.h"
#include "platform_sync.h"  /* CODE_REVIEW8 P2.2 Phase 1: Platform shim for sync */
#undef TAG
#include "command_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "esp_rom_sys.h"

// Define this for ESP32 builds
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#define TAG "BT_MGR"
#else
#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#define TAG "BT_MGR"
#endif

#define safe_vsnprintf util_safe_vsnprintf
#define safe_snprintf util_safe_snprintf
#define safe_copy_str util_safe_copy_str
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset
#define parse_mac_bytes util_parse_mac

#if CONFIG_BT_MOCK_TESTING
#define BT_SOURCE_SKIP_DEVICE_STRUCT 1
#include "bt_source.h"
#undef BT_SOURCE_SKIP_DEVICE_STRUCT
/* Forward declarations from the Bluetooth abstraction so we can delegate
 * pairing to the mock implementation when mock testing configuration is
 * enabled. These symbols live in the bt_mock component. */
esp_err_t bt_start_pairing(const char* addr);
bt_pairing_method_t bt_mock_get_pairing_method(void);
esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size);
#endif

#ifdef UNIT_TEST
/* Test hooks defined in bt_manager_mocks.c; forward-declare so the unpair
 * paths in this translation unit can reference them across TUs. */
void bt_manager_test_record_unpair(const char* mac);
int bt_manager_test_should_force_unpair_failure(void);
int bt_manager_test_should_force_unpair_all_failure(void);
void bt_manager_test_record_unpair_all_call(int cleared_before, int removed);
void bt_manager_test_record_unpair_all_temp_alloc(int delta);
#endif

// Stop audio streaming
bt_err_t bt_stop_audio(void) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bt_ctx.audio_playing) {
        return ESP_OK; // Not playing
    }

#ifdef ESP_PLATFORM
    // Stop audio stream
    if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to suspend audio stream");  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Suspended audio streaming");  // NOLINT(bugprone-branch-clone)
#else
    // For testing without ESP-IDF
    bt_ctx.audio_playing = false;
#endif

    return ESP_OK;
}

// Start audio streaming
 bt_err_t bt_start_audio(void) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bt_ctx.connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (bt_ctx.audio_playing) {
        return ESP_OK; // Already playing
    }

#ifdef ESP_PLATFORM
    /* Stronger safety checks: ensure not only that total free DRAM looks
     * reasonable but also that a sufficiently large contiguous block is
     * available and that some internal (MALLOC_CAP_INTERNAL) memory exists.
     * This reduces false positives where total free bytes are sufficient
     * but fragmentation or internal-heap scarcity causes later allocations
     * (inside the BT stack/codec) to fail. */
    {
        const size_t DRAM_THRESHOLD = 64 * 1024; /* require >=64KiB total free */
        const size_t LARGEST_REQUIRED = 32 * 1024; /* require a 32KiB contiguous block */
        const size_t INTERNAL_REQUIRED = 8 * 1024; /* prefer >=8KiB internal-capable */

        size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        if (free_dram <= DRAM_THRESHOLD || largest < LARGEST_REQUIRED || internal_free < INTERNAL_REQUIRED) {
            ESP_LOGW(TAG, "bt_start_audio: skipping start due to low memory: free_dram=%zu largest=%zu internal_free=%zu; not starting A2DP", free_dram, largest, internal_free);  // NOLINT(bugprone-branch-clone)
            return ESP_ERR_NO_MEM;
        }
    }

    // Start audio stream
    if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio stream");  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started audio streaming");  // NOLINT(bugprone-branch-clone)
#else
    // For testing without ESP-IDF
    bt_ctx.audio_playing = true;
#endif

    return ESP_OK;
}

// Set volume level
 bt_err_t bt_set_volume(int volume) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume < 0 || volume > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    bt_ctx.volume = volume;

#ifdef ESP_PLATFORM
    // Volume control would be implemented here if supported
    // Currently not directly supported by ESP-IDF A2DP API
    ESP_LOGI(TAG, "Volume set to %d%%", volume);  // NOLINT(bugprone-branch-clone)
#endif

    return ESP_OK;
}

// Pair with a device
 bt_err_t bt_pair(const char* mac) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    // Convert MAC string to address
    esp_bd_addr_t bda;
    if (!bt_pairing_parse_mac(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_BT_MOCK_TESTING
    /* Under the mock-testing configuration use the higher-level pairing helper
     * so tests exercise the mock's deterministic flow instead of attempting to
     * touch the real controller. Capture the pending pairing state so command
     * handlers can accept PIN submissions later. */
    esp_err_t mock_err = bt_start_pairing(mac);
    if (mock_err == ESP_OK) {
        bt_pairing_method_t method = bt_mock_get_pairing_method();
        if (method == BT_PAIRING_METHOD_SSP) {
            char passkey_str[16] = {0};
            uint32_t passkey = 0;
            if (bt_mock_get_ssp_passkey(passkey_str, sizeof(passkey_str)) == ESP_OK) {
                passkey = (uint32_t)strtoul(passkey_str, NULL, 10);
            }
            bt_pairing_set_mock_state(bda, false, passkey);
        } else if (method == BT_PAIRING_METHOD_PIN) {
            bt_pairing_set_mock_state(bda, true, 0);
        } else {
            bt_pairing_prepare_for_initiation(bda);
        }
    }
    return mock_err;
#endif

    // Stop an active discovery session; some controllers reject bonding requests
    // while inquiry is running.
    if (bt_ctx.scanning) {
        if (esp_bt_gap_cancel_discovery() == ESP_OK) {
            bt_ctx.scanning = false;
        }
    }

    // Short-circuit if the controller already holds a bond for this device.
    int bonded = esp_bt_gap_get_bond_device_num();
    if (bonded > 0) {
        esp_bd_addr_t bonded_list[20] = {0};
        int list_capacity = bonded;
        if (list_capacity > (int)(sizeof(bonded_list) / sizeof(bonded_list[0]))) {
            list_capacity = sizeof(bonded_list) / sizeof(bonded_list[0]);
        }
        if (esp_bt_gap_get_bond_device_list(&list_capacity, bonded_list) == ESP_OK) {
            for (int i = 0; i < list_capacity; ++i) {
                if (memcmp(bonded_list[i], bda, sizeof(esp_bd_addr_t)) == 0) {
                    ESP_LOGI(TAG, "Device %s already bonded; skipping pairing", mac);  // NOLINT(bugprone-branch-clone)
                    return ESP_OK;
                }
            }
        }
    }

    bt_pairing_prepare_for_initiation(bda);

    // Initiate an A2DP connection to the target device.  A2DP requires L2CAP
    // channel security, so the controller always runs the SSP authentication
    // flow before the profile connects — this is the reliable way to drive
    // the AUTH_CMPL event that bt_pairing_handle_auth_complete needs.
    bt_err_t connect_err = bt_connect(mac);
    if (connect_err == ESP_OK) {
        ESP_LOGI(TAG, "Initiated A2DP connection to pair with %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to initiate A2DP connection for pairing with %s", mac);  // NOLINT(bugprone-branch-clone)
    bt_pairing_clear_pending();
    return ESP_FAIL;
#else
    esp_bd_addr_t bda = {0};
    if (!bt_pairing_parse_mac(mac, bda)) {
        return ESP_ERR_INVALID_ARG;
    }
    bt_pairing_prepare_for_initiation(bda);
    return ESP_OK;
#endif
}

// Unpair a device
 bt_err_t bt_unpair(const char* mac) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef UNIT_TEST
    if (bt_manager_test_should_force_unpair_failure()) {
        return ESP_FAIL;
    }
#endif

    esp_bd_addr_t bda = {0};
    if (!bt_pairing_parse_mac(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = esp_bt_gap_remove_bond_device(bda);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove controller bond for %s: %s", mac, esp_err_to_name(result));  // NOLINT(bugprone-branch-clone)
        goto exit;
    }

    esp_err_t storage_err = nvs_storage_remove_paired_device(mac);
    if (storage_err == ESP_OK) {
        ESP_LOGI(TAG, "Unpaired device: %s", mac);  // NOLINT(bugprone-branch-clone)
        result = ESP_OK;
    } else {
        const char* err_name = esp_err_to_name(storage_err);
        if (storage_err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Controller bond for %s removed, but NVS entry missing (%s)", mac, err_name);  // NOLINT(bugprone-branch-clone)
        } else {
            ESP_LOGE(TAG, "Failed to purge NVS entry for %s: %s", mac, err_name);  // NOLINT(bugprone-branch-clone)
        }
        result = storage_err;
        goto exit;
    }

exit:
#ifdef UNIT_TEST
    bt_manager_test_record_unpair(mac);
#endif

    return result;
}

// Unpair all devices
 bt_err_t bt_unpair_all(void) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t controller_status = ESP_OK;
    int removed_count = 0;

#ifdef UNIT_TEST
    if (bt_manager_test_should_force_unpair_all_failure()) {
        return ESP_FAIL;
    }
#endif

    int bonded_devices = esp_bt_gap_get_bond_device_num();
    if (bonded_devices < 0) {
        ESP_LOGW(TAG, "Unable to query bonded device count");  // NOLINT(bugprone-branch-clone)
        controller_status = ESP_FAIL;
    } else if (bonded_devices > 0) {
        int list_capacity = bonded_devices;
        esp_bd_addr_t *bond_list = (esp_bd_addr_t *)platform_calloc((size_t)list_capacity, sizeof(esp_bd_addr_t), PLATFORM_MEM_CAP_DEFAULT);
        if (!bond_list) {
            ESP_LOGE(TAG, "Insufficient memory to retrieve bonded device list");  // NOLINT(bugprone-branch-clone)
            return ESP_ERR_NO_MEM;
        }
#ifdef UNIT_TEST
        bt_manager_test_record_unpair_all_temp_alloc(+1);
#endif

        esp_err_t list_err = esp_bt_gap_get_bond_device_list(&list_capacity, bond_list);
        if (list_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to fetch bonded devices: %s", esp_err_to_name(list_err));  // NOLINT(bugprone-branch-clone)
            controller_status = list_err;
        } else {
            for (int i = 0; i < list_capacity; ++i) {
                esp_err_t err = esp_bt_gap_remove_bond_device(bond_list[i]);
                if (err == ESP_OK) {
                    removed_count++;
                } else {
                    if (controller_status == ESP_OK) {
                        controller_status = err;
                    }
                    char mac[18] = {0};
                    util_format_mac(bond_list[i], mac, sizeof(mac));
                    ESP_LOGE(TAG, "Failed to remove bond for %s: %s", mac, esp_err_to_name(err));  // NOLINT(bugprone-branch-clone)
                }
            }
        }

        platform_free(bond_list);
    #ifdef UNIT_TEST
        bt_manager_test_record_unpair_all_temp_alloc(-1);
    #endif
    }

    if (controller_status == ESP_OK) {
        ESP_LOGI(TAG, "Removed %d bonded device(s) from controller", removed_count);  // NOLINT(bugprone-branch-clone)
    }

    int cleared_before = 0;
    (void)nvs_storage_get_paired_count(&cleared_before);

    esp_err_t storage_err = nvs_storage_clear_paired_devices();
    if (storage_err != ESP_OK && storage_err != ESP_ERR_NOT_FOUND) {
        if (controller_status == ESP_OK) {
            controller_status = storage_err;
        }
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to clear paired devices from NVS: %s", esp_err_to_name(storage_err));  // NOLINT(bugprone-branch-clone)
#endif
    }

    safe_memset(&bt_ctx.paired_devices, sizeof(bt_ctx.paired_devices), 0, sizeof(bt_ctx.paired_devices));

#ifdef UNIT_TEST
    bt_manager_test_record_unpair_all_call(cleared_before, removed_count);
#endif

    return controller_status;
}

// Set PIN code for pairing
 bt_err_t bt_set_pin(const char* pin) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (pin == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    // Set PIN code
    esp_bt_pin_code_t pin_code;
    size_t pin_len = strlen(pin);
    if (pin_len > ESP_BT_PIN_CODE_LEN) {
        pin_len = ESP_BT_PIN_CODE_LEN;
    }
    safe_memcpy(pin_code, pin_len, pin, pin_len);
    esp_bt_pin_type_t pin_type = (pin_len == 16) ? ESP_BT_PIN_TYPE_VARIABLE : ESP_BT_PIN_TYPE_FIXED;

    esp_bt_gap_set_pin(pin_type, pin_len, pin_code);
    ESP_LOGI(TAG, "PIN code set to: %s, length: %d", pin, pin_len);  // NOLINT(bugprone-branch-clone)
#endif

    return ESP_OK;
}

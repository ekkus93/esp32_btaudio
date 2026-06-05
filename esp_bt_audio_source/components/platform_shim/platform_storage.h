/**
 * @file platform_storage.h
 * @brief Platform abstraction for non-volatile storage (NVS)
 *
 * This header provides a unified interface for key-value storage across:
 * - ESP32: Maps to NVS (nvs_flash) API
 * - Host: Maps to in-memory hash table for testing
 *
 * Eliminates platform-specific #ifdefs from nvs_storage component.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Platform-agnostic NVS error codes
 * 
 * These map to ESP-IDF NVS error codes on ESP32, and are defined for host builds.
 */
#ifdef ESP_PLATFORM
#include "nvs.h"  // Use ESP-IDF error codes directly
#define PLATFORM_ERR_STORAGE_NO_FREE_PAGES   ESP_ERR_NVS_NO_FREE_PAGES
#define PLATFORM_ERR_STORAGE_NEW_VERSION     ESP_ERR_NVS_NEW_VERSION_FOUND
#define PLATFORM_ERR_STORAGE_NOT_FOUND       ESP_ERR_NVS_NOT_FOUND
#define PLATFORM_ERR_STORAGE_INVALID_LENGTH  ESP_ERR_NVS_INVALID_LENGTH
#else
// Host build: define error codes to match ESP-IDF values
#define PLATFORM_ERR_STORAGE_NO_FREE_PAGES   0x1101  // ESP_ERR_NVS_NO_FREE_PAGES
#define PLATFORM_ERR_STORAGE_NEW_VERSION     0x1102  // ESP_ERR_NVS_NEW_VERSION_FOUND
#define PLATFORM_ERR_STORAGE_NOT_FOUND       0x1105  // ESP_ERR_NVS_NOT_FOUND
#define PLATFORM_ERR_STORAGE_INVALID_LENGTH  0x110A  // ESP_ERR_NVS_INVALID_LENGTH
#endif

/**
 * @brief Opaque handle for storage namespace
 *
 * ESP32: nvs_handle_t (uint32_t, fits in uintptr_t on 32-bit)
 * Host:  Pointer to in-memory storage context cast to integer
 *
 * uintptr_t is used instead of uint32_t so the host implementation can round-
 * trip a pointer through the handle without truncation on 64-bit systems.
 */
typedef uintptr_t platform_storage_handle_t;

/**
 * @brief Storage access mode
 */
typedef enum {
    PLATFORM_STORAGE_READONLY = 0,   ///< Read only
    PLATFORM_STORAGE_READWRITE = 1   ///< Read and write
} platform_storage_mode_t;

/**
 * @brief Initialize the storage subsystem
 *
 * @return ESP_OK on success, error code otherwise
 *
 * ESP32: Calls nvs_flash_init()
 * Host: Initializes in-memory hash table
 */
esp_err_t platform_storage_init(void);

/**
 * @brief Erase all storage data
 *
 * @return ESP_OK on success, error code otherwise
 *
 * ESP32: Calls nvs_flash_erase()
 * Host: Clears in-memory hash table
 */
esp_err_t platform_storage_erase(void);

/**
 * @brief Open a storage namespace
 *
 * @param namespace Namespace name (e.g., "bt_audio_cfg")
 * @param mode Access mode (readonly or readwrite)
 * @param handle Output handle to opened namespace
 *
 * @return ESP_OK on success, error code otherwise
 *
 * ESP32: Calls nvs_open()
 * Host: Creates/retrieves namespace in hash table
 */
esp_err_t platform_storage_open(const char* namespace, platform_storage_mode_t mode, platform_storage_handle_t* handle);

/**
 * @brief Close a storage namespace
 *
 * @param handle Handle to close
 *
 * @return ESP_OK on success
 *
 * ESP32: Calls nvs_close()
 * Host: Releases namespace handle
 */
esp_err_t platform_storage_close(platform_storage_handle_t handle);

/**
 * @brief Get a 32-bit integer value
 *
 * @param handle Storage handle
 * @param key Key name
 * @param out_value Pointer to store the value
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key doesn't exist
 */
esp_err_t platform_storage_get_i32(platform_storage_handle_t handle, const char* key, int32_t* out_value);

/**
 * @brief Set a 32-bit integer value
 *
 * @param handle Storage handle
 * @param key Key name
 * @param value Value to store
 *
 * @return ESP_OK on success
 */
esp_err_t platform_storage_set_i32(platform_storage_handle_t handle, const char* key, int32_t value);

/**
 * @brief Get a string value
 *
 * @param handle Storage handle
 * @param key Key name
 * @param out_str Buffer to store the string
 * @param length Pointer to buffer length (input/output)
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key doesn't exist
 *
 * On input, *length contains the buffer size.
 * On output, *length contains the actual string length.
 */
esp_err_t platform_storage_get_str(platform_storage_handle_t handle, const char* key, char* out_str, size_t* length);

/**
 * @brief Set a string value
 *
 * @param handle Storage handle
 * @param key Key name
 * @param value String value to store
 *
 * @return ESP_OK on success
 */
esp_err_t platform_storage_set_str(platform_storage_handle_t handle, const char* key, const char* value);

/**
 * @brief Get a binary blob value
 *
 * @param handle Storage handle
 * @param key Key name
 * @param out_value Buffer to store the blob
 * @param length Pointer to buffer length (input/output)
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key doesn't exist
 *
 * On input, *length contains the buffer size.
 * On output, *length contains the actual blob length.
 */
esp_err_t platform_storage_get_blob(platform_storage_handle_t handle, const char* key, void* out_value, size_t* length);

/**
 * @brief Set a binary blob value
 *
 * @param handle Storage handle
 * @param key Key name
 * @param value Blob data to store
 * @param length Blob length in bytes
 *
 * @return ESP_OK on success
 */
esp_err_t platform_storage_set_blob(platform_storage_handle_t handle, const char* key, const void* value, size_t length);

/**
 * @brief Erase a key from storage
 *
 * @param handle Storage handle
 * @param key Key name to erase
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key doesn't exist
 */
esp_err_t platform_storage_erase_key(platform_storage_handle_t handle, const char* key);

/**
 * @brief Commit pending writes to storage
 *
 * @param handle Storage handle
 *
 * @return ESP_OK on success
 *
 * ESP32: Calls nvs_commit() - writes to flash
 * Host: No-op (in-memory storage is always "committed")
 */
esp_err_t platform_storage_commit(platform_storage_handle_t handle);

#ifdef __cplusplus
}
#endif

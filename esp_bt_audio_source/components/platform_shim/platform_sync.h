/**
 * @file platform_sync.h
 * @brief Platform abstraction layer for synchronization primitives
 * 
 * This shim provides a consistent interface for synchronization primitives
 * across ESP32 (FreeRTOS) and host test environments. It eliminates the need
 * for #ifdef ESP_PLATFORM / #ifdef UNIT_TEST scattered throughout the codebase.
 * 
 * CODE_REVIEW8 P2.2 - Platform Shim Layer
 * 
 * Design principles:
 * - ESP32 builds: Use FreeRTOS semaphores for real thread synchronization
 * - Host builds: Use simple mock implementations (single-threaded test environment)
 * - Errors return esp_err_t for consistency with ESP-IDF conventions
 * - Timeouts specified in milliseconds (portable across platforms)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for a binary semaphore
 * 
 * Platform-specific implementation is hidden behind this handle.
 * ESP32: Maps to SemaphoreHandle_t (FreeRTOS binary semaphore)
 * Host: Simple boolean flag (no actual blocking in single-threaded tests)
 */
typedef struct platform_binary_sem_s* platform_binary_sem_t;

/**
 * @brief Create a binary semaphore
 * 
 * Binary semaphores are used for synchronization between tasks.
 * Typical use case: Request/response pattern where one task signals another.
 * 
 * @return Semaphore handle on success, NULL on failure
 * 
 * Example:
 * @code
 * platform_binary_sem_t sem = platform_binary_sem_create();
 * if (sem == NULL) {
 *     ESP_LOGE(TAG, "Failed to create semaphore");
 *     return ESP_ERR_NO_MEM;
 * }
 * @endcode
 */
platform_binary_sem_t platform_binary_sem_create(void);

/**
 * @brief Delete (destroy) a binary semaphore
 * 
 * Frees resources associated with the semaphore. After deletion,
 * the handle must not be used.
 * 
 * @param sem Semaphore handle to delete (can be NULL - no-op in that case)
 * 
 * Example:
 * @code
 * platform_binary_sem_delete(sem);
 * sem = NULL;  // Good practice: nullify after delete
 * @endcode
 */
void platform_binary_sem_delete(platform_binary_sem_t sem);

/**
 * @brief Take (acquire) a binary semaphore with timeout
 * 
 * Blocks the calling task until:
 * 1. The semaphore is given by another task (success), OR
 * 2. The timeout expires (timeout error)
 * 
 * @param sem Semaphore handle
 * @param timeout_ms Maximum time to wait in milliseconds
 *                   - 0 = return immediately (no wait)
 *                   - UINT32_MAX = wait forever (blocking until semaphore available)
 * 
 * @return ESP_OK on success (semaphore acquired)
 *         ESP_ERR_TIMEOUT if timeout expired before semaphore available
 *         ESP_ERR_INVALID_ARG if sem is NULL
 * 
 * Example (request/response pattern):
 * @code
 * platform_binary_sem_t response_sem = platform_binary_sem_create();
 * 
 * // Post request to another task
 * post_request_to_worker_task(request, response_sem);
 * 
 * // Wait up to 100ms for response
 * esp_err_t err = platform_binary_sem_take(response_sem, 100);
 * if (err == ESP_OK) {
 *     // Success: worker task gave semaphore (response ready)
 *     process_response();
 * } else if (err == ESP_ERR_TIMEOUT) {
 *     ESP_LOGW(TAG, "Worker task timeout - no response in 100ms");
 * }
 * 
 * platform_binary_sem_delete(response_sem);
 * @endcode
 */
esp_err_t platform_binary_sem_take(platform_binary_sem_t sem, uint32_t timeout_ms);

/**
 * @brief Give (release) a binary semaphore
 * 
 * Signals that a resource is available or an event has occurred.
 * If another task is blocked waiting on this semaphore (via take()),
 * it will be unblocked.
 * 
 * @param sem Semaphore handle
 * 
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if sem is NULL
 * 
 * Example (worker task responding):
 * @code
 * void worker_task_handler(request_t* req) {
 *     // Process request
 *     fill_response_data(req->response);
 *     
 *     // Signal requester that response is ready
 *     platform_binary_sem_give(req->response_sem);
 * }
 * @endcode
 */
esp_err_t platform_binary_sem_give(platform_binary_sem_t sem);

#ifdef __cplusplus
}
#endif

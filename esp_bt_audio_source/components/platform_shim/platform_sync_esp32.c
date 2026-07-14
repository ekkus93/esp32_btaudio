/**
 * @file platform_sync_esp32.c
 * @brief ESP32 (FreeRTOS) implementation of synchronization primitives
 * 
 * Provides real thread-safe synchronization using FreeRTOS binary semaphores.
 * This implementation is used in ESP_PLATFORM builds (actual hardware).
 * 
 * CODE_REVIEW8 P2.2 - Platform Shim Layer
 */

#include "platform_sync.h"
#include "platform_memory.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <stddef.h>

static const char* TAG = "PLAT_SYNC";

/**
 * @brief Internal structure for binary semaphore
 * 
 * Wraps FreeRTOS SemaphoreHandle_t with metadata for debugging
 */
struct platform_binary_sem_s {
    SemaphoreHandle_t freertos_sem;  ///< Underlying FreeRTOS binary semaphore
};

platform_binary_sem_t platform_binary_sem_create(void) {
    // Allocate wrapper structure
    struct platform_binary_sem_s* sem = (struct platform_binary_sem_s*)platform_malloc(sizeof(struct platform_binary_sem_s), PLATFORM_MEM_CAP_DEFAULT);
    if (sem == NULL) {
        ESP_LOGE(TAG, "Failed to allocate semaphore structure");
        return NULL;
    }

    // Create FreeRTOS binary semaphore
    sem->freertos_sem = xSemaphoreCreateBinary();
    if (sem->freertos_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS binary semaphore");
        free(sem);
        return NULL;
    }

    ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, TAG, "Created binary semaphore %p", sem);
    return sem;
}

void platform_binary_sem_delete(platform_binary_sem_t sem) {
    if (sem == NULL) {
        return;  // Gracefully handle NULL (idempotent delete)
    }

    if (sem->freertos_sem != NULL) {
        vSemaphoreDelete(sem->freertos_sem);
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, TAG, "Deleted binary semaphore %p", sem);
    }

    platform_free(sem);
}

esp_err_t platform_binary_sem_take(platform_binary_sem_t sem, uint32_t timeout_ms) {
    if (sem == NULL || sem->freertos_sem == NULL) {
        ESP_LOGE(TAG, "Invalid semaphore handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    // Convert milliseconds to FreeRTOS ticks
    TickType_t timeout_ticks;
    if (timeout_ms == UINT32_MAX) {
        timeout_ticks = portMAX_DELAY;  // Wait forever
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }

    // Attempt to take semaphore
    BaseType_t result = xSemaphoreTake(sem->freertos_sem, timeout_ticks);
    
    if (result == pdTRUE) {
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, TAG, "Semaphore %p taken successfully", sem);
        return ESP_OK;
    } else {
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, TAG, "Semaphore %p take timeout (%lu ms)", sem, (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t platform_binary_sem_give(platform_binary_sem_t sem) {
    if (sem == NULL || sem->freertos_sem == NULL) {
        ESP_LOGE(TAG, "Invalid semaphore handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    // Give the semaphore
    BaseType_t result = xSemaphoreGive(sem->freertos_sem);

    if (result == pdTRUE) {
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, TAG, "Semaphore %p given successfully", sem);
        return ESP_OK;
    } else {
        // This can happen if semaphore is already available (not taken)
        // Not necessarily an error, but worth logging
        ESP_LOGW(TAG, "Semaphore %p give failed (already available?)", sem);
        return ESP_OK;  // Return OK anyway - idempotent give
    }
}

/* ============================================================================
 * Mutex implementation (ESP32/FreeRTOS)
 * ============================================================================ */

struct platform_mutex_s {
    SemaphoreHandle_t freertos_mutex;
};

platform_mutex_t platform_mutex_create(void) {
    struct platform_mutex_s *mutex = (struct platform_mutex_s *)platform_malloc(
        sizeof(struct platform_mutex_s), PLATFORM_MEM_CAP_DEFAULT);
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mutex structure");
        return NULL;
    }

    mutex->freertos_mutex = xSemaphoreCreateMutex();
    if (mutex->freertos_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS mutex");
        platform_free(mutex);
        return NULL;
    }

    ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, TAG, "Created mutex %p", mutex);
    return mutex;
}

void platform_mutex_delete(platform_mutex_t mutex) {
    if (mutex == NULL) {
        return;
    }

    if (mutex->freertos_mutex != NULL) {
        vSemaphoreDelete(mutex->freertos_mutex);
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, TAG, "Deleted mutex %p", mutex);
    }

    platform_free(mutex);
}

esp_err_t platform_mutex_lock(platform_mutex_t mutex, uint32_t timeout_ms) {
    if (mutex == NULL || mutex->freertos_mutex == NULL) {
        ESP_LOGE(TAG, "Invalid mutex handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t timeout_ticks;
    if (timeout_ms == PLATFORM_WAIT_FOREVER) {
        timeout_ticks = portMAX_DELAY;
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }

    BaseType_t result = xSemaphoreTake(mutex->freertos_mutex, timeout_ticks);

    if (result == pdTRUE) {
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, TAG, "Mutex %p locked successfully", mutex);
        return ESP_OK;
    } else {
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, TAG, "Mutex %p lock timeout (%lu ms)",
                            mutex, (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t platform_mutex_unlock(platform_mutex_t mutex) {
    if (mutex == NULL || mutex->freertos_mutex == NULL) {
        ESP_LOGE(TAG, "Invalid mutex handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t result = xSemaphoreGive(mutex->freertos_mutex);

    if (result == pdTRUE) {
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, TAG, "Mutex %p unlocked successfully", mutex);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Mutex %p unlock failed", mutex);
        return ESP_FAIL;
    }
}

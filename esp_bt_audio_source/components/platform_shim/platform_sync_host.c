/**
 * @file platform_sync_host.c
 * @brief Host/test implementation of synchronization primitives
 * 
 * Provides mock (non-blocking) synchronization for single-threaded test environment.
 * Since host tests run in a single thread, real blocking synchronization would deadlock.
 * Instead, we provide simple flag-based implementations that satisfy the API contract.
 * 
 * CODE_REVIEW8 P2.2 - Platform Shim Layer
 * 
 * NOTE: This implementation is NOT thread-safe - it's designed for single-threaded
 * host tests only. The ESP32 implementation (platform_sync_esp32.c) provides real
 * thread-safe synchronization via FreeRTOS.
 */

#include "platform_sync.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <string.h>

static const char* TAG = "PLAT_SYNC_HOST";

/**
 * @brief Internal structure for binary semaphore (host mock)
 * 
 * Simple boolean flag - no actual blocking since tests are single-threaded
 */
struct platform_binary_sem_s {
    bool available;  ///< Semaphore state: true = available (can take), false = taken
};

platform_binary_sem_t platform_binary_sem_create(void) {
    struct platform_binary_sem_s* sem = (struct platform_binary_sem_s*)calloc(1, sizeof(struct platform_binary_sem_s));
    if (sem == NULL) {
        ESP_LOGE(TAG, "Failed to allocate semaphore structure");
        return NULL;
    }

    // Binary semaphores start unavailable (must be given before first take)
    sem->available = false;

    ESP_LOGD(TAG, "Created mock binary semaphore %p", (void*)sem);
    return sem;
}

void platform_binary_sem_delete(platform_binary_sem_t sem) {
    if (sem == NULL) {
        return;  // Gracefully handle NULL (idempotent delete)
    }

    ESP_LOGD(TAG, "Deleted mock binary semaphore %p", (void*)sem);
    free(sem);
}

esp_err_t platform_binary_sem_take(platform_binary_sem_t sem, uint32_t timeout_ms) {
    if (sem == NULL) {
        ESP_LOGE(TAG, "Invalid semaphore handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    // Host test implementation: immediate return (no real blocking)
    // In single-threaded tests, if semaphore is not available, it would deadlock
    // So we return timeout immediately if not available
    
    if (sem->available) {
        sem->available = false;  // Take the semaphore (consume it)
        ESP_LOGV(TAG, "Mock semaphore %p taken successfully", (void*)sem);
        return ESP_OK;
    } else {
        // Not available - would block forever in single-threaded environment
        // Return timeout immediately (simulates timeout expiry)
        ESP_LOGV(TAG, "Mock semaphore %p not available (simulated timeout)", (void*)sem);
        return ESP_ERR_TIMEOUT;
    }
    
    // Note: timeout_ms parameter is ignored in host tests since we don't actually block
    (void)timeout_ms;
}

esp_err_t platform_binary_sem_give(platform_binary_sem_t sem) {
    if (sem == NULL) {
        ESP_LOGE(TAG, "Invalid semaphore handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    // Make semaphore available for next take
    sem->available = true;

    ESP_LOGV(TAG, "Mock semaphore %p given successfully", (void*)sem);
    return ESP_OK;
}

/* ============================================================================
 * Mutex implementation (host/pthread)
 *
 * Uses a real pthread mutex so lock/unlock semantics are correct for
 * single-threaded host tests.  Real contention cannot occur in the
 * single-threaded test harness, but the mutex provides correct
 * lock/unlock accounting for tests that verify the API contract.
 * ============================================================================ */

#include <pthread.h>

struct platform_mutex_s {
    pthread_mutex_t pthread_mutex;
};

platform_mutex_t platform_mutex_create(void) {
    struct platform_mutex_s *mutex = (struct platform_mutex_s *)calloc(
        1, sizeof(struct platform_mutex_s));
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mutex structure");
        return NULL;
    }

    if (pthread_mutex_init(&mutex->pthread_mutex, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to initialize pthread mutex");
        free(mutex);
        return NULL;
    }

    ESP_LOGD(TAG, "Created mutex %p", mutex);
    return mutex;
}

void platform_mutex_delete(platform_mutex_t mutex) {
    if (mutex == NULL) {
        return;
    }

    pthread_mutex_destroy(&mutex->pthread_mutex);
    ESP_LOGD(TAG, "Deleted mutex %p", mutex);
    free(mutex);
}

esp_err_t platform_mutex_lock(platform_mutex_t mutex, uint32_t timeout_ms) {
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Invalid mutex handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    (void)timeout_ms; /* timeout not supported in host (pthread needs timed
                        * lock API which is Linux-specific). Lock/unlock is
                        * sufficient for single-threaded host tests. */

    int rc = pthread_mutex_lock(&mutex->pthread_mutex);
    if (rc != 0) {
        ESP_LOGE(TAG, "pthread_mutex_lock failed: %s", strerror(rc));
        return ESP_FAIL;
    }

    ESP_LOGV(TAG, "Mutex %p locked successfully", (void *)mutex);
    return ESP_OK;
}

esp_err_t platform_mutex_unlock(platform_mutex_t mutex) {
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Invalid mutex handle (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    int rc = pthread_mutex_unlock(&mutex->pthread_mutex);
    if (rc != 0) {
        ESP_LOGE(TAG, "pthread_mutex_unlock failed: %s", strerror(rc));
        return ESP_FAIL;
    }

    ESP_LOGV(TAG, "Mutex %p unlocked successfully", (void *)mutex);
    return ESP_OK;
}

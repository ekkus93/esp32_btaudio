#include "hfp_i2s_output_internal.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>

#include "util_safe.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static void release_allocations(void)
{
    if (s_output.writer_pcm != NULL) {
        hfp_i2s_output_ops_free(s_output.writer_pcm);
        s_output.writer_pcm = NULL;
    }
    if (s_output.ring_storage != NULL) {
        hfp_i2s_output_ops_free(s_output.ring_storage);
        s_output.ring_storage = NULL;
    }
    memset(&s_output.ring, 0, sizeof(s_output.ring));
}

static void delete_task_sync(void)
{
    platform_binary_sem_delete(s_output.start_gate);
    platform_binary_sem_delete(s_output.stopped);
    s_output.start_gate = NULL;
    s_output.stopped = NULL;
}

static esp_err_t wait_for_pushes_to_drain(uint32_t timeout_ms)
{
#ifdef ESP_PLATFORM
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ticks == 0U) timeout_ticks = 1U;
    TickType_t started = xTaskGetTickCount();
    for (;;) {
        if (atomic_load_explicit(&s_output.active_pushes,
                                 memory_order_acquire) == 0U) {
            return ESP_OK;
        }
        if ((xTaskGetTickCount() - started) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1U);
    }
#else
    (void)timeout_ms;
    return atomic_load_explicit(&s_output.active_pushes,
                                memory_order_acquire) == 0U
        ? ESP_OK : ESP_ERR_TIMEOUT;
#endif
}

static esp_err_t rollback_start(esp_err_t cause)
{
    HFP_I2S_LOGE("start failed; rolling back: error=%d", (int)cause);
    bool task_created = s_output.task_created;
    bool channel_enabled = s_output.channel_enabled;

    atomic_store_explicit(&s_output.accepting_pushes, false,
                          memory_order_release);
    atomic_store_explicit(&s_output.stop_requested, true, memory_order_release);

    if (task_created) {
        (void)hfp_i2s_output_ops_task_release_start(s_output.task);
        esp_err_t wait_err = hfp_i2s_output_ops_task_wait_stopped(
            s_output.task, s_output.config.stop_timeout_ms);
        if (wait_err != ESP_OK) {
            HFP_I2S_LOGE("start rollback task stop failed: cause=%d cleanup=%d",
                         (int)cause, (int)wait_err);
            if (hfp_i2s_output_lock() == ESP_OK) {
                s_output.start_failures++;
                hfp_i2s_output_enter_quarantine_locked(wait_err);
                (void)hfp_i2s_output_unlock(ESP_OK);
            }
            return wait_err;
        }
        s_output.task_confirmed_stopped = true;
    }

    esp_err_t cleanup_error = ESP_OK;
    if (channel_enabled) {
        esp_err_t err = hfp_i2s_output_ops_channel_disable(s_output.channel);
        if (err != ESP_OK) cleanup_error = err;
        else s_output.channel_enabled = false;
    }
    if (s_output.channel != NULL) {
        esp_err_t err = hfp_i2s_output_ops_channel_delete(s_output.channel);
        if (err != ESP_OK && cleanup_error == ESP_OK) cleanup_error = err;
        if (err == ESP_OK) s_output.channel = NULL;
    }

    if (cleanup_error != ESP_OK) {
        HFP_I2S_LOGE("start rollback resource cleanup failed: cause=%d cleanup=%d",
                     (int)cause, (int)cleanup_error);
        if (hfp_i2s_output_lock() == ESP_OK) {
            s_output.start_failures++;
            hfp_i2s_output_enter_quarantine_locked(cleanup_error);
            (void)hfp_i2s_output_unlock(ESP_OK);
        }
        return cleanup_error;
    }

    release_allocations();
    delete_task_sync();
    if (hfp_i2s_output_lock() == ESP_OK) {
        s_output.start_failures++;
        s_output.state = HFP_I2S_OUTPUT_STOPPED;
        s_output.last_error = cause;
        s_output.task = NULL;
        s_output.task_created = false;
        s_output.task_confirmed_stopped = false;
        hfp_i2s_output_clear_session_locked();
        (void)hfp_i2s_output_unlock(ESP_OK);
    }
    return cause;
}

esp_err_t hfp_i2s_output_start(uint32_t generation, const char *peer_mac)
{
    uint8_t parsed[6];
    if (generation == 0U || peer_mac == NULL ||
        !util_parse_mac(peer_mac, parsed)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = hfp_i2s_output_lock();
    if (err != ESP_OK) return err;
    if (!s_output.initialized) return hfp_i2s_output_unlock(ESP_ERR_INVALID_STATE);
    if (s_output.state == HFP_I2S_OUTPUT_QUARANTINED ||
        s_output.state == HFP_I2S_OUTPUT_FAULTED) {
        return hfp_i2s_output_unlock(ESP_ERR_INVALID_STATE);
    }
    if (s_output.state == HFP_I2S_OUTPUT_RUNNING) {
        bool same = atomic_load_explicit(&s_output.generation,
                                         memory_order_acquire) == generation &&
                    strcasecmp(s_output.peer_mac, peer_mac) == 0;
        return hfp_i2s_output_unlock(same ? ESP_OK : ESP_ERR_INVALID_STATE);
    }
    if (s_output.state != HFP_I2S_OUTPUT_STOPPED) {
        return hfp_i2s_output_unlock(ESP_ERR_INVALID_STATE);
    }

    s_output.start_calls++;
    s_output.state = HFP_I2S_OUTPUT_STARTING;
    atomic_store_explicit(&s_output.generation, generation,
                          memory_order_release);
    util_safe_copy_str(s_output.peer_mac, sizeof(s_output.peer_mac), peer_mac);
    s_output.last_error = ESP_OK;
    atomic_store_explicit(&s_output.stop_requested, false, memory_order_release);
    atomic_store_explicit(&s_output.accepting_pushes, false,
                          memory_order_release);
    err = hfp_i2s_output_unlock(ESP_OK);
    if (err != ESP_OK) return err;

    s_output.ring_storage = hfp_i2s_output_ops_alloc(s_output.config.ring_bytes);
    if (s_output.ring_storage == NULL) return rollback_start(ESP_ERR_NO_MEM);
    size_t scratch_bytes = s_output.config.writer_samples * sizeof(int16_t);
    if (scratch_bytes / sizeof(int16_t) != s_output.config.writer_samples) {
        return rollback_start(ESP_ERR_INVALID_SIZE);
    }
    s_output.writer_pcm = hfp_i2s_output_ops_alloc(scratch_bytes);
    if (s_output.writer_pcm == NULL) return rollback_start(ESP_ERR_NO_MEM);

    err = hfp_pcm_ring_init(&s_output.ring, s_output.ring_storage,
                            s_output.config.ring_bytes, generation);
    if (err != ESP_OK) return rollback_start(err);

    s_output.start_gate = platform_binary_sem_create();
    s_output.stopped = platform_binary_sem_create();
    if (s_output.start_gate == NULL || s_output.stopped == NULL) {
        return rollback_start(ESP_ERR_NO_MEM);
    }
    hfp_i2s_output_drain_sem(s_output.start_gate);
    hfp_i2s_output_drain_sem(s_output.stopped);

    err = hfp_i2s_output_ops_channel_new(&s_output.config, &s_output.channel);
    if (err != ESP_OK) return rollback_start(err);
    err = hfp_i2s_output_ops_channel_init_mode(s_output.channel, &s_output.config);
    if (err != ESP_OK) return rollback_start(err);
    err = hfp_i2s_output_ops_task_create(&s_output.task);
    if (err != ESP_OK) return rollback_start(err);
    s_output.task_created = true;
    err = hfp_i2s_output_ops_channel_enable(s_output.channel);
    if (err != ESP_OK) return rollback_start(err);
    s_output.channel_enabled = true;

    err = hfp_i2s_output_lock();
    if (err != ESP_OK) return rollback_start(err);
    s_output.state = HFP_I2S_OUTPUT_RUNNING;
    atomic_store_explicit(&s_output.accepting_pushes, true,
                          memory_order_release);
    err = hfp_i2s_output_unlock(ESP_OK);
    if (err != ESP_OK) return rollback_start(err);

    err = hfp_i2s_output_ops_task_release_start(s_output.task);
    if (err != ESP_OK) return rollback_start(err);
    HFP_I2S_LOGI("started: generation=%" PRIu32 " peer=%s", generation,
                 peer_mac);
    return ESP_OK;
}

static esp_err_t stop_and_cleanup(uint32_t timeout_ms)
{
    atomic_store_explicit(&s_output.accepting_pushes, false,
                          memory_order_release);
    esp_err_t err = wait_for_pushes_to_drain(timeout_ms);
    if (err != ESP_OK) {
        if (hfp_i2s_output_lock() == ESP_OK) {
            s_output.stop_timeouts++;
            hfp_i2s_output_enter_quarantine_locked(err);
            (void)hfp_i2s_output_unlock(ESP_OK);
        }
        return err;
    }

    atomic_store_explicit(&s_output.stop_requested, true, memory_order_release);
    (void)hfp_i2s_output_ops_task_release_start(s_output.task);
    err = hfp_i2s_output_ops_task_wait_stopped(s_output.task, timeout_ms);
    if (err != ESP_OK) {
        if (hfp_i2s_output_lock() == ESP_OK) {
            s_output.stop_timeouts++;
            hfp_i2s_output_enter_quarantine_locked(err);
            (void)hfp_i2s_output_unlock(ESP_OK);
        }
        return err;
    }
    s_output.task_confirmed_stopped = true;

    esp_err_t first_error = ESP_OK;
    if (s_output.channel_enabled) {
        err = hfp_i2s_output_ops_channel_disable(s_output.channel);
        if (err != ESP_OK) first_error = err;
        else s_output.channel_enabled = false;
    }
    if (s_output.channel != NULL) {
        err = hfp_i2s_output_ops_channel_delete(s_output.channel);
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        if (err == ESP_OK) s_output.channel = NULL;
    }
    if (first_error != ESP_OK) {
        if (hfp_i2s_output_lock() == ESP_OK) {
            hfp_i2s_output_enter_quarantine_locked(first_error);
            (void)hfp_i2s_output_unlock(ESP_OK);
        }
        return first_error;
    }

    release_allocations();
    delete_task_sync();
    return ESP_OK;
}

esp_err_t hfp_i2s_output_stop(uint32_t timeout_ms)
{
    if (timeout_ms == 0U) return ESP_ERR_INVALID_ARG;
    esp_err_t err = hfp_i2s_output_lock();
    if (err != ESP_OK) return err;
    if (!s_output.initialized) return hfp_i2s_output_unlock(ESP_ERR_INVALID_STATE);
    s_output.stop_calls++;
    if (s_output.state == HFP_I2S_OUTPUT_STOPPED) {
        return hfp_i2s_output_unlock(ESP_OK);
    }
    if (s_output.state == HFP_I2S_OUTPUT_QUARANTINED) {
        return hfp_i2s_output_unlock(ESP_ERR_INVALID_STATE);
    }
    bool was_faulted = s_output.state == HFP_I2S_OUTPUT_FAULTED;
    if (!was_faulted && s_output.state != HFP_I2S_OUTPUT_RUNNING) {
        return hfp_i2s_output_unlock(ESP_ERR_INVALID_STATE);
    }
    if (!was_faulted) s_output.state = HFP_I2S_OUTPUT_STOPPING;
    err = hfp_i2s_output_unlock(ESP_OK);
    if (err != ESP_OK) return err;

    err = stop_and_cleanup(timeout_ms);
    if (err != ESP_OK) return err;

    esp_err_t result = ESP_OK;
    err = hfp_i2s_output_lock();
    if (err != ESP_OK) return err;
    if (was_faulted) {
        result = s_output.last_error != ESP_OK ? s_output.last_error : ESP_FAIL;
        s_output.state = HFP_I2S_OUTPUT_FAULTED;
    } else {
        s_output.state = HFP_I2S_OUTPUT_STOPPED;
        s_output.last_error = ESP_OK;
    }
    s_output.degraded = false;
    s_output.consecutive_underflows = 0U;
    s_output.task = NULL;
    s_output.task_created = false;
    s_output.task_confirmed_stopped = false;
    hfp_i2s_output_clear_session_locked();
    err = hfp_i2s_output_unlock(result);
    if (err != result && result == ESP_OK) return err;
    result = err;
    if (result == ESP_OK) HFP_I2S_LOGI("stopped");
    else HFP_I2S_LOGE("stopped after writer fault: error=%d", (int)result);
    return result;
}

esp_err_t hfp_i2s_output_deinit(void)
{
    if (!s_output.initialized) return ESP_OK;
    if (s_output.state == HFP_I2S_OUTPUT_RUNNING) {
        esp_err_t err = hfp_i2s_output_stop(s_output.config.stop_timeout_ms);
        if (err != ESP_OK) return err;
    }
    esp_err_t err = hfp_i2s_output_lock();
    if (err != ESP_OK) return err;
    if (s_output.state == HFP_I2S_OUTPUT_QUARANTINED ||
        s_output.task_created || s_output.channel != NULL ||
        s_output.ring_storage != NULL || s_output.writer_pcm != NULL) {
        return hfp_i2s_output_unlock(ESP_ERR_INVALID_STATE);
    }
    platform_mutex_t lock = s_output.lock;
    memset(&s_output, 0, sizeof(s_output));
    (void)platform_mutex_unlock(lock);
    platform_mutex_delete(lock);
    return ESP_OK;
}

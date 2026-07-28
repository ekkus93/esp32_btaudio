#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hfp_i2s_output.h"
#include "platform_sync.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define HFP_I2S_LOGE(fmt, ...) ESP_LOGE("hfp_i2s_output", fmt, ##__VA_ARGS__)
#define HFP_I2S_LOGW(fmt, ...) ESP_LOGW("hfp_i2s_output", fmt, ##__VA_ARGS__)
#define HFP_I2S_LOGI(fmt, ...) ESP_LOGI("hfp_i2s_output", fmt, ##__VA_ARGS__)
#else
#define HFP_I2S_LOGE(...) do { } while (0)
#define HFP_I2S_LOGW(...) do { } while (0)
#define HFP_I2S_LOGI(...) do { } while (0)
#endif

typedef struct {
    platform_mutex_t lock;
    platform_binary_sem_t start_gate;
    platform_binary_sem_t stopped;
    bool initialized;
    bool channel_enabled;
    bool task_created;
    bool task_confirmed_stopped;
    hfp_i2s_output_state_t state;
    hfp_i2s_output_config_t config;
    hfp_i2s_pin_owners_t owners;
    atomic_uint generation;
    char peer_mac[HFP_I2S_OUTPUT_MAC_STR_LEN];
    hfp_pcm_ring_t ring;
    uint8_t *ring_storage;
    int16_t *writer_pcm;
    void *channel;
    void *task;
    atomic_bool stop_requested;
    atomic_bool accepting_pushes;
    atomic_uint active_pushes;
    atomic_uint push_calls;
    atomic_uint push_failures;
    atomic_uint stale_pushes;
    atomic_uint invalid_pushes;
    uint32_t consecutive_write_failures;
    uint32_t consecutive_underflows;
    uint64_t start_calls;
    uint64_t stop_calls;
    uint64_t start_failures;
    uint64_t stop_timeouts;
    uint64_t write_calls;
    uint64_t write_failures;
    uint64_t short_writes;
    uint64_t silence_intervals;
    uint64_t silence_samples;
    uint64_t quarantine_events;
    esp_err_t last_error;
} hfp_i2s_output_context_t;

extern hfp_i2s_output_context_t g_hfp_i2s_output;
#define s_output g_hfp_i2s_output
#ifdef UNIT_TEST
extern hfp_i2s_output_platform_ops_t g_hfp_i2s_test_ops;
extern bool g_hfp_i2s_test_ops_set;
#define s_test_ops g_hfp_i2s_test_ops
#define s_test_ops_set g_hfp_i2s_test_ops_set
#endif

esp_err_t hfp_i2s_output_lock(void);
esp_err_t hfp_i2s_output_unlock(esp_err_t prior);
void hfp_i2s_output_drain_sem(platform_binary_sem_t sem);
void hfp_i2s_output_set_error_locked(esp_err_t error);
void hfp_i2s_output_clear_session_locked(void);
void *hfp_i2s_output_ops_alloc(size_t bytes);
void hfp_i2s_output_ops_free(void *ptr);
esp_err_t hfp_i2s_output_ops_channel_new(const hfp_i2s_output_config_t *config, void **channel_out);
esp_err_t hfp_i2s_output_ops_channel_init_mode(void *channel, const hfp_i2s_output_config_t *config);
esp_err_t hfp_i2s_output_ops_channel_enable(void *channel);
esp_err_t hfp_i2s_output_ops_channel_disable(void *channel);
esp_err_t hfp_i2s_output_ops_channel_delete(void *channel);
esp_err_t hfp_i2s_output_ops_task_create(void **task_out);
esp_err_t hfp_i2s_output_ops_task_release_start(void *task);
esp_err_t hfp_i2s_output_ops_task_wait_stopped(void *task, uint32_t timeout_ms);
esp_err_t hfp_i2s_output_ops_channel_write(void *channel, const void *data, size_t bytes, size_t *bytes_written, uint32_t timeout_ms);
esp_err_t hfp_i2s_output_writer_iteration(void);

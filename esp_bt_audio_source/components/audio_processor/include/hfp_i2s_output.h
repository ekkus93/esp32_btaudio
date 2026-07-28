#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hfp_pcm_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HFP_I2S_OUTPUT_MAC_STR_LEN 18U
#define HFP_I2S_CVSD_MAX_INPUT_SAMPLES 120U

typedef enum {
    HFP_I2S_OUTPUT_UNINITIALIZED = 0,
    HFP_I2S_OUTPUT_STOPPED,
    HFP_I2S_OUTPUT_STARTING,
    HFP_I2S_OUTPUT_RUNNING,
    HFP_I2S_OUTPUT_STOPPING,
    HFP_I2S_OUTPUT_FAULTED,
    HFP_I2S_OUTPUT_QUARANTINED,
} hfp_i2s_output_state_t;

typedef struct {
    int port;
    int bclk_gpio;
    int ws_gpio;
    int dout_gpio;
    uint32_t sample_rate_hz;
    size_t ring_bytes;
    uint32_t dma_desc_num;
    uint32_t dma_frame_num;
    size_t writer_samples;
    uint32_t task_stack_bytes;
    uint32_t task_priority;
    uint32_t write_timeout_ms;
    uint32_t stop_timeout_ms;
    uint32_t max_consecutive_write_failures;
    uint32_t underflow_degraded_threshold;
} hfp_i2s_output_config_t;

typedef struct {
    bool playback_i2s_valid;
    int playback_i2s_port;
    int playback_bclk_gpio;
    int playback_ws_gpio;
    int playback_din_gpio;
    int playback_dout_gpio;
    bool uart2_enabled;
    int uart2_rx_gpio;
    int uart2_tx_gpio;
} hfp_i2s_pin_owners_t;

typedef struct {
    bool initialized;
    hfp_i2s_output_state_t state;
    bool quarantined;
    bool degraded;
    uint32_t generation;
    char peer_mac[HFP_I2S_OUTPUT_MAC_STR_LEN];
    hfp_i2s_output_config_t config;
    hfp_pcm_ring_snapshot_t ring;
    uint64_t start_calls;
    uint64_t stop_calls;
    uint64_t start_failures;
    uint64_t stop_timeouts;
    uint64_t write_calls;
    uint64_t write_failures;
    uint64_t short_writes;
    uint64_t silence_intervals;
    uint64_t silence_samples;
    uint64_t degraded_events;
    uint32_t consecutive_underflows;
    uint64_t push_calls;
    uint64_t push_failures;
    uint64_t stale_pushes;
    uint64_t invalid_pushes;
    uint64_t quarantine_events;
    esp_err_t last_error;
} hfp_i2s_output_snapshot_t;

esp_err_t hfp_i2s_output_default_config(hfp_i2s_output_config_t *out);
esp_err_t hfp_i2s_output_get_runtime_pin_owners(hfp_i2s_pin_owners_t *out);
esp_err_t hfp_i2s_output_validate_config(
    const hfp_i2s_output_config_t *config,
    const hfp_i2s_pin_owners_t *owners);

esp_err_t hfp_i2s_output_init(const hfp_i2s_output_config_t *config,
                              const hfp_i2s_pin_owners_t *owners);
esp_err_t hfp_i2s_output_start(uint32_t generation, const char *peer_mac);
esp_err_t hfp_i2s_output_stop(uint32_t timeout_ms);
esp_err_t hfp_i2s_output_deinit(void);

bool hfp_i2s_output_push_cvsd(const int16_t *samples_8k,
                              size_t sample_count,
                              uint32_t generation);

esp_err_t hfp_i2s_output_get_snapshot(hfp_i2s_output_snapshot_t *out);
const char *hfp_i2s_output_state_to_string(hfp_i2s_output_state_t state);

#ifdef UNIT_TEST
typedef struct {
    void *(*alloc)(size_t bytes);
    void (*free)(void *ptr);
    esp_err_t (*channel_new)(const hfp_i2s_output_config_t *config,
                             void **channel_out);
    esp_err_t (*channel_init_mode)(void *channel,
                                   const hfp_i2s_output_config_t *config);
    esp_err_t (*channel_enable)(void *channel);
    esp_err_t (*channel_disable)(void *channel);
    esp_err_t (*channel_delete)(void *channel);
    esp_err_t (*task_create)(void **task_out);
    esp_err_t (*task_release_start)(void *task);
    esp_err_t (*task_wait_stopped)(void *task, uint32_t timeout_ms);
    esp_err_t (*channel_write)(void *channel, const void *data, size_t bytes,
                               size_t *bytes_written, uint32_t timeout_ms);
} hfp_i2s_output_platform_ops_t;

esp_err_t hfp_i2s_output_test_set_platform_ops(
    const hfp_i2s_output_platform_ops_t *ops);
void hfp_i2s_output_test_reset(void);
size_t hfp_i2s_output_test_read_pcm(void *dst, size_t bytes,
                                    uint32_t generation);
esp_err_t hfp_i2s_output_test_writer_once(void);
#endif

#ifdef __cplusplus
}
#endif

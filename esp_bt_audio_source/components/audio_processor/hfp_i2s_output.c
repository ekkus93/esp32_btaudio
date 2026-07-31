#include "hfp_i2s_output_internal.h"

#include <stdatomic.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "audio_processor.h"
#include "hfp_voice_convert.h"
#include "platform_sync.h"
#include "util_safe.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifndef CONFIG_HFP_I2S_PORT
#define CONFIG_HFP_I2S_PORT 0
#define CONFIG_HFP_I2S_BCLK_GPIO 32
#define CONFIG_HFP_I2S_WS_GPIO 33
#define CONFIG_HFP_I2S_DOUT_GPIO 27
#define CONFIG_HFP_I2S_SAMPLE_RATE 16000
#define CONFIG_HFP_I2S_RING_BYTES 4096
#define CONFIG_HFP_I2S_DMA_DESC_NUM 8
#define CONFIG_HFP_I2S_DMA_FRAME_NUM 120
#define CONFIG_HFP_I2S_WRITER_SAMPLES 120
#define CONFIG_HFP_I2S_TASK_STACK_BYTES 4096
#define CONFIG_HFP_I2S_TASK_PRIORITY 5
#define CONFIG_HFP_I2S_WRITE_TIMEOUT_MS 20
#define CONFIG_HFP_I2S_STOP_TIMEOUT_MS 1000
#define CONFIG_HFP_I2S_MAX_CONSECUTIVE_WRITE_FAILURES 3
#define CONFIG_HFP_I2S_UNDERFLOW_DEGRADED_THRESHOLD 20
#endif

#ifndef CONFIG_CMD_UART2_ENABLED
#define CONFIG_CMD_UART2_ENABLED 1
#define CONFIG_CMD_UART2_RX_PIN 16
#define CONFIG_CMD_UART2_TX_PIN 17
#endif

hfp_i2s_output_context_t g_hfp_i2s_output;
#ifdef UNIT_TEST
hfp_i2s_output_platform_ops_t g_hfp_i2s_test_ops;
bool g_hfp_i2s_test_ops_set;
#endif

static bool is_strapping_gpio(int gpio)
{
    return gpio == 0 || gpio == 2 || gpio == 5 || gpio == 12 || gpio == 15;
}

static bool is_flash_gpio(int gpio)
{
    return gpio >= 6 && gpio <= 11;
}

static bool is_existing_console_gpio(int gpio)
{
    return gpio == 1 || gpio == 3;
}

static bool is_valid_esp32_output_gpio(int gpio)
{
    if (gpio < 0 || gpio > 33) return false;
    if (gpio == 20 || gpio == 24 || (gpio >= 28 && gpio <= 31)) return false;
    return !is_flash_gpio(gpio);
}

static bool pins_equal_any(int gpio, const hfp_i2s_pin_owners_t *owners)
{
    if (owners->playback_i2s_valid) {
        if (gpio == owners->playback_bclk_gpio ||
            gpio == owners->playback_ws_gpio ||
            gpio == owners->playback_din_gpio ||
            gpio == owners->playback_dout_gpio) {
            return true;
        }
    }
    if (owners->uart2_enabled &&
        (gpio == owners->uart2_rx_gpio || gpio == owners->uart2_tx_gpio)) {
        return true;
    }
    return false;
}

static bool config_equal(const hfp_i2s_output_config_t *a,
                         const hfp_i2s_output_config_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static bool owners_equal(const hfp_i2s_pin_owners_t *a,
                         const hfp_i2s_pin_owners_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

esp_err_t hfp_i2s_output_lock(void)
{
    if (s_output.lock == NULL) return ESP_ERR_INVALID_STATE;
    return platform_mutex_lock(s_output.lock, PLATFORM_WAIT_FOREVER);
}

esp_err_t hfp_i2s_output_unlock(esp_err_t prior)
{
    esp_err_t err = platform_mutex_unlock(s_output.lock);
    return prior != ESP_OK ? prior : err;
}

void hfp_i2s_output_drain_sem(platform_binary_sem_t sem)
{
    if (sem != NULL) (void)platform_binary_sem_take(sem, 0U);
}

void hfp_i2s_output_set_error_locked(esp_err_t error)
{
    s_output.last_error = error;
}

void hfp_i2s_output_enter_quarantine_locked(esp_err_t error)
{
    s_output.state = HFP_I2S_OUTPUT_QUARANTINED;
    s_output.quarantine_events++;
    s_output.last_error = error;
    atomic_store_explicit(&s_output.accepting_pushes, false,
                          memory_order_release);
    HFP_I2S_LOGE("component quarantined: error=%d", (int)error);
}

void hfp_i2s_output_clear_session_locked(void)
{
    atomic_store_explicit(&s_output.generation, 0U, memory_order_release);
    s_output.peer_mac[0] = '\0';
    atomic_store_explicit(&s_output.accepting_pushes, false,
                          memory_order_release);
}

esp_err_t hfp_i2s_output_default_config(hfp_i2s_output_config_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    *out = (hfp_i2s_output_config_t) {
        .port = CONFIG_HFP_I2S_PORT,
        .bclk_gpio = CONFIG_HFP_I2S_BCLK_GPIO,
        .ws_gpio = CONFIG_HFP_I2S_WS_GPIO,
        .dout_gpio = CONFIG_HFP_I2S_DOUT_GPIO,
        .sample_rate_hz = CONFIG_HFP_I2S_SAMPLE_RATE,
        .ring_bytes = CONFIG_HFP_I2S_RING_BYTES,
        .dma_desc_num = CONFIG_HFP_I2S_DMA_DESC_NUM,
        .dma_frame_num = CONFIG_HFP_I2S_DMA_FRAME_NUM,
        .writer_samples = CONFIG_HFP_I2S_WRITER_SAMPLES,
        .task_stack_bytes = CONFIG_HFP_I2S_TASK_STACK_BYTES,
        .task_priority = CONFIG_HFP_I2S_TASK_PRIORITY,
        .write_timeout_ms = CONFIG_HFP_I2S_WRITE_TIMEOUT_MS,
        .stop_timeout_ms = CONFIG_HFP_I2S_STOP_TIMEOUT_MS,
        .max_consecutive_write_failures =
            CONFIG_HFP_I2S_MAX_CONSECUTIVE_WRITE_FAILURES,
        .underflow_degraded_threshold =
            CONFIG_HFP_I2S_UNDERFLOW_DEGRADED_THRESHOLD,
    };
    return ESP_OK;
}

esp_err_t hfp_i2s_output_get_runtime_pin_owners(hfp_i2s_pin_owners_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
#ifdef ESP_PLATFORM
    audio_config_t playback;
    esp_err_t err = audio_processor_get_config(&playback);
    if (err != ESP_OK) return err;
    out->playback_i2s_valid = true;
    out->playback_i2s_port = playback.i2s_port;
    out->playback_bclk_gpio = playback.i2s_bclk_pin;
    out->playback_ws_gpio = playback.i2s_ws_pin;
    out->playback_din_gpio = playback.i2s_din_pin;
    out->playback_dout_gpio = playback.i2s_dout_pin;
    out->uart2_enabled = CONFIG_CMD_UART2_ENABLED;
    out->uart2_rx_gpio = CONFIG_CMD_UART2_RX_PIN;
    out->uart2_tx_gpio = CONFIG_CMD_UART2_TX_PIN;
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_validate_config(
    const hfp_i2s_output_config_t *config,
    const hfp_i2s_pin_owners_t *owners)
{
    if (config == NULL || owners == NULL || !owners->playback_i2s_valid) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->port < 0 || config->port > 1 ||
        config->sample_rate_hz != 16000U ||
        config->ring_bytes < 1024U || config->ring_bytes > UINT32_MAX / 2U ||
        config->dma_desc_num < 2U || config->dma_frame_num == 0U ||
        config->writer_samples == 0U ||
        config->writer_samples > config->ring_bytes / sizeof(int16_t) ||
        config->task_stack_bytes < 3072U || config->task_priority == 0U ||
        config->write_timeout_ms == 0U || config->stop_timeout_ms == 0U ||
        config->max_consecutive_write_failures == 0U ||
        config->underflow_degraded_threshold == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->port == owners->playback_i2s_port) return ESP_ERR_INVALID_STATE;

    const int pins[] = {config->bclk_gpio, config->ws_gpio, config->dout_gpio};
    for (size_t index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        int pin = pins[index];
        if (!is_valid_esp32_output_gpio(pin) || is_strapping_gpio(pin) ||
            is_existing_console_gpio(pin) || pins_equal_any(pin, owners)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (config->bclk_gpio == config->ws_gpio ||
        config->bclk_gpio == config->dout_gpio ||
        config->ws_gpio == config->dout_gpio) {
        return ESP_ERR_INVALID_ARG;
    }
    /* GPIO25/26 are the ESP32's DAC1/DAC2 pins. Routing any I2S signal
     * (BCLK, WS, or DOUT) onto them repeats the exact playback-side bug
     * documented in main/main.c and i2s_manager.c: BCLK/WS had to be moved
     * off the DAC pins to GPIO18/19 because the WROOM32, as I2S master,
     * must output the clock and GPIO25/26 could not do so reliably. */
    if (config->bclk_gpio == 25 || config->bclk_gpio == 26 ||
        config->ws_gpio == 25 || config->ws_gpio == 26 ||
        config->dout_gpio == 25 || config->dout_gpio == 26) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t hfp_i2s_output_init(const hfp_i2s_output_config_t *config,
                              const hfp_i2s_pin_owners_t *owners)
{
    esp_err_t err = hfp_i2s_output_validate_config(config, owners);
    if (err != ESP_OK) return err;

    if (s_output.initialized) {
        if (s_output.state == HFP_I2S_OUTPUT_STOPPED &&
            config_equal(&s_output.config, config) &&
            owners_equal(&s_output.owners, owners)) {
            return ESP_OK;
        }
        return ESP_ERR_INVALID_STATE;
    }
#ifdef UNIT_TEST
    if (!g_hfp_i2s_test_ops_set) return ESP_ERR_INVALID_STATE;
#endif
    platform_mutex_t lock = platform_mutex_create();
    if (lock == NULL) return ESP_ERR_NO_MEM;

    memset(&s_output, 0, sizeof(s_output));
    s_output.lock = lock;
    s_output.initialized = true;
    s_output.state = HFP_I2S_OUTPUT_STOPPED;
    s_output.config = *config;
    s_output.owners = *owners;
    s_output.last_error = ESP_OK;
    atomic_init(&s_output.generation, 0U);
    atomic_init(&s_output.stop_requested, false);
    atomic_init(&s_output.accepting_pushes, false);
    atomic_init(&s_output.active_pushes, 0U);
    atomic_init(&s_output.push_calls, 0U);
    atomic_init(&s_output.push_failures, 0U);
    atomic_init(&s_output.stale_pushes, 0U);
    atomic_init(&s_output.invalid_pushes, 0U);
    HFP_I2S_LOGI("initialized: port=%d bclk=%d ws=%d dout=%d rate=%" PRIu32,
                 config->port, config->bclk_gpio, config->ws_gpio,
                 config->dout_gpio, config->sample_rate_hz);
    return ESP_OK;
}

#ifdef UNIT_TEST
esp_err_t hfp_i2s_output_test_set_platform_ops(
    const hfp_i2s_output_platform_ops_t *ops)
{
    if (ops == NULL || ops->alloc == NULL || ops->free == NULL ||
        ops->channel_new == NULL || ops->channel_init_mode == NULL ||
        ops->channel_enable == NULL || ops->channel_disable == NULL ||
        ops->channel_delete == NULL || ops->task_create == NULL ||
        ops->task_release_start == NULL || ops->task_wait_stopped == NULL ||
        ops->channel_write == NULL || s_output.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    g_hfp_i2s_test_ops = *ops;
    g_hfp_i2s_test_ops_set = true;
    return ESP_OK;
}

void hfp_i2s_output_test_reset(void)
{
    if (s_output.writer_pcm != NULL && g_hfp_i2s_test_ops_set) {
        g_hfp_i2s_test_ops.free(s_output.writer_pcm);
    }
    if (s_output.ring_storage != NULL && g_hfp_i2s_test_ops_set) {
        g_hfp_i2s_test_ops.free(s_output.ring_storage);
    }
    platform_binary_sem_delete(s_output.start_gate);
    platform_binary_sem_delete(s_output.stopped);
    platform_mutex_t lock = s_output.lock;
    memset(&s_output, 0, sizeof(s_output));
    platform_mutex_delete(lock);
    memset(&g_hfp_i2s_test_ops, 0, sizeof(g_hfp_i2s_test_ops));
    g_hfp_i2s_test_ops_set = false;
}

size_t hfp_i2s_output_test_read_pcm(void *dst, size_t bytes,
                                    uint32_t generation)
{
    return hfp_pcm_ring_read(&s_output.ring, dst, bytes, generation);
}

esp_err_t hfp_i2s_output_test_writer_once(void)
{
    return hfp_i2s_output_writer_iteration();
}
#endif

#include "hfp_i2s_output_internal.h"

#ifdef ESP_PLATFORM
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#ifdef ESP_PLATFORM
static void *default_alloc(size_t bytes)
{
    return heap_caps_calloc(1U, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void default_free(void *ptr)
{
    heap_caps_free(ptr);
}

static esp_err_t default_channel_new(const hfp_i2s_output_config_t *config,
                                     void **channel_out)
{
    if (config == NULL || channel_out == NULL) return ESP_ERR_INVALID_ARG;
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)config->port, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = config->dma_desc_num;
    chan_cfg.dma_frame_num = config->dma_frame_num;
    chan_cfg.auto_clear = true;
    i2s_chan_handle_t tx = NULL;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx, NULL);
    if (err == ESP_OK) *channel_out = tx;
    return err;
}

static esp_err_t default_channel_init_mode(
    void *channel, const hfp_i2s_output_config_t *config)
{
    if (channel == NULL || config == NULL) return ESP_ERR_INVALID_ARG;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)config->bclk_gpio,
            .ws = (gpio_num_t)config->ws_gpio,
            .dout = (gpio_num_t)config->dout_gpio,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std_cfg.slot_cfg.ws_width = 16U;
    return i2s_channel_init_std_mode((i2s_chan_handle_t)channel, &std_cfg);
}

static esp_err_t default_channel_enable(void *channel)
{
    return channel == NULL ? ESP_ERR_INVALID_ARG
                           : i2s_channel_enable((i2s_chan_handle_t)channel);
}

static esp_err_t default_channel_disable(void *channel)
{
    return channel == NULL ? ESP_ERR_INVALID_ARG
                           : i2s_channel_disable((i2s_chan_handle_t)channel);
}

static esp_err_t default_channel_delete(void *channel)
{
    return channel == NULL ? ESP_ERR_INVALID_ARG
                           : i2s_del_channel((i2s_chan_handle_t)channel);
}

static esp_err_t default_channel_write(void *channel, const void *data,
                                       size_t bytes, size_t *bytes_written,
                                       uint32_t timeout_ms)
{
    return i2s_channel_write((i2s_chan_handle_t)channel, data, bytes,
                             bytes_written, timeout_ms);
}

static void writer_task(void *arg)
{
    (void)arg;
    if (platform_binary_sem_take(s_output.start_gate,
                                 PLATFORM_WAIT_FOREVER) == ESP_OK) {
        while (!atomic_load_explicit(&s_output.stop_requested,
                                     memory_order_acquire)) {
            esp_err_t err = hfp_i2s_output_writer_iteration();
            if (err != ESP_OK) {
                bool faulted = false;
                if (hfp_i2s_output_lock() == ESP_OK) {
                    faulted = s_output.state == HFP_I2S_OUTPUT_FAULTED;
                    (void)hfp_i2s_output_unlock(ESP_OK);
                }
                if (faulted) break;
            }
        }
    }
    (void)platform_binary_sem_give(s_output.stopped);
    vTaskDelete(NULL);
}

static esp_err_t default_task_create(void **task_out)
{
    if (task_out == NULL) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = NULL;
    BaseType_t created = xTaskCreate(writer_task, "hfp_i2s_tx",
                                     s_output.config.task_stack_bytes, NULL,
                                     s_output.config.task_priority, &task);
    if (created != pdPASS) return ESP_ERR_NO_MEM;
    *task_out = task;
    return ESP_OK;
}

static esp_err_t default_task_release_start(void *task)
{
    (void)task;
    return platform_binary_sem_give(s_output.start_gate);
}

static esp_err_t default_task_wait_stopped(void *task, uint32_t timeout_ms)
{
    (void)task;
    return platform_binary_sem_take(s_output.stopped, timeout_ms);
}
#endif

void *hfp_i2s_output_ops_alloc(size_t bytes)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.alloc(bytes);
#endif
#ifdef ESP_PLATFORM
    return default_alloc(bytes);
#else
    (void)bytes;
    return NULL;
#endif
}

void hfp_i2s_output_ops_free(void *ptr)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) {
        s_test_ops.free(ptr);
        return;
    }
#endif
#ifdef ESP_PLATFORM
    default_free(ptr);
#else
    (void)ptr;
#endif
}

esp_err_t hfp_i2s_output_ops_channel_new(const hfp_i2s_output_config_t *config,
                                         void **channel_out)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.channel_new(config, channel_out);
#endif
#ifdef ESP_PLATFORM
    return default_channel_new(config, channel_out);
#else
    (void)config;
    (void)channel_out;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_channel_init_mode(
    void *channel, const hfp_i2s_output_config_t *config)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.channel_init_mode(channel, config);
#endif
#ifdef ESP_PLATFORM
    return default_channel_init_mode(channel, config);
#else
    (void)channel;
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_channel_enable(void *channel)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.channel_enable(channel);
#endif
#ifdef ESP_PLATFORM
    return default_channel_enable(channel);
#else
    (void)channel;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_channel_disable(void *channel)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.channel_disable(channel);
#endif
#ifdef ESP_PLATFORM
    return default_channel_disable(channel);
#else
    (void)channel;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_channel_delete(void *channel)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.channel_delete(channel);
#endif
#ifdef ESP_PLATFORM
    return default_channel_delete(channel);
#else
    (void)channel;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_task_create(void **task_out)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.task_create(task_out);
#endif
#ifdef ESP_PLATFORM
    return default_task_create(task_out);
#else
    (void)task_out;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_task_release_start(void *task)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.task_release_start(task);
#endif
#ifdef ESP_PLATFORM
    return default_task_release_start(task);
#else
    (void)task;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_task_wait_stopped(void *task,
                                               uint32_t timeout_ms)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) return s_test_ops.task_wait_stopped(task, timeout_ms);
#endif
#ifdef ESP_PLATFORM
    return default_task_wait_stopped(task, timeout_ms);
#else
    (void)task;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t hfp_i2s_output_ops_channel_write(void *channel, const void *data,
                                           size_t bytes,
                                           size_t *bytes_written,
                                           uint32_t timeout_ms)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) {
        return s_test_ops.channel_write(channel, data, bytes,
                                        bytes_written, timeout_ms);
    }
#endif
#ifdef ESP_PLATFORM
    return default_channel_write(channel, data, bytes,
                                 bytes_written, timeout_ms);
#else
    (void)channel;
    (void)data;
    (void)bytes;
    (void)bytes_written;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

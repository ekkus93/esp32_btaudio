#include "audio_processor_internal.h"
#include "platform_memory.h"
#include "platform_timing.h"
#include "nvs_storage.h"

#include <stdlib.h>
#include <string.h>

static bool s_stub_i2s_running = false;
static bool s_stub_uart_active = false;
static size_t s_stub_uart_fill_bytes = 0;
static bool s_stub_beep_active = false;
static size_t s_stub_i2s_fill_bytes = 0;
static size_t s_stub_synth_fill_bytes = 0;
static esp_err_t s_stub_i2s_init_result = ESP_OK;
static esp_err_t s_stub_nvs_set_volume_result = ESP_OK;
static uint32_t s_stub_nvs_set_volume_calls = 0;
static uint8_t s_stub_last_nvs_set_volume_value = 0;

void audio_processor_core_stub_set_i2s_running(bool running)
{
    s_stub_i2s_running = running;
}

void audio_processor_core_stub_set_beep_active(bool active)
{
    s_stub_beep_active = active;
}

void audio_processor_core_stub_set_uart_active(bool active)
{
    s_stub_uart_active = active;
}

void audio_processor_core_stub_set_uart_fill_bytes(size_t bytes)
{
    s_stub_uart_fill_bytes = bytes;
}

bool uart_source_is_active(void)
{
    return s_stub_uart_active;
}

size_t uart_source_fill(uint8_t* dst, size_t dst_bytes)
{
    size_t out = s_stub_uart_fill_bytes;
    if (out > dst_bytes) {
        out = dst_bytes;
    }
    if (dst != NULL && out > 0) {
        memset(dst, 0x44, out);
    }
    return out;
}

void audio_processor_core_stub_set_i2s_fill_bytes(size_t bytes)
{
    s_stub_i2s_fill_bytes = bytes;
}

void audio_processor_core_stub_set_synth_fill_bytes(size_t bytes)
{
    s_stub_synth_fill_bytes = bytes;
}

void audio_processor_core_stub_reset(void)
{
    s_stub_i2s_running = false;
    s_stub_uart_active = false;
    s_stub_uart_fill_bytes = 0;
    s_stub_beep_active = false;
    s_stub_i2s_fill_bytes = 0;
    s_stub_synth_fill_bytes = 0;
    s_stub_i2s_init_result = ESP_OK;
    s_stub_nvs_set_volume_result = ESP_OK;
    s_stub_nvs_set_volume_calls = 0;
    s_stub_last_nvs_set_volume_value = 0;
}

void audio_processor_core_stub_set_nvs_set_volume_result(esp_err_t result)
{
    s_stub_nvs_set_volume_result = result;
}

uint32_t audio_processor_core_stub_get_nvs_set_volume_calls(void)
{
    return s_stub_nvs_set_volume_calls;
}

uint8_t audio_processor_core_stub_get_last_nvs_set_volume_value(void)
{
    return s_stub_last_nvs_set_volume_value;
}

esp_err_t nvs_storage_set_volume(uint8_t volume)
{
    s_stub_nvs_set_volume_calls++;
    s_stub_last_nvs_set_volume_value = volume;
    return s_stub_nvs_set_volume_result;
}

esp_err_t nvs_storage_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    (void)bclk_pin;
    (void)ws_pin;
    (void)din_pin;
    (void)dout_pin;
    return ESP_OK;
}

esp_err_t i2s_manager_init(const audio_config_t* config, const i2s_manager_buffers_t* buffers)
{
    (void)config;
    (void)buffers;
    return s_stub_i2s_init_result;
}

void i2s_manager_deinit(void)
{
}

esp_err_t i2s_manager_start(void)
{
    s_stub_i2s_running = true;
    return ESP_OK;
}

esp_err_t i2s_manager_stop(void)
{
    s_stub_i2s_running = false;
    return ESP_OK;
}

bool i2s_manager_is_running(void)
{
    return s_stub_i2s_running;
}

size_t i2s_source_fill(uint8_t* dst, size_t dst_bytes)
{
    size_t out = s_stub_i2s_fill_bytes;
    if (out > dst_bytes) {
        out = dst_bytes;
    }
    if (dst != NULL && out > 0) {
        memset(dst, 0x11, out);
    }
    return out;
}

esp_err_t beep_manager_init(void)
{
    return ESP_OK;
}

void beep_manager_deinit(void)
{
}

void beep_manager_set_done_callback(beep_done_cb_t cb, void* ctx)
{
    (void)cb;
    (void)ctx;
}

esp_err_t beep_manager_play(const beep_request_t* req, const audio_config_t* config)
{
    (void)req;
    (void)config;
    s_stub_beep_active = true;
    return ESP_OK;
}

void beep_manager_stop(void)
{
    s_stub_beep_active = false;
}

bool beep_overlay_is_active(void)
{
    return s_stub_beep_active;
}

void beep_overlay_fill(uint8_t* dst, size_t dst_bytes, const audio_config_t* config)
{
    (void)config;
    if (dst != NULL && dst_bytes > 0) {
        dst[0] ^= 0x7F;
    }
}

void beep_overlay_stop(void)
{
    s_stub_beep_active = false;
}

size_t synth_source_fill(uint8_t* dst, size_t dst_bytes)
{
    size_t out = s_stub_synth_fill_bytes;
    if (out > dst_bytes) {
        out = dst_bytes;
    }
    if (dst != NULL && out > 0) {
        memset(dst, 0x22, out);
    }
    return out;
}

size_t synth_manager_generate_audio(uint8_t* dst, size_t dst_bytes, const audio_config_t* config, bool* force_synth, synth_lock_t* lock)
{
    (void)config;
    (void)force_synth;
    (void)lock;
    size_t out = s_stub_synth_fill_bytes;
    if (out > dst_bytes) {
        out = dst_bytes;
    }
    if (dst != NULL && out > 0) {
        memset(dst, 0x33, out);
    }
    return out;
}

void synth_manager_reset_state(void)
{
}

bool span_log_init(size_t capacity)
{
    (void)capacity;
    return true;
}

void span_log_deinit(void)
{
}

bool span_log_push(uint32_t seq, uint32_t timestamp_ms, size_t bytes_produced, size_t ring_used, uint8_t source_type, uint8_t flags)
{
    (void)seq;
    (void)timestamp_ms;
    (void)bytes_produced;
    (void)ring_used;
    (void)source_type;
    (void)flags;
    return true;
}

esp_err_t audio_rb_init(audio_rb_t** rb, size_t capacity, bool use_psram)
{
    (void)rb;
    (void)capacity;
    (void)use_psram;
    return ESP_OK;
}

void audio_rb_deinit(audio_rb_t* rb)
{
    (void)rb;
}

size_t audio_rb_capacity(const audio_rb_t* rb)
{
    (void)rb;
    return 32768;
}

size_t audio_rb_available_to_write(const audio_rb_t* rb)
{
    (void)rb;
    return 32768;
}

size_t audio_rb_available_to_read(const audio_rb_t* rb)
{
    (void)rb;
    return 0;
}

size_t audio_rb_write(audio_rb_t* rb, const uint8_t* data, size_t size)
{
    (void)rb;
    (void)data;
    return size;
}

size_t audio_rb_read(audio_rb_t* rb, uint8_t* data, size_t size)
{
    (void)rb;
    (void)data;
    return size;
}

esp_err_t convert_audio_format(const audio_convert_args_t* args)
{
    if (args == NULL || args->dst_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *args->dst_size = args->src_size;
    return ESP_OK;
}

esp_err_t resample_audio(const audio_resample_args_t* args)
{
    if (args == NULL || args->dst_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *args->dst_size = args->src_size;
    return ESP_OK;
}

void __attribute__((weak)) diag_dump_bytes(const void* data, size_t len, const char* tag)
{
    (void)data;
    (void)len;
    (void)tag;
}

/* WAV playback stubs removed (play_manager deleted) */

void audio_processor_beep_reset(void)
{
    s_stub_beep_active = false;
}

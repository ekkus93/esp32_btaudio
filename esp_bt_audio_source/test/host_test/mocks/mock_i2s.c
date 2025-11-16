#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>  // Add for uint16_t definition
#include <math.h>    // Add for sin function

// Mock I2S driver for testing

// I2S port enumeration
typedef enum {
    I2S_NUM_0 = 0,
    I2S_NUM_1 = 1,
    I2S_NUM_MAX
} i2s_port_t;

// I2S communication format
typedef enum {
    I2S_COMM_FORMAT_STAND_I2S = 0x01,
    I2S_COMM_FORMAT_STAND_MSB = 0x02,
    I2S_COMM_FORMAT_STAND_PCM_SHORT = 0x04,
    I2S_COMM_FORMAT_STAND_PCM_LONG = 0x08,
    I2S_COMM_FORMAT_STAND_MAX,
} i2s_comm_format_t;

// I2S channel format
typedef enum {
    I2S_CHANNEL_FMT_RIGHT_LEFT = 0x00,
    I2S_CHANNEL_FMT_ALL_RIGHT,
    I2S_CHANNEL_FMT_ALL_LEFT,
    I2S_CHANNEL_FMT_ONLY_RIGHT,
    I2S_CHANNEL_FMT_ONLY_LEFT,
} i2s_channel_fmt_t;

// I2S bits per sample
typedef enum {
    I2S_BITS_PER_SAMPLE_8BIT = 8,
    I2S_BITS_PER_SAMPLE_16BIT = 16,
    I2S_BITS_PER_SAMPLE_24BIT = 24,
    I2S_BITS_PER_SAMPLE_32BIT = 32,
} i2s_bits_per_sample_t;

// I2S Mode
typedef enum {
    I2S_MODE_MASTER = 1,
    I2S_MODE_SLAVE = 2,
    I2S_MODE_TX = 4,
    I2S_MODE_RX = 8,
} i2s_mode_t;

// I2S configuration
typedef struct {
    i2s_mode_t mode;
    int sample_rate;
    i2s_bits_per_sample_t bits_per_sample;
    i2s_channel_fmt_t channel_format;
    i2s_comm_format_t communication_format;
    int intr_alloc_flags;
    int dma_buf_count;
    int dma_buf_len;
    bool use_apll;
    bool tx_desc_auto_clear;
    int fixed_mclk;
} i2s_config_t;

// I2S pin configuration
typedef struct {
    int bck_io_num;
    int ws_io_num;
    int data_out_num;
    int data_in_num;
} i2s_pin_config_t;

// Status codes
typedef enum {
    ESP_OK = 0,
    ESP_FAIL = -1,
    ESP_ERR_NO_MEM = -100,
    ESP_ERR_INVALID_ARG = -101,
    ESP_ERR_INVALID_STATE = -102,
} esp_err_t;

// Define FreeRTOS tick type for mocking
typedef uint32_t TickType_t;  // Add this definition for TickType_t

// Mock state for verification
static struct {
    bool initialized[I2S_NUM_MAX];
    i2s_config_t config[I2S_NUM_MAX];
    i2s_pin_config_t pins[I2S_NUM_MAX];
    bool running[I2S_NUM_MAX];
} i2s_mock_state = { 
    .initialized = {false, false},
    .running = {false, false}
};

// Mock sample generator for testing
static uint16_t generate_samples(int freq, int sample_rate, int index) {
    // Generate a simple sine wave
    double angle = ((double)index * freq * 2 * 3.14159) / sample_rate;
    double sample = sin(angle) * 32767; // Scale to 16-bit range
    return (uint16_t)(sample);
}

// Driver implementation

esp_err_t i2s_driver_install(i2s_port_t i2s_num, const i2s_config_t *i2s_config, int queue_size, void *i2s_queue) {
    if (i2s_num >= I2S_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (i2s_mock_state.initialized[i2s_num]) {
        return ESP_ERR_INVALID_STATE; // Already installed
    }
    
    // Store configuration for verification
    memcpy(&i2s_mock_state.config[i2s_num], i2s_config, sizeof(i2s_config_t));
    i2s_mock_state.initialized[i2s_num] = true;
    i2s_mock_state.running[i2s_num] = false;
    
    printf("Mock I2S driver installed for port %d, sample rate: %d\n", i2s_num, i2s_config->sample_rate);
    return ESP_OK;
}

esp_err_t i2s_driver_uninstall(i2s_port_t i2s_num) {
    if (i2s_num >= I2S_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!i2s_mock_state.initialized[i2s_num]) {
        return ESP_ERR_INVALID_STATE; // Not installed
    }
    
    i2s_mock_state.initialized[i2s_num] = false;
    i2s_mock_state.running[i2s_num] = false;
    
    printf("Mock I2S driver uninstalled for port %d\n", i2s_num);
    return ESP_OK;
}

esp_err_t i2s_set_pin(i2s_port_t i2s_num, const i2s_pin_config_t *pin) {
    if (i2s_num >= I2S_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!i2s_mock_state.initialized[i2s_num]) {
        return ESP_ERR_INVALID_STATE; // Not installed
    }
    
    // Store pin configuration for verification
    memcpy(&i2s_mock_state.pins[i2s_num], pin, sizeof(i2s_pin_config_t));
    
    printf("Mock I2S pins set: BCK=%d, WS=%d, OUT=%d, IN=%d\n", 
           pin->bck_io_num, pin->ws_io_num, pin->data_out_num, pin->data_in_num);
    return ESP_OK;
}

esp_err_t i2s_start(i2s_port_t i2s_num) {
    if (i2s_num >= I2S_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!i2s_mock_state.initialized[i2s_num]) {
        return ESP_ERR_INVALID_STATE; // Not installed
    }
    
    i2s_mock_state.running[i2s_num] = true;
    printf("Mock I2S driver started for port %d\n", i2s_num);
    return ESP_OK;
}

esp_err_t i2s_stop(i2s_port_t i2s_num) {
    if (i2s_num >= I2S_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!i2s_mock_state.initialized[i2s_num]) {
        return ESP_ERR_INVALID_STATE; // Not installed
    }
    
    i2s_mock_state.running[i2s_num] = false;
    printf("Mock I2S driver stopped for port %d\n", i2s_num);
    return ESP_OK;
}

esp_err_t i2s_read(i2s_port_t i2s_num, void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait) {
    if (i2s_num >= I2S_NUM_MAX || dest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!i2s_mock_state.initialized[i2s_num] || !i2s_mock_state.running[i2s_num]) {
        return ESP_ERR_INVALID_STATE;
    }
    
    static int sample_index = 0;
    uint16_t *samples = (uint16_t*)dest;
    int num_samples = size / sizeof(uint16_t);
    
    // Generate sine wave samples
    for (int i = 0; i < num_samples; i++) {
        samples[i] = generate_samples(440, i2s_mock_state.config[i2s_num].sample_rate, sample_index++);
    }
    
    *bytes_read = size;
    return ESP_OK;
}

esp_err_t i2s_write(i2s_port_t i2s_num, const void *src, size_t size, size_t *bytes_written, TickType_t ticks_to_wait) {
    if (i2s_num >= I2S_NUM_MAX || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!i2s_mock_state.initialized[i2s_num] || !i2s_mock_state.running[i2s_num]) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Pretend we wrote all data
    *bytes_written = size;
    return ESP_OK;
}

// Compatibility wrappers for production code that uses i2s_channel_* APIs
int i2s_channel_read(void* channel, void* data, size_t size, size_t* bytes_read, unsigned ticks_to_wait)
{
    (void)channel; // channel is ignored in this host mock; use port 0
    esp_err_t r = i2s_read(I2S_NUM_0, data, size, bytes_read, (TickType_t)ticks_to_wait);
    return (r == ESP_OK) ? 0 : -1; // production code expects esp_err style; use 0 for OK
}

int i2s_del_channel(void* channel)
{
    (void)channel;
    return i2s_driver_uninstall(I2S_NUM_0) == ESP_OK ? 0 : -1;
}

// Test helper functions

bool mock_i2s_is_initialized(i2s_port_t i2s_num) {
    if (i2s_num >= I2S_NUM_MAX) {
        return false;
    }
    return i2s_mock_state.initialized[i2s_num];
}

bool mock_i2s_is_running(i2s_port_t i2s_num) {
    if (i2s_num >= I2S_NUM_MAX) {
        return false;
    }
    return i2s_mock_state.running[i2s_num];
}

i2s_config_t* mock_i2s_get_config(i2s_port_t i2s_num) {
    if (i2s_num >= I2S_NUM_MAX) {
        return NULL;
    }
    return &i2s_mock_state.config[i2s_num];
}

i2s_pin_config_t* mock_i2s_get_pins(i2s_port_t i2s_num) {
    if (i2s_num >= I2S_NUM_MAX) {
        return NULL;
    }
    return &i2s_mock_state.pins[i2s_num];
}

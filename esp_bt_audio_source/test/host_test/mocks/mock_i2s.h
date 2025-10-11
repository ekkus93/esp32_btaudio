#ifndef MOCK_I2S_H
#define MOCK_I2S_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief I2S port number
 *
 * Guard the typedef so it doesn't conflict when the production headers
 * provide a different definition (e.g. `typedef int i2s_port_t`).
 */
/* If the production headers (which define `i2s_port_t` as an `int`) are
 * already included we must not redefine this type. Check the production
 * header guard `_AUDIO_PROCESSOR_H_` as a heuristic. Also allow an explicit
 * __I2S_PORT_T_DEFINED guard if set by other headers.
 */
#if !defined(_AUDIO_PROCESSOR_H_) && !defined(__I2S_PORT_T_DEFINED)
typedef enum {
    I2S_NUM_0 = 0, /*!< I2S port 0 */
    I2S_NUM_1 = 1, /*!< I2S port 1 */
    I2S_NUM_MAX    /*!< Max I2S port number */
} i2s_port_t;
#define __I2S_PORT_T_DEFINED
#endif

/**
 * @brief I2S communication format
 */
typedef enum {
    I2S_COMM_FORMAT_STAND_I2S = 0x01,      /*!< Standard I2S format */
    I2S_COMM_FORMAT_STAND_MSB = 0x02,      /*!< MSB alignment format */
    I2S_COMM_FORMAT_STAND_PCM_SHORT = 0x04,/*!< PCM short format */
    I2S_COMM_FORMAT_STAND_PCM_LONG = 0x08, /*!< PCM long format */
    I2S_COMM_FORMAT_STAND_MAX,             /*!< Max format */
} i2s_comm_format_t;

/**
 * @brief I2S channel format
 */
typedef enum {
    I2S_CHANNEL_FMT_RIGHT_LEFT = 0x00,     /*!< Right channel, left channel */
    I2S_CHANNEL_FMT_ALL_RIGHT,             /*!< All right channel */
    I2S_CHANNEL_FMT_ALL_LEFT,              /*!< All left channel */
    I2S_CHANNEL_FMT_ONLY_RIGHT,            /*!< Only right channel */
    I2S_CHANNEL_FMT_ONLY_LEFT,             /*!< Only left channel */
} i2s_channel_fmt_t;

/**
 * @brief I2S bits per sample
 */
typedef enum {
    I2S_BITS_PER_SAMPLE_8BIT = 8,          /*!< 8 bits per sample */
    I2S_BITS_PER_SAMPLE_16BIT = 16,        /*!< 16 bits per sample */
    I2S_BITS_PER_SAMPLE_24BIT = 24,        /*!< 24 bits per sample */
    I2S_BITS_PER_SAMPLE_32BIT = 32,        /*!< 32 bits per sample */
} i2s_bits_per_sample_t;

/**
 * @brief I2S mode
 */
typedef enum {
    I2S_MODE_MASTER = 1,                   /*!< Master mode */
    I2S_MODE_SLAVE = 2,                    /*!< Slave mode */
    I2S_MODE_TX = 4,                       /*!< TX mode */
    I2S_MODE_RX = 8,                       /*!< RX mode */
} i2s_mode_t;

/**
 * @brief I2S configuration
 */
typedef struct {
    i2s_mode_t mode;                        /*!< I2S mode */
    int sample_rate;                        /*!< Sample rate */
    i2s_bits_per_sample_t bits_per_sample;  /*!< Bits per sample */
    i2s_channel_fmt_t channel_format;       /*!< Channel format */
    i2s_comm_format_t communication_format; /*!< Communication format */
    int intr_alloc_flags;                   /*!< Interrupt allocation flags */
    int dma_buf_count;                      /*!< DMA buffer count */
    int dma_buf_len;                        /*!< DMA buffer length */
    bool use_apll;                          /*!< Use APLL */
    bool tx_desc_auto_clear;                /*!< Auto clear TX descriptor */
    int fixed_mclk;                         /*!< Fixed MCLK output */
} i2s_config_t;

/**
 * @brief I2S pin configuration
 */
typedef struct {
    int bck_io_num;                        /*!< BCK pin number */
    int ws_io_num;                         /*!< WS pin number */
    int data_out_num;                      /*!< DATA output pin */
    int data_in_num;                       /*!< DATA input pin */
} i2s_pin_config_t;

/**
 * @brief ESP error codes
 *
 * Guard these definitions so they don't conflict with a test-provided
 * `esp_err.h` that may define `esp_err_t` and `ESP_OK` as macros/typedefs.
 */
#ifndef __ESP_ERR_T_DEFINED
typedef enum {
    ESP_OK = 0,                  /*!< Success */
    ESP_FAIL = -1,               /*!< Generic failure */
    ESP_ERR_NO_MEM = -100,       /*!< Out of memory */
    ESP_ERR_INVALID_ARG = -101,  /*!< Invalid argument */
    ESP_ERR_INVALID_STATE = -102 /*!< Invalid state */
} esp_err_t;
#define __ESP_ERR_T_DEFINED
#endif

/**
 * @brief FreeRTOS tick type
 */
typedef uint32_t TickType_t;

/**
 * ESP-IDF I2S driver installation
 * 
 * @param i2s_num I2S port number
 * @param i2s_config I2S configuration
 * @param queue_size Event queue size
 * @param i2s_queue Event queue handle
 * @return ESP_OK on success
 */
esp_err_t i2s_driver_install(i2s_port_t i2s_num, const i2s_config_t *i2s_config, 
                            int queue_size, void *i2s_queue);

/**
 * ESP-IDF I2S driver uninstallation
 * 
 * @param i2s_num I2S port number
 * @return ESP_OK on success
 */
esp_err_t i2s_driver_uninstall(i2s_port_t i2s_num);

/**
 * ESP-IDF I2S set pin configuration
 * 
 * @param i2s_num I2S port number
 * @param pin I2S pin configuration
 * @return ESP_OK on success
 */
esp_err_t i2s_set_pin(i2s_port_t i2s_num, const i2s_pin_config_t *pin);

/**
 * ESP-IDF I2S start
 * 
 * @param i2s_num I2S port number
 * @return ESP_OK on success
 */
esp_err_t i2s_start(i2s_port_t i2s_num);

/**
 * ESP-IDF I2S stop
 * 
 * @param i2s_num I2S port number
 * @return ESP_OK on success
 */
esp_err_t i2s_stop(i2s_port_t i2s_num);

/**
 * ESP-IDF I2S read
 * 
 * @param i2s_num I2S port number
 * @param dest Destination buffer
 * @param size Buffer size
 * @param bytes_read Bytes read
 * @param ticks_to_wait Ticks to wait
 * @return ESP_OK on success
 */
esp_err_t i2s_read(i2s_port_t i2s_num, void *dest, size_t size, 
                  size_t *bytes_read, TickType_t ticks_to_wait);

/**
 * ESP-IDF I2S write
 * 
 * @param i2s_num I2S port number
 * @param src Source buffer
 * @param size Buffer size
 * @param bytes_written Bytes written
 * @param ticks_to_wait Ticks to wait
 * @return ESP_OK on success
 */
esp_err_t i2s_write(i2s_port_t i2s_num, const void *src, size_t size, 
                   size_t *bytes_written, TickType_t ticks_to_wait);

/**
 * Test helper - Check if I2S is initialized
 * 
 * @param i2s_num I2S port number
 * @return True if initialized
 */
bool mock_i2s_is_initialized(i2s_port_t i2s_num);

/**
 * Test helper - Check if I2S is running
 * 
 * @param i2s_num I2S port number
 * @return True if running
 */
bool mock_i2s_is_running(i2s_port_t i2s_num);

/**
 * Test helper - Get I2S configuration
 * 
 * @param i2s_num I2S port number
 * @return Pointer to configuration
 */
i2s_config_t* mock_i2s_get_config(i2s_port_t i2s_num);

/**
 * Test helper - Get I2S pin configuration
 * 
 * @param i2s_num I2S port number
 * @return Pointer to pin configuration
 */
i2s_pin_config_t* mock_i2s_get_pins(i2s_port_t i2s_num);

#endif /* MOCK_I2S_H */

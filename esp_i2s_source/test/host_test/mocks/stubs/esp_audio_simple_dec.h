/* Stub esp_audio_simple_dec.h for host tests */
#ifndef STUB_ESP_AUDIO_SIMPLE_DEC_H
#define STUB_ESP_AUDIO_SIMPLE_DEC_H
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* Handle type — matches ESP-IDF convention (void *) */
typedef void *esp_audio_simple_dec_handle_t;

/* Error type */
typedef int esp_audio_err_t;
#define ESP_AUDIO_ERR_OK 0

/* Decoder types */
#define ESP_AUDIO_SIMPLE_DEC_TYPE_MP3 0
#define ESP_AUDIO_SIMPLE_DEC_TYPE_AAC 1

/* Config */
typedef struct {
    int dec_type;
    void *dec_cfg;
    size_t cfg_size;
} esp_audio_simple_dec_cfg_t;

/* Raw/input buffer */
typedef struct {
    uint8_t *buffer;
    uint32_t len;
    uint32_t consumed;
} esp_audio_simple_dec_raw_t;

/* Output buffer */
typedef struct {
    uint8_t *buffer;
    size_t len;
    size_t decoded_size;
} esp_audio_simple_dec_out_t;

/* Decoder info */
typedef struct {
    int sample_rate;
    int channel;
} esp_audio_simple_dec_info_t;

esp_err_t esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t *cfg,
                                     esp_audio_simple_dec_handle_t *out);
void esp_audio_simple_dec_close(esp_audio_simple_dec_handle_t h);
esp_audio_err_t esp_audio_simple_dec_process(esp_audio_simple_dec_handle_t h,
                                              esp_audio_simple_dec_raw_t *raw,
                                              esp_audio_simple_dec_out_t *out);
esp_err_t esp_audio_simple_dec_get_info(esp_audio_simple_dec_handle_t h,
                                         esp_audio_simple_dec_info_t *info);
#endif

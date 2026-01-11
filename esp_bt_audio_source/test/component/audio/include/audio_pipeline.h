/**
 * @file audio_pipeline.h
 * @brief Audio buffer and processing pipeline definitions
 */

#ifndef _AUDIO_PIPELINE_H_
#define _AUDIO_PIPELINE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio buffer structure
 */
typedef struct audio_buffer {
    void *data;          /*!< Buffer data */
    size_t size;         /*!< Total buffer size */
    size_t length;       /*!< Current data length in buffer */
} audio_buffer_t;

/**
 * @brief Audio buffer pool configuration
 */
typedef struct {
    int buffer_count;    /*!< Number of buffers in pool */
    size_t buffer_size;  /*!< Size of each buffer in bytes */
} audio_buffer_cfg_t;

/**
 * @brief Audio buffer pool
 */
typedef struct audio_buffer_pool {
    audio_buffer_t *buffers;         /*!< Array of buffers */
    bool *buffer_availability;       /*!< Availability status of each buffer */
    int buffer_count;                /*!< Total number of buffers */
    int available_count;             /*!< Number of available buffers */
    size_t buffer_size;              /*!< Size of each buffer */
} audio_buffer_pool_t;

/**
 * @brief Audio pipeline structure
 */
typedef struct audio_pipeline {
    float volume;          /*!< Volume level (0.0-1.0) */
    bool volume_enabled;   /*!< Volume processing enabled */
    bool eq_enabled;       /*!< Equalizer enabled */
    float eq_gain;         /*!< Equalizer gain */
    float eq_frequency;    /*!< Equalizer center frequency */
} audio_pipeline_t;

/**
 * Buffer pool functions
 */
audio_buffer_pool_t* audio_buffer_pool_init(const audio_buffer_cfg_t *config);
void audio_buffer_pool_deinit(audio_buffer_pool_t *pool);
bool audio_buffer_pool_is_initialized(void);
int audio_buffer_pool_get_count(void);
size_t audio_buffer_pool_get_buffer_size(void);

/**
 * Buffer allocation functions
 */
audio_buffer_t* audio_buffer_alloc(audio_buffer_pool_t *pool);
esp_err_t audio_buffer_release(audio_buffer_pool_t *pool, audio_buffer_t *buffer);

/**
 * Buffer operations
 */
esp_err_t audio_buffer_init(audio_buffer_t* buffer, size_t size);
esp_err_t audio_buffer_deinit(audio_buffer_t* buffer);
esp_err_t audio_buffer_write(audio_buffer_t* buffer, void* data, size_t size);
esp_err_t audio_buffer_read(audio_buffer_t* buffer, void* data, size_t size);

/**
 * Audio pipeline functions
 */
audio_pipeline_t* audio_pipeline_init(void);
void audio_pipeline_deinit(audio_pipeline_t *pipeline);
esp_err_t audio_pipeline_add_volume_stage(audio_pipeline_t *pipeline, float volume);
esp_err_t audio_pipeline_add_eq_stage(audio_pipeline_t *pipeline, float gain, float frequency);
esp_err_t audio_pipeline_process(audio_pipeline_t *pipeline, audio_buffer_t *input, audio_buffer_t *output);

/**
 * Volume control functions
 */
esp_err_t audio_volume_set(float volume);
esp_err_t audio_volume_get(float* volume);
esp_err_t audio_volume_apply(int16_t* input, int16_t* output, size_t len);
esp_err_t audio_volume_mute(void);
esp_err_t audio_volume_unmute(void);

/**
 * Sample rate handling
 */
esp_err_t audio_configure_sample_rate(int rate);
esp_err_t audio_get_sample_rate(int* rate);
esp_err_t audio_calculate_buffer_size(int duration_ms, int sample_rate, size_t* size);
esp_err_t audio_convert_sample_rate(int16_t* src, size_t src_len, int src_rate, int16_t* dst, size_t dst_len, int dst_rate);

#ifdef __cplusplus
}
#endif

#endif /* _AUDIO_PIPELINE_H_ */

/**
 * @file audio_pipeline.h
 * @brief Audio buffer and processing pipeline implementation
 */

#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio buffer configuration
 */
typedef struct {
    uint32_t buffer_size;        /*!< Size of each audio buffer in bytes */
    uint32_t buffer_count;       /*!< Number of buffers in the pool */
    uint32_t sample_rate;        /*!< Audio sample rate in Hz */
    uint8_t bits_per_sample;     /*!< Bits per sample (8, 16, 24, 32) */
    uint8_t num_channels;        /*!< Number of channels (1=mono, 2=stereo) */
} audio_buffer_cfg_t;

/**
 * @brief Audio buffer structure
 */
typedef struct audio_buffer {
    uint8_t *data;               /*!< Audio data pointer */
    uint32_t size;               /*!< Size of the buffer in bytes */
    uint32_t data_size;          /*!< Size of valid data in the buffer */
    uint64_t timestamp;          /*!< Timestamp of the audio data */
    struct audio_buffer *next;   /*!< Next buffer in linked list */
    bool in_use;                 /*!< Flag indicating if buffer is in use */
} audio_buffer_t;

/**
 * @brief Audio buffer pool structure
 */
typedef struct {
    audio_buffer_t *buffers;     /*!< Array of audio buffers */
    uint32_t buffer_count;       /*!< Total number of buffers */
    uint32_t buffer_size;        /*!< Size of each buffer */
    uint32_t free_count;         /*!< Number of free buffers */
} audio_buffer_pool_t;

/**
 * @brief Audio processing stage callback function type
 * 
 * @param in_buffer Input audio buffer
 * @param out_buffer Output audio buffer
 * @param user_data User data pointer
 * @return ESP_OK if successful, error code otherwise
 */
typedef esp_err_t (*audio_process_cb_t)(audio_buffer_t *in_buffer, 
                                       audio_buffer_t *out_buffer,
                                       void *user_data);

/**
 * @brief Audio processing stage configuration
 */
typedef struct {
    audio_process_cb_t process_cb;  /*!< Processing callback function */
    void *user_data;                /*!< User data pointer */
    const char *name;               /*!< Name of the processing stage */
} audio_process_stage_cfg_t;

/**
 * @brief Audio pipeline structure
 */
typedef struct {
    audio_process_stage_cfg_t *stages;  /*!< Array of processing stages */
    uint32_t stage_count;               /*!< Number of processing stages */
    audio_buffer_pool_t *buffer_pool;   /*!< Audio buffer pool */
    bool is_running;                    /*!< Flag indicating if pipeline is running */
} audio_pipeline_t;

/**
 * @brief Initialize audio buffer pool
 * 
 * @param config Buffer pool configuration
 * @return Pointer to initialized buffer pool or NULL on error
 */
audio_buffer_pool_t* audio_buffer_pool_init(const audio_buffer_cfg_t *config);

/**
 * @brief Free audio buffer pool
 * 
 * @param pool Buffer pool to free
 */
void audio_buffer_pool_deinit(audio_buffer_pool_t *pool);

/**
 * @brief Get a free buffer from the pool
 * 
 * @param pool Buffer pool
 * @return Pointer to free buffer or NULL if none available
 */
audio_buffer_t* audio_buffer_get(audio_buffer_pool_t *pool);

/**
 * @brief Return a buffer to the pool
 * 
 * @param pool Buffer pool
 * @param buffer Buffer to return
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t audio_buffer_release(audio_buffer_pool_t *pool, audio_buffer_t *buffer);

/**
 * @brief Create an audio processing pipeline
 * 
 * @param buffer_pool Audio buffer pool to use
 * @param stage_count Number of processing stages
 * @return Pointer to created pipeline or NULL on error
 */
audio_pipeline_t* audio_pipeline_create(audio_buffer_pool_t *buffer_pool, uint32_t stage_count);

/**
 * @brief Add a processing stage to the pipeline
 * 
 * @param pipeline Audio pipeline
 * @param stage_idx Index of stage to add
 * @param stage_cfg Stage configuration
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t audio_pipeline_add_stage(audio_pipeline_t *pipeline, uint32_t stage_idx, 
                                  const audio_process_stage_cfg_t *stage_cfg);

/**
 * @brief Process a buffer through the pipeline
 * 
 * @param pipeline Audio pipeline
 * @param in_buffer Input buffer
 * @param out_buffer Output buffer
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t audio_pipeline_process(audio_pipeline_t *pipeline, 
                                audio_buffer_t *in_buffer,
                                audio_buffer_t *out_buffer);

/**
 * @brief Start the audio pipeline
 * 
 * @param pipeline Audio pipeline
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t audio_pipeline_start(audio_pipeline_t *pipeline);

/**
 * @brief Stop the audio pipeline
 * 
 * @param pipeline Audio pipeline
 * @return ESP_OK if successful, error code otherwise
 */
esp_err_t audio_pipeline_stop(audio_pipeline_t *pipeline);

/**
 * @brief Free audio pipeline
 * 
 * @param pipeline Audio pipeline to free
 */
void audio_pipeline_deinit(audio_pipeline_t *pipeline);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PIPELINE_H */

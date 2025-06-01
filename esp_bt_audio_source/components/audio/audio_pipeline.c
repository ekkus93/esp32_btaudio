/**
 * @file audio_pipeline.c
 * @brief Audio buffer and processing pipeline implementation
 */

#include <string.h>
#include <stdlib.h>
#include <inttypes.h> // Added this include for PRIu32 macros
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_pipeline.h"

static const char *TAG = "AUDIO_PIPELINE";

/**
 * Initialize audio buffer pool
 */
audio_buffer_pool_t* audio_buffer_pool_init(const audio_buffer_cfg_t *config)
{
    if (!config || config->buffer_size == 0 || config->buffer_count == 0) {
        ESP_LOGE(TAG, "Invalid buffer pool configuration");
        return NULL;
    }
    
    // Allocate the pool structure
    audio_buffer_pool_t *pool = calloc(1, sizeof(audio_buffer_pool_t));
    if (!pool) {
        ESP_LOGE(TAG, "Failed to allocate buffer pool");
        return NULL;
    }
    
    // Allocate the buffer array
    pool->buffers = calloc(config->buffer_count, sizeof(audio_buffer_t));
    if (!pool->buffers) {
        ESP_LOGE(TAG, "Failed to allocate buffer array");
        free(pool);
        return NULL;
    }
    
    // Initialize each buffer
    for (uint32_t i = 0; i < config->buffer_count; i++) {
        // Allocate buffer data memory
        pool->buffers[i].data = heap_caps_malloc(config->buffer_size, MALLOC_CAP_8BIT);
        if (!pool->buffers[i].data) {
            // Clean up previously allocated buffers
            for (uint32_t j = 0; j < i; j++) {
                free(pool->buffers[j].data);
            }
            free(pool->buffers);
            free(pool);
            ESP_LOGE(TAG, "Failed to allocate buffer data memory");
            return NULL;
        }
        
        // Initialize buffer properties
        pool->buffers[i].size = config->buffer_size;
        pool->buffers[i].data_size = 0;
        pool->buffers[i].timestamp = 0;
        pool->buffers[i].in_use = false;
        pool->buffers[i].next = NULL;
    }
    
    // Set up pool properties
    pool->buffer_count = config->buffer_count;
    pool->buffer_size = config->buffer_size;
    pool->free_count = config->buffer_count;
    pool->sample_rate = config->sample_rate;         // Store sample rate from config
    pool->bits_per_sample = config->bits_per_sample; // Store bits per sample
    pool->num_channels = config->num_channels;       // Store number of channels
    
    ESP_LOGI(TAG, "Audio buffer pool initialized: %" PRIu32 " buffers of %" PRIu32 " bytes each",
            config->buffer_count, config->buffer_size);
    
    return pool;
}

/**
 * Free audio buffer pool
 */
void audio_buffer_pool_deinit(audio_buffer_pool_t *pool)
{
    if (!pool) {
        return;
    }
    
    // Free each buffer's data
    if (pool->buffers) {
        for (uint32_t i = 0; i < pool->buffer_count; i++) {
            if (pool->buffers[i].data) {
                free(pool->buffers[i].data);
            }
        }
        free(pool->buffers);
    }
    
    // Free the pool itself
    free(pool);
    
    ESP_LOGI(TAG, "Audio buffer pool deinitialized");
}

/**
 * Get a free buffer from the pool
 */
audio_buffer_t* audio_buffer_get(audio_buffer_pool_t *pool)
{
    if (!pool || !pool->buffers) {
        ESP_LOGE(TAG, "Invalid buffer pool");
        return NULL;
    }
    
    // No free buffers
    if (pool->free_count == 0) {
        ESP_LOGW(TAG, "No free buffers available");
        return NULL;
    }
    
    // Find a free buffer
    for (uint32_t i = 0; i < pool->buffer_count; i++) {
        if (!pool->buffers[i].in_use) {
            pool->buffers[i].in_use = true;
            pool->buffers[i].data_size = 0;
            pool->buffers[i].timestamp = 0;
            pool->free_count--;
            return &pool->buffers[i];
        }
    }
    
    // Should never reach here if free_count > 0
    ESP_LOGE(TAG, "Buffer pool inconsistency: free_count = %" PRIu32 " but no free buffer found",
            pool->free_count);
    return NULL;
}

/**
 * Return a buffer to the pool
 */
esp_err_t audio_buffer_release(audio_buffer_pool_t *pool, audio_buffer_t *buffer)
{
    if (!pool || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if buffer belongs to this pool
    bool buffer_found = false;
    for (uint32_t i = 0; i < pool->buffer_count; i++) {
        if (&pool->buffers[i] == buffer) {
            buffer_found = true;
            break;
        }
    }
    
    if (!buffer_found) {
        ESP_LOGE(TAG, "Buffer doesn't belong to this pool");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Mark buffer as free
    if (buffer->in_use) {
        buffer->in_use = false;
        buffer->data_size = 0;
        buffer->timestamp = 0;
        pool->free_count++;
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Buffer was already released");
        return ESP_ERR_INVALID_STATE;
    }
}

/**
 * Create an audio processing pipeline
 */
audio_pipeline_t* audio_pipeline_create(audio_buffer_pool_t *buffer_pool, uint32_t stage_count)
{
    if (!buffer_pool || stage_count == 0) {
        ESP_LOGE(TAG, "Invalid pipeline parameters");
        return NULL;
    }
    
    // Allocate the pipeline structure
    audio_pipeline_t *pipeline = calloc(1, sizeof(audio_pipeline_t));
    if (!pipeline) {
        ESP_LOGE(TAG, "Failed to allocate pipeline structure");
        return NULL;
    }
    
    // Allocate the stages array
    pipeline->stages = calloc(stage_count, sizeof(audio_process_stage_cfg_t));
    if (!pipeline->stages) {
        ESP_LOGE(TAG, "Failed to allocate pipeline stages array");
        free(pipeline);
        return NULL;
    }
    
    // Initialize pipeline properties
    pipeline->stage_count = stage_count;
    pipeline->buffer_pool = buffer_pool;
    pipeline->is_running = false;
    
    // Initialize volume control settings
    pipeline->volume = 100;       // Default to full volume
    pipeline->is_muted = false;   // Not muted by default
    pipeline->pre_mute_volume = 100;
    
    ESP_LOGI(TAG, "Audio pipeline created with %" PRIu32 " stages", stage_count);
    
    return pipeline;
}

/**
 * Add a processing stage to the pipeline
 */
esp_err_t audio_pipeline_add_stage(audio_pipeline_t *pipeline, uint32_t stage_idx, 
                                  const audio_process_stage_cfg_t *stage_cfg)
{
    if (!pipeline || !stage_cfg || stage_idx >= pipeline->stage_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy stage configuration
    pipeline->stages[stage_idx] = *stage_cfg;
    
    ESP_LOGI(TAG, "Added processing stage '%s' at index %" PRIu32,
            stage_cfg->name ? stage_cfg->name : "unnamed", stage_idx);
    
    return ESP_OK;
}

/**
 * Process a buffer through the pipeline
 */
esp_err_t audio_pipeline_process(audio_pipeline_t *pipeline, 
                                audio_buffer_t *in_buffer,
                                audio_buffer_t *out_buffer)
{
    if (!pipeline || !in_buffer || !out_buffer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!pipeline->is_running) {
        ESP_LOGW(TAG, "Pipeline is not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (in_buffer->data_size == 0) {
        ESP_LOGW(TAG, "Input buffer is empty");
        return ESP_ERR_INVALID_STATE;
    }
    
    // For now, this just processes a single stage
    // In a real implementation, you'd want a more sophisticated pipeline with
    // intermediate buffers and proper stage management
    
    // Get the stage (assume single stage for now)
    audio_process_stage_cfg_t *stage = &pipeline->stages[0];
    if (!stage->process_cb) {
        ESP_LOGE(TAG, "Stage has no processing callback");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Call the processing function
    esp_err_t ret = stage->process_cb(in_buffer, out_buffer, stage->user_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stage processing failed: %d", ret);
        return ret;
    }
    
    return ESP_OK;
}

/**
 * Start the audio pipeline
 */
esp_err_t audio_pipeline_start(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (pipeline->is_running) {
        ESP_LOGW(TAG, "Pipeline already running");
        return ESP_OK;
    }
    
    // Check if all stages are properly initialized
    for (uint32_t i = 0; i < pipeline->stage_count; i++) {
        if (!pipeline->stages[i].process_cb) {
            ESP_LOGE(TAG, "Stage %" PRIu32 " has no processing callback", i);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    pipeline->is_running = true;
    ESP_LOGI(TAG, "Audio pipeline started");
    
    return ESP_OK;
}

/**
 * Stop the audio pipeline
 */
esp_err_t audio_pipeline_stop(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!pipeline->is_running) {
        ESP_LOGW(TAG, "Pipeline already stopped");
        return ESP_OK;
    }
    
    pipeline->is_running = false;
    ESP_LOGI(TAG, "Audio pipeline stopped");
    
    return ESP_OK;
}

/**
 * Free audio pipeline
 */
void audio_pipeline_deinit(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return;
    }
    
    // Make sure the pipeline is stopped
    if (pipeline->is_running) {
        audio_pipeline_stop(pipeline);
    }
    
    // Free the stages array
    if (pipeline->stages) {
        free(pipeline->stages);
    }
    
    // Free the pipeline structure itself
    free(pipeline);
    
    ESP_LOGI(TAG, "Audio pipeline deinitialized");
}

/**
 * Set volume level for the audio pipeline
 */
esp_err_t audio_pipeline_set_volume(audio_pipeline_t *pipeline, int volume)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clamp volume to valid range (0-100)
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    
    // Set volume if not muted
    if (!pipeline->is_muted) {
        pipeline->volume = volume;
    }
    
    // Always store the intended volume even when muted
    pipeline->pre_mute_volume = volume;
    
    ESP_LOGI(TAG, "Audio pipeline volume set to %d%%", volume);
    
    return ESP_OK;
}

/**
 * Get current volume level
 */
esp_err_t audio_pipeline_get_volume(audio_pipeline_t *pipeline, int *volume)
{
    if (!pipeline || !volume) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Return pre-mute volume if the pipeline is muted
    if (pipeline->is_muted) {
        *volume = pipeline->pre_mute_volume;
    } else {
        // Return current volume
        *volume = pipeline->volume;
    }
    
    return ESP_OK;
}

/**
 * Mute audio output
 */
esp_err_t audio_pipeline_mute(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!pipeline->is_muted) {
        // Store current volume before muting
        pipeline->pre_mute_volume = pipeline->volume;
        pipeline->volume = 0;
        pipeline->is_muted = true;
        
        ESP_LOGI(TAG, "Audio pipeline muted");
    }
    
    return ESP_OK;
}

/**
 * Unmute audio output
 */
esp_err_t audio_pipeline_unmute(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (pipeline->is_muted) {
        // Restore volume from before muting
        pipeline->volume = pipeline->pre_mute_volume;
        pipeline->is_muted = false;
        
        ESP_LOGI(TAG, "Audio pipeline unmuted, volume restored to %d%%", pipeline->volume);
    }
    
    return ESP_OK;
}

/**
 * Toggle mute state
 */
esp_err_t audio_pipeline_toggle_mute(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (pipeline->is_muted) {
        return audio_pipeline_unmute(pipeline);
    } else {
        return audio_pipeline_mute(pipeline);
    }
}

/**
 * Check if audio is muted
 */
esp_err_t audio_pipeline_is_muted(audio_pipeline_t *pipeline, bool *muted)
{
    if (!pipeline || !muted) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *muted = pipeline->is_muted;
    
    return ESP_OK;
}

/**
 * @brief Set the sample rate of an audio buffer pool
 */
esp_err_t audio_buffer_pool_set_sample_rate(audio_buffer_pool_t *pool, uint32_t sample_rate)
{
    if (!pool || sample_rate == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Store the sample rate
    pool->sample_rate = sample_rate;
    
    ESP_LOGI(TAG, "Audio buffer pool sample rate set to %lu Hz", sample_rate);
    
    return ESP_OK;
}

/**
 * @brief Get the current sample rate of an audio buffer pool
 */
uint32_t audio_buffer_pool_get_sample_rate(audio_buffer_pool_t *pool)
{
    if (!pool) {
        return 0;
    }
    
    return pool->sample_rate;
}

/**
 * @brief Calculate buffer size needed for a given duration and sample rate
 */
uint32_t audio_buffer_calculate_size(float duration_ms, uint32_t sample_rate, 
                                    uint32_t channels, uint32_t bits_per_sample)
{
    if (duration_ms <= 0 || sample_rate == 0 || channels == 0 || bits_per_sample == 0) {
        return 0;
    }
    
    // Calculate bytes per sample
    uint32_t bytes_per_sample = bits_per_sample / 8;
    
    // Calculate total bytes needed
    uint32_t buffer_size = (uint32_t)((duration_ms / 1000.0f) * sample_rate * channels * bytes_per_sample);
    
    // Ensure buffer size is a multiple of the sample frame size (channels * bytes_per_sample)
    uint32_t frame_size = channels * bytes_per_sample;
    if (buffer_size % frame_size != 0) {
        buffer_size = (buffer_size / frame_size + 1) * frame_size;
    }
    
    return buffer_size;
}

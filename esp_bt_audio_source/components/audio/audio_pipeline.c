/**
 * @file audio_pipeline.c
 * @brief Audio buffer and processing pipeline implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
// Make sure we're including the header from the include directory
#include "audio_pipeline.h"
#include <inttypes.h>  // For PRIu32 and other format macros

static const char *TAG = "AUDIO_PIPELINE";

/* Local bounded helpers to avoid analyzer warnings. */
static void pipeline_memcpy(void *dst, size_t dst_size, const void *src, size_t len) {
    if (dst == NULL || src == NULL || dst_size == 0 || len == 0) {
        return;
    }
    size_t to_copy = len;
    if (to_copy > dst_size) {
        to_copy = dst_size;
    }
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < to_copy; i++) {
        d[i] = s[i];
    }
}

static void pipeline_memmove(void *dst, size_t dst_size, const void *src, size_t len) {
    if (dst == NULL || src == NULL || dst_size == 0 || len == 0) {
        return;
    }
    size_t to_move = len;
    if (to_move > dst_size) {
        to_move = dst_size;
    }
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < to_move; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = to_move; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
}

// Global buffer pool
static audio_buffer_pool_t *g_buffer_pool = NULL;

/**
 * Initialize a buffer pool
 */
audio_buffer_pool_t* audio_buffer_pool_init(const audio_buffer_cfg_t *config) {
    if (!config || config->buffer_count <= 0 || config->buffer_size <= 0) {
        ESP_LOGE(TAG, "Invalid buffer pool configuration");
        return NULL;
    }

    audio_buffer_pool_t *pool = calloc(1, sizeof(audio_buffer_pool_t));
    if (!pool) {
        ESP_LOGE(TAG, "Failed to allocate buffer pool");
        return NULL;
    }

    pool->buffer_count = config->buffer_count;
    pool->buffer_size = config->buffer_size;
    pool->available_count = config->buffer_count;

    // Allocate buffer array
    pool->buffers = calloc(config->buffer_count, sizeof(audio_buffer_t));
    if (!pool->buffers) {
        ESP_LOGE(TAG, "Failed to allocate buffer array");
        free(pool);
        return NULL;
    }

    // Allocate availability tracker
    pool->buffer_availability = calloc(config->buffer_count, sizeof(bool));
    if (!pool->buffer_availability) {
        ESP_LOGE(TAG, "Failed to allocate buffer availability array");
        free(pool->buffers);
        free(pool);
        return NULL;
    }

    // Initialize each buffer 
    for (int i = 0; i < config->buffer_count; i++) {
        pool->buffers[i].data = calloc(1, config->buffer_size);
        if (!pool->buffers[i].data) {
            ESP_LOGE(TAG, "Failed to allocate buffer %d", i);
            // Clean up previous buffers
            for (int j = 0; j < i; j++) {
                free(pool->buffers[j].data);
            }
            free(pool->buffer_availability);
            free(pool->buffers);
            free(pool);
            return NULL;
        }

        pool->buffers[i].size = config->buffer_size;
        pool->buffers[i].length = 0;
        pool->buffer_availability[i] = true;
    }

    // Store global reference 
    g_buffer_pool = pool;
    ESP_LOGI(TAG, "Buffer pool initialized with %d buffers of size %zu bytes", 
             (int)config->buffer_count, (size_t)config->buffer_size);
    return pool;
}

/**
 * Deinitialize a buffer pool
 */
void audio_buffer_pool_deinit(audio_buffer_pool_t *pool) {
    if (!pool) {
        ESP_LOGW(TAG, "Cannot deinit NULL buffer pool");
        return;
    }
    
    if (pool->buffers) {
        // Free buffer data
        for (int i = 0; i < pool->buffer_count; i++) {
            if (pool->buffers[i].data) {
                free(pool->buffers[i].data);
            }
        }
        free(pool->buffers);
    }
    
    if (pool->buffer_availability) {
        free(pool->buffer_availability);
    }
    
    free(pool);
    
    // Clear global reference if this was the global pool
    if (g_buffer_pool == pool) {
        g_buffer_pool = NULL;
    }
    
    ESP_LOGI(TAG, "Buffer pool deinitialized");
}

/**
 * Check if buffer pool is initialized
 */
bool audio_buffer_pool_is_initialized(void) {
    return (g_buffer_pool != NULL);
}

/**
 * Get buffer pool count
 */
int audio_buffer_pool_get_count(void) {
    if (!g_buffer_pool) return 0;
    return g_buffer_pool->buffer_count;
}

/**
 * Get buffer size
 */
size_t audio_buffer_pool_get_buffer_size(void) {
    if (!g_buffer_pool) return 0;
    return g_buffer_pool->buffer_size;
}

/**
 * Allocate a buffer from the pool
 */
audio_buffer_t* audio_buffer_alloc(audio_buffer_pool_t *pool) {
    if (!pool) {
        ESP_LOGW(TAG, "Buffer pool is NULL");
        return NULL;
    }
    
    if (pool->available_count <= 0) {
        ESP_LOGW(TAG, "No buffers available");
        return NULL;
    }
    
    // Find an available buffer
    for (int i = 0; i < pool->buffer_count; i++) {
        if (pool->buffer_availability[i]) {
            pool->buffer_availability[i] = false;
            pool->available_count--;
            ESP_LOGD(TAG, "Allocated buffer %d, %d remaining", i, pool->available_count);
            return &pool->buffers[i];
        }
    }
    
    ESP_LOGE(TAG, "Failed to find an available buffer despite count > 0");
    return NULL;
}

/**
 * Release a buffer back to the pool
 */
esp_err_t audio_buffer_release(audio_buffer_pool_t *pool, audio_buffer_t *buffer) {
    if (!pool || !buffer) {
        ESP_LOGW(TAG, "Cannot release NULL buffer or pool");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find this buffer in the pool
    int buffer_index = -1;
    for (int i = 0; i < pool->buffer_count; i++) {
        if (&pool->buffers[i] == buffer) {
            buffer_index = i;
            break;
        }
    }
    
    if (buffer_index < 0) {
        ESP_LOGE(TAG, "Buffer does not belong to this pool");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (pool->buffer_availability[buffer_index]) {
        ESP_LOGW(TAG, "Buffer %d already released", buffer_index);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Release the buffer
    buffer->length = 0;
    pool->buffer_availability[buffer_index] = true;
    pool->available_count++;
    ESP_LOGD(TAG, "Released buffer %d, %d now available", 
             buffer_index, pool->available_count);
    
    return ESP_OK;
}

/**
 * Initialize an audio pipeline
 */
audio_pipeline_t* audio_pipeline_init(void) {
    audio_pipeline_t *pipeline = calloc(1, sizeof(audio_pipeline_t));
    if (!pipeline) {
        ESP_LOGE(TAG, "Failed to allocate audio pipeline");
        return NULL;
    }
    
    // Initialize with defaults
    pipeline->volume = 1.0f;  // Full volume
    pipeline->volume_enabled = false;
    pipeline->eq_enabled = false;
    pipeline->eq_gain = 1.0f;
    pipeline->eq_frequency = 0.0f;
    
    ESP_LOGI(TAG, "Audio pipeline initialized");
    return pipeline;
}

/**
 * Deinitialize an audio pipeline
 */
void audio_pipeline_deinit(audio_pipeline_t *pipeline) {
    if (!pipeline) {
        ESP_LOGW(TAG, "Cannot deinit NULL pipeline");
        return;
    }
    
    free(pipeline);
    ESP_LOGI(TAG, "Audio pipeline deinitialized");
}

/**
 * Add a volume processing stage to the pipeline
 */
esp_err_t audio_pipeline_add_volume_stage(audio_pipeline_t *pipeline, float volume) {
    if (!pipeline) {
        ESP_LOGE(TAG, "Pipeline is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (volume < 0.0f || volume > 1.0f) {
        ESP_LOGW(TAG, "Volume out of range (0.0-1.0), clamping");
        volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
    }
    
    pipeline->volume = volume;
    pipeline->volume_enabled = true;
    ESP_LOGI(TAG, "Added volume stage to pipeline: %.2f", (double)volume);
    
    return ESP_OK;
}

/**
 * Add an equalizer stage to the pipeline
 */
esp_err_t audio_pipeline_add_eq_stage(audio_pipeline_t *pipeline, float gain, float frequency) {
    if (!pipeline) {
        ESP_LOGE(TAG, "Pipeline is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    pipeline->eq_enabled = true;
    pipeline->eq_gain = gain;
    pipeline->eq_frequency = frequency;
    ESP_LOGI(TAG, "Added EQ stage to pipeline: gain=%.2f, freq=%.2f Hz", (double)gain, (double)frequency);
    
    return ESP_OK;
}

/**
 * Process audio through the pipeline
 */
esp_err_t audio_pipeline_process(audio_pipeline_t *pipeline,
                               audio_buffer_t *input, audio_buffer_t *output) {
    if (!pipeline || !input || !output) {
        ESP_LOGE(TAG, "Invalid pipeline or buffers");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!input->data || !output->data) {
        ESP_LOGE(TAG, "Buffer data pointers are NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy input to output first (bounded to output buffer size)
    size_t to_copy = input->length;
    if (to_copy > output->size) {
        to_copy = output->size;
    }
    pipeline_memcpy(output->data, output->size, input->data, to_copy);
    output->length = to_copy;
    
    // Process stages
    int16_t *samples = (int16_t*)output->data;
    size_t sample_count = output->length / sizeof(int16_t);
    
    // Apply volume if enabled
    if (pipeline->volume_enabled) {
        for (size_t i = 0; i < sample_count; i++) {
            samples[i] = (int16_t)(samples[i] * pipeline->volume);
        }
        // Fix the format string issue by casting float to double
        ESP_LOGD(TAG, "Applied volume: %g", (double)pipeline->volume);
    }
    
    // Apply EQ if enabled (simplified implementation)
    if (pipeline->eq_enabled) {
        // Simple gain boost for demonstration 
        for (size_t i = 0; i < sample_count; i++) {
            int32_t sample = samples[i] * pipeline->eq_gain;
            // Clip if necessary
            if (sample > INT16_MAX) sample = INT16_MAX;
            if (sample < INT16_MIN) sample = INT16_MIN;
            samples[i] = (int16_t)sample;
        }
        // Fix format string issues by casting floats to doubles
        ESP_LOGD(TAG, "Applied EQ: gain=%g, freq=%g Hz",
                 (double)pipeline->eq_gain, (double)pipeline->eq_frequency);
    }
    
    ESP_LOGD(TAG, "Pipeline processed %zu samples", sample_count);
    return ESP_OK;
}

/**
 * Initialize a buffer
 */
esp_err_t audio_buffer_init(audio_buffer_t* buffer, size_t size) {
    if (!buffer) {
        ESP_LOGE(TAG, "Buffer pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (size == 0) {
        ESP_LOGE(TAG, "Buffer size cannot be zero");
        return ESP_ERR_INVALID_ARG;
    }
    
    buffer->data = malloc(size);
    if (!buffer->data) {
        ESP_LOGE(TAG, "Failed to allocate buffer data");
        return ESP_ERR_NO_MEM;
    }
    
    buffer->size = size;
    buffer->length = 0;  // Using 'length' instead of 'used'
    ESP_LOGD(TAG, "Initialized buffer of size %zu bytes", size);
    
    return ESP_OK;
}

/**
 * Clean up a buffer
 */
esp_err_t audio_buffer_deinit(audio_buffer_t* buffer) {
    if (!buffer) {
        ESP_LOGE(TAG, "Buffer pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (buffer->data) {
        free(buffer->data);
        buffer->data = NULL;
    }
    
    buffer->size = 0;
    buffer->length = 0;  // Using 'length' instead of 'used'
    ESP_LOGD(TAG, "Deinitialized buffer");
    
    return ESP_OK;
}

/**
 * Write data to a buffer
 */
esp_err_t audio_buffer_write(audio_buffer_t* buffer, void* data, size_t size) {
    if (!buffer || !data) {
        ESP_LOGE(TAG, "Buffer or data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (buffer->length + size > buffer->size) {
        ESP_LOGW(TAG, "Buffer overflow: %zu + %zu > %zu", 
                 buffer->length, size, buffer->size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    pipeline_memcpy((uint8_t*)buffer->data + buffer->length, buffer->size - buffer->length, data, size);
    buffer->length += size;
    ESP_LOGD(TAG, "Written %zu bytes to buffer, now contains %zu bytes", 
             size, buffer->length);
    
    return ESP_OK;
}

/**
 * Read data from a buffer
 */
esp_err_t audio_buffer_read(audio_buffer_t* buffer, void* data, size_t size) {
    if (!buffer || !data) {
        ESP_LOGE(TAG, "Buffer or data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (size > buffer->length) {
        size = buffer->length;
    }
    
    // Copy data to the output buffer
    pipeline_memcpy(data, size, buffer->data, size);
    ESP_LOGD(TAG, "Read %zu bytes from buffer", size);
    
    // Move remaining data to the beginning of the buffer
    if (size < buffer->length) {
        pipeline_memmove(buffer->data, buffer->size, (uint8_t*)buffer->data + size, buffer->length - size);
    }
    
    buffer->length -= size;
    ESP_LOGD(TAG, "Buffer now contains %zu bytes", buffer->length);
    
    return ESP_OK;
}

// Volume control functions
float current_volume = 1.0f;
bool is_muted = false;
float pre_mute_volume = 1.0f;

/**
 * Set the volume level
 */
esp_err_t audio_volume_set(float volume) {
    if (volume < 0.0f || volume > 1.0f) {
        ESP_LOGW(TAG, "Volume out of range (0.0-1.0), clamping");
        volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
    }
    
    current_volume = volume;
    if (!is_muted) {
        pre_mute_volume = volume;
    }
    
    ESP_LOGI(TAG, "Volume set to %.2f", (double)volume);
    return ESP_OK;
}

/**
 * Get the current volume level
 */
esp_err_t audio_volume_get(float* volume) {
    if (!volume) {
        ESP_LOGE(TAG, "Volume pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *volume = current_volume;
    return ESP_OK;
}

/**
 * Apply volume to audio samples
 */
esp_err_t audio_volume_apply(int16_t* input, int16_t* output, size_t len) {
    if (!input || !output) {
        ESP_LOGE(TAG, "Input or output pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < len; i++) {
        output[i] = (int16_t)(input[i] * current_volume);
    }
    
    ESP_LOGD(TAG, "Applied volume %.2f to %zu samples", (double)current_volume, len);
    return ESP_OK;
}

/**
 * Mute audio
 */
esp_err_t audio_volume_mute(void) {
    if (!is_muted) {
        pre_mute_volume = current_volume;
        current_volume = 0.0f;
        is_muted = true;
        ESP_LOGI(TAG, "Audio muted");
    }
    return ESP_OK;
}

/**
 * Unmute audio
 */
esp_err_t audio_volume_unmute(void) {
    if (is_muted) {
        current_volume = pre_mute_volume;
        is_muted = false;
        ESP_LOGI(TAG, "Audio unmuted, volume restored to %.2f", (double)current_volume);
    }
    return ESP_OK;
}

// Sample rate handling
int current_sample_rate = 44100;

/**
 * Configure the sample rate
 */
esp_err_t audio_configure_sample_rate(int rate) {
    if (rate != 8000 && rate != 11025 && rate != 16000 && 
        rate != 22050 && rate != 32000 && rate != 44100 && 
        rate != 48000 && rate != 88200 && rate != 96000) {
        ESP_LOGW(TAG, "Unsupported sample rate: %d Hz", rate);
        return ESP_ERR_INVALID_ARG;
    }
    
    current_sample_rate = rate;
    ESP_LOGI(TAG, "Sample rate set to %d Hz", rate);
    return ESP_OK;
}

/**
 * Get the current sample rate
 */
esp_err_t audio_get_sample_rate(int* rate) {
    if (!rate) {
        ESP_LOGE(TAG, "Rate pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *rate = current_sample_rate;
    return ESP_OK;
}

/**
 * Calculate buffer size for given duration and sample rate
 */
esp_err_t audio_calculate_buffer_size(int duration_ms, int sample_rate, size_t* size) {
    if (!size || duration_ms <= 0 || sample_rate <= 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate size in bytes (assuming stereo, 16-bit)
    *size = (size_t)((duration_ms * sample_rate * 2 * 2) / 1000);
    ESP_LOGD(TAG, "Calculated buffer size: %zu bytes for %d ms @ %d Hz", 
             *size, duration_ms, sample_rate);
    
    return ESP_OK;
}

/**
 * Very simple sample rate conversion (for testing purposes only)
 */
esp_err_t audio_convert_sample_rate(int16_t* src, size_t src_len, int src_rate,
                               int16_t* dst, size_t dst_len, int dst_rate) {
    if (!src || !dst || src_len == 0 || dst_len == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    float ratio = (float)src_rate / (float)dst_rate;
    size_t src_samples = src_len / sizeof(int16_t);
    size_t dst_samples = dst_len / sizeof(int16_t);
    
    // Simple resampling
    for (size_t i = 0; i < dst_samples; i++) {
        float src_pos = i * ratio;
        size_t src_idx = (size_t)src_pos;
        
        if (src_idx >= src_samples) {
            break;
        }
        
        dst[i] = src[src_idx];
    }
    
    ESP_LOGI(TAG, "Converted %zu samples from %d Hz to %d Hz", 
             dst_samples, src_rate, dst_rate);
    
    return ESP_OK;
}

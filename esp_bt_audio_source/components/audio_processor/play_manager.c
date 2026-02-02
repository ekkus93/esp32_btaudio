/**
 * WAV playback manager using audio_queue for zero-copy handoff.
 */

#include "play_manager.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_queue.h"
#include "audio_util.h"
#include "util_safe.h"

static const char *TAG = "play_manager";

#define PLAY_HIGH_WATER_PCT       90U
#define PLAY_WAIT_FOR_FREE_MS     5U
#define PLAY_ENQUEUE_TIMEOUT_MS   500U

typedef struct {
    bool initialized;
    bool active;
    audio_config_t out_cfg;          /* output format (dst) */
    audio_bit_depth_t src_bit;
    audio_sample_rate_t src_rate;
    size_t frame_bytes_src;
    size_t frame_bytes_dst;
    FILE *file;
    size_t remaining_bytes;
    size_t pending_bytes;
    size_t work_bytes;
    SemaphoreHandle_t mutex;
#ifdef CONFIG_BT_MOCK_TESTING
    bool test_zero_resample;
#endif
} play_manager_state_t;

static play_manager_state_t s_pm = {0};

/* WAV playback instrumentation (CODE_REVIEW4 Task 0.2) */
static size_t s_bytes_read_from_file_total = 0;
static size_t s_bytes_enqueued_total = 0;
static size_t s_enqueue_fail_count = 0;
static size_t s_dst_block_null_count = 0;
static size_t s_expected_data_bytes = 0;

static inline int bytes_per_sample(audio_bit_depth_t bit_depth)
{
    switch (bit_depth) {
        case AUDIO_BIT_DEPTH_24:
        case AUDIO_BIT_DEPTH_32:
            return 4;
        case AUDIO_BIT_DEPTH_16:
        default:
            return 2;
    }
}

/* Helper: Read and validate RIFF/WAVE header */
static esp_err_t read_wav_riff_header(FILE *file)
{
    uint32_t tmp32 = 0;
    char riff[4];
    if (fread(riff, 1, 4, file) != 4) {
        return ESP_ERR_INVALID_STATE;
    }
    if (memcmp(riff, "RIFF", 4) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (fread(&tmp32, 4, 1, file) != 1) {
        return ESP_ERR_INVALID_STATE; /* skip size */
    }
    char wave[4];
    if (fread(wave, 1, 4, file) != 4) {
        return ESP_ERR_INVALID_STATE;
    }
    if (memcmp(wave, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* Helper: Parse fmt chunk */
static esp_err_t parse_fmt_chunk(FILE *file, uint32_t chunk_size,
                                uint16_t *audio_format,
                                uint16_t *num_channels,
                                uint32_t *sample_rate,
                                uint16_t *bits_per_sample)
{
    if (chunk_size < 16) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t tmp32 = 0;
    uint16_t tmp16 = 0;
    bool success =
        fread(audio_format, 2, 1, file) == 1 &&
        fread(num_channels, 2, 1, file) == 1 &&
        fread(sample_rate, 4, 1, file) == 1 &&
        fread(&tmp32, 4, 1, file) == 1 && /* byte rate */
        fread(&tmp16, 2, 1, file) == 1 && /* block align */
        fread(bits_per_sample, 2, 1, file) == 1;
    
    if (!success) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Skip extra bytes with padding (CODE_REVIEW4 Task 1.4) */
    if (chunk_size > 16) {
        size_t extra = chunk_size - 16;
        size_t skip = extra + (extra & 1);  /* Add padding if odd */
        fseek(file, (long)skip, SEEK_CUR);
    }
    
    return ESP_OK;
}

/* Helper: Skip unknown chunk with padding */
static void skip_wav_chunk(FILE *file, uint32_t chunk_size)
{
    /* WAV chunks are word-aligned: odd-sized chunks have 1 padding byte */
    size_t skip = chunk_size + (chunk_size & 1);
    fseek(file, (long)skip, SEEK_CUR);
}

/* Helper: Validate and convert WAV format parameters */
static esp_err_t validate_wav_params(uint16_t audio_format,
                                     uint16_t num_channels,
                                     uint16_t bits_per_sample,
                                     audio_bit_depth_t *src_bit)
{
    if (audio_format != 1) {
        return ESP_ERR_NOT_SUPPORTED; /* PCM only */
    }
    if (num_channels != 1 && num_channels != 2) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (bits_per_sample == 16) {
        *src_bit = AUDIO_BIT_DEPTH_16;
    } else if (bits_per_sample == 24) {
        *src_bit = AUDIO_BIT_DEPTH_24;
    } else if (bits_per_sample == 32) {
        *src_bit = AUDIO_BIT_DEPTH_32;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_OK;
}

static esp_err_t parse_wav_header(FILE *file,
                                  audio_bit_depth_t *src_bit,
                                  audio_sample_rate_t *src_rate,
                                  uint16_t *channels,
                                  size_t *data_bytes)
{
    if (!file || !src_bit || !src_rate || !channels || !data_bytes) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read RIFF/WAVE header */
    esp_err_t ret = read_wav_riff_header(file);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Parse chunks */
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_sz = 0;
    bool have_fmt = false;

    while (!feof(file)) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (fread(chunk_id, 1, 4, file) != 4 || fread(&chunk_size, 4, 1, file) != 1) {
            break;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            ret = parse_fmt_chunk(file, chunk_size, &audio_format, &num_channels,
                                 &sample_rate, &bits_per_sample);
            if (ret != ESP_OK) {
                return ret;
            }
            have_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_sz = chunk_size;
            break;
        } else {
            skip_wav_chunk(file, chunk_size);
        }
    }

    /* Validate we found required chunks */
    if (!have_fmt || data_sz == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Validate and convert parameters */
    ret = validate_wav_params(audio_format, num_channels, bits_per_sample, src_bit);
    if (ret != ESP_OK) {
        return ret;
    }

    *src_rate = (audio_sample_rate_t)sample_rate;
    *channels = num_channels;
    *data_bytes = data_sz;
    return ESP_OK;
}

esp_err_t play_manager_init(const audio_config_t *config,
                            const play_manager_buffers_t *buffers)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pm.initialized) {
        return ESP_OK;
    }

    util_safe_memset(&s_pm, 0, sizeof(s_pm));
    s_pm.out_cfg = *config;
    s_pm.work_bytes = (buffers && buffers->work_bytes > 0) ? buffers->work_bytes : AUDIO_CHUNK_BLOCK_BYTES;
    s_pm.mutex = xSemaphoreCreateMutex();
    if (s_pm.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_pm.initialized = true;
    return ESP_OK;
}

void play_manager_deinit(void)
{
    if (!s_pm.initialized) {
        return;
    }

    if (s_pm.mutex) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            if (s_pm.file) {
                fclose(s_pm.file);
                s_pm.file = NULL;
            }
            s_pm.active = false;
            s_pm.remaining_bytes = 0;
            s_pm.pending_bytes = 0;
            xSemaphoreGive(s_pm.mutex);
        }
        vSemaphoreDelete(s_pm.mutex);
        s_pm.mutex = NULL;
    }
    s_pm.initialized = false;
}

bool play_manager_is_active(void)
{
    return s_pm.active;
}

/* Helper: Allocate src and dst blocks */
static esp_err_t allocate_audio_blocks(uint8_t **src_block, uint8_t **dst_block)
{
    *src_block = audio_chunk_alloc_block(pdMS_TO_TICKS(5));
    if (*src_block == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *dst_block = audio_chunk_alloc_block(pdMS_TO_TICKS(5));
    if (*dst_block == NULL) {
        s_dst_block_null_count++;  /* Instrumentation */
        audio_chunk_release_block(*src_block);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/* Helper: Calculate aligned read size */
static size_t calculate_read_size(size_t frame_bytes, size_t remaining_bytes)
{
    size_t to_read = AUDIO_CHUNK_BLOCK_BYTES;
    
    /* Clamp to remaining bytes FIRST (CODE_REVIEW4 Task 1.5) */
    if (to_read > remaining_bytes) {
        to_read = remaining_bytes;
    }
    
    /* Align down to frame boundary */
    size_t frame_src = (frame_bytes != 0) ? frame_bytes : 1U;
    to_read = (to_read / frame_src) * frame_src;
    if (to_read == 0) {
        to_read = frame_src;
    }
    
    return to_read;
}

/* Helper: Read data from file and update accounting */
static esp_err_t read_audio_data(uint8_t *buffer, size_t to_read, size_t *bytes_read)
{
    *bytes_read = fread(buffer, 1, to_read, s_pm.file);
    if (*bytes_read == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Update instrumentation and state */
    s_bytes_read_from_file_total += *bytes_read;
    if (*bytes_read > s_pm.remaining_bytes) {
        s_pm.remaining_bytes = 0;
    } else {
        s_pm.remaining_bytes -= *bytes_read;
    }
    
    return ESP_OK;
}

/* Helper: Convert audio format */
static esp_err_t convert_audio_block(uint8_t *src_block, size_t src_size, size_t *conv_size)
{
    audio_convert_args_t conv_args = {
        .src = src_block,
        .dst = src_block,
        .src_size = src_size,
        .src_bit_depth = s_pm.src_bit,
        .dst_bit_depth = s_pm.out_cfg.bit_depth,
        .dst_size = conv_size,
        .work_bytes = s_pm.work_bytes,
    };
    return convert_audio_format(&conv_args);
}

/* Helper: Resample audio */
static esp_err_t resample_audio_block(const uint8_t *src_block, uint8_t *dst_block,
                                     size_t src_size, size_t *res_size)
{
    audio_resample_args_t res_args = {
        .src = src_block,
        .dst = dst_block,
        .src_size = src_size,
        .src_rate = s_pm.src_rate,
        .dst_rate = s_pm.out_cfg.sample_rate,
        .bit_depth = s_pm.out_cfg.bit_depth,
        .channels = s_pm.out_cfg.channels,
        .dst_size = res_size,
        .work_bytes = s_pm.work_bytes,
    };
    return resample_audio(&res_args);
}

/* Helper: Rewind file after enqueue failure */
static void rewind_after_enqueue_failure(size_t bytes_to_rewind)
{
    /* Rewind file (CODE_REVIEW4 Task 1.2) */
    if (fseek(s_pm.file, -(long)bytes_to_rewind, SEEK_CUR) != 0) {
        ESP_LOGW(TAG, "Failed to rewind file after enqueue failure");
    }
    
    /* Restore accounting - bytes_to_rewind always <= bytes_read_from_file_total at this point */
    s_pm.remaining_bytes += bytes_to_rewind;
    s_enqueue_fail_count++;
    s_bytes_read_from_file_total -= bytes_to_rewind;
}

/* Helper: Process one audio block (read, convert, resample, enqueue) */
static esp_err_t process_audio_block(void)
{
    uint8_t *src_block = NULL;
    uint8_t *dst_block = NULL;
    
    /* Allocate blocks */
    esp_err_t ret = allocate_audio_blocks(&src_block, &dst_block);
    if (ret != ESP_OK) {
        return ESP_OK;  /* Not an error, just no memory available */
    }
    
    /* Calculate read size */
    size_t to_read = calculate_read_size(s_pm.frame_bytes_src, s_pm.remaining_bytes);
    
    /* Read from file */
    size_t got = 0;
    ret = read_audio_data(src_block, to_read, &got);
    if (ret != ESP_OK) {
        audio_chunk_release_block(dst_block);
        audio_chunk_release_block(src_block);
        s_pm.remaining_bytes = 0;
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Convert format */
    size_t conv_size = 0;
    ret = convert_audio_block(src_block, got, &conv_size);
    if (ret != ESP_OK) {
        audio_chunk_release_block(dst_block);
        audio_chunk_release_block(src_block);
        return ret;
    }
    
    /* Resample */
    size_t res_size = 0;
    ret = resample_audio_block(src_block, dst_block, conv_size, &res_size);
    audio_chunk_release_block(src_block);
    if (ret != ESP_OK) {
        audio_chunk_release_block(dst_block);
        return ret;
    }
    
#ifdef CONFIG_BT_MOCK_TESTING
    if (s_pm.test_zero_resample) {
        res_size = 0;
    }
#endif
    
    /* Skip if empty */
    if (res_size == 0) {
        audio_chunk_release_block(dst_block);
        return ESP_OK;
    }
    
    /* Enqueue */
    if (!audio_chunk_enqueue_block(dst_block, res_size, AUDIO_SOURCE_TAG_WAV)) {
        rewind_after_enqueue_failure(got);
        audio_chunk_release_block(dst_block);
        return ESP_ERR_NO_MEM;  /* Signal to stop loop */
    }
    
    /* Update instrumentation */
    s_bytes_enqueued_total += res_size;
    s_pm.pending_bytes += res_size;
    
    return ESP_OK;
}

esp_err_t play_manager_fill(void)
{
    if (!s_pm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_pm.active || s_pm.file == NULL) {
        return ESP_OK;
    }
    if (s_pm.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Process multiple blocks per call */
    const int max_iters = 4;
    for (int iter = 0; iter < max_iters && s_pm.remaining_bytes > 0; ++iter) {
        ret = process_audio_block();
        if (ret == ESP_ERR_NO_MEM) {
            /* Queue full or allocation failed - stop and retry later */
            ret = ESP_OK;
            break;
        }
        if (ret != ESP_OK) {
            /* Real error - propagate */
            break;
        }
    }

    xSemaphoreGive(s_pm.mutex);
    return ret;
}

/* Helper: Open and parse WAV file */
static esp_err_t open_and_parse_wav(const char *path, FILE **file, 
                                    audio_bit_depth_t *src_bit,
                                    audio_sample_rate_t *src_rate,
                                    uint16_t *channels,
                                    size_t *data_bytes)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "play_manager_play_wav: failed to open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t status = parse_wav_header(f, src_bit, src_rate, channels, data_bytes);
    if (status != ESP_OK) {
        fclose(f);
        return status;
    }

    *file = f;
    return ESP_OK;
}

/* Helper: Calculate frame sizes from audio format */
static esp_err_t calculate_frame_sizes(audio_bit_depth_t src_bit, uint16_t channels,
                                       size_t *frame_bytes_src, size_t *frame_bytes_dst)
{
    int dst_bps = bytes_per_sample(s_pm.out_cfg.bit_depth);
    int dst_ch = (s_pm.out_cfg.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    *frame_bytes_dst = (size_t)dst_bps * (size_t)dst_ch;
    *frame_bytes_src = ((size_t)bytes_per_sample(src_bit)) * (size_t)channels;
    
    if (*frame_bytes_dst == 0 || *frame_bytes_src == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* Helper: Initialize playback state under mutex */
static esp_err_t initialize_playback_state(FILE *file, 
                                           audio_bit_depth_t src_bit,
                                           audio_sample_rate_t src_rate,
                                           size_t frame_bytes_src,
                                           size_t frame_bytes_dst,
                                           size_t data_bytes)
{
    if (s_pm.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_pm.active) {
        xSemaphoreGive(s_pm.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_pm.src_bit = src_bit;
    s_pm.src_rate = src_rate;
    s_pm.frame_bytes_src = frame_bytes_src;
    s_pm.frame_bytes_dst = frame_bytes_dst;
    s_pm.file = file;
    s_pm.remaining_bytes = data_bytes;
    s_pm.pending_bytes = 0;
    s_pm.active = true;

    /* Initialize instrumentation counters (CODE_REVIEW4 Task 0.2) */
    s_bytes_read_from_file_total = 0;
    s_bytes_enqueued_total = 0;
    s_enqueue_fail_count = 0;
    s_dst_block_null_count = 0;
    s_expected_data_bytes = data_bytes;

    xSemaphoreGive(s_pm.mutex);
    return ESP_OK;
}

/* Helper: Log playback instrumentation results */
static void log_playback_completion(void)
{
    ESP_LOGI(TAG, "WAV playback complete - instrumentation report:");
    ESP_LOGI(TAG, "  Expected data bytes: %zu", s_expected_data_bytes);
    ESP_LOGI(TAG, "  Bytes read from file: %zu", s_bytes_read_from_file_total);
    ESP_LOGI(TAG, "  Bytes enqueued: %zu", s_bytes_enqueued_total);
    ESP_LOGI(TAG, "  dst_block alloc failures: %zu", s_dst_block_null_count);
    ESP_LOGI(TAG, "  Enqueue failures: %zu", s_enqueue_fail_count);
    
    if (s_expected_data_bytes > 0) {
        size_t bytes_lost = 0;
        if (s_bytes_read_from_file_total > s_bytes_enqueued_total) {
            bytes_lost = s_bytes_read_from_file_total - s_bytes_enqueued_total;
        }
        float percent_lost = (float)bytes_lost / (float)s_expected_data_bytes * 100.0F;
        ESP_LOGI(TAG, "  Data loss: %zu bytes (%.2f%%)", bytes_lost, (double)percent_lost);
    }
}

/* Helper: Cleanup playback state */
static void cleanup_playback_state(void)
{
    if (s_pm.file) {
        fclose(s_pm.file);
        s_pm.file = NULL;
    }
    s_pm.active = false;
}

esp_err_t play_manager_play_wav(const char *path)
{
    /* Validate parameters */
    if (!s_pm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Open and parse WAV file */
    FILE *file = NULL;
    audio_bit_depth_t src_bit = AUDIO_BIT_DEPTH_16;
    audio_sample_rate_t src_rate = AUDIO_SAMPLE_RATE_44K;
    uint16_t channels = 0;
    size_t data_bytes = 0;

    esp_err_t status = open_and_parse_wav(path, &file, &src_bit, &src_rate, &channels, &data_bytes);
    if (status != ESP_OK) {
        return status;
    }

    /* Calculate frame sizes */
    size_t frame_bytes_src = 0;
    size_t frame_bytes_dst = 0;
    status = calculate_frame_sizes(src_bit, channels, &frame_bytes_src, &frame_bytes_dst);
    if (status != ESP_OK) {
        fclose(file);
        return status;
    }

    /* Initialize playback state */
    status = initialize_playback_state(file, src_bit, src_rate, frame_bytes_src, frame_bytes_dst, data_bytes);
    if (status != ESP_OK) {
        fclose(file);
        return status;
    }

    /* Start filling audio queue */
    status = play_manager_fill();
    if (status != ESP_OK) {
        play_manager_abort(false);
        return status;
    }

    /* Log playback start */
    ESP_LOGI(TAG, "play_manager: streaming %s (src %u Hz %d-bit ch=%u)",
             path,
             (unsigned)src_rate,
             (int)src_bit,
             (unsigned)channels);
    return ESP_OK;
}

void play_manager_abort(bool allow_resume)
{
    (void)allow_resume;
    if (!s_pm.initialized || s_pm.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
        if (s_pm.file) {
            fclose(s_pm.file);
            s_pm.file = NULL;
        }
        s_pm.active = false;
        s_pm.remaining_bytes = 0;
        s_pm.pending_bytes = 0;
        xSemaphoreGive(s_pm.mutex);
    }
}

bool play_manager_consume(size_t bytes)
{
    if (!s_pm.initialized || s_pm.mutex == NULL) {
        return false;
    }
    
    bool drained = false;
    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
        /* Update pending bytes accounting */
        if (bytes > s_pm.pending_bytes) {
            s_pm.pending_bytes = 0;
        } else {
            s_pm.pending_bytes -= bytes;
        }
        
        /* Check if playback complete */
        if (s_pm.pending_bytes == 0 && s_pm.remaining_bytes == 0) {
            log_playback_completion();
            cleanup_playback_state();
            drained = true;
        }
        
        xSemaphoreGive(s_pm.mutex);
    }
    return drained;
}

size_t play_manager_pending_bytes(void)
{
    size_t pending = 0;
    if (s_pm.initialized && s_pm.mutex != NULL) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            pending = s_pm.pending_bytes;
            xSemaphoreGive(s_pm.mutex);
        }
    }
    return pending;
}

#ifdef CONFIG_BT_MOCK_TESTING
void play_manager_test_set_frame_bytes_dst(size_t frame_bytes)
{
    if (s_pm.initialized && s_pm.mutex != NULL) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            s_pm.frame_bytes_dst = frame_bytes;
            xSemaphoreGive(s_pm.mutex);
        }
    }
}

void play_manager_test_force_zero_resample(bool enable)
{
    if (s_pm.initialized && s_pm.mutex != NULL) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            s_pm.test_zero_resample = enable;
            xSemaphoreGive(s_pm.mutex);
        }
    }
}
#endif

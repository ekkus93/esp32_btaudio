/**
 * WAV playback manager using audio_queue for zero-copy handoff.
 */

#include "play_manager.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "audio_queue.h"
#include "audio_util.h"
#include "util_safe.h"

static const char *TAG = "play_manager";

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
    uint8_t *proc_buf;
    uint8_t *proc_buf2;
    size_t work_bytes;
    uint8_t residual[AUDIO_CHUNK_BLOCK_BYTES];
    size_t residual_len;
    size_t residual_pos;
    SemaphoreHandle_t mutex;
} play_manager_state_t;

static play_manager_state_t s_pm = {0};

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

static esp_err_t parse_wav_header(FILE *f,
                                  audio_bit_depth_t *src_bit,
                                  audio_sample_rate_t *src_rate,
                                  uint16_t *channels,
                                  size_t *data_bytes)
{
    if (!f || !src_bit || !src_rate || !channels || !data_bytes) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t tmp32 = 0;
    char riff[4];
    if (fread(riff, 1, 4, f) != 4) return ESP_ERR_INVALID_STATE;
    if (memcmp(riff, "RIFF", 4) != 0) return ESP_ERR_INVALID_STATE;
    if (fread(&tmp32, 4, 1, f) != 1) return ESP_ERR_INVALID_STATE; /* skip size */
    char wave[4];
    if (fread(wave, 1, 4, f) != 4) return ESP_ERR_INVALID_STATE;
    if (memcmp(wave, "WAVE", 4) != 0) return ESP_ERR_INVALID_STATE;

    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_sz = 0;
    bool have_fmt = false;

    while (!feof(f)) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (fread(chunk_id, 1, 4, f) != 4) break;
        if (fread(&chunk_size, 4, 1, f) != 1) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) return ESP_ERR_INVALID_STATE;
            uint16_t tmp16 = 0;
            bool ok =
                fread(&audio_format, 2, 1, f) == 1 &&
                fread(&num_channels, 2, 1, f) == 1 &&
                fread(&sample_rate, 4, 1, f) == 1 &&
                fread(&tmp32, 4, 1, f) == 1 && /* byte rate */
                fread(&tmp16, 2, 1, f) == 1 && /* block align */
                fread(&bits_per_sample, 2, 1, f) == 1;
            if (!ok) return ESP_ERR_INVALID_STATE;
            if (chunk_size > 16) {
                fseek(f, (long)(chunk_size - 16), SEEK_CUR);
            }
            have_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_sz = chunk_size;
            break;
        } else {
            fseek(f, (long)chunk_size, SEEK_CUR);
        }
    }

    if (!have_fmt || data_sz == 0) return ESP_ERR_INVALID_STATE;
    if (audio_format != 1) return ESP_ERR_NOT_SUPPORTED; /* PCM only */
    if (num_channels != 1 && num_channels != 2) return ESP_ERR_INVALID_STATE;

    audio_bit_depth_t bit = AUDIO_BIT_DEPTH_16;
    if (bits_per_sample == 16) bit = AUDIO_BIT_DEPTH_16;
    else if (bits_per_sample == 24) bit = AUDIO_BIT_DEPTH_24;
    else if (bits_per_sample == 32) bit = AUDIO_BIT_DEPTH_32;
    else return ESP_ERR_NOT_SUPPORTED;

    *src_bit = bit;
    *src_rate = (audio_sample_rate_t)sample_rate;
    *channels = num_channels;
    *data_bytes = data_sz;
    return ESP_OK;
}

esp_err_t play_manager_init(const audio_config_t *config,
                            const play_manager_buffers_t *buffers)
{
    if (config == NULL || buffers == NULL || buffers->proc_buf == NULL || buffers->proc_buf2 == NULL || buffers->work_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pm.initialized) {
        return ESP_OK;
    }

    util_safe_memset(&s_pm, 0, sizeof(s_pm));
    s_pm.out_cfg = *config;
    s_pm.proc_buf = buffers->proc_buf;
    s_pm.proc_buf2 = buffers->proc_buf2;
    s_pm.work_bytes = buffers->work_bytes;
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
            s_pm.residual_len = 0;
            s_pm.residual_pos = 0;
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

static esp_err_t enqueue_buffer(const uint8_t *buf, size_t len, audio_source_tag_t tag, size_t frame_bytes)
{
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > AUDIO_CHUNK_BLOCK_BYTES) {
            chunk = AUDIO_CHUNK_BLOCK_BYTES;
        }
        if (frame_bytes > 0) {
            size_t aligned = (chunk / frame_bytes) * frame_bytes;
            if (aligned == 0) aligned = frame_bytes;
            if (aligned > chunk) aligned = chunk;
            chunk = aligned;
        }
        if (chunk == 0) break;
        if (!audio_chunk_enqueue_bytes(buf + offset, chunk, tag)) {
            return (offset > 0) ? ESP_OK : ESP_FAIL;
        }
        offset += chunk;
    }
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

    /* Flush any residual first */
    if (s_pm.residual_len > s_pm.residual_pos) {
        size_t pending = s_pm.residual_len - s_pm.residual_pos;
        if (enqueue_buffer(s_pm.residual + s_pm.residual_pos, pending, AUDIO_SOURCE_TAG_WAV, s_pm.frame_bytes_dst) == ESP_OK) {
            s_pm.pending_bytes += pending;
            s_pm.residual_len = 0;
            s_pm.residual_pos = 0;
        } else {
            xSemaphoreGive(s_pm.mutex);
            return ESP_OK; /* queue full; try later */
        }
    }

    const int max_iters = 4;
    for (int iter = 0; iter < max_iters && s_pm.remaining_bytes > 0; ++iter) {
        size_t frame_src = (s_pm.frame_bytes_src != 0) ? s_pm.frame_bytes_src : 1U;
        size_t to_read = s_pm.work_bytes;
        to_read = (to_read / frame_src) * frame_src;
        if (to_read == 0) {
            to_read = frame_src;
        }
        if (to_read > s_pm.remaining_bytes) {
            to_read = s_pm.remaining_bytes;
        }
        size_t got = fread(s_pm.proc_buf, 1, to_read, s_pm.file);
        if (got == 0) {
            s_pm.remaining_bytes = 0;
            break;
        }
        if (got > s_pm.remaining_bytes) {
            s_pm.remaining_bytes = 0;
        } else {
            s_pm.remaining_bytes -= got;
        }

        size_t conv_size = 0;
        audio_convert_args_t conv_args = {
            .src = s_pm.proc_buf,
            .dst = s_pm.proc_buf,
            .src_size = got,
            .src_bit_depth = s_pm.src_bit,
            .dst_bit_depth = s_pm.out_cfg.bit_depth,
            .dst_size = &conv_size,
            .work_bytes = s_pm.work_bytes,
        };
        ret = convert_audio_format(&conv_args);
        if (ret != ESP_OK) {
            break;
        }

        size_t res_size = 0;
        audio_resample_args_t res_args = {
            .src = s_pm.proc_buf,
            .dst = s_pm.proc_buf2,
            .src_size = conv_size,
            .src_rate = s_pm.src_rate,
            .dst_rate = s_pm.out_cfg.sample_rate,
            .bit_depth = s_pm.out_cfg.bit_depth,
            .channels = s_pm.out_cfg.channels,
            .dst_size = &res_size,
            .work_bytes = s_pm.work_bytes,
        };
        ret = resample_audio(&res_args);
        if (ret != ESP_OK) {
            break;
        }
        if (res_size == 0) {
            continue;
        }

        esp_err_t enq_ret = enqueue_buffer(s_pm.proc_buf2, res_size, AUDIO_SOURCE_TAG_WAV, s_pm.frame_bytes_dst);
        if (enq_ret != ESP_OK) {
            /* If queue was full, stash the remainder for next time. */
            size_t storable = res_size;
            if (storable > sizeof(s_pm.residual)) {
                storable = sizeof(s_pm.residual);
            }
            util_safe_memcpy(s_pm.residual, sizeof(s_pm.residual), s_pm.proc_buf2, storable);
            s_pm.residual_len = storable;
            s_pm.residual_pos = 0;
            ret = ESP_OK; /* not fatal; try again later */
            break;
        }
        s_pm.pending_bytes += res_size;
    }

    xSemaphoreGive(s_pm.mutex);
    return ret;
}

esp_err_t play_manager_play_wav(const char *path)
{
    if (!s_pm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t status = ESP_OK;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "play_manager_play_wav: failed to open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    audio_bit_depth_t src_bit = AUDIO_BIT_DEPTH_16;
    audio_sample_rate_t src_rate = AUDIO_SAMPLE_RATE_44K;
    uint16_t channels = 0;
    size_t data_bytes = 0;

    status = parse_wav_header(f, &src_bit, &src_rate, &channels, &data_bytes);
    if (status != ESP_OK) {
        fclose(f);
        return status;
    }

    int dst_bps = bytes_per_sample(s_pm.out_cfg.bit_depth);
    int dst_ch = (s_pm.out_cfg.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    size_t frame_bytes_dst = (size_t)dst_bps * (size_t)dst_ch;
    size_t frame_bytes_src = ((size_t)bytes_per_sample(src_bit)) * (size_t)channels;
    if (frame_bytes_dst == 0 || frame_bytes_src == 0) {
        fclose(f);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pm.mutex == NULL) {
        fclose(f);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) != pdTRUE) {
        fclose(f);
        return ESP_ERR_TIMEOUT;
    }

    if (s_pm.active) {
        xSemaphoreGive(s_pm.mutex);
        fclose(f);
        return ESP_ERR_INVALID_STATE;
    }

    s_pm.src_bit = src_bit;
    s_pm.src_rate = src_rate;
    s_pm.frame_bytes_src = frame_bytes_src;
    s_pm.frame_bytes_dst = frame_bytes_dst;
    s_pm.file = f;
    s_pm.remaining_bytes = data_bytes;
    s_pm.pending_bytes = 0;
    s_pm.residual_len = 0;
    s_pm.residual_pos = 0;
    s_pm.active = true;

    xSemaphoreGive(s_pm.mutex);

    status = play_manager_fill();
    if (status != ESP_OK) {
        play_manager_abort(false);
        return status;
    }

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
        s_pm.residual_len = 0;
        s_pm.residual_pos = 0;
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
        if (bytes > s_pm.pending_bytes) {
            s_pm.pending_bytes = 0;
        } else {
            s_pm.pending_bytes -= bytes;
        }
        if (s_pm.pending_bytes == 0 && s_pm.remaining_bytes == 0 && s_pm.residual_len == 0) {
            if (s_pm.file) {
                fclose(s_pm.file);
                s_pm.file = NULL;
            }
            s_pm.active = false;
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

size_t play_manager_test_residual_bytes(void)
{
    size_t rem = 0;
    if (s_pm.initialized && s_pm.mutex != NULL) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            if (s_pm.residual_len > s_pm.residual_pos) {
                rem = s_pm.residual_len - s_pm.residual_pos;
            }
            xSemaphoreGive(s_pm.mutex);
        }
    }
    return rem;
}
#endif

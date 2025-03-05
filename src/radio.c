#include "radio.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bluetooth/bt_app_audio.h"
#include "minimp3.h"
#include <string.h>
#include <inttypes.h>
#undef PRIu32
#define PRIu32 "u"
#include "esp_task_wdt.h"
#include "mp3dec.h"
#include "custom_log.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define TAG "RADIO"
#define BUFFER_SIZE 256
#define PCM_RING_BUFFER_SIZE (2 * 1024)
#define MP3_BUFFER_SIZE (2 * 1024)

static bool s_radio_active = false;
static bool s_radio_streaming_active = false;
static uint8_t s_pcm_ring[PCM_RING_BUFFER_SIZE] __attribute__((aligned(4)));
static size_t s_ring_head = 0, s_ring_tail = 0;
static uint8_t s_mp3_buffer[MP3_BUFFER_SIZE] __attribute__((unused, aligned(4)));
static size_t s_mp3_head __attribute__((unused)) = 0, s_mp3_tail __attribute__((unused)) = 0;

static TaskHandle_t s_radio_task_handle = NULL;

static SemaphoreHandle_t s_radio_mutex = NULL;
static SemaphoreHandle_t s_mp3_mutex __attribute__((unused)) = NULL;

static volatile bool s_radio_task_finished = true;

#define SIZE_FMT "%u"
#define SIZE_CAST(x) ((unsigned int)(x))

void radio_task(void *param) {
    radio_task_params_t *params = (radio_task_params_t*)param;
    uint8_t buf[BUFFER_SIZE];
    uint8_t frame_buf[BUFFER_SIZE * 2];
    int frame_buf_len = 0;
    int16_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_t mp3d;
    mp3dec_frame_info_t info;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    SAFE_ESP_LOGI(TAG, "radio_task: invoked");
    SAFE_ESP_LOGI(TAG, "radio_task: starting file playback");
    SAFE_ESP_LOGI(TAG, "File descriptor: %d", fileno(params->fp));

    {
        char id3_header[10];
        if (fread(id3_header, 1, 10, params->fp) == 10) {
            if (id3_header[0]=='I' && id3_header[1]=='D' && id3_header[2]=='3') {
                uint32_t tag_size = ((id3_header[6] & 0x7F) << 21) |
                                  ((id3_header[7] & 0x7F) << 14) |
                                  ((id3_header[8] & 0x7F) << 7)  |
                                  (id3_header[9] & 0x7F);
                SAFE_ESP_LOGI(TAG, "Skipping ID3 tag of size %d bytes", (int)tag_size);
                fseek(params->fp, tag_size, SEEK_CUR);
            } else {
                fseek(params->fp, -10, SEEK_CUR);
            }
        }
    }

    mp3dec_init(&mp3d);
    size_t remaining = params->size;
    int free_format_bytes = 0; // Initialize free_format_bytes
    
    while (remaining > 0) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        size_t to_read = MINIMP3_MIN(remaining, BUFFER_SIZE);
        size_t read = fread(buf, 1, to_read, params->fp);
        
        if (read == 0) break;
        
        SAFE_ESP_LOGI(TAG, "Processing %d bytes from file", (int)read);

        memcpy(frame_buf + frame_buf_len, buf, read);
        frame_buf_len += read;

        int processed = 0;
        while (processed < frame_buf_len) {
            int frame_bytes;
            int frame_offset = mp3d_find_frame(frame_buf + processed, frame_buf_len - processed, &free_format_bytes, &frame_bytes);

            if (frame_offset == 0 && frame_bytes > 0) {
                SAFE_ESP_LOGI(TAG, "Found MP3 frame at position %d, frame_bytes = %d", processed, frame_bytes);
                int frame_size = mp3dec_decode_frame(&mp3d, frame_buf + processed, frame_buf_len - processed, pcm_buf, &info);
                SAFE_ESP_LOGI(TAG, "frame_size: %d", frame_size);
                if (frame_size > 0) {
                    SAFE_ESP_LOGI(TAG, "Decoded %d samples", frame_size);

                    size_t written = frame_size * sizeof(int16_t);
                    esp_err_t err = bluetooth_write_audio((const uint8_t*)pcm_buf, &written);
                    if (err != ESP_OK) {
                        SAFE_ESP_LOGE(TAG, "Failed to write audio data: %d", err);
                        break;
                    }
                    
                    if (written < frame_size * sizeof(int16_t)) {
                        SAFE_ESP_LOGW(TAG, "Partial write: %d/%d bytes", 
                                     (int)written, (int)(frame_size * sizeof(int16_t)));
                    }

                    // Move to the next frame using the size of the decoded frame
                    processed += frame_bytes;
                } else {
                    SAFE_ESP_LOGE(TAG, "Failed to decode frame at position %d", processed);
                    processed++;
                }
            } else {
                if (frame_offset > 0) {
                    SAFE_ESP_LOGW(TAG, "Skipping %d bytes to find next frame", frame_offset);
                    processed += frame_offset;
                } else {
                    // No frame found, skip one byte
                    processed++;
                }
            }
        }

        frame_buf_len -= processed;
        memmove(frame_buf, frame_buf + processed, frame_buf_len);

        remaining -= read;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (params->fp) fclose(params->fp);
    free(params);
    
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
    s_radio_task_finished = true;
    vTaskDelete(NULL);
}

void radio_write_pcm(const int16_t *samples, int num_samples) {
    size_t bytes_to_write = num_samples * sizeof(int16_t);
    size_t space_available = (s_ring_tail >= s_ring_head) ? 
        (PCM_RING_BUFFER_SIZE - s_ring_tail + s_ring_head - 1) : 
        (s_ring_head - s_ring_tail - 1);

    if (bytes_to_write > space_available) {
        SAFE_ESP_LOGW(TAG, "Ring buffer overflow, buffer usage: %u/%u bytes", 
                    SIZE_CAST(PCM_RING_BUFFER_SIZE - space_available), 
                    SIZE_CAST(PCM_RING_BUFFER_SIZE));
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }

    if (s_ring_tail + bytes_to_write <= PCM_RING_BUFFER_SIZE) {
        memcpy(&s_pcm_ring[s_ring_tail], samples, bytes_to_write);
        s_ring_tail = (s_ring_tail + bytes_to_write) % PCM_RING_BUFFER_SIZE;
    } else {
        size_t first_chunk = PCM_RING_BUFFER_SIZE - s_ring_tail;
        memcpy(&s_pcm_ring[s_ring_tail], samples, first_chunk);
        memcpy(s_pcm_ring, (uint8_t*)samples + first_chunk, bytes_to_write - first_chunk);
        s_ring_tail = bytes_to_write - first_chunk;
    }

    static uint32_t last_log = 0;
    unsigned int now = (unsigned int)esp_log_timestamp();
    if (now - last_log > 1000) {
        SAFE_ESP_LOGI(TAG, "Ring buffer usage: %u/%u bytes", 
                    SIZE_CAST(PCM_RING_BUFFER_SIZE - space_available),
                    SIZE_CAST(PCM_RING_BUFFER_SIZE));
        last_log = now;
    }
}

size_t radio_read_pcm(uint8_t *dest, size_t len) {
    size_t bytes_to_read = len;
    size_t available = (s_ring_tail >= s_ring_head)
        ? (s_ring_tail - s_ring_head)
        : (PCM_RING_BUFFER_SIZE - s_ring_head + s_ring_tail);
    if (bytes_to_read > available) bytes_to_read = available;

    if (s_ring_head + bytes_to_read <= PCM_RING_BUFFER_SIZE) {
        memcpy(dest, &s_pcm_ring[s_ring_head], bytes_to_read);
        s_ring_head = (s_ring_head + bytes_to_read) % PCM_RING_BUFFER_SIZE;
    } else {
        size_t first_chunk = PCM_RING_BUFFER_SIZE - s_ring_head;
        memcpy(dest, &s_pcm_ring[s_ring_head], first_chunk);
        memcpy(dest + first_chunk, s_pcm_ring, bytes_to_read - first_chunk);
        s_ring_head = bytes_to_read - first_chunk;
    }
    return bytes_to_read;
}

static __attribute__((unused)) size_t write_mp3_data(const uint8_t* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        if (!s_mp3_mutex || xSemaphoreTake(s_mp3_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t space = (s_mp3_head > s_mp3_tail) ?
            (s_mp3_head - s_mp3_tail - 1) :
            (MP3_BUFFER_SIZE - s_mp3_tail + s_mp3_head - 1);
        if (space == 0) {
            xSemaphoreGive(s_mp3_mutex);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t to_write = ((len - written) > space) ? space : (len - written);
        if (s_mp3_tail + to_write <= MP3_BUFFER_SIZE) {
            memcpy(&s_mp3_buffer[s_mp3_tail], data + written, to_write);
            s_mp3_tail = (s_mp3_tail + to_write) % MP3_BUFFER_SIZE;
        } else {
            size_t first = MP3_BUFFER_SIZE - s_mp3_tail;
            memcpy(&s_mp3_buffer[s_mp3_tail], data + written, first);
            memcpy(s_mp3_buffer, data + written + first, to_write - first);
            s_mp3_tail = to_write - first;
        }
        written += to_write;
        xSemaphoreGive(s_mp3_mutex);
    }
    SAFE_ESP_LOGI(TAG, "Written %d bytes to MP3 buffer", (int)written);
    return written;
}

esp_err_t radio_play(const char *url) {
    SAFE_ESP_LOGI(TAG, "radio_play: called with URL: %s", url);
    if (!s_radio_mutex) {
        s_radio_mutex = xSemaphoreCreateMutex();
        if (!s_radio_mutex) {
            SAFE_ESP_LOGE(TAG, "Failed to create radio mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        SAFE_ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        SAFE_ESP_LOGW(TAG, "Radio already playing");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_radio_mutex);

    if (!url) {
        SAFE_ESP_LOGE(TAG, "Invalid URL");
        return ESP_ERR_INVALID_ARG;
    }

    if (xTaskCreate(radio_task, "radio_task", 8192, (void*)url, 5, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t radio_stop(void) {
    SAFE_ESP_LOGI(TAG, "radio_stop: called");
    if (!s_radio_mutex || xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        SAFE_ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (!s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        SAFE_ESP_LOGW(TAG, "Radio not active");
        return ESP_ERR_INVALID_STATE;
    }

    s_radio_active = false;
    xSemaphoreGive(s_radio_mutex);

    int wait = 0;
    while (!s_radio_task_finished && (wait < 100)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait++;
    }
    if (!s_radio_task_finished) {
        SAFE_ESP_LOGE(TAG, "Radio task did not finish in time");
        return ESP_ERR_TIMEOUT;
    }
    s_radio_task_handle = NULL;
    return ESP_OK;
}

void radio_set_active(bool active) {
    SAFE_ESP_LOGI(TAG, "radio_set_active: Changing active state to: %d", active);
    s_radio_streaming_active = active;
}

esp_err_t radio_play_stream(const char *url) {
    SAFE_ESP_LOGI(TAG, "radio_play_stream: called with URL: %s", url);
    if (url == NULL || strlen(url) == 0) {
        SAFE_ESP_LOGE("RADIO", "Invalid URL provided to radio_play_stream");
        return ESP_ERR_INVALID_ARG;
    }
    
    SAFE_ESP_LOGI("RADIO", "Starting radio stream with URL: %s", url);
    
    // ...existing code to initialize the stream...
    
    return ESP_OK;
}

static __attribute__((unused)) esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static size_t total_bytes = 0;

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            SAFE_ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            SAFE_ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            SAFE_ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            SAFE_ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            total_bytes += evt->data_len;
            SAFE_ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%" PRIu32 ", total=%" PRIu32,
                     evt->data_len, total_bytes);
            if (evt->data_len > 0) {
                SAFE_ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA: hex data:");
                for (int i = 0; i < evt->data_len; i++) {
                    printf("%02x ", ((uint8_t*)evt->data)[i]);
                }
                printf("\n");

                size_t written = write_mp3_data((const uint8_t*)evt->data, evt->data_len);
                SAFE_ESP_LOGI(TAG, "Written %d bytes to MP3 buffer", written);
                if (written < evt->data_len) {
                    SAFE_ESP_LOGW(TAG, "Buffer full, some data dropped");
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            SAFE_ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            SAFE_ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            SAFE_ESP_LOGI(TAG, "%s", "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}
#include "radio.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"  // Include the minimp3 header

#define TAG "RADIO"
#define BUFFER_SIZE 512
#define PCM_RING_BUFFER_SIZE (4 * 1024)  // Further reduced
#define MP3_BUFFER_SIZE (4 * 1024)  // Further reduced

static bool s_radio_active = false;
static bool s_radio_streaming_active = false;
static uint8_t s_pcm_ring[PCM_RING_BUFFER_SIZE] __attribute__((aligned(4)));
static size_t s_ring_head = 0, s_ring_tail = 0;
static uint8_t s_mp3_buffer[MP3_BUFFER_SIZE] __attribute__((aligned(4)));
static size_t s_mp3_head = 0, s_mp3_tail = 0;

static TaskHandle_t s_radio_task_handle = NULL;
static mp3dec_t s_mp3_decoder;  // Minimp3 decoder context

// Add mutex for protecting shared state
static SemaphoreHandle_t s_radio_mutex = NULL;
static SemaphoreHandle_t s_mp3_mutex = NULL;

// Forward declaration of the event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt);

// Utility to store PCM samples in our ring buffer
void radio_write_pcm(const int16_t *samples, int num_samples) {
    size_t bytes_to_write = num_samples * sizeof(int16_t);
    size_t space_available = (s_ring_tail >= s_ring_head) ? 
        (PCM_RING_BUFFER_SIZE - s_ring_tail + s_ring_head - 1) : 
        (s_ring_head - s_ring_tail - 1);

    if (bytes_to_write > space_available) {
        ESP_LOGW(TAG, "Ring buffer overflow, buffer usage: %d/%d bytes", 
                 PCM_RING_BUFFER_SIZE - space_available, PCM_RING_BUFFER_SIZE);
        // Wait a bit when buffer is full
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

    // Log buffer usage periodically
    static uint32_t last_log = 0;
    uint32_t now = esp_log_timestamp();
    if (now - last_log > 1000) {  // Log every second
        ESP_LOGI(TAG, "Ring buffer usage: %d/%d bytes", 
                 PCM_RING_BUFFER_SIZE - space_available, PCM_RING_BUFFER_SIZE);
        last_log = now;
    }
}

// New accessor to read PCM data from the ring buffer.
// Returns the number of bytes read.
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

// Helper function to write to MP3 buffer
static size_t write_mp3_data(const uint8_t* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        // Wait for the mutex with a timeout
        if (!s_mp3_mutex || xSemaphoreTake(s_mp3_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t space = (s_mp3_head > s_mp3_tail) ?
            (s_mp3_head - s_mp3_tail - 1) :
            (MP3_BUFFER_SIZE - s_mp3_tail + s_mp3_head - 1);
        if (space == 0) {
            // Buffer full, release the mutex and wait briefly
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
    return written;
}

// Helper function to read from MP3 buffer
static size_t read_mp3_data(uint8_t* data, size_t len) {
    if (!s_mp3_mutex || xSemaphoreTake(s_mp3_mutex, 0) != pdTRUE) {
        return 0;
    }
    
    size_t available = (s_mp3_tail >= s_mp3_head) ?
        (s_mp3_tail - s_mp3_head) :
        (MP3_BUFFER_SIZE - s_mp3_head + s_mp3_tail);
        
    size_t to_read = (len > available) ? available : len;
    
    if (to_read > 0) {
        if (s_mp3_head + to_read <= MP3_BUFFER_SIZE) {
            memcpy(data, &s_mp3_buffer[s_mp3_head], to_read);
            s_mp3_head = (s_mp3_head + to_read) % MP3_BUFFER_SIZE;
        } else {
            size_t first = MP3_BUFFER_SIZE - s_mp3_head;
            memcpy(data, &s_mp3_buffer[s_mp3_head], first);
            memcpy(data + first, s_mp3_buffer, to_read - first);
            s_mp3_head = to_read - first;
        }
    }
    
    xSemaphoreGive(s_mp3_mutex);
    return to_read;
}

// HTTP client + decode task
void radio_task(void *param) {
    ESP_LOGD(TAG, "radio_task: param pointer=%p", param);
    if (param == NULL) {  // Added explicit check
        ESP_LOGE(TAG, "radio_task: NULL parameter provided");
        vTaskDelete(NULL);
        return;
    }
    const char *url_str = (const char *)param;
    if (strlen(url_str) == 0) {  // Added check for empty URL string
        ESP_LOGE(TAG, "radio_task: Empty URL string provided");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "radio_task: started");
    char *url_copy = strdup(url_str);
    ESP_LOGI(TAG, "radio_task: url_copy=%p, contents=%s", url_copy, url_copy);
    if (!url_copy) {
        ESP_LOGE(TAG, "Failed to allocate URL copy");
        vTaskDelete(NULL);
        return;
    }
    // Extra verification: check that the duplicated URL is of an expected minimum length.
    size_t url_len = strlen(url_copy);
    if (url_len < 10) {
        ESP_LOGE(TAG, "radio_task: URL copy seems invalid (length=%d)", url_len);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Allocated url_copy: %p, contents: %s", url_copy, url_copy);

    // Added: log the available stack to diagnose potential overflow
    ESP_LOGI(TAG, "Radio task stack high watermark: %d", (int)uxTaskGetStackHighWaterMark(NULL));

    // Define a static array for the URL buffer
    static char url_buffer[512];
    ESP_LOGI(TAG, "Using static url_buffer: %p", (void*)url_buffer);

    if (strlen(url_copy) >= 512) {
        ESP_LOGE(TAG, "URL too long");
        goto cleanup;
    }
    strncpy(url_buffer, url_copy, 511);
    url_buffer[511] = '\0';
    ESP_LOGI(TAG, "URL buffer: %s", url_buffer);

    // Ensure the mutex is initialized
    if (!s_radio_mutex) {
        s_radio_mutex = xSemaphoreCreateMutex();
        if (!s_radio_mutex) {
            ESP_LOGE(TAG, "Failed to create radio mutex");
            goto cleanup;
        }
    }

    if (!s_mp3_mutex) {
        s_mp3_mutex = xSemaphoreCreateMutex();
    }

    // Use mutex for state changes
    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take radio mutex");
        goto cleanup;
    }
    ESP_LOGI(TAG, "radio_task: setting radio active");
    s_radio_active = true;
    xSemaphoreGive(s_radio_mutex);

    ESP_LOGI(TAG, "radio_task: fetching: %s", url_buffer);

    esp_http_client_config_t config = {
        .url = url_buffer,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 0,  // Changed to 0 for streaming
        .event_handler = http_event_handler,
        .buffer_size = BUFFER_SIZE,
        .buffer_size_tx = 512,
        // --- Added user agent for compatibility with some streaming servers ---
        .user_agent = "ESP32-A2DP/1.0",
        // --- end addition ---
        .disable_auto_redirect = false,
        .skip_cert_common_name_check = true
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        goto cleanup;
    }

    // Instead of blocking perform(), open the connection and read data in a loop.
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        esp_http_client_cleanup(client);
        goto cleanup;
    }
    ESP_LOGI(TAG, "HTTP connection opened");

    mp3dec_init(&s_mp3_decoder);
    uint8_t buffer[BUFFER_SIZE];
    mp3dec_frame_info_t info;
    int16_t pcm_output[MINIMP3_MAX_SAMPLES_PER_FRAME];

    // Instead of extensive debug in every loop, log only when processing actual MP3 data.
    while (s_radio_active) {
        int bytes_read = esp_http_client_read(client, (char*)buffer, BUFFER_SIZE);
        if (bytes_read > 0) {
            size_t written = write_mp3_data(buffer, bytes_read);
            // Log only once when MP3 data is written.
            ESP_LOGD(TAG, "HTTP read %d bytes, wrote %d to MP3 buffer", bytes_read, written);
        } else if (bytes_read < 0) {
            ESP_LOGE(TAG, "HTTP read error: %d", bytes_read);
            break;
        }
        size_t available = read_mp3_data(buffer, BUFFER_SIZE);
        if (available > 0) {
            int samples = mp3dec_decode_frame(&s_mp3_decoder, buffer, available, pcm_output, &info);
            if (samples == 0) {
                ESP_LOGD(TAG, "No samples decoded – waiting for more MP3 data");
            }
            if (samples > 0) {
                int total = 0;
                int num_samples = samples * info.channels;
                for (int j = 0; j < num_samples; j++) {
                    total += (pcm_output[j] < 0) ? -pcm_output[j] : pcm_output[j];
                }
                int avg = total / num_samples;
                // Log average amplitude only when samples are decoded.
                ESP_LOGI(TAG, "PCM amplitude average: %d", avg);
                if (avg < 100) {
                    ESP_LOGW(TAG, "PCM amplitude appears low");
                }
                radio_write_pcm(pcm_output, num_samples);
            }
        }
        // Minimal periodic delay to yield.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

cleanup:
    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_radio_active = false;
        xSemaphoreGive(s_radio_mutex);
    }
    free(url_copy);
    ESP_LOGI(TAG, "radio_task: finished");
    vTaskDelete(NULL);
}

// Start the radio streaming
esp_err_t radio_play(const char *url) {
    ESP_LOGI(TAG, "radio_play: called with URL: %s", url);
    if (!s_radio_mutex) {
        s_radio_mutex = xSemaphoreCreateMutex();
        if (!s_radio_mutex) {
            ESP_LOGE(TAG, "Failed to create radio mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        ESP_LOGW(TAG, "Radio already playing");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_radio_mutex);

    if (!url) {
        ESP_LOGE(TAG, "Invalid URL");
        return ESP_ERR_INVALID_ARG;
    }

    if (xTaskCreate(radio_task, "radio_task", 8192, (void*)url, 5, &s_radio_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radio task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

// Stop the radio streaming
esp_err_t radio_stop(void) {
    ESP_LOGI(TAG, "radio_stop: called");
    if (!s_radio_mutex || xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (!s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        ESP_LOGW(TAG, "Radio not active");
        return ESP_ERR_INVALID_STATE;
    }

    s_radio_active = false;
    xSemaphoreGive(s_radio_mutex);

    // Wait for task to finish
    if (s_radio_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to clean up
    }

    return ESP_OK;
}

void radio_set_active(bool active) {
    ESP_LOGI(TAG, "radio_set_active: called with active: %d", active);
    s_radio_streaming_active = active;
}

// Added validation for URL pointer.
esp_err_t radio_play_stream(const char *url) {
    ESP_LOGI(TAG, "radio_play_stream: called with URL: %s", url);
    if (url == NULL || strlen(url) == 0) {
        ESP_LOGE("RADIO", "Invalid URL provided to radio_play_stream");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI("RADIO", "Starting radio stream with URL: %s", url);
    
    // ...existing code to initialize the stream...
    
    return ESP_OK;
}

// Event handler for HTTP client
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static size_t total_bytes = 0;

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            total_bytes += evt->data_len;
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d, total=%d", evt->data_len, total_bytes);
            if (evt->data_len > 0) {
                size_t written = write_mp3_data((const uint8_t*)evt->data, evt->data_len);
                ESP_LOGI(TAG, "Written %d bytes to MP3 buffer", written);
                if (written < evt->data_len) {
                    ESP_LOGW(TAG, "Buffer full, some data dropped");
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}
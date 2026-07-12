#ifdef _POSIX_READER_WRITER_LOCKS
// Clear toolchain define so newlib can set the standard value without a redefinition warning.
#undef _POSIX_READER_WRITER_LOCKS
#endif

#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
#include <stdatomic.h>

#include "audio_processor_internal.h"
#include "uart_source.h"
#include "util_safe.h"
#include "nvs_storage.h"
#include "esp_timer.h"
#include "platform_memory.h"
#include "audio_span_log.h"
#include "platform_timing.h"
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
#include "esp_psram.h"
#endif

static esp_err_t configure_i2s(const audio_config_t* config);

/**
 * @brief Set the output sample rate
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_config.sample_rate == sample_rate) {
        return ESP_OK;
    }

    bool was_running = s_is_running;
    esp_err_t ret = ESP_OK;
    if (was_running) {
        ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    s_audio_config.sample_rate = sample_rate;

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);  // NOLINT(bugprone-branch-clone)
        return ret;
    }

    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio sample rate changed to %d Hz", sample_rate);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

/**
 * @brief Set audio volume
 */
esp_err_t audio_processor_set_volume(uint8_t volume)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    // Clamp volume to 0-100
    if (volume > 100) {
        volume = 100;
    }

    s_volume_gain = volume;
    s_audio_config.volume = volume;

    /* CODE_REVIEW8 Task D: Debounce NVS commit to prevent flash wear
     * Cancel pending timer (if any) and restart with 500ms delay.
     * Volume persisted only after user "settles" on final value.
     * Reduces flash writes from "every change" to "once per adjustment session" */
    #ifndef UNIT_TEST
    if (s_volume_commit_timer != NULL) {
        esp_timer_stop(s_volume_commit_timer);  /* Cancel pending commit */
        esp_timer_start_once(s_volume_commit_timer, 500000);  /* 500ms = 500,000 microseconds */
    }
    #endif

    ESP_LOGI(TAG, "Audio volume set to %d%% (NVS commit debounced)", volume);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

/**
 * @brief Mute or unmute audio
 */
esp_err_t audio_processor_set_mute(bool mute)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    s_audio_config.mute = mute;
    ESP_LOGI(TAG, "Audio %s", mute ? "muted" : "unmuted");  // NOLINT(bugprone-branch-clone)

    return ESP_OK;
}

/**
 * @brief Get current audio configuration
 */
esp_err_t audio_processor_get_config(audio_config_t* config)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Null config pointer");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    safe_memcpy(config, sizeof(*config), &s_audio_config, sizeof(audio_config_t));
    return ESP_OK;
}

/**
 * @brief Get audio processing statistics
 */
esp_err_t audio_processor_get_stats(audio_stats_t* stats)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (stats == NULL) {
        ESP_LOGE(TAG, "Null stats pointer");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    /* Protect read access (CODE_REVIEW 2602101453, P1.2.4)
     * WHY: Ensure atomic snapshot of all fields to prevent torn reads */
    portENTER_CRITICAL(&s_audio_stats_lock);
    safe_memcpy(stats, sizeof(*stats), &s_audio_stats, sizeof(audio_stats_t));
    portEXIT_CRITICAL(&s_audio_stats_lock);
    return ESP_OK;
}

/*******************************
 * Internal functions
 *******************************/

/**
 * @brief Configure capture path via i2s_manager using shared buffers.
 */
static esp_err_t configure_i2s(const audio_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_manager_deinit();

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };

    return i2s_manager_init(config, &bufs);
}

esp_err_t audio_processor_get_status(audio_status_t* status)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (status == NULL) {
    	return ESP_ERR_INVALID_ARG;
    }
    status->initialized = s_is_initialized;
    status->running = s_is_running;
    status->volume = s_volume_gain;
    status->mute = s_audio_config.mute;
    status->sample_rate = s_audio_config.sample_rate;
    status->bit_depth = s_audio_config.bit_depth;
    status->channels = s_audio_config.channels;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool was_running = s_is_running;
    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
        	return ret;
        }
    }

    s_audio_config.i2s_bclk_pin = bclk_pin;
    s_audio_config.i2s_ws_pin = ws_pin;
    s_audio_config.i2s_din_pin = din_pin;
    s_audio_config.i2s_dout_pin = dout_pin;

    // Persist pins
    nvs_storage_set_i2s_pins(bclk_pin, ws_pin, din_pin, dout_pin);

    esp_err_t ret = configure_i2s(&s_audio_config);

    if (was_running && ret == ESP_OK) {
        ret = audio_processor_start();
    }

    return ret;
}
/**
 * @brief Set the audio bit depth
 */
esp_err_t audio_processor_set_channels(audio_channel_t channels)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (channels != AUDIO_CHANNEL_MONO && channels != AUDIO_CHANNEL_STEREO) {
        ESP_LOGE(TAG, "Invalid channel configuration: %d", (int)channels);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    if (s_audio_config.channels == channels) {
        return ESP_OK;
    }

    bool was_running = s_is_running;
    esp_err_t ret = ESP_OK;

    if (was_running) {
        ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    s_audio_config.channels = channels;

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);  // NOLINT(bugprone-branch-clone)
        return ret;
    }

    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio channels changed to %d", (int)channels);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

/**
 * @brief Set the audio bit depth
 */
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_config.bit_depth == bit_depth) {
        return ESP_OK; // Nothing to change
    }

    bool was_running = s_is_running;
    esp_err_t ret = ESP_OK;

    if (was_running) {
        ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    s_audio_config.bit_depth = bit_depth;

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);  // NOLINT(bugprone-branch-clone)
        return ret;
    }

    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);  // NOLINT(bugprone-branch-clone)
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio bit depth changed to %d bits", bit_depth);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

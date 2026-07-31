#include "bt_events_a2dp_internal.h"

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
#include "esp_log.h"
#include "audio_processor.h"
#include <string.h>

#define TAG "BT_EVT_A2DP"
#define A2DP_DATA_ERROR_LOG_INTERVAL 64U

typedef struct {
    uint64_t audio_read_failures;
    esp_err_t last_audio_read_error;
    uint32_t suppressed_audio_read_error_logs;
} a2dp_data_diagnostics_t;

/* The ESP-IDF A2DP source invokes its data callback serially. Only that
 * callback writes this diagnostic state in production; UNIT_TEST snapshots are
 * taken after synchronous callback return. Avoiding the manager mutex keeps the
 * real-time callback nonblocking and allocation-free. */
static a2dp_data_diagnostics_t s_a2dp_data_diag;

static void a2dp_data_record_audio_read_failure(esp_err_t err,
                                                   size_t requested)
{
    if (s_a2dp_data_diag.audio_read_failures != UINT64_MAX) {
        s_a2dp_data_diag.audio_read_failures++;
    }
    s_a2dp_data_diag.last_audio_read_error = err;

    const uint64_t failures = s_a2dp_data_diag.audio_read_failures;
    const bool should_log =
        failures == 1U ||
        (failures % A2DP_DATA_ERROR_LOG_INTERVAL) == 0U;
    if (!should_log) {
        if (s_a2dp_data_diag.suppressed_audio_read_error_logs != UINT32_MAX) {
            s_a2dp_data_diag.suppressed_audio_read_error_logs++;
        }
        return;
    }

    ESP_LOGE(TAG,
             "A2DP data callback audio_processor_read failed: requested=%u error=%s failures=%llu suppressed_logs=%u",
             (unsigned)requested,
             esp_err_to_name(err),
             (unsigned long long)failures,
             (unsigned)s_a2dp_data_diag.suppressed_audio_read_error_logs);
}

// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written
int32_t bt_events_a2dp_data_callback(uint8_t *buf, int32_t len)
{
    if (buf == NULL) {
        ESP_LOGE(TAG, "bt_events_a2dp_data_callback: NULL buffer pointer");
        return 0;
    }

    if (len < 0) {
        ESP_LOGE(TAG, "bt_events_a2dp_data_callback: INVALID negative length=%d (BT stack bug!)", len);
        return 0;
    }

    if (len == 0) {
        ESP_LOGW(TAG, "bt_events_a2dp_data_callback: zero-length request (should never happen)");
        return 0;
    }

    size_t req = (size_t)len;

    size_t bytes_read = 0;
    esp_err_t ret = audio_processor_read(buf, req, &bytes_read);
    if (ret != ESP_OK) {
        a2dp_data_record_audio_read_failure(ret, req);
        return 0;
    }

    return (int32_t)bytes_read;
}

#ifdef UNIT_TEST
void bt_events_a2dp_test_reset_data_diagnostics(void)
{
    memset(&s_a2dp_data_diag, 0, sizeof(s_a2dp_data_diag));
    s_a2dp_data_diag.last_audio_read_error = ESP_OK;
}

esp_err_t bt_events_a2dp_test_get_data_diagnostics(
    bt_events_a2dp_data_diag_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    out->audio_read_failures = s_a2dp_data_diag.audio_read_failures;
    out->last_audio_read_error = s_a2dp_data_diag.last_audio_read_error;
    out->suppressed_audio_read_error_logs =
        s_a2dp_data_diag.suppressed_audio_read_error_logs;
    return ESP_OK;
}
#endif

#endif // ESP_PLATFORM || UNIT_TEST

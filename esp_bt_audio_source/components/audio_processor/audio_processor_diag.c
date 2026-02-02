#include "audio_processor_internal.h"

static int16_t clamp_int16(int32_t value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if  (value < INT16_MIN) {
    	return INT16_MIN;
    }
    return (int16_t)value;
}

static int32_t clamp_int32(int64_t value)
{
    if (value > INT32_MAX) return INT32_MAX;
    if  (value < INT32_MIN) {
    	return INT32_MIN;
    }
    return (int32_t)value;
}

void apply_volume(void* buffer, size_t size, uint8_t volume)
{
    if (volume >= 100U || buffer == NULL || size == 0U) {
        return;
    }

    if (volume == 0U) {
        safe_memset(buffer, size, 0, size);
        return;
    }

    const int32_t scale = (int32_t)volume;
    const int32_t divisor = 100;

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* samples = (int16_t*)buffer;
        int sample_count = (int)(size / sizeof(int16_t));

        for (int i = 0; i < sample_count; i++) {
            int32_t scaled = ((int32_t)samples[i] * scale + (divisor / 2)) / divisor;
            samples[i] = clamp_int16(scaled);
        }
    }
    else if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_24 ||
             s_audio_config.bit_depth == AUDIO_BIT_DEPTH_32) {
        int32_t* samples = (int32_t*)buffer;
        int sample_count = (int)(size / sizeof(int32_t));

        for (int i = 0; i < sample_count; i++) {
            int64_t scaled = ((int64_t)samples[i] * (int64_t)scale + (divisor / 2)) / divisor;
            samples[i] = clamp_int32(scaled);
        }
    }
}

esp_err_t audio_processor_emit_diag_summary(void)
{
    unsigned ops = __atomic_load_n(&s_i2s_read_ops, __ATOMIC_RELAXED);
    unsigned bytes = __atomic_load_n(&s_i2s_total_read_bytes, __ATOMIC_RELAXED);
    unsigned timeouts = __atomic_load_n(&s_i2s_timeout_count, __ATOMIC_RELAXED);
    uint32_t tag_miss = audio_processor_test_get_tag_miss_count();
    size_t queue_free = audio_processor_queue_free_bytes();

    ESP_LOGI(TAG, "AUDIO-DIAG-SUMMARY: i2s_ops=%u i2s_bytes=%u i2s_timeouts=%u tag_miss=%u queue_free=%zu underruns=%u overruns=%u",
             ops, bytes, timeouts, (unsigned)tag_miss, queue_free,
             (unsigned)s_audio_stats.buffer_underruns,
             (unsigned)s_audio_stats.buffer_overruns);

    printf("AUDIO-DIAG-SUMMARY: i2s_ops=%u i2s_bytes=%u i2s_timeouts=%u tag_miss=%u queue_free=%zu underruns=%u overruns=%u\n",
           ops, bytes, timeouts, (unsigned)tag_miss, queue_free,
           (unsigned)s_audio_stats.buffer_underruns,
           (unsigned)s_audio_stats.buffer_overruns);

    return ESP_OK;
}

void audio_processor_arm_probe(size_t n_entries)
{
    if  (n_entries == 0) {
    	return;
    }
    if  (n_entries > I2S_PROBE_MAX_ENTRIES) {
    	n_entries = I2S_PROBE_MAX_ENTRIES;
    }
    __atomic_store_n(&s_probe_captured, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_probe_target, (unsigned)n_entries, __ATOMIC_RELAXED);
    ESP_LOGI(TAG, "I2S probe armed for %u entries", (unsigned)n_entries);
}

esp_err_t audio_processor_emit_probe(void)
{
    unsigned captured = __atomic_exchange_n(&s_probe_captured, 0U, __ATOMIC_RELAXED);
    unsigned target = __atomic_exchange_n(&s_probe_target, 0U, __ATOMIC_RELAXED);
    if  (captured > target) {
    	captured = target;
    }

    if (captured == 0) {
        ESP_LOGI(TAG, "I2S probe: no entries captured");
        printf("I2S-PROBE: none\n");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "I2S probe: captured=%u target=%u", captured, target);
    printf("I2S-PROBE: captured=%u\n", captured);
    for (unsigned i = 0; i < captured && i < I2S_PROBE_MAX_ENTRIES; ++i) {
        i2s_probe_entry_t *entry = &s_probe_buf[i];
        ESP_LOGI(TAG, "I2S-PROBE-ENTRY: idx=%u before=%lld after=%lld dur=%u req=%zu got=%zu err=%d",
                 i,
                 (long long)entry->t_before_us,
                 (long long)entry->t_after_us,
                 (unsigned)entry->dur_us,
                 entry->requested,
                 entry->got,
                 entry->err);
        printf("I2S-PROBE-ENTRY: %u %lld %lld %u %zu %zu %d\n",
               i,
               (long long)entry->t_before_us,
               (long long)entry->t_after_us,
               (unsigned)entry->dur_us,
               entry->requested,
               entry->got,
               entry->err);
    }

    return ESP_OK;
}

esp_err_t audio_processor_dump_tag_queue(size_t max_items, size_t *captured_out)
{
    if (max_items == 0 || max_items > AUDIO_CHUNK_POOL_BLOCKS) {
        max_items = AUDIO_CHUNK_POOL_BLOCKS;
    }

    audio_chunk_t snapshot[AUDIO_CHUNK_POOL_BLOCKS] = {0};
    size_t captured = 0;
    esp_err_t err = audio_descriptor_snapshot(snapshot, max_items, &captured);
    if (captured_out != NULL) {
        *captured_out = captured;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AUDIO-TAG-DUMP: snapshot failed (%s)", esp_err_to_name(err));
        return err;
    }

    if (captured == 0) {
        ESP_LOGI(TAG, "AUDIO-TAG-DUMP: queue empty");
        return ESP_OK;
    }

    for (size_t i = 0; i < captured; ++i) {
        ESP_LOGI(TAG, "AUDIO-TAG-DUMP: idx=%zu tag=%d id=%u len=%zu",
                 i,
                 (int)snapshot[i].tag,
                 (unsigned)snapshot[i].tag_id,
                 snapshot[i].len);
    }

    return ESP_OK;
}

void diag_dump_bytes(const void* data, size_t len, const char* tag)
{
    if  (data == NULL || len == 0 || tag == NULL) {
    	return;
    }
    const uint8_t* byte_data = (const uint8_t*)data;
    size_t off = 0;
    while (off < len) {
        char line[128];
        size_t row = (len - off) < 16 ? (len - off) : 16;
        size_t pos = 0;
        size_t tag_len = strlen(tag);
        if (tag_len > sizeof(line) - 3) {
            tag_len = sizeof(line) - 3;
        }
        pos = safe_memcpy(line, sizeof(line), tag, tag_len);
        if (pos < sizeof(line)) {
            line[pos++] = ':';
        }
        if (pos < sizeof(line)) {
            line[pos++] = ' ';
        }
        static const char HEX[] = "0123456789ABCDEF";
        for (size_t i = 0; i < row && (pos + 3U) < sizeof(line); ++i) {
            uint8_t byte = byte_data[off + i];
            line[pos++] = HEX[(byte >> 4) & 0xF];
            line[pos++] = HEX[byte & 0xF];
            line[pos++] = ' ';
        }
        line[(pos < sizeof(line)) ? pos : (sizeof(line) - 1U)] = '\0';
        ESP_LOGI(TAG, "%s", line);
        off += row;
    }
}
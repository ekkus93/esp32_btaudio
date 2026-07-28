#include "bt_hfp_audio.h"

#include <ctype.h>
#include <limits.h>
#include <stdatomic.h>
#include <string.h>

#include "hfp_i2s_output.h"
#include "platform_sync.h"

#ifdef ESP_PLATFORM
#include "esp_hf_ag_api.h"
#include "esp_timer.h"
#endif

_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "HFP audio callback requires lock-free 32-bit atomics");
_Static_assert(UINT_MAX == UINT32_MAX,
               "HFP audio callback expects 32-bit unsigned int atomics");

typedef struct {
    atomic_uint sequence;
    atomic_uint low;
    atomic_uint high;
} bt_hfp_audio_counter64_t;

typedef struct {
    platform_mutex_t lock;
    bool initialized;
    bool callback_registered;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
    esp_err_t last_error;

    atomic_bool accepting_incoming;
    atomic_uint generation;
    atomic_uint sync_conn_handle;
    atomic_uint preferred_frame_size;
    atomic_uint codec;
    atomic_uint active_callbacks;
    atomic_uint callback_last_us;

    bt_hfp_audio_counter64_t registration_failures;
    bt_hfp_audio_counter64_t activation_failures;
    bt_hfp_audio_counter64_t incoming_callbacks;
    bt_hfp_audio_counter64_t accepted_frames;
    bt_hfp_audio_counter64_t accepted_bytes;
    bt_hfp_audio_counter64_t dropped_frames;
    bt_hfp_audio_counter64_t dropped_bytes;
    bt_hfp_audio_counter64_t invalid_frames;
    bt_hfp_audio_counter64_t invalid_bytes;
    bt_hfp_audio_counter64_t inactive_frames;
    bt_hfp_audio_counter64_t inactive_bytes;
    bt_hfp_audio_counter64_t stale_handle_frames;
    bt_hfp_audio_counter64_t stale_handle_bytes;
    bt_hfp_audio_counter64_t bad_frames;
    bt_hfp_audio_counter64_t bad_bytes;
    bt_hfp_audio_counter64_t unsupported_codec_frames;
    bt_hfp_audio_counter64_t unsupported_codec_bytes;
    bt_hfp_audio_counter64_t ring_rejected_frames;
    bt_hfp_audio_counter64_t ring_rejected_bytes;
#ifdef UNIT_TEST
    bt_hfp_audio_platform_ops_t test_ops;
    bool test_ops_set;
#endif
} bt_hfp_audio_context_t;

static bt_hfp_audio_context_t s_audio;

/* These values describe the process lifetime, not one HFP profile lifecycle.
 * They intentionally live outside s_audio so verified profile teardown cannot
 * silently erase fields exposed as MAX_US_LIFETIME and
 * OVER_BUDGET_LIFETIME. */
static atomic_uint s_callback_max_us_lifetime = ATOMIC_VAR_INIT(0U);
static bt_hfp_audio_counter64_t s_callback_over_budget_lifetime = {
    .sequence = ATOMIC_VAR_INIT(0U),
    .low = ATOMIC_VAR_INIT(0U),
    .high = ATOMIC_VAR_INIT(0U),
};

static void counter64_init(bt_hfp_audio_counter64_t *counter)
{
    atomic_init(&counter->sequence, 0U);
    atomic_init(&counter->low, 0U);
    atomic_init(&counter->high, 0U);
}

static void counter64_add(bt_hfp_audio_counter64_t *counter, uint64_t amount)
{
    (void)atomic_fetch_add_explicit(&counter->sequence, 1U,
                                    memory_order_acq_rel);
    uint64_t value =
        ((uint64_t)atomic_load_explicit(&counter->high,
                                        memory_order_relaxed) << 32U) |
        atomic_load_explicit(&counter->low, memory_order_relaxed);
    value += amount;
    atomic_store_explicit(&counter->low, (uint32_t)value,
                          memory_order_relaxed);
    atomic_store_explicit(&counter->high, (uint32_t)(value >> 32U),
                          memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&counter->sequence, 1U,
                                    memory_order_release);
}

static bool counter64_read(const bt_hfp_audio_counter64_t *counter,
                           uint64_t *value_out)
{
    enum { SNAPSHOT_RETRIES = 32 };
    for (unsigned attempt = 0; attempt < SNAPSHOT_RETRIES; ++attempt) {
        uint32_t before = atomic_load_explicit(&counter->sequence,
                                               memory_order_acquire);
        if ((before & 1U) != 0U) continue;
        uint32_t low = atomic_load_explicit(&counter->low,
                                            memory_order_relaxed);
        uint32_t high = atomic_load_explicit(&counter->high,
                                             memory_order_relaxed);
        uint32_t after = atomic_load_explicit(&counter->sequence,
                                              memory_order_acquire);
        if (before == after) {
            *value_out = ((uint64_t)high << 32U) | low;
            return true;
        }
    }
    return false;
}

static void counters_init(void)
{
#define INIT_COUNTER(name) counter64_init(&s_audio.name)
    INIT_COUNTER(registration_failures);
    INIT_COUNTER(activation_failures);
    INIT_COUNTER(incoming_callbacks);
    INIT_COUNTER(accepted_frames);
    INIT_COUNTER(accepted_bytes);
    INIT_COUNTER(dropped_frames);
    INIT_COUNTER(dropped_bytes);
    INIT_COUNTER(invalid_frames);
    INIT_COUNTER(invalid_bytes);
    INIT_COUNTER(inactive_frames);
    INIT_COUNTER(inactive_bytes);
    INIT_COUNTER(stale_handle_frames);
    INIT_COUNTER(stale_handle_bytes);
    INIT_COUNTER(bad_frames);
    INIT_COUNTER(bad_bytes);
    INIT_COUNTER(unsupported_codec_frames);
    INIT_COUNTER(unsupported_codec_bytes);
    INIT_COUNTER(ring_rejected_frames);
    INIT_COUNTER(ring_rejected_bytes);
#undef INIT_COUNTER
}

static esp_err_t context_ensure(void)
{
    if (s_audio.initialized) return ESP_OK;
    platform_mutex_t lock = platform_mutex_create();
    if (lock == NULL) return ESP_ERR_NO_MEM;

#ifdef UNIT_TEST
    bt_hfp_audio_platform_ops_t saved_ops = s_audio.test_ops;
    bool saved_ops_set = s_audio.test_ops_set;
#endif
    memset(&s_audio, 0, sizeof(s_audio));
#ifdef UNIT_TEST
    s_audio.test_ops = saved_ops;
    s_audio.test_ops_set = saved_ops_set;
#endif
    s_audio.lock = lock;
    s_audio.initialized = true;
    s_audio.last_error = ESP_OK;
    atomic_init(&s_audio.accepting_incoming, false);
    atomic_init(&s_audio.generation, 0U);
    atomic_init(&s_audio.sync_conn_handle,
                (unsigned)BT_HFP_AUDIO_INVALID_SYNC_HANDLE);
    atomic_init(&s_audio.preferred_frame_size, 0U);
    atomic_init(&s_audio.codec, (unsigned)BT_HFP_CODEC_NONE);
    atomic_init(&s_audio.active_callbacks, 0U);
    atomic_init(&s_audio.callback_last_us, 0U);
    counters_init();
    return ESP_OK;
}

static esp_err_t audio_lock(void)
{
    if (!s_audio.initialized || s_audio.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_mutex_lock(s_audio.lock, PLATFORM_WAIT_FOREVER);
}

static esp_err_t audio_unlock(esp_err_t prior)
{
    esp_err_t err = platform_mutex_unlock(s_audio.lock);
    return prior != ESP_OK ? prior : err;
}

static int64_t platform_now_us(void)
{
#ifdef UNIT_TEST
    if (s_audio.test_ops_set && s_audio.test_ops.now_us != NULL) {
        return s_audio.test_ops.now_us();
    }
#endif
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    return 0;
#endif
}

#ifdef ESP_PLATFORM
static void production_incoming_callback(esp_hf_sync_conn_hdl_t sync_conn_hdl,
                                         esp_hf_audio_buff_t *audio_buf,
                                         bool bad_frame);
#endif

static esp_err_t platform_register_callback(void)
{
#ifdef UNIT_TEST
    if (s_audio.test_ops_set && s_audio.test_ops.register_callback != NULL) {
        return s_audio.test_ops.register_callback();
    }
#endif
#ifdef ESP_PLATFORM
    return esp_hf_ag_register_audio_data_callback(
        production_incoming_callback);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void copy_peer(char out[BT_DUPLEX_MAC_STR_LEN], const char *peer)
{
    size_t length = strlen(peer);
    if (length >= BT_DUPLEX_MAC_STR_LEN) length = BT_DUPLEX_MAC_STR_LEN - 1U;
    memcpy(out, peer, length);
    out[length] = '\0';
}

static bool same_peer(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) return false;
    for (size_t i = 0; i < BT_DUPLEX_MAC_STR_LEN - 1U; ++i) {
        if (tolower((unsigned char)lhs[i]) !=
            tolower((unsigned char)rhs[i])) {
            return false;
        }
    }
    return lhs[BT_DUPLEX_MAC_STR_LEN - 1U] == '\0' &&
           rhs[BT_DUPLEX_MAC_STR_LEN - 1U] == '\0';
}

static bool snapshot_is_cvsd_ready(const bt_duplex_snapshot_t *snapshot)
{
    return snapshot->peer_valid && snapshot->session_generation != 0U &&
           snapshot->hfp_profile_state == BT_HFP_PROFILE_SLC_CONNECTED &&
           snapshot->hfp_audio_state == BT_HFP_AUDIO_CONNECTED_CVSD &&
           snapshot->codec == BT_HFP_CODEC_CVSD &&
           snapshot->i2s_state == BT_HFP_I2S_RUNNING;
}

esp_err_t bt_hfp_audio_register_callback(void)
{
    esp_err_t err = context_ensure();
    if (err != ESP_OK) return err;
    err = audio_lock();
    if (err != ESP_OK) return err;
    if (s_audio.callback_registered) return audio_unlock(ESP_OK);
    err = audio_unlock(ESP_OK);
    if (err != ESP_OK) return err;

    err = platform_register_callback();
    if (audio_lock() != ESP_OK) return ESP_FAIL;
    if (err == ESP_OK) {
        s_audio.callback_registered = true;
        s_audio.last_error = ESP_OK;
    } else {
        counter64_add(&s_audio.registration_failures, 1U);
        s_audio.last_error = err;
    }
    return audio_unlock(err);
}

void bt_hfp_audio_profile_stopping(void)
{
    atomic_store_explicit(&s_audio.accepting_incoming, false,
                          memory_order_release);
    atomic_store_explicit(&s_audio.generation, 0U, memory_order_release);
    atomic_store_explicit(&s_audio.sync_conn_handle,
                          (unsigned)BT_HFP_AUDIO_INVALID_SYNC_HANDLE,
                          memory_order_release);
    atomic_store_explicit(&s_audio.preferred_frame_size, 0U,
                          memory_order_release);
    atomic_store_explicit(&s_audio.codec, (unsigned)BT_HFP_CODEC_NONE,
                          memory_order_release);
}

esp_err_t bt_hfp_audio_apply_duplex_state(
    const bt_duplex_snapshot_t *snapshot,
    const char *event_peer_mac,
    uint16_t sync_conn_handle,
    uint16_t preferred_frame_size)
{
    if (snapshot == NULL || event_peer_mac == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = context_ensure();
    if (err != ESP_OK) return err;

    if (!snapshot->peer_valid ||
        strlen(event_peer_mac) != BT_DUPLEX_MAC_STR_LEN - 1U ||
        !same_peer(snapshot->peer_mac, event_peer_mac)) {
        counter64_add(&s_audio.activation_failures, 1U);
        if (audio_lock() == ESP_OK) {
            s_audio.last_error = ESP_ERR_INVALID_STATE;
            (void)audio_unlock(ESP_OK);
        }
        return ESP_ERR_INVALID_STATE;
    }

    /* Only an event for the bound peer may alter the fast callback gate. A
     * wrong-peer event is rejected above without disrupting valid audio. */
    bt_hfp_audio_profile_stopping();

    if (snapshot->hfp_audio_state == BT_HFP_AUDIO_CONNECTED_MSBC ||
        snapshot->codec == BT_HFP_CODEC_MSBC) {
        counter64_add(&s_audio.activation_failures, 1U);
        if (audio_lock() == ESP_OK) {
            s_audio.last_error = ESP_ERR_NOT_SUPPORTED;
            (void)audio_unlock(ESP_OK);
        }
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (snapshot->hfp_audio_state != BT_HFP_AUDIO_CONNECTED_CVSD) {
        return ESP_OK;
    }
    if (sync_conn_handle == BT_HFP_AUDIO_INVALID_SYNC_HANDLE) {
        counter64_add(&s_audio.activation_failures, 1U);
        if (audio_lock() == ESP_OK) {
            s_audio.last_error = ESP_ERR_INVALID_ARG;
            (void)audio_unlock(ESP_OK);
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (!snapshot_is_cvsd_ready(snapshot) ||
        !s_audio.callback_registered) {
        counter64_add(&s_audio.activation_failures, 1U);
        if (audio_lock() == ESP_OK) {
            s_audio.last_error = ESP_ERR_INVALID_STATE;
            (void)audio_unlock(ESP_OK);
        }
        return ESP_ERR_INVALID_STATE;
    }

    err = audio_lock();
    if (err != ESP_OK) return err;
    copy_peer(s_audio.peer_mac, snapshot->peer_mac);
    s_audio.last_error = ESP_OK;
    atomic_store_explicit(&s_audio.generation, snapshot->session_generation,
                          memory_order_release);
    atomic_store_explicit(&s_audio.sync_conn_handle, sync_conn_handle,
                          memory_order_release);
    atomic_store_explicit(&s_audio.preferred_frame_size, preferred_frame_size,
                          memory_order_release);
    atomic_store_explicit(&s_audio.codec, (unsigned)BT_HFP_CODEC_CVSD,
                          memory_order_release);
    atomic_store_explicit(&s_audio.accepting_incoming, true,
                          memory_order_release);
    return audio_unlock(ESP_OK);
}

static void update_max_duration(uint32_t duration_us)
{
    atomic_store_explicit(&s_audio.callback_last_us, duration_us,
                          memory_order_relaxed);
    uint32_t current = atomic_load_explicit(&s_callback_max_us_lifetime,
                                            memory_order_relaxed);
    while (duration_us > current &&
           !atomic_compare_exchange_weak_explicit(
               &s_callback_max_us_lifetime, &current, duration_us,
               memory_order_relaxed, memory_order_relaxed)) {
    }
    if (duration_us > BT_HFP_AUDIO_CALLBACK_BUDGET_US) {
        counter64_add(&s_callback_over_budget_lifetime, 1U);
    }
}

static void record_drop(bt_hfp_audio_counter64_t *frame_counter,
                        bt_hfp_audio_counter64_t *byte_counter,
                        size_t bytes)
{
    counter64_add(frame_counter, 1U);
    counter64_add(byte_counter, bytes);
    counter64_add(&s_audio.dropped_frames, 1U);
    counter64_add(&s_audio.dropped_bytes, bytes);
}

static void process_incoming(uint16_t sync_conn_handle,
                             const uint8_t *data,
                             size_t data_len,
                             size_t buffer_capacity,
                             bool bad_frame)
{
    counter64_add(&s_audio.incoming_callbacks, 1U);

    if (data == NULL || data_len == 0U || data_len > buffer_capacity ||
        (data_len % sizeof(int16_t)) != 0U ||
        data_len > HFP_I2S_CVSD_MAX_INPUT_SAMPLES * sizeof(int16_t)) {
        record_drop(&s_audio.invalid_frames, &s_audio.invalid_bytes, data_len);
        goto out;
    }

    if (!atomic_load_explicit(&s_audio.accepting_incoming,
                              memory_order_acquire)) {
        record_drop(&s_audio.inactive_frames, &s_audio.inactive_bytes,
                    data_len);
        goto out;
    }

    uint32_t generation = atomic_load_explicit(&s_audio.generation,
                                               memory_order_acquire);
    uint32_t expected_handle = atomic_load_explicit(&s_audio.sync_conn_handle,
                                                    memory_order_acquire);
    bt_hfp_codec_t codec = (bt_hfp_codec_t)atomic_load_explicit(
        &s_audio.codec, memory_order_acquire);
    if (!atomic_load_explicit(&s_audio.accepting_incoming,
                              memory_order_acquire) ||
        generation == 0U) {
        record_drop(&s_audio.inactive_frames, &s_audio.inactive_bytes,
                    data_len);
        goto out;
    }
    if (sync_conn_handle != expected_handle) {
        record_drop(&s_audio.stale_handle_frames,
                    &s_audio.stale_handle_bytes, data_len);
        goto out;
    }
    if (codec != BT_HFP_CODEC_CVSD) {
        record_drop(&s_audio.unsupported_codec_frames,
                    &s_audio.unsupported_codec_bytes, data_len);
        goto out;
    }
    if (bad_frame) {
        record_drop(&s_audio.bad_frames, &s_audio.bad_bytes, data_len);
        goto out;
    }

    int16_t aligned[HFP_I2S_CVSD_MAX_INPUT_SAMPLES];
    memcpy(aligned, data, data_len);
    size_t sample_count = data_len / sizeof(aligned[0]);
    if (!hfp_i2s_output_push_cvsd(aligned, sample_count, generation)) {
        record_drop(&s_audio.ring_rejected_frames,
                    &s_audio.ring_rejected_bytes, data_len);
        goto out;
    }
    counter64_add(&s_audio.accepted_frames, 1U);
    counter64_add(&s_audio.accepted_bytes, data_len);

out:
    return;
}

#ifdef UNIT_TEST
static void handle_incoming_timed(uint16_t sync_conn_handle,
                                  const uint8_t *data,
                                  size_t data_len,
                                  size_t buffer_capacity,
                                  bool bad_frame)
{
    atomic_fetch_add_explicit(&s_audio.active_callbacks, 1U,
                              memory_order_acq_rel);
    int64_t started = platform_now_us();
    process_incoming(sync_conn_handle, data, data_len, buffer_capacity,
                     bad_frame);
    int64_t finished = platform_now_us();
    uint32_t duration = 0U;
    if (finished > started) {
        uint64_t delta = (uint64_t)(finished - started);
        duration = delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
    }
    update_max_duration(duration);
    atomic_fetch_sub_explicit(&s_audio.active_callbacks, 1U,
                              memory_order_acq_rel);
}
#endif

#ifdef ESP_PLATFORM
static void production_incoming_callback(esp_hf_sync_conn_hdl_t sync_conn_hdl,
                                         esp_hf_audio_buff_t *audio_buf,
                                         bool bad_frame)
{
    atomic_fetch_add_explicit(&s_audio.active_callbacks, 1U,
                              memory_order_acq_rel);
    int64_t started = platform_now_us();
    if (audio_buf == NULL) {
        process_incoming(sync_conn_hdl, NULL, 0U, 0U, bad_frame);
    } else {
        process_incoming(sync_conn_hdl, audio_buf->data, audio_buf->data_len,
                         audio_buf->buff_size, bad_frame);
        /* Required by ESP-IDF v5.5.1's modern HCI callback contract. This is
         * release of a stack-owned buffer, not application heap allocation. */
        esp_hf_ag_audio_buff_free(audio_buf);
    }
    int64_t finished = platform_now_us();
    uint32_t duration = 0U;
    if (finished > started) {
        uint64_t delta = (uint64_t)(finished - started);
        duration = delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
    }
    update_max_duration(duration);
    atomic_fetch_sub_explicit(&s_audio.active_callbacks, 1U,
                              memory_order_acq_rel);
}
#endif

esp_err_t bt_hfp_audio_get_snapshot(bt_hfp_audio_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = audio_lock();
    if (err != ESP_OK) return err;
    memset(out, 0, sizeof(*out));
    out->initialized = s_audio.initialized;
    out->callback_registered = s_audio.callback_registered;
    out->accepting_incoming = atomic_load_explicit(
        &s_audio.accepting_incoming, memory_order_acquire);
    out->generation = atomic_load_explicit(&s_audio.generation,
                                           memory_order_acquire);
    out->sync_conn_handle = (uint16_t)atomic_load_explicit(
        &s_audio.sync_conn_handle, memory_order_acquire);
    out->preferred_frame_size = (uint16_t)atomic_load_explicit(
        &s_audio.preferred_frame_size, memory_order_acquire);
    out->codec = (bt_hfp_codec_t)atomic_load_explicit(
        &s_audio.codec, memory_order_acquire);
    memcpy(out->peer_mac, s_audio.peer_mac, sizeof(out->peer_mac));
    out->callback_last_us = atomic_load_explicit(&s_audio.callback_last_us,
                                                 memory_order_relaxed);
    out->callback_max_us = atomic_load_explicit(&s_callback_max_us_lifetime,
                                                memory_order_relaxed);
    out->active_callbacks = atomic_load_explicit(&s_audio.active_callbacks,
                                                 memory_order_acquire);
    out->last_error = s_audio.last_error;
#define READ_COUNTER(name) \
    if (!counter64_read(&s_audio.name, &out->name)) err = ESP_ERR_TIMEOUT
    READ_COUNTER(registration_failures);
    READ_COUNTER(activation_failures);
    READ_COUNTER(incoming_callbacks);
    READ_COUNTER(accepted_frames);
    READ_COUNTER(accepted_bytes);
    READ_COUNTER(dropped_frames);
    READ_COUNTER(dropped_bytes);
    READ_COUNTER(invalid_frames);
    READ_COUNTER(invalid_bytes);
    READ_COUNTER(inactive_frames);
    READ_COUNTER(inactive_bytes);
    READ_COUNTER(stale_handle_frames);
    READ_COUNTER(stale_handle_bytes);
    READ_COUNTER(bad_frames);
    READ_COUNTER(bad_bytes);
    READ_COUNTER(unsupported_codec_frames);
    READ_COUNTER(unsupported_codec_bytes);
    READ_COUNTER(ring_rejected_frames);
    READ_COUNTER(ring_rejected_bytes);
#undef READ_COUNTER
    if (!counter64_read(&s_callback_over_budget_lifetime,
                        &out->callback_over_budget)) {
        err = ESP_ERR_TIMEOUT;
    }
    return audio_unlock(err);
}

esp_err_t bt_hfp_audio_cleanup_after_stack_shutdown(void)
{
    if (!s_audio.initialized) return ESP_OK;
    bt_hfp_audio_profile_stopping();
    if (atomic_load_explicit(&s_audio.active_callbacks,
                             memory_order_acquire) != 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    platform_mutex_t lock = s_audio.lock;
#ifdef UNIT_TEST
    bt_hfp_audio_platform_ops_t saved_ops = s_audio.test_ops;
    bool saved_ops_set = s_audio.test_ops_set;
#endif
    memset(&s_audio, 0, sizeof(s_audio));
#ifdef UNIT_TEST
    s_audio.test_ops = saved_ops;
    s_audio.test_ops_set = saved_ops_set;
#endif
    platform_mutex_delete(lock);
    return ESP_OK;
}

#ifdef UNIT_TEST
esp_err_t bt_hfp_audio_test_set_platform_ops(
    const bt_hfp_audio_platform_ops_t *ops)
{
    if (ops == NULL || ops->register_callback == NULL || ops->now_us == NULL ||
        s_audio.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    s_audio.test_ops = *ops;
    s_audio.test_ops_set = true;
    return ESP_OK;
}

void bt_hfp_audio_test_reset(void)
{
    (void)bt_hfp_audio_cleanup_after_stack_shutdown();
    memset(&s_audio, 0, sizeof(s_audio));
    atomic_store_explicit(&s_callback_max_us_lifetime, 0U,
                          memory_order_relaxed);
    counter64_init(&s_callback_over_budget_lifetime);
}

void bt_hfp_audio_test_handle_incoming(uint16_t sync_conn_handle,
                                       const uint8_t *data,
                                       size_t data_len,
                                       size_t buffer_capacity,
                                       bool bad_frame)
{
    handle_incoming_timed(sync_conn_handle, data, data_len, buffer_capacity,
                          bad_frame);
}

void bt_hfp_audio_test_record_callback_duration(uint32_t duration_us)
{
    update_max_duration(duration_us);
}
#endif

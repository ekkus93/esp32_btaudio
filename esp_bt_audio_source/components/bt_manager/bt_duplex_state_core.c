#include "bt_duplex_state_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "bt_hfp_event_contract.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define TAG "BT_DUPLEX_STATE"
#endif

bt_duplex_context_t g_bt_duplex_ctx;

/* These diagnostics must remain writable when the authoritative state mutex is
 * unavailable. They are process-lifetime, lock-free 32-bit atomics on ESP32;
 * the public API widens the saturating count to uint64_t. */
static atomic_uint s_health_report_failures_lifetime = ATOMIC_VAR_INIT(0U);
static atomic_int s_last_health_report_error_lifetime = ATOMIC_VAR_INIT(ESP_OK);

static uint64_t next_health_event_count_locked(void)
{
    if (g_bt_duplex_ctx.health_event_count != UINT64_MAX) {
        g_bt_duplex_ctx.health_event_count++;
    }
    return g_bt_duplex_ctx.health_event_count;
}

static bool record_health_report_failure(esp_err_t error, bool update_last)
{
    if (error == ESP_OK) return false;

    unsigned current = atomic_load_explicit(
        &s_health_report_failures_lifetime, memory_order_relaxed);
    bool first_failure = false;
    while (current != UINT_MAX) {
        const unsigned next = current + 1U;
        if (atomic_compare_exchange_weak_explicit(
                &s_health_report_failures_lifetime, &current, next,
                memory_order_relaxed, memory_order_relaxed)) {
            first_failure = current == 0U;
            break;
        }
    }
    if (update_last) {
        atomic_store_explicit(&s_last_health_report_error_lifetime, error,
                              memory_order_relaxed);
    }
    return first_failure;
}

static void log_health_report_failure_once(bool first_failure,
                                           esp_err_t error)
{
#ifdef ESP_PLATFORM
    if (first_failure) {
        ESP_LOGE(TAG, "health report rejected: %s", esp_err_to_name(error));
    }
#else
    (void)first_failure;
    (void)error;
#endif
}

static esp_err_t reject_health_report_before_lock(esp_err_t error)
{
    const bool first_failure = record_health_report_failure(error, true);
    log_health_report_failure_once(first_failure, error);
    return error;
}

static void note_delivery_result(esp_err_t result,
                                 uint32_t generation,
                                 const char *peer_mac)
{
    if (result != ESP_OK) {
        bt_duplex_record_event_delivery_failure(
            generation, peer_mac, result);
    }
}

void bt_duplex_snapshot_defaults(bt_duplex_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->requested_mode = BT_DUPLEX_MODE_DISABLED;
    snapshot->effective_mode = BT_DUPLEX_MODE_DISABLED;
    snapshot->a2dp_profile_state = BT_A2DP_PROFILE_DISCONNECTED;
    snapshot->a2dp_audio_state = BT_A2DP_AUDIO_STOPPED;
    snapshot->hfp_profile_state = BT_HFP_PROFILE_UNINITIALIZED;
    snapshot->hfp_audio_state = BT_HFP_AUDIO_DISCONNECTED;
    snapshot->codec = BT_HFP_CODEC_NONE;
    snapshot->i2s_state = BT_HFP_I2S_STOPPED;
    snapshot->health = BT_AUDIO_HEALTH_OK;
    snapshot->last_error = ESP_OK;
}

bool bt_duplex_valid_mac(const char *mac)
{
    if (mac == NULL || strlen(mac) != BT_DUPLEX_MAC_STR_LEN - 1U) {
        return false;
    }
    for (size_t i = 0; i < BT_DUPLEX_MAC_STR_LEN - 1U; ++i) {
        if ((i + 1U) % 3U == 0U) {
            if (mac[i] != ':') return false;
        } else if (!isxdigit((unsigned char)mac[i])) {
            return false;
        }
    }
    return true;
}

bool bt_duplex_same_mac(const char *lhs, const char *rhs)
{
    for (size_t i = 0; i < BT_DUPLEX_MAC_STR_LEN - 1U; ++i) {
        if (tolower((unsigned char)lhs[i]) != tolower((unsigned char)rhs[i])) {
            return false;
        }
    }
    return true;
}

esp_err_t bt_duplex_lock(void)
{
    if (!g_bt_duplex_ctx.initialized || g_bt_duplex_ctx.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_mutex_lock(g_bt_duplex_ctx.lock, PLATFORM_WAIT_FOREVER);
}

esp_err_t bt_duplex_unlock_result(esp_err_t current_result)
{
    esp_err_t unlock_result = platform_mutex_unlock(g_bt_duplex_ctx.lock);
    return current_result != ESP_OK ? current_result : unlock_result;
}

bool bt_duplex_transient_resources_stopped_locked(void)
{
    return g_bt_duplex_ctx.snapshot.hfp_audio_state == BT_HFP_AUDIO_DISCONNECTED &&
           g_bt_duplex_ctx.snapshot.i2s_state == BT_HFP_I2S_STOPPED;
}

uint32_t bt_duplex_next_generation(uint32_t current)
{
    current++;
    return current == 0U ? 1U : current;
}

esp_err_t bt_duplex_validate_event_locked(uint32_t generation,
                                          const char *peer_mac)
{
    if (!bt_duplex_valid_mac(peer_mac)) {
        g_bt_duplex_ctx.snapshot.counters.invalid_arguments++;
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_bt_duplex_ctx.snapshot.peer_valid ||
        !bt_duplex_same_mac(peer_mac, g_bt_duplex_ctx.snapshot.peer_mac)) {
        g_bt_duplex_ctx.snapshot.counters.wrong_peer_events++;
        return ESP_ERR_INVALID_STATE;
    }
    if (generation == 0U ||
        generation != g_bt_duplex_ctx.snapshot.session_generation) {
        g_bt_duplex_ctx.snapshot.counters.stale_generation_events++;
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t bt_duplex_illegal_locked(void)
{
    g_bt_duplex_ctx.snapshot.counters.illegal_transitions++;
    return ESP_ERR_INVALID_STATE;
}

void bt_duplex_record_event_delivery_failure(uint32_t generation,
                                             const char *peer_mac,
                                             esp_err_t error)
{
    if (error == ESP_OK || bt_duplex_lock() != ESP_OK) return;

    const bool same_identity = generation == 0U
        ? (!g_bt_duplex_ctx.snapshot.peer_valid &&
           g_bt_duplex_ctx.snapshot.session_generation == 0U)
        : (g_bt_duplex_ctx.snapshot.peer_valid &&
           peer_mac != NULL &&
           generation == g_bt_duplex_ctx.snapshot.session_generation &&
           bt_duplex_valid_mac(peer_mac) &&
           bt_duplex_same_mac(peer_mac, g_bt_duplex_ctx.snapshot.peer_mac));

    if (same_identity) {
        if (g_bt_duplex_ctx.snapshot.health == BT_AUDIO_HEALTH_OK) {
            g_bt_duplex_ctx.snapshot.health = BT_AUDIO_HEALTH_DEGRADED;
            (void)next_health_event_count_locked();
        }
        static const char text[] = "HFP event delivery failed";
        g_bt_duplex_ctx.snapshot.last_error = error;
        memcpy(g_bt_duplex_ctx.snapshot.last_error_text, text, sizeof(text));
    }
    (void)bt_duplex_unlock_result(ESP_OK);
}

esp_err_t bt_duplex_state_init(void)
{
    if (g_bt_duplex_ctx.initialized) return ESP_ERR_INVALID_STATE;
    platform_mutex_t lock = platform_mutex_create();
    if (lock == NULL) return ESP_ERR_NO_MEM;
    memset(&g_bt_duplex_ctx, 0, sizeof(g_bt_duplex_ctx));
    g_bt_duplex_ctx.lock = lock;
    bt_duplex_snapshot_defaults(&g_bt_duplex_ctx.snapshot);
#ifdef UNIT_TEST
    g_bt_duplex_ctx.test_health_report_result = ESP_OK;
#endif
    g_bt_duplex_ctx.initialized = true;
    return ESP_OK;
}

void bt_duplex_state_deinit(void)
{
    platform_mutex_t lock = g_bt_duplex_ctx.lock;
    memset(&g_bt_duplex_ctx, 0, sizeof(g_bt_duplex_ctx));
    platform_mutex_delete(lock);
}

esp_err_t bt_duplex_session_begin(const char *peer_mac,
                                  bt_duplex_mode_t requested_mode,
                                  uint32_t *generation_out)
{
    if (!bt_duplex_valid_mac(peer_mac) ||
        !BT_DUPLEX_ENUM_VALUE_VALID(requested_mode, BT_DUPLEX_MODE_COUNT) ||
        generation_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    if (g_bt_duplex_ctx.snapshot.peer_valid &&
        !bt_duplex_same_mac(peer_mac, g_bt_duplex_ctx.snapshot.peer_mac)) {
        g_bt_duplex_ctx.snapshot.counters.wrong_peer_events++;
        return bt_duplex_unlock_result(ESP_ERR_INVALID_STATE);
    }
    if (g_bt_duplex_ctx.snapshot.peer_valid &&
        !bt_duplex_transient_resources_stopped_locked()) {
        return bt_duplex_unlock_result(bt_duplex_illegal_locked());
    }

    bt_duplex_mode_t old_mode = g_bt_duplex_ctx.snapshot.requested_mode;
    bt_audio_health_t old_health = g_bt_duplex_ctx.snapshot.health;
    bt_duplex_counters_t counters = g_bt_duplex_ctx.snapshot.counters;
    uint32_t generation =
        bt_duplex_next_generation(g_bt_duplex_ctx.snapshot.session_generation);
    bt_duplex_snapshot_defaults(&g_bt_duplex_ctx.snapshot);
    g_bt_duplex_ctx.snapshot.counters = counters;
    g_bt_duplex_ctx.snapshot.peer_valid = true;
    memcpy(g_bt_duplex_ctx.snapshot.peer_mac, peer_mac, BT_DUPLEX_MAC_STR_LEN);
    g_bt_duplex_ctx.snapshot.session_generation = generation;
    g_bt_duplex_ctx.snapshot.hfp_profile_state = BT_HFP_PROFILE_DISCONNECTED;
    g_bt_duplex_ctx.snapshot.requested_mode = requested_mode;
    g_bt_duplex_ctx.snapshot.effective_mode =
        requested_mode == BT_DUPLEX_MODE_AUTO
            ? BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC
            : requested_mode;
    uint64_t health_count = 0U;
    if (old_health != BT_AUDIO_HEALTH_OK) {
        health_count = next_health_event_count_locked();
    }
    *generation_out = generation;
    err = bt_duplex_unlock_result(ESP_OK);
    if (err != ESP_OK) return err;

    note_delivery_result(bt_hfp_event_emit_profile(
        BT_HFP_PROFILE_DISCONNECTED, peer_mac, generation),
        generation, peer_mac);
    if (old_mode != requested_mode) {
        note_delivery_result(bt_hfp_event_emit_mode(
            old_mode, requested_mode, BT_HFP_MODE_EVENT_REASON_SESSION_BEGIN,
            generation), generation, peer_mac);
    }
    if (health_count != 0U) {
        note_delivery_result(bt_hfp_event_emit_health(
            BT_AUDIO_HEALTH_OK, ESP_OK, health_count, generation),
            generation, peer_mac);
    }
    return ESP_OK;
}

esp_err_t bt_duplex_get_snapshot(bt_duplex_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    memcpy(out, &g_bt_duplex_ctx.snapshot, sizeof(*out));
    return bt_duplex_unlock_result(ESP_OK);
}

esp_err_t bt_duplex_get_health_report_diagnostics(
    uint64_t *failure_count_out,
    esp_err_t *last_error_out)
{
    if (failure_count_out == NULL || last_error_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *failure_count_out = (uint64_t)atomic_load_explicit(
        &s_health_report_failures_lifetime, memory_order_relaxed);
    *last_error_out = (esp_err_t)atomic_load_explicit(
        &s_last_health_report_error_lifetime, memory_order_relaxed);
    return ESP_OK;
}

#define DEFINE_ENUM_SETTER(name, field, type, count)                         \
    esp_err_t name(uint32_t generation, const char *peer_mac, type value)    \
    {                                                                        \
        if (!BT_DUPLEX_ENUM_VALUE_VALID(value, count)) {                     \
            return ESP_ERR_INVALID_ARG;                                      \
        }                                                                    \
        esp_err_t err = bt_duplex_lock();                                    \
        if (err != ESP_OK) return err;                                       \
        err = bt_duplex_validate_event_locked(generation, peer_mac);         \
        if (err == ESP_OK) g_bt_duplex_ctx.snapshot.field = value;           \
        return bt_duplex_unlock_result(err);                                 \
    }

DEFINE_ENUM_SETTER(bt_duplex_set_requested_mode, requested_mode,
                   bt_duplex_mode_t, BT_DUPLEX_MODE_COUNT)
DEFINE_ENUM_SETTER(bt_duplex_set_effective_mode, effective_mode,
                   bt_duplex_mode_t, BT_DUPLEX_MODE_COUNT)

esp_err_t bt_duplex_set_health(uint32_t generation,
                               const char *peer_mac,
                               bt_audio_health_t health,
                               esp_err_t error,
                               const char *error_text)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(health, BT_AUDIO_HEALTH_COUNT)) {
        return reject_health_report_before_lock(ESP_ERR_INVALID_ARG);
    }
    if (error_text != NULL &&
        strlen(error_text) >= BT_DUPLEX_ERROR_TEXT_LEN) {
        return reject_health_report_before_lock(ESP_ERR_INVALID_SIZE);
    }

    esp_err_t operation_err = bt_duplex_lock();
    if (operation_err != ESP_OK) {
        const bool first = record_health_report_failure(operation_err, true);
        log_health_report_failure_once(first, operation_err);
        return operation_err;
    }
#ifdef UNIT_TEST
    if (g_bt_duplex_ctx.test_health_report_result != ESP_OK) {
        operation_err = g_bt_duplex_ctx.test_health_report_result;
    } else
#endif
    {
        operation_err = bt_duplex_validate_event_locked(generation, peer_mac);
    }
    if (operation_err == ESP_OK &&
        g_bt_duplex_ctx.snapshot.health >= BT_AUDIO_HEALTH_FAULTED &&
        health < g_bt_duplex_ctx.snapshot.health) {
        operation_err = bt_duplex_illegal_locked();
    }

    bool changed = false;
    uint64_t count = 0U;
    if (operation_err == ESP_OK) {
        changed = g_bt_duplex_ctx.snapshot.health != health;
        g_bt_duplex_ctx.snapshot.health = health;
        g_bt_duplex_ctx.snapshot.last_error = error;
        if (error_text == NULL) error_text = "";
        size_t n = strlen(error_text);
        memcpy(g_bt_duplex_ctx.snapshot.last_error_text, error_text, n);
        g_bt_duplex_ctx.snapshot.last_error_text[n] = '\0';
        if (changed) count = next_health_event_count_locked();
    }

    bool first_report_failure = false;
    if (operation_err != ESP_OK) {
        first_report_failure =
            record_health_report_failure(operation_err, true);
    }

    const esp_err_t unlock_err =
        platform_mutex_unlock(g_bt_duplex_ctx.lock);
    if (unlock_err != ESP_OK) {
        const bool first_unlock_failure = record_health_report_failure(
            unlock_err, operation_err == ESP_OK);
        if (!first_report_failure) first_report_failure = first_unlock_failure;
    }

    if (operation_err != ESP_OK) {
        log_health_report_failure_once(first_report_failure, operation_err);
        return operation_err;
    }
    if (unlock_err != ESP_OK) {
        log_health_report_failure_once(first_report_failure, unlock_err);
        return unlock_err;
    }

    if (changed) {
        note_delivery_result(bt_hfp_event_emit_health(
            health, error, count, generation), generation, peer_mac);
    }
    return ESP_OK;
}

esp_err_t bt_duplex_recover(uint32_t generation,
                            const char *peer_mac,
                            uint32_t *new_generation_out)
{
    if (new_generation_out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    bool has_fault =
        g_bt_duplex_ctx.snapshot.health >= BT_AUDIO_HEALTH_FAULTED ||
        g_bt_duplex_ctx.snapshot.hfp_profile_state == BT_HFP_PROFILE_FAULTED ||
        g_bt_duplex_ctx.snapshot.hfp_audio_state == BT_HFP_AUDIO_FAULTED ||
        g_bt_duplex_ctx.snapshot.i2s_state == BT_HFP_I2S_FAULTED ||
        g_bt_duplex_ctx.snapshot.i2s_state == BT_HFP_I2S_QUARANTINED;
    if (err == ESP_OK && !has_fault) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK && !bt_duplex_transient_resources_stopped_locked()) {
        err = bt_duplex_illegal_locked();
    }

    bool profile_changed = false;
    uint64_t health_count = 0U;
    uint32_t new_generation = 0U;
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.session_generation =
            bt_duplex_next_generation(
                g_bt_duplex_ctx.snapshot.session_generation);
        new_generation = g_bt_duplex_ctx.snapshot.session_generation;
        if (g_bt_duplex_ctx.snapshot.health != BT_AUDIO_HEALTH_OK) {
            health_count = next_health_event_count_locked();
        }
        g_bt_duplex_ctx.snapshot.health = BT_AUDIO_HEALTH_OK;
        g_bt_duplex_ctx.snapshot.last_error = ESP_OK;
        g_bt_duplex_ctx.snapshot.last_error_text[0] = '\0';
        if (g_bt_duplex_ctx.snapshot.hfp_profile_state ==
            BT_HFP_PROFILE_FAULTED) {
            g_bt_duplex_ctx.snapshot.hfp_profile_state =
                BT_HFP_PROFILE_DISCONNECTED;
            profile_changed = true;
        }
        g_bt_duplex_ctx.snapshot.counters.recoveries++;
        *new_generation_out = new_generation;
    }
    err = bt_duplex_unlock_result(err);
    if (err != ESP_OK) return err;

    if (profile_changed) {
        note_delivery_result(bt_hfp_event_emit_profile(
            BT_HFP_PROFILE_DISCONNECTED, peer_mac, new_generation),
            new_generation, peer_mac);
    }
    if (health_count != 0U) {
        note_delivery_result(bt_hfp_event_emit_health(
            BT_AUDIO_HEALTH_OK, ESP_OK, health_count, new_generation),
            new_generation, peer_mac);
    }
    return ESP_OK;
}

esp_err_t bt_duplex_record_incoming(uint32_t generation,
                                    const char *peer_mac,
                                    size_t bytes,
                                    bool accepted)
{
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK) {
        if (accepted) {
            g_bt_duplex_ctx.snapshot.counters.incoming_frames++;
            g_bt_duplex_ctx.snapshot.counters.incoming_bytes += bytes;
        } else {
            g_bt_duplex_ctx.snapshot.counters.incoming_dropped_frames++;
            g_bt_duplex_ctx.snapshot.counters.incoming_dropped_bytes += bytes;
        }
    }
    return bt_duplex_unlock_result(err);
}

esp_err_t bt_duplex_record_i2s_underflow(uint32_t generation,
                                         const char *peer_mac)
{
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.counters.i2s_underflows++;
    }
    return bt_duplex_unlock_result(err);
}

esp_err_t bt_duplex_record_i2s_timeout(uint32_t generation,
                                       const char *peer_mac,
                                       esp_err_t error)
{
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;
    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.counters.i2s_timeouts++;
        g_bt_duplex_ctx.snapshot.last_error = error;
    }
    return bt_duplex_unlock_result(err);
}

#ifdef UNIT_TEST
void bt_duplex_test_reset(void)
{
    atomic_store_explicit(&s_health_report_failures_lifetime, 0U,
                          memory_order_relaxed);
    atomic_store_explicit(&s_last_health_report_error_lifetime, ESP_OK,
                          memory_order_relaxed);
    if (!g_bt_duplex_ctx.initialized) return;
    if (bt_duplex_lock() != ESP_OK) return;
    bt_duplex_snapshot_defaults(&g_bt_duplex_ctx.snapshot);
    g_bt_duplex_ctx.health_event_count = 0U;
    g_bt_duplex_ctx.test_health_report_result = ESP_OK;
    (void)bt_duplex_unlock_result(ESP_OK);
}

void bt_duplex_test_set_health_report_result(esp_err_t result)
{
    if (!g_bt_duplex_ctx.initialized) return;
    if (bt_duplex_lock() != ESP_OK) return;
    g_bt_duplex_ctx.test_health_report_result = result;
    (void)bt_duplex_unlock_result(ESP_OK);
}
#endif

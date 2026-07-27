#include "bt_duplex_policy.h"

#include <ctype.h>
#include <string.h>

#include "platform_sync.h"

typedef struct {
    platform_mutex_t lock;
    bool initialized;
    bt_duplex_snapshot_t snapshot;
} bt_duplex_context_t;

static bt_duplex_context_t s_ctx;

static void snapshot_defaults(bt_duplex_snapshot_t *snapshot)
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

static bool valid_mac(const char *mac)
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

static bool same_mac(const char *lhs, const char *rhs)
{
    for (size_t i = 0; i < BT_DUPLEX_MAC_STR_LEN - 1U; ++i) {
        if (tolower((unsigned char)lhs[i]) != tolower((unsigned char)rhs[i])) {
            return false;
        }
    }
    return true;
}

static esp_err_t lock_ctx(void)
{
    if (!s_ctx.initialized || s_ctx.lock == NULL) return ESP_ERR_INVALID_STATE;
    return platform_mutex_lock(s_ctx.lock, PLATFORM_WAIT_FOREVER);
}

static void unlock_ctx(void)
{
    (void)platform_mutex_unlock(s_ctx.lock);
}

static uint32_t next_generation(uint32_t current)
{
    current++;
    return current == 0U ? 1U : current;
}

static esp_err_t validate_event_locked(uint32_t generation, const char *peer_mac)
{
    if (!valid_mac(peer_mac)) {
        s_ctx.snapshot.counters.invalid_arguments++;
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctx.snapshot.peer_valid || !same_mac(peer_mac, s_ctx.snapshot.peer_mac)) {
        s_ctx.snapshot.counters.wrong_peer_events++;
        return ESP_ERR_INVALID_STATE;
    }
    if (generation == 0U || generation != s_ctx.snapshot.session_generation) {
        s_ctx.snapshot.counters.stale_generation_events++;
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t illegal_locked(void)
{
    s_ctx.snapshot.counters.illegal_transitions++;
    return ESP_ERR_INVALID_STATE;
}

static bool a2dp_profile_transition_ok(bt_a2dp_profile_state_t from,
                                       bt_a2dp_profile_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_A2DP_PROFILE_DISCONNECTED:
        return to == BT_A2DP_PROFILE_CONNECTING;
    case BT_A2DP_PROFILE_CONNECTING:
        return to == BT_A2DP_PROFILE_CONNECTED ||
               to == BT_A2DP_PROFILE_DISCONNECTED;
    case BT_A2DP_PROFILE_CONNECTED:
        return to == BT_A2DP_PROFILE_DISCONNECTING ||
               to == BT_A2DP_PROFILE_DISCONNECTED;
    case BT_A2DP_PROFILE_DISCONNECTING:
        return to == BT_A2DP_PROFILE_DISCONNECTED;
    default:
        return false;
    }
}

static bool a2dp_audio_transition_ok(bt_a2dp_audio_state_t from,
                                     bt_a2dp_audio_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_A2DP_AUDIO_STOPPED:
        return to == BT_A2DP_AUDIO_STARTED ||
               to == BT_A2DP_AUDIO_REMOTE_SUSPENDED;
    case BT_A2DP_AUDIO_STARTED:
        return to == BT_A2DP_AUDIO_STOPPED ||
               to == BT_A2DP_AUDIO_REMOTE_SUSPENDED;
    case BT_A2DP_AUDIO_REMOTE_SUSPENDED:
        return to == BT_A2DP_AUDIO_STARTED ||
               to == BT_A2DP_AUDIO_STOPPED;
    default:
        return false;
    }
}

static bool hfp_profile_transition_ok(bt_hfp_profile_state_t from,
                                      bt_hfp_profile_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_HFP_PROFILE_UNINITIALIZED:
        return to == BT_HFP_PROFILE_DISCONNECTED || to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_DISCONNECTED:
        return to == BT_HFP_PROFILE_CONNECTING || to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_CONNECTING:
        return to == BT_HFP_PROFILE_SLC_CONNECTED ||
               to == BT_HFP_PROFILE_DISCONNECTED || to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_SLC_CONNECTED:
        return to == BT_HFP_PROFILE_DISCONNECTING ||
               to == BT_HFP_PROFILE_DISCONNECTED || to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_DISCONNECTING:
        return to == BT_HFP_PROFILE_DISCONNECTED || to == BT_HFP_PROFILE_FAULTED;
    case BT_HFP_PROFILE_FAULTED:
    default:
        return false;
    }
}

static bool hfp_audio_transition_ok(bt_hfp_audio_state_t from,
                                    bt_hfp_audio_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_HFP_AUDIO_DISCONNECTED:
        return to == BT_HFP_AUDIO_CONNECTING || to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_CONNECTING:
        return to == BT_HFP_AUDIO_CONNECTED_CVSD ||
               to == BT_HFP_AUDIO_CONNECTED_MSBC ||
               to == BT_HFP_AUDIO_DISCONNECTED || to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_CONNECTED_CVSD:
    case BT_HFP_AUDIO_CONNECTED_MSBC:
        return to == BT_HFP_AUDIO_DISCONNECTING ||
               to == BT_HFP_AUDIO_DISCONNECTED || to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_DISCONNECTING:
        return to == BT_HFP_AUDIO_DISCONNECTED || to == BT_HFP_AUDIO_FAULTED;
    case BT_HFP_AUDIO_FAULTED:
    default:
        return false;
    }
}

static bool i2s_transition_ok(bt_hfp_i2s_state_t from, bt_hfp_i2s_state_t to)
{
    if (from == to) return true;
    switch (from) {
    case BT_HFP_I2S_STOPPED:
        return to == BT_HFP_I2S_STARTING || to == BT_HFP_I2S_FAULTED;
    case BT_HFP_I2S_STARTING:
        return to == BT_HFP_I2S_RUNNING || to == BT_HFP_I2S_STOPPED ||
               to == BT_HFP_I2S_FAULTED;
    case BT_HFP_I2S_RUNNING:
        return to == BT_HFP_I2S_STOPPING || to == BT_HFP_I2S_FAULTED;
    case BT_HFP_I2S_STOPPING:
        return to == BT_HFP_I2S_STOPPED || to == BT_HFP_I2S_FAULTED ||
               to == BT_HFP_I2S_QUARANTINED;
    case BT_HFP_I2S_FAULTED:
        return to == BT_HFP_I2S_QUARANTINED;
    case BT_HFP_I2S_QUARANTINED:
    default:
        return false;
    }
}

esp_err_t bt_duplex_state_init(void)
{
    if (s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    platform_mutex_t lock = platform_mutex_create();
    if (lock == NULL) return ESP_ERR_NO_MEM;
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.lock = lock;
    snapshot_defaults(&s_ctx.snapshot);
    s_ctx.initialized = true;
    return ESP_OK;
}

void bt_duplex_state_deinit(void)
{
    platform_mutex_t lock = s_ctx.lock;
    memset(&s_ctx, 0, sizeof(s_ctx));
    platform_mutex_delete(lock);
}

esp_err_t bt_duplex_session_begin(const char *peer_mac,
                                  bt_duplex_mode_t requested_mode,
                                  uint32_t *generation_out)
{
    if (!valid_mac(peer_mac) || requested_mode >= BT_DUPLEX_MODE_COUNT ||
        generation_out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    if (s_ctx.snapshot.peer_valid && !same_mac(peer_mac, s_ctx.snapshot.peer_mac)) {
        s_ctx.snapshot.counters.wrong_peer_events++;
        unlock_ctx();
        return ESP_ERR_INVALID_STATE;
    }
    bt_duplex_counters_t counters = s_ctx.snapshot.counters;
    uint32_t generation = next_generation(s_ctx.snapshot.session_generation);
    snapshot_defaults(&s_ctx.snapshot);
    s_ctx.snapshot.counters = counters;
    s_ctx.snapshot.peer_valid = true;
    memcpy(s_ctx.snapshot.peer_mac, peer_mac, BT_DUPLEX_MAC_STR_LEN);
    s_ctx.snapshot.session_generation = generation;
    s_ctx.snapshot.hfp_profile_state = BT_HFP_PROFILE_DISCONNECTED;
    s_ctx.snapshot.requested_mode = requested_mode;
    s_ctx.snapshot.effective_mode = requested_mode == BT_DUPLEX_MODE_AUTO
                                        ? BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC
                                        : requested_mode;
    *generation_out = generation;
    unlock_ctx();
    return ESP_OK;
}

esp_err_t bt_duplex_get_snapshot(bt_duplex_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    memcpy(out, &s_ctx.snapshot, sizeof(*out));
    unlock_ctx();
    return ESP_OK;
}

#define DEFINE_ENUM_SETTER(name, field, type, count) \
esp_err_t name(uint32_t generation, const char *peer_mac, type value) \
{ \
    if (value >= count) return ESP_ERR_INVALID_ARG; \
    esp_err_t err = lock_ctx(); \
    if (err != ESP_OK) return err; \
    err = validate_event_locked(generation, peer_mac); \
    if (err == ESP_OK) s_ctx.snapshot.field = value; \
    unlock_ctx(); \
    return err; \
}

DEFINE_ENUM_SETTER(bt_duplex_set_requested_mode, requested_mode,
                   bt_duplex_mode_t, BT_DUPLEX_MODE_COUNT)
DEFINE_ENUM_SETTER(bt_duplex_set_effective_mode, effective_mode,
                   bt_duplex_mode_t, BT_DUPLEX_MODE_COUNT)

esp_err_t bt_duplex_set_a2dp_profile_state(uint32_t generation,
                                          const char *peer_mac,
                                          bt_a2dp_profile_state_t state)
{
    if (state >= BT_A2DP_PROFILE_STATE_COUNT) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    err = validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        !a2dp_profile_transition_ok(s_ctx.snapshot.a2dp_profile_state, state)) {
        err = illegal_locked();
    }
    if (err == ESP_OK) s_ctx.snapshot.a2dp_profile_state = state;
    unlock_ctx();
    return err;
}

esp_err_t bt_duplex_set_a2dp_audio_state(uint32_t generation,
                                        const char *peer_mac,
                                        bt_a2dp_audio_state_t state)
{
    if (state >= BT_A2DP_AUDIO_STATE_COUNT) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    err = validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        !a2dp_audio_transition_ok(s_ctx.snapshot.a2dp_audio_state, state)) {
        err = illegal_locked();
    }
    if (err == ESP_OK) s_ctx.snapshot.a2dp_audio_state = state;
    unlock_ctx();
    return err;
}

esp_err_t bt_duplex_set_hfp_profile_state(uint32_t generation,
                                         const char *peer_mac,
                                         bt_hfp_profile_state_t state)
{
    if (state >= BT_HFP_PROFILE_STATE_COUNT) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    err = validate_event_locked(generation, peer_mac);
    if (err == ESP_OK && !hfp_profile_transition_ok(s_ctx.snapshot.hfp_profile_state, state)) {
        err = illegal_locked();
    }
    if (err == ESP_OK) s_ctx.snapshot.hfp_profile_state = state;
    unlock_ctx();
    return err;
}

esp_err_t bt_duplex_set_hfp_audio_state(uint32_t generation,
                                       const char *peer_mac,
                                       bt_hfp_audio_state_t state)
{
    if (state >= BT_HFP_AUDIO_STATE_COUNT) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    err = validate_event_locked(generation, peer_mac);
    if (err == ESP_OK && !hfp_audio_transition_ok(s_ctx.snapshot.hfp_audio_state, state)) {
        err = illegal_locked();
    }
    if (err == ESP_OK) {
        s_ctx.snapshot.hfp_audio_state = state;
        if (state == BT_HFP_AUDIO_CONNECTED_CVSD) s_ctx.snapshot.codec = BT_HFP_CODEC_CVSD;
        else if (state == BT_HFP_AUDIO_CONNECTED_MSBC) s_ctx.snapshot.codec = BT_HFP_CODEC_MSBC;
        else if (state == BT_HFP_AUDIO_DISCONNECTED) s_ctx.snapshot.codec = BT_HFP_CODEC_NONE;
    }
    unlock_ctx();
    return err;
}

esp_err_t bt_duplex_set_i2s_state(uint32_t generation,
                                 const char *peer_mac,
                                 bt_hfp_i2s_state_t state)
{
    if (state >= BT_HFP_I2S_STATE_COUNT) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    err = validate_event_locked(generation, peer_mac);
    if (err == ESP_OK && !i2s_transition_ok(s_ctx.snapshot.i2s_state, state)) {
        err = illegal_locked();
    }
    if (err == ESP_OK) s_ctx.snapshot.i2s_state = state;
    unlock_ctx();
    return err;
}

esp_err_t bt_duplex_set_health(uint32_t generation,
                              const char *peer_mac,
                              bt_audio_health_t health,
                              esp_err_t error,
                              const char *error_text)
{
    if (health >= BT_AUDIO_HEALTH_COUNT) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    err = validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        s_ctx.snapshot.health >= BT_AUDIO_HEALTH_FAULTED &&
        health < s_ctx.snapshot.health) {
        err = illegal_locked();
    }
    if (err == ESP_OK) {
        s_ctx.snapshot.health = health;
        s_ctx.snapshot.last_error = error;
        if (error_text == NULL) error_text = "";
        size_t n = strlen(error_text);
        if (n >= sizeof(s_ctx.snapshot.last_error_text)) n = sizeof(s_ctx.snapshot.last_error_text) - 1U;
        memcpy(s_ctx.snapshot.last_error_text, error_text, n);
        s_ctx.snapshot.last_error_text[n] = '\0';
    }
    unlock_ctx();
    return err;
}

esp_err_t bt_duplex_recover(uint32_t generation, const char *peer_mac,
                            uint32_t *new_generation_out)
{
    if (new_generation_out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = lock_ctx();
    if (err != ESP_OK) return err;
    err = validate_event_locked(generation, peer_mac);
    if (err == ESP_OK &&
        (s_ctx.snapshot.hfp_audio_state != BT_HFP_AUDIO_DISCONNECTED ||
         s_ctx.snapshot.i2s_state != BT_HFP_I2S_STOPPED)) {
        err = illegal_locked();
    }
    if (err == ESP_OK) {
        s_ctx.snapshot.session_generation = next_generation(s_ctx.snapshot.session_generation);
        s_ctx.snapshot.health = BT_AUDIO_HEALTH_OK;
        s_ctx.snapshot.last_error = ESP_OK;
        s_ctx.snapshot.last_error_text[0] = '\0';
        if (s_ctx.snapshot.hfp_profile_state == BT_HFP_PROFILE_FAULTED) {
            s_ctx.snapshot.hfp_profile_state = BT_HFP_PROFILE_DISCONNECTED;
        }
        s_ctx.snapshot.counters.recoveries++;
        *new_generation_out = s_ctx.snapshot.session_generation;
    }
    unlock_ctx();
    return err;
}

void bt_duplex_record_incoming(uint32_t generation, const char *peer_mac,
                               size_t bytes, bool accepted)
{
    if (lock_ctx() != ESP_OK) return;
    if (validate_event_locked(generation, peer_mac) == ESP_OK) {
        if (accepted) {
            s_ctx.snapshot.counters.incoming_frames++;
            s_ctx.snapshot.counters.incoming_bytes += bytes;
        } else {
            s_ctx.snapshot.counters.incoming_dropped_frames++;
            s_ctx.snapshot.counters.incoming_dropped_bytes += bytes;
        }
    }
    unlock_ctx();
}

void bt_duplex_record_i2s_underflow(uint32_t generation, const char *peer_mac)
{
    if (lock_ctx() != ESP_OK) return;
    if (validate_event_locked(generation, peer_mac) == ESP_OK) {
        s_ctx.snapshot.counters.i2s_underflows++;
    }
    unlock_ctx();
}

void bt_duplex_record_i2s_timeout(uint32_t generation, const char *peer_mac,
                                  esp_err_t error)
{
    if (lock_ctx() != ESP_OK) return;
    if (validate_event_locked(generation, peer_mac) == ESP_OK) {
        s_ctx.snapshot.counters.i2s_timeouts++;
        s_ctx.snapshot.last_error = error;
    }
    unlock_ctx();
}

#define STRING_CASE(value) case value: return #value
const char *bt_duplex_mode_to_string(bt_duplex_mode_t value)
{
    switch (value) {
    STRING_CASE(BT_DUPLEX_MODE_DISABLED);
    STRING_CASE(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC);
    STRING_CASE(BT_DUPLEX_MODE_HFP_FULL_DUPLEX);
    STRING_CASE(BT_DUPLEX_MODE_AUTO);
    default: return "BT_DUPLEX_MODE_UNKNOWN";
    }
}
const char *bt_hfp_profile_state_to_string(bt_hfp_profile_state_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_PROFILE_UNINITIALIZED);
    STRING_CASE(BT_HFP_PROFILE_DISCONNECTED);
    STRING_CASE(BT_HFP_PROFILE_CONNECTING);
    STRING_CASE(BT_HFP_PROFILE_SLC_CONNECTED);
    STRING_CASE(BT_HFP_PROFILE_DISCONNECTING);
    STRING_CASE(BT_HFP_PROFILE_FAULTED);
    default: return "BT_HFP_PROFILE_UNKNOWN";
    }
}
const char *bt_hfp_audio_state_to_string(bt_hfp_audio_state_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_AUDIO_DISCONNECTED);
    STRING_CASE(BT_HFP_AUDIO_CONNECTING);
    STRING_CASE(BT_HFP_AUDIO_CONNECTED_CVSD);
    STRING_CASE(BT_HFP_AUDIO_CONNECTED_MSBC);
    STRING_CASE(BT_HFP_AUDIO_DISCONNECTING);
    STRING_CASE(BT_HFP_AUDIO_FAULTED);
    default: return "BT_HFP_AUDIO_UNKNOWN";
    }
}
const char *bt_hfp_codec_to_string(bt_hfp_codec_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_CODEC_NONE);
    STRING_CASE(BT_HFP_CODEC_CVSD);
    STRING_CASE(BT_HFP_CODEC_MSBC);
    default: return "BT_HFP_CODEC_UNKNOWN";
    }
}
const char *bt_hfp_i2s_state_to_string(bt_hfp_i2s_state_t value)
{
    switch (value) {
    STRING_CASE(BT_HFP_I2S_STOPPED);
    STRING_CASE(BT_HFP_I2S_STARTING);
    STRING_CASE(BT_HFP_I2S_RUNNING);
    STRING_CASE(BT_HFP_I2S_STOPPING);
    STRING_CASE(BT_HFP_I2S_FAULTED);
    STRING_CASE(BT_HFP_I2S_QUARANTINED);
    default: return "BT_HFP_I2S_UNKNOWN";
    }
}
const char *bt_audio_health_to_string(bt_audio_health_t value)
{
    switch (value) {
    STRING_CASE(BT_AUDIO_HEALTH_OK);
    STRING_CASE(BT_AUDIO_HEALTH_DEGRADED);
    STRING_CASE(BT_AUDIO_HEALTH_FAULTED);
    STRING_CASE(BT_AUDIO_HEALTH_QUARANTINED);
    default: return "BT_AUDIO_HEALTH_UNKNOWN";
    }
}

#ifdef UNIT_TEST
void bt_duplex_test_reset(void)
{
    if (!s_ctx.initialized) return;
    if (lock_ctx() != ESP_OK) return;
    snapshot_defaults(&s_ctx.snapshot);
    unlock_ctx();
}
#endif

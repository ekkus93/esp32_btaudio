#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bt_hfp_audio.h"
#include "platform_sync.h"

#define BT_HFP_AUDIO_WORK_EVENT 0x7002U
#define BT_HFP_AUDIO_REMOTE_CLEANUP_EVENT 0x7003U
#define BT_HFP_AUDIO_FALLBACK_I2S_STOP_TIMEOUT_MS 1000U

typedef struct {
    uint32_t serial;
    bt_hfp_audio_operation_type_t type;
    bool cleanup_only;
} bt_hfp_audio_work_request_t;

typedef struct {
    uint32_t generation;
    bool disconnect_lower;
    bool stop_i2s;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
} bt_hfp_audio_remote_cleanup_t;

typedef struct {
    bool initialized;
    platform_mutex_t lock;
    platform_binary_sem_t request_done;
    platform_binary_sem_t event_done;
    bt_hfp_audio_control_snapshot_t snapshot;
} bt_hfp_audio_control_context_t;

extern bt_hfp_audio_control_context_t s_control;

#ifdef UNIT_TEST
extern bt_hfp_audio_control_platform_ops_t s_test_ops;
extern bool s_test_ops_set;
#endif

/* Shared helpers (bt_hfp_audio_control.c) */
esp_err_t control_lock(void);
esp_err_t control_unlock(esp_err_t prior);
void drain_sem(platform_binary_sem_t sem);
esp_err_t context_ensure(void);
esp_err_t reserve_operation(bt_hfp_audio_operation_type_t type,
                            const char *peer_mac,
                            uint32_t *serial_out);
void update_operation_generation(uint32_t serial, uint32_t generation);
void finish_operation(uint32_t serial, esp_err_t result,
                      bt_hfp_audio_operation_state_t failure_state);

/* MAC/peer helpers (bt_hfp_audio_control_work.c) */
bool parse_mac(const char *text, esp_bd_addr_t out);
bool same_peer(const char *lhs, const char *rhs);
void copy_peer(char out[BT_DUPLEX_MAC_STR_LEN], const char *peer);
esp_err_t platform_audio_connect(esp_bd_addr_t remote_bda);
esp_err_t platform_audio_disconnect(esp_bd_addr_t remote_bda);
esp_err_t queue_lower_request(uint32_t serial,
                              bt_hfp_audio_operation_type_t type);
esp_err_t issue_cleanup_disconnect(uint32_t serial);
bool lower_request_may_be_live(uint32_t serial);
esp_err_t rollback_started_i2s(uint32_t serial, uint32_t generation,
                               const char *peer_mac,
                               esp_err_t cause,
                               bool disconnect_lower);
void dispatch_remote_cleanup(uint32_t generation, const char *peer_mac,
                             bool disconnect_lower, bool stop_i2s);

/* I2S session helpers (bt_hfp_audio_control_i2s.c) */
void stop_i2s_for_session(uint32_t generation, const char *peer_mac,
                          esp_err_t *result_out);
esp_err_t start_i2s_for_session(uint32_t generation, const char *peer_mac);
esp_err_t set_audio_state_if_needed(uint32_t generation,
                                    const char *peer_mac,
                                    bt_hfp_audio_state_t state);
void set_health(uint32_t generation, const char *peer_mac,
                bt_audio_health_t health, esp_err_t error,
                const char *text);

/**
 * Bluetooth definitions shared by all components
 */
#ifndef BT_DEFS_H
#define BT_DEFS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device type enum
 */
typedef enum {
    BT_DEVICE_TYPE_UNKNOWN = 0,
    BT_DEVICE_TYPE_CLASSIC,
    BT_DEVICE_TYPE_BLE,
    BT_DEVICE_TYPE_DUAL,
    BT_DEVICE_TYPE_AUDIO
} bt_device_type_t;

/**
 * @brief Bluetooth streaming state
 */
typedef enum {
    BT_STREAMING_STATE_STOPPED = 0,
    BT_STREAMING_STATE_PLAYING,
    BT_STREAMING_STATE_PAUSED
} bt_streaming_state_t;

/**
 * @brief Pairing state enumeration
 */
typedef enum {
    BT_PAIRING_STATE_IDLE = 0,
    BT_PAIRING_STATE_INITIATED,
    BT_PAIRING_STATE_PIN_REQUESTED,
    BT_PAIRING_STATE_SSP_REQUESTED,
    BT_PAIRING_STATE_PAIRED,
    BT_PAIRING_STATE_FAILED,
    BT_PAIRING_STATE_TIMEOUT
} bt_pairing_state_t;

/**
 * @brief Pairing method enum
 */
typedef enum {
    BT_PAIRING_METHOD_NONE = 0,
    BT_PAIRING_METHOD_PIN,
    BT_PAIRING_METHOD_SSP,
    BT_PAIRING_METHOD_JUST_WORKS
} bt_pairing_method_t;

/**
 * @brief Bluetooth device structure
 */
typedef struct {
    uint8_t addr[6];
    char name[64];
    uint32_t cod;
    int rssi;
    bt_device_type_t type;
    bool supports_a2dp;
    bool paired;
} bt_device_t;

/**
 * @brief Connection info structure
 */
typedef struct {
    bool connected;
    char remote_addr[18];
    char remote_name[64];
    uint8_t raw_addr[6];
    bt_device_type_t device_type;
} bt_connection_info_t;

#ifdef __cplusplus
}
#endif

#endif /* BT_DEFS_H */

/**
 * bt_manager_api_mock.c — mock for the bt_manager wrapper API
 *
 * command_interface's handlers call the bt_manager_* / bt_pairing_* /
 * bt_unpair* wrapper layer. In this fully-mocked test app those symbols
 * must resolve here: any reference satisfied from libbt_manager.a pulls
 * real objects whose other definitions collide with bt_source_mock.c.
 *
 * This file includes bt_manager.h for its types; the base mock functions
 * it delegates to are declared locally because bt_manager.h's bt_device_t
 * conflicts with bt_source.h's (bt_source_mock.c owns that side).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "bt_manager.h"

/* base mock implementations (bt_source_mock.c / bt_source_stubs.c) */
extern bool bt_is_connected(void);
extern bool bt_is_streaming(void);
extern esp_err_t bt_connect_device(const char* addr);
extern esp_err_t bt_disconnect(void);
extern esp_err_t bt_scan_start(void);
extern esp_err_t bt_start_pairing(const char* addr);
extern esp_err_t bt_send_pin_code(const char* pin);
extern esp_err_t bt_ssp_confirm(bool confirm);
extern bool bt_is_ssp_confirm_requested(void);
extern esp_err_t bt_get_ssp_passkey(char* passkey, size_t size);
extern esp_err_t bt_mock_unpair_device(const char* addr);
extern esp_err_t bt_mock_unpair_all_devices(void);
extern esp_err_t bt_a2dp_start_streaming(void);
extern esp_err_t bt_a2dp_stop_streaming(void);
extern void bt_source_mock_set_initialized(bool initialized);

bt_err_t bt_manager_init(const bt_manager_init_t* config)
{
    (void)config;
    bt_source_mock_set_initialized(true);
    return ESP_OK;
}

void bt_manager_force_initialized(bool value)
{
    bt_source_mock_set_initialized(value);
}

int bt_manager_is_connected(void)
{
    return bt_is_connected() ? 1 : 0;
}

int bt_manager_connect(const char* mac)
{
    return bt_connect_device(mac);
}

int bt_manager_disconnect(void)
{
    return bt_disconnect();
}

int bt_manager_start_scan(void)
{
    return bt_scan_start();
}

int bt_manager_set_name(const char* name)
{
    (void)name; /* mock has no GAP name */
    return ESP_OK;
}

int bt_manager_start_audio(void)
{
    return bt_a2dp_start_streaming();
}

int bt_manager_stop_audio(void)
{
    return bt_a2dp_stop_streaming();
}

bt_err_t bt_manager_start_pair(const char* mac)
{
    /* EVENT|PAIR|* lines are emitted by the production notifier
     * (bt_pairing_store.c, linked with UNIT_TEST hooks in this app) —
     * do not emit here or events duplicate. */
    return bt_start_pairing(mac);
}

bt_err_t bt_pairing_confirm(const char* mac, bool accept)
{
    (void)mac;
    return bt_ssp_confirm(accept);
}

bt_err_t bt_pairing_submit_pin(const char* mac, const char* pin)
{
    (void)mac;
    return bt_send_pin_code(pin);
}

bt_err_t bt_unpair(const char* mac)
{
    return bt_mock_unpair_device(mac);
}

bt_err_t bt_unpair_all(void)
{
    return bt_mock_unpair_all_devices();
}

bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info)
{
    if (info == NULL) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    if (bt_is_ssp_confirm_requested()) {
        info->ssp_confirm_pending = true;
        char passkey[16] = {0};
        if (bt_get_ssp_passkey(passkey, sizeof(passkey)) == ESP_OK) {
            info->passkey = (uint32_t)strtoul(passkey, NULL, 10);
        }
        return true;
    }
    return false;
}

int bt_get_streaming_state_int(void)
{
    return bt_is_streaming() ? 1 : 0;
}

void bt_connection_manager_clear_reconnect_target(void)
{
    /* mock has no reconnect machinery */
}

void bt_manager_test_gap_auth_complete(const char* mac, bool success)
{
    (void)mac;
    (void)success;
}

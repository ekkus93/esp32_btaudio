/**
 * bt_pairing_store.c - Bluetooth pairing state management implementation
 * 
 * This module was extracted from bt_manager.c to improve modularity and
 * maintainability. It handles all aspects of Bluetooth pairing including
 * PIN-based legacy pairing and SSP (Secure Simple Pairing).
 */

#include "bt_pairing_store.h"
#include "bt_manager_internal.h"
#include "nvs_storage.h"
#include "util_safe.h"
#include "command_interface.h"
#include <string.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#define TAG "BT_PAIR"
#else
#include "esp_log.h"
#define TAG "BT_PAIR"
#endif

#if CONFIG_BT_MOCK_TESTING
/* Forward declarations from bt_mock component for testing */
typedef enum bt_pairing_method bt_pairing_method_t;  /* Forward decl from bt_source.h */
extern esp_err_t bt_mock_send_pin(const char* pin);
extern bt_pairing_method_t bt_mock_get_pairing_method(void);
extern esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size);
#endif

/* Pending pairing state */
typedef struct {
    bool pin_pending;
    bool ssp_pending;
    bool pairing_in_progress; /* Set by prepare_for_initiation; cleared on auth complete */
    /* Set by bt_pairing_handle_connection_failed to suppress the duplicate FAILED
     * event that would otherwise fire from bt_pairing_handle_auth_complete for the
     * same page-timeout disconnect.  Cleared on the next prepare_for_initiation. */
    bool connection_failed_handled;
    esp_bd_addr_t bda;
    char mac[18];
    uint32_t passkey;
} bt_pairing_pending_t;

static bt_pairing_pending_t s_pair_pending = {0};

/* Test hooks - weak symbols overridden by tests */
#ifdef UNIT_TEST
MAYBE_WEAK void bt_manager_test_record_pair_event(const char* subtype, const char* data) {
    (void)subtype;
    (void)data;
}
#endif

/* Internal helper functions */

static void bt_pairing_format_mac(const esp_bd_addr_t bda, char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    safe_snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static void bt_pairing_set_pending_addr(const esp_bd_addr_t bda)
{
    safe_memcpy(s_pair_pending.bda, sizeof(s_pair_pending.bda), bda, sizeof(esp_bd_addr_t));
    bt_pairing_format_mac(bda, s_pair_pending.mac, sizeof(s_pair_pending.mac));
}

static void bt_pairing_clear_pending_flags(bool clear_pin, bool clear_ssp)
{
    if (clear_pin) {
        s_pair_pending.pin_pending = false;
    }
    if (clear_ssp) {
        s_pair_pending.ssp_pending = false;
    }
    if (!s_pair_pending.pin_pending && !s_pair_pending.ssp_pending) {
        safe_memset(s_pair_pending.bda, sizeof(s_pair_pending.bda), 0, sizeof(s_pair_pending.bda));
        s_pair_pending.mac[0] = '\0';
        s_pair_pending.passkey = 0;
        s_pair_pending.pairing_in_progress = false;
    }
}

static bool bt_pairing_parse_mac_string(const char* mac, esp_bd_addr_t out)
{
    return parse_mac_bytes(mac, out);
}

static bool bt_pairing_addr_is_zero(const esp_bd_addr_t addr)
{
    if (!addr) {
        return true;
    }
    for (size_t i = 0; i < sizeof(esp_bd_addr_t); ++i) {
        if (addr[i] != 0) {
            return false;
        }
    }
    return true;
}

static void bt_pairing_prepare_pending_for_event(const esp_bd_addr_t addr)
{
    if (!addr) {
        return;
    }

    if (!bt_pairing_addr_is_zero(s_pair_pending.bda) &&
        memcmp(s_pair_pending.bda, addr, sizeof(esp_bd_addr_t)) != 0) {
        char prev_mac[18] = {0};
        char new_mac[18] = {0};
        bt_pairing_format_mac(s_pair_pending.bda, prev_mac, sizeof(prev_mac));
        bt_pairing_format_mac(addr, new_mac, sizeof(new_mac));
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Pairing target changed from %s to %s due to GAP event", prev_mac, new_mac);  // NOLINT(bugprone-branch-clone)
#endif
        bt_pairing_clear_pending_flags(true, true);
    }

    bt_pairing_set_pending_addr(addr);
    s_pair_pending.passkey = 0;
}

static void bt_pairing_send_event(const char* subtype, const char* data)
{
    if (!subtype || !data) {
        return;
    }

#if defined(ESP_PLATFORM)
    cmd_send_event_pair(subtype, data);
#elif defined(UNIT_TEST)
    bt_manager_test_record_pair_event(subtype, data);
#endif
}

/* Public and internal API implementation */

void bt_pairing_handle_pin_request(const esp_bd_addr_t bda)
{
    char bda_str[18];
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    ESP_LOGI(TAG, "PIN request from device: %s", bda_str);  // NOLINT(bugprone-branch-clone)
    bt_pairing_prepare_pending_for_event(bda);
    s_pair_pending.pin_pending = true;
    s_pair_pending.ssp_pending = false;
    s_pair_pending.passkey = 0;
    s_pair_pending.pairing_in_progress = true;
    bt_pairing_send_event("PIN_REQUEST", bda_str);
}

void bt_pairing_handle_ssp_confirm(const esp_bd_addr_t bda, uint32_t passkey)
{
    char bda_str[18];
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    char data[64];
    safe_snprintf(data, sizeof(data), "%s,%u", bda_str, (unsigned int)passkey);
    ESP_LOGI(TAG, "SSP confirm request from %s value=%u", bda_str, (unsigned int)passkey);  // NOLINT(bugprone-branch-clone)
    bt_pairing_prepare_pending_for_event(bda);
    s_pair_pending.ssp_pending = true;
    s_pair_pending.pin_pending = false;
    s_pair_pending.passkey = passkey;
    s_pair_pending.pairing_in_progress = true;
    bt_pairing_send_event("CONFIRM", data);
}

void bt_pairing_handle_auth_complete(const esp_bd_addr_t bda, esp_bt_status_t stat)
{
    char bda_str[18] = {0};
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    if (s_pair_pending.connection_failed_handled) {
        /* bt_pairing_handle_connection_failed already emitted FAILED for this
         * pairing session.  Suppress the duplicate AUTH_CMPL FAILED event that
         * arrives after a page-timeout HCI disconnect. */
        s_pair_pending.connection_failed_handled = false;
        return;
    }

    bt_pairing_prepare_pending_for_event(bda);
    if (stat == ESP_BT_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Authentication (pairing) successful: %s", bda_str);  // NOLINT(bugprone-branch-clone)
        bt_pairing_send_event("SUCCESS", bda_str);
#if defined(ESP_PLATFORM)
        char dev_name[32] = {0};
        if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) == ESP_OK) {
            for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
                if (strcmp(bt_ctx.discovered_devices.devices[i].mac, bda_str) == 0) {
                    safe_copy_str(dev_name, sizeof(dev_name), bt_ctx.discovered_devices.devices[i].name);
                    break;
                }
            }
            bt_ctx_unlock();
        }
        nvs_storage_add_paired_device(bda_str, dev_name[0] ? dev_name : NULL);
#endif
    } else {
        ESP_LOGW(TAG, "Authentication (pairing) failed: %s", bda_str);  // NOLINT(bugprone-branch-clone)
        bt_pairing_send_event("FAILED", bda_str);
    }

    bt_pairing_clear_pending_flags(true, true);
}

void bt_pairing_clear_pending(void)
{
    bt_pairing_clear_pending_flags(true, true);
}

bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info)
{
    if (!info) {
        return false;
    }
    if (!s_pair_pending.pin_pending && !s_pair_pending.ssp_pending) {
        safe_memset(info, sizeof(*info), 0, sizeof(*info));
        return false;
    }
    info->pin_request_pending = s_pair_pending.pin_pending;
    info->ssp_confirm_pending = s_pair_pending.ssp_pending;
    safe_copy_str(info->mac, sizeof(info->mac), s_pair_pending.mac);
    info->mac[sizeof(info->mac) - 1] = '\0';
    info->passkey = s_pair_pending.passkey;
    return true;
}

bt_err_t bt_pairing_confirm(const char* mac, bool accept)
{
    (void)mac;
    (void)accept;
#ifdef ESP_PLATFORM
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return err;
    }
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    bt_ctx_unlock();

    esp_bd_addr_t target = {0};

    if (mac && mac[0] != '\0') {
        if (!bt_pairing_parse_mac_string(mac, target)) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (s_pair_pending.ssp_pending) {
        safe_memcpy(target, sizeof(target), s_pair_pending.bda, sizeof(target));
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_bt_gap_ssp_confirm_reply(target, accept);
    if (err == ESP_OK) {
        bt_pairing_clear_pending_flags(false, true);
    }
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bt_err_t bt_pairing_submit_pin(const char* mac, const char* pin)
{
#ifdef ESP_PLATFORM
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return err;
    }
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    bt_ctx_unlock();
    if (pin == NULL || pin[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_BT_MOCK_TESTING
    esp_bd_addr_t target = {0};
    char formatted_mac[18] = {0};
    const char* mac_to_emit = NULL;

    if (mac && mac[0] != '\0') {
        if (!bt_pairing_parse_mac_string(mac, target)) {
            return ESP_ERR_INVALID_ARG;
        }
        bt_pairing_format_mac(target, formatted_mac, sizeof(formatted_mac));
        mac_to_emit = formatted_mac;
    } else if (s_pair_pending.pin_pending) {
        safe_memcpy(target, sizeof(target), s_pair_pending.bda, sizeof(target));
        mac_to_emit = s_pair_pending.mac;
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mac_to_emit || mac_to_emit[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char confirm_payload[64];
    safe_snprintf(confirm_payload, sizeof(confirm_payload), "%s,%s", mac_to_emit, pin);
    cmd_send_event_pair("CONFIRM", confirm_payload);

    esp_err_t mock_err = bt_mock_send_pin(pin);
    if (mock_err == ESP_OK) {
        bt_pairing_clear_pending_flags(true, false);
    }
    return mock_err;
#else
    esp_bd_addr_t target = {0};

    if (mac && mac[0] != '\0') {
        if (!bt_pairing_parse_mac_string(mac, target)) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (s_pair_pending.pin_pending) {
        safe_memcpy(target, sizeof(target), s_pair_pending.bda, sizeof(target));
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
    size_t pin_len = strlen(pin);
    if (pin_len > ESP_BT_PIN_CODE_LEN) {
        pin_len = ESP_BT_PIN_CODE_LEN;
    }
    safe_memcpy(pin_code, pin_len, pin, pin_len);

    esp_err_t err = esp_bt_gap_pin_reply(target, true, (uint8_t)pin_len, pin_code);
    if (err == ESP_OK) {
        bt_pairing_clear_pending_flags(true, false);
    }
    return err;
#endif
#else
    (void)mac;
    (void)pin;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* Additional internal API for bt_pair() and other bt_manager functions */

bool bt_pairing_parse_mac(const char* mac, esp_bd_addr_t out)
{
    return bt_pairing_parse_mac_string(mac, out);
}

void bt_pairing_prepare_for_initiation(const esp_bd_addr_t bda)
{
    bt_pairing_clear_pending_flags(true, true);
    bt_pairing_set_pending_addr(bda);
    s_pair_pending.passkey = 0;
    s_pair_pending.pairing_in_progress = true;
    s_pair_pending.connection_failed_handled = false;
}

bool bt_pairing_handle_connection_failed(const esp_bd_addr_t bda)
{
    if (!s_pair_pending.pairing_in_progress) {
        return false;
    }
    if (bt_pairing_addr_is_zero(s_pair_pending.bda)) {
        return false;
    }
    if (memcmp(s_pair_pending.bda, bda, sizeof(esp_bd_addr_t)) != 0) {
        return false;
    }
    char mac[18];
    bt_pairing_format_mac(bda, mac, sizeof(mac));
    ESP_LOGW(TAG, "Connection failed while pairing pending: %s", mac);  // NOLINT(bugprone-branch-clone)
    bt_pairing_send_event("FAILED", mac);
    /* Mark that FAILED was already emitted so bt_pairing_handle_auth_complete
     * skips the duplicate event for the same session. */
    s_pair_pending.connection_failed_handled = true;
    bt_pairing_clear_pending_flags(true, true);
    return true;
}

#if CONFIG_BT_MOCK_TESTING
void bt_pairing_set_mock_state(const esp_bd_addr_t bda, bool is_pin, uint32_t passkey)
{
    bt_pairing_clear_pending_flags(true, true);
    bt_pairing_set_pending_addr(bda);
    
    if (is_pin) {
        s_pair_pending.pin_pending = true;
        s_pair_pending.passkey = 0;
    } else {
        s_pair_pending.ssp_pending = true;
        s_pair_pending.passkey = passkey;
    }
}
#endif


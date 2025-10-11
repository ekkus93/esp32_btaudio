/*
 * bt_mock_devices.c
 * Clean, single-file implementation of mock device helpers used by tests.
 * Keeps authoritative device storage, pairing/SSP helpers, connect/disconnect
 * and scan helpers in one place. bt_mock_init()/bt_mock_reset() live in
 * bt_mock.c to avoid duplicate symbols across translation units.
 */

#include "bt_mock_devices.h"
#include "bt_source.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
// When running on-device, forward mock pairing events to the serial command interface
#ifdef ESP_PLATFORM
#include "command_interface.h"
#endif

static const char *TAG = "BT_MOCK_DEVICES";

#define MAX_TEST_DEVICES 32

typedef struct {
    bool is_connected;
    bool is_scanning;
    bt_connection_state_t connection_state;
    bt_streaming_state_t streaming_state;
    char connected_addr[18];

    bt_device_t devices[MAX_TEST_DEVICES];
    int device_count;

    bool connect_by_name_hook_enabled;
    char connect_by_name_device[64];
    char connect_by_name_addr[18];
} bt_mock_internal_state_t;

static bt_mock_internal_state_t mock_state = {0};

static bt_pairing_state_t current_pairing_state = BT_PAIRING_STATE_IDLE;
static bt_pairing_method_t current_pairing_method = BT_PAIRING_METHOD_NONE;
static char current_pairing_addr[18] = {0};
static bool is_pairing = false;
static bool s_ssp_support_enabled = true;
static bool pin_failure_simulation = false;
static uint32_t s_ssp_passkey_value = 0;
static char s_ssp_passkey[16] = {0};
static bool s_ssp_confirmation_requested = false;
static char s_default_pin[16] = "1234";

static void normalize_addr_upper(const char* in, char* out, size_t out_len)
{
    if (!in || !out) return;
    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
    for (size_t i = 0; out[i]; i++) {
        if (out[i] >= 'a' && out[i] <= 'f') out[i] = out[i] - 'a' + 'A';
    }
}

void bt_mock_devices_init(void)
{
    memset(&mock_state, 0, sizeof(mock_state));
    mock_state.connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    mock_state.streaming_state = BT_STREAMING_STATE_STOPPED;
}

void bt_mock_devices_cleanup(void)
{
    bt_mock_devices_reset();
}

void bt_mock_devices_reset(void)
{
    int prev = mock_state.device_count;
    memset(&mock_state, 0, sizeof(mock_state));
    mock_state.connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    mock_state.streaming_state = BT_STREAMING_STATE_STOPPED;

    current_pairing_state = BT_PAIRING_STATE_IDLE;
    current_pairing_method = BT_PAIRING_METHOD_NONE;
    current_pairing_addr[0] = '\0';
    is_pairing = false;
    s_ssp_support_enabled = true;
    pin_failure_simulation = false;
    s_ssp_passkey_value = 0;
    s_ssp_passkey[0] = '\0';
    s_ssp_confirmation_requested = false;
    strncpy(s_default_pin, "1234", sizeof(s_default_pin)-1);

    ESP_LOGI(TAG, "bt_mock_devices_reset: cleared %d devices", prev);
}

int bt_mock_devices_count(void)
{
    return mock_state.device_count;
}

esp_err_t bt_mock_add_device(const char* addr_str, const char* name, bt_device_type_t type, bool paired)
{
    if (!addr_str || !name) return ESP_ERR_INVALID_ARG;
    if (mock_state.device_count >= MAX_TEST_DEVICES) return ESP_ERR_NO_MEM;

    bt_device_t* d = &mock_state.devices[mock_state.device_count];
    memset(d, 0, sizeof(*d));
    sscanf(addr_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &d->addr[0], &d->addr[1], &d->addr[2], &d->addr[3], &d->addr[4], &d->addr[5]);
    strncpy(d->name, name, sizeof(d->name)-1);
    d->name[sizeof(d->name)-1] = '\0';
    d->paired = paired;
    d->rssi = -70;
    switch (type) {
        case BT_DEVICE_TYPE_AUDIO: d->cod = 0x240404; break;
        case BT_DEVICE_TYPE_PHONE: d->cod = 0x500204; break;
        default: d->cod = 0x120104; break;
    }

    mock_state.device_count++;
    ESP_LOGI(TAG, "bt_mock_add_device: added %s (%s) index %d", name, addr_str, mock_state.device_count-1);
    return ESP_OK;
}

esp_err_t bt_mock_get_device(int index, bt_device_t* device)
{
    if (!device) return ESP_ERR_INVALID_ARG;
    if (index < 0 || index >= mock_state.device_count) return ESP_ERR_NOT_FOUND;
    memcpy(device, &mock_state.devices[index], sizeof(bt_device_t));
    return ESP_OK;
}

esp_err_t bt_mock_remove_device(int index)
{
    if (index < 0 || index >= mock_state.device_count) return ESP_ERR_NOT_FOUND;
    for (int j = index; j < mock_state.device_count - 1; j++) {
        memcpy(&mock_state.devices[j], &mock_state.devices[j+1], sizeof(bt_device_t));
    }
    mock_state.device_count--;
    return ESP_OK;
}

bool bt_mock_find_device(const char* addr_str, int* index)
{
    if (!addr_str) return false;
    char norm[18];
    normalize_addr_upper(addr_str, norm, sizeof(norm));
    for (int i = 0; i < mock_state.device_count; i++) {
        char a[18];
        snprintf(a, sizeof(a), "%02X:%02X:%02X:%02X:%02X:%02X",
                mock_state.devices[i].addr[0], mock_state.devices[i].addr[1],
                mock_state.devices[i].addr[2], mock_state.devices[i].addr[3],
                mock_state.devices[i].addr[4], mock_state.devices[i].addr[5]);
        if (strcmp(a, norm) == 0) {
            if (index) *index = i;
            return true;
        }
    }
    return false;
}

/* Connection and scan helpers */
esp_err_t bt_mock_connect(const char* addr)
{
    if (!addr) return ESP_ERR_INVALID_ARG;

    /* Only allow connecting to addresses present in the authoritative
     * device list. Tests expect connecting to unknown addresses to fail. */
    int idx = -1;
    if (!bt_mock_find_device(addr, &idx)) {
        ESP_LOGI(TAG, "Connect failed: address %s not found in mock device list", addr);
        return ESP_ERR_NOT_FOUND;
    }

    if (mock_state.is_connected) {
        ESP_LOGI(TAG, "Already connected, ignoring connect request");
        return ESP_ERR_INVALID_STATE;
    }

    mock_state.is_connected = true;
    strncpy(mock_state.connected_addr, addr, sizeof(mock_state.connected_addr)-1);
    mock_state.connected_addr[sizeof(mock_state.connected_addr)-1] = '\0';
    mock_state.connection_state = BT_CONNECTION_STATE_CONNECTED;
    ESP_LOGI(TAG, "Connected to %s", addr);
    return ESP_OK;
}

esp_err_t bt_mock_disconnect(void)
{
    if (!mock_state.is_connected) {
        ESP_LOGI(TAG, "Not connected, ignoring disconnect request");
        return ESP_FAIL;
    }
    mock_state.is_connected = false;
    mock_state.connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    mock_state.connected_addr[0] = '\0';
    ESP_LOGI(TAG, "Disconnected");
    return ESP_OK;
}

bool bt_mock_is_connected(void)
{
    return mock_state.is_connected;
}

void bt_mock_set_connect_by_name_hook(const char* name, const char* addr)
{
    if (!name || !addr) {
        mock_state.connect_by_name_hook_enabled = false;
        return;
    }
    mock_state.connect_by_name_hook_enabled = true;
    strncpy(mock_state.connect_by_name_device, name, sizeof(mock_state.connect_by_name_device)-1);
    mock_state.connect_by_name_device[sizeof(mock_state.connect_by_name_device)-1] = '\0';
    strncpy(mock_state.connect_by_name_addr, addr, sizeof(mock_state.connect_by_name_addr)-1);
    mock_state.connect_by_name_addr[sizeof(mock_state.connect_by_name_addr)-1] = '\0';
}

static esp_err_t bt_mock_connect_by_name(const char* device_name)
{
    if (device_name == NULL) return ESP_ERR_INVALID_ARG;

    /* If a hook is explicitly registered and matches, use it */
    if (mock_state.connect_by_name_hook_enabled && strcmp(device_name, mock_state.connect_by_name_device) == 0) {
        return bt_mock_connect(mock_state.connect_by_name_addr);
    }

    /* No hook registered (or didn't match) - attempt to find a device by name
     * in the authoritative device list and connect to it. This provides a
     * convenient fallback for tests that add devices by name and expect
     * connect-by-name to succeed without setting a hook.
     */
    for (int i = 0; i < mock_state.device_count; i++) {
        /* `name` is an embedded char array in bt_device_t; it cannot be NULL.
         * Check for an empty string instead. This avoids -Werror=address
         * which warns when comparing an array field to NULL.
         */
        if (mock_state.devices[i].name[0] == '\0') continue;
        if (strcasecmp(mock_state.devices[i].name, device_name) == 0) {
            /* Build address string and connect */
            char addr_str[18];
            snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mock_state.devices[i].addr[0], mock_state.devices[i].addr[1],
                     mock_state.devices[i].addr[2], mock_state.devices[i].addr[3],
                     mock_state.devices[i].addr[4], mock_state.devices[i].addr[5]);
            return bt_mock_connect(addr_str);
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t bt_mock_hook_connect_by_name(const char* name)
{
    return bt_mock_connect_by_name(name);
}

void bt_mock_start_scan(void)
{
    mock_state.is_scanning = true;
    ESP_LOGI(TAG, "Scanning started");
}

void bt_mock_stop_scan(void)
{
    mock_state.is_scanning = false;
    ESP_LOGI(TAG, "Scanning stopped");
}

int bt_mock_get_scan_results(bt_device_t* devices, int max_count)
{
    if (!devices || max_count <= 0) return 0;
    int copied = 0;
    for (int i = 0; i < mock_state.device_count && copied < max_count; i++) {
        memcpy(&devices[copied], &mock_state.devices[i], sizeof(bt_device_t));
        copied++;
    }
    return copied;
}

/* Expose scanner state so callers can observe whether a delegated scan is active */
bool bt_mock_is_scanning(void)
{
    return mock_state.is_scanning;
}


/* Pairing / SSP helpers (these are used by bt_mock.c's higher-level wrappers) */
bt_pairing_state_t bt_mock_get_pairing_state(void) { return current_pairing_state; }
bt_pairing_method_t bt_mock_get_pairing_method(void) { return current_pairing_method; }

esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey)
{
    if (!is_pairing) return ESP_ERR_INVALID_STATE;
    s_ssp_passkey_value = passkey;
    snprintf(s_ssp_passkey, sizeof(s_ssp_passkey), "%06" PRIu32, passkey);
    current_pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
    s_ssp_confirmation_requested = true;
    current_pairing_method = BT_PAIRING_METHOD_SSP;
#ifdef ESP_PLATFORM
    /* Emit pairing CONFIRM event so the command interface / host can respond */
    char bda_str[18] = {0};
    if (current_pairing_addr[0]) {
        strncpy(bda_str, current_pairing_addr, sizeof(bda_str)-1);
    }
    char data[64];
    snprintf(data, sizeof(data), "%s,%u", bda_str, (unsigned int)passkey);
    cmd_send_event_pair("CONFIRM", data);
#endif
    return ESP_OK;
}

bool bt_mock_is_ssp_confirm_requested(void) { return s_ssp_confirmation_requested; }

esp_err_t bt_mock_confirm_ssp(bool confirm)
{
    if (!s_ssp_confirmation_requested) return ESP_OK;
    s_ssp_confirmation_requested = false;
    current_pairing_state = confirm ? BT_PAIRING_STATE_PAIRED : BT_PAIRING_STATE_FAILED;
#ifdef ESP_PLATFORM
    /* Emit pairing result so host sees SUCCESS/FAILED */
    char bda_str[18] = {0};
    if (current_pairing_addr[0]) {
        strncpy(bda_str, current_pairing_addr, sizeof(bda_str)-1);
    }
    cmd_send_event_pair(confirm ? "SUCCESS" : "FAILED", bda_str);
#endif
    return ESP_OK;
}

esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size)
{
    if (!passkey || size == 0) return ESP_ERR_INVALID_ARG;
    if (!s_ssp_confirmation_requested) return ESP_ERR_NOT_FOUND;
    /* copy up to size-1 bytes and NUL terminate */
    strncpy(passkey, s_ssp_passkey, size - 1);
    passkey[size - 1] = '\0';
    return ESP_OK;
}

esp_err_t bt_mock_start_pairing(const char* addr)
{
    if (!addr) return ESP_ERR_INVALID_ARG;
    strncpy(current_pairing_addr, addr, sizeof(current_pairing_addr)-1);
    current_pairing_addr[sizeof(current_pairing_addr)-1] = '\0';
    is_pairing = true;
    if (s_ssp_support_enabled) {
        current_pairing_method = BT_PAIRING_METHOD_SSP;
        current_pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
        s_ssp_passkey_value = 123456;
        snprintf(s_ssp_passkey, sizeof(s_ssp_passkey), "%06" PRIu32, s_ssp_passkey_value);
        s_ssp_confirmation_requested = true;
#ifdef ESP_PLATFORM
        /* Notify host about SSP confirm request */
        char data[64];
        snprintf(data, sizeof(data), "%s,%u", current_pairing_addr, (unsigned int)s_ssp_passkey_value);
    cmd_send_event_pair("CONFIRM", data);
#endif
    } else {
        current_pairing_method = BT_PAIRING_METHOD_PIN;
        current_pairing_state = BT_PAIRING_STATE_STARTED;
#ifdef ESP_PLATFORM
        /* Notify host about legacy PIN request */
    cmd_send_event_pair("PIN_REQUEST", current_pairing_addr);
#endif
    }
    return ESP_OK;
}

esp_err_t bt_mock_send_pin(const char* pin)
{
    if (!pin) return ESP_ERR_INVALID_ARG;
    if (!is_pairing) return ESP_OK; /* tests tolerate soft-OK */
    if (pin_failure_simulation) {
        current_pairing_state = BT_PAIRING_STATE_FAILED;
        pin_failure_simulation = false;
        return ESP_FAIL;
    }

    /* Mark pairing as successful */
    current_pairing_state = BT_PAIRING_STATE_PAIRED;

#ifdef ESP_PLATFORM
    /* Emit pairing success so host can observe the result */
        if (current_pairing_addr[0] != '\0') {
            cmd_send_event_pair("SUCCESS", current_pairing_addr);
        }
#endif

    /* If the device already exists in the device list, mark it paired. */
    if (current_pairing_addr[0] != '\0') {
        int idx = -1;
        if (bt_mock_find_device(current_pairing_addr, &idx) && idx >= 0 && idx < mock_state.device_count) {
            mock_state.devices[idx].paired = true;
        } else {
            /* If device not present, append a new paired device (preserve existing behavior) */
            if (mock_state.device_count < MAX_TEST_DEVICES) {
                bt_device_t *d = &mock_state.devices[mock_state.device_count];
                memset(d, 0, sizeof(*d));
                sscanf(current_pairing_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                       &d->addr[0], &d->addr[1], &d->addr[2], &d->addr[3], &d->addr[4], &d->addr[5]);
                snprintf(d->name, sizeof(d->name), "Paired Device %d", mock_state.device_count+1);
                d->paired = true; d->cod = 0x240404; d->rssi = -70;
                mock_state.device_count++;
            }
        }
    }

    return ESP_OK;
}

void bt_mock_simulate_pin_failure(void) { pin_failure_simulation = true; is_pairing = true; current_pairing_method = BT_PAIRING_METHOD_PIN; current_pairing_state = BT_PAIRING_STATE_PIN_REQUESTED; }

void bt_mock_set_ssp_supported(bool supported) { s_ssp_support_enabled = supported; if (!supported && is_pairing) { current_pairing_method = BT_PAIRING_METHOD_PIN; current_pairing_state = BT_PAIRING_STATE_STARTED; } }

/* Default PIN helpers -----------------------------------------------------*/
esp_err_t bt_mock_set_default_pin(const char* pin) {
    if (!pin) return ESP_ERR_INVALID_ARG;
    strncpy(s_default_pin, pin, sizeof(s_default_pin)-1);
    s_default_pin[sizeof(s_default_pin)-1] = '\0';
    return ESP_OK;
}

esp_err_t bt_mock_get_default_pin(char* pin, size_t size) {
    if (!pin) return ESP_ERR_INVALID_ARG;
    if (size <= strlen(s_default_pin)) return ESP_ERR_INVALID_SIZE;
    strncpy(pin, s_default_pin, size-1);
    pin[size-1] = '\0';
    return ESP_OK;
}

esp_err_t bt_mock_unpair_device(const char* addr) {
    if (!addr) return ESP_ERR_INVALID_ARG;

    char normalized_addr[18];
    strncpy(normalized_addr, addr, sizeof(normalized_addr)-1);
    normalized_addr[sizeof(normalized_addr)-1] = '\0';
    for (int i = 0; normalized_addr[i]; i++) {
        if (normalized_addr[i] >= 'a' && normalized_addr[i] <= 'f') {
            normalized_addr[i] = normalized_addr[i] - 'a' + 'A';
        }
    }

    bool found_any = false;
    int i = 0;
    while (i < mock_state.device_count) {
        char device_addr[18];
        sprintf(device_addr, "%02X:%02X:%02X:%02X:%02X:%02X",
                mock_state.devices[i].addr[0], mock_state.devices[i].addr[1], 
                mock_state.devices[i].addr[2], mock_state.devices[i].addr[3],
                mock_state.devices[i].addr[4], mock_state.devices[i].addr[5]);

        if (strcmp(device_addr, normalized_addr) == 0) {
            for (int j = i; j < mock_state.device_count - 1; j++) {
                memcpy(&mock_state.devices[j], &mock_state.devices[j+1], sizeof(bt_device_t));
            }
            mock_state.device_count--;
            found_any = true;
        } else {
            i++;
        }
    }

    return found_any ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t bt_mock_unpair_all_devices(void) {
    mock_state.device_count = 0;
    return ESP_OK;
}

uint16_t bt_mock_get_paired_device_count(void) {
    uint16_t count = 0;
    for (int i = 0; i < mock_state.device_count; i++) {
        if (mock_state.devices[i].paired) count++;
    }
    return count;
}

bool bt_mock_is_device_paired(const char* addr)
{
    if (!addr) return false;
    int idx = -1;
    if (bt_mock_find_device(addr, &idx)) {
        if (idx >= 0 && idx < mock_state.device_count) {
            return mock_state.devices[idx].paired;
        }
    }
    return false;
}

void bt_mock_print_device_list(void)
{
    ESP_LOGI(TAG, "Current device count: %d", mock_state.device_count);
    for (int i = 0; i < mock_state.device_count; i++) {
        char remaining_addr[18];
        sprintf(remaining_addr, "%02X:%02X:%02X:%02X:%02X:%02X",
                mock_state.devices[i].addr[0], mock_state.devices[i].addr[1], 
                mock_state.devices[i].addr[2], mock_state.devices[i].addr[3],
                mock_state.devices[i].addr[4], mock_state.devices[i].addr[5]);
        ESP_LOGI(TAG, "Remaining device %d: %s, paired: %d", 
                i, remaining_addr, mock_state.devices[i].paired);
    }
}

esp_err_t bt_mock_get_connected_addr(char* buf, size_t len)
{
    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    if (!mock_state.is_connected) return ESP_ERR_NOT_FOUND;
    strncpy(buf, mock_state.connected_addr, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
}

bt_streaming_state_t bt_mock_get_streaming_state(void)
{
    return mock_state.streaming_state;
}

/* Test helper: simulate a pairing timeout at the device level so that
 * bt_mock_get_pairing_state() reports BT_PAIRING_STATE_TIMEOUT and
 * is_pairing is cleared to match test expectations. This is called by
 * the higher-level bt_mock_simulate_pairing_timeout() wrapper.
 */
void bt_mock_devices_simulate_pairing_timeout(void)
{
    if (!is_pairing) {
        /* If nothing is pairing, just mark state as timeout anyway so tests
         * that expect a timeout immediately after triggering pairing will
         * observe the timeout state.
         */
        current_pairing_state = BT_PAIRING_STATE_TIMEOUT;
    } else {
        current_pairing_state = BT_PAIRING_STATE_TIMEOUT;
        is_pairing = false;
    }
    /* Clear SSP confirmation request flag if it was set */
    s_ssp_confirmation_requested = false;
    ESP_LOGI(TAG, "bt_mock_devices_simulate_pairing_timeout: pairing state set to TIMEOUT");
}

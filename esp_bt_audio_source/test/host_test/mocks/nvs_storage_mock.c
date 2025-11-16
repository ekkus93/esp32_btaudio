#include "nvs_storage.h"
#include "esp_err.h"
#include "esp_bt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

// Simple in-memory mock storage used only for host tests
static char s_default_pin[ESP_BT_PIN_CODE_LEN + 1] = {0};

esp_err_t nvs_storage_get_default_pin(char* buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    // copy up to buf_len-1 characters
    if (s_default_pin[0] == '\0') {
        // not set
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }
    strncpy(buf, s_default_pin, buf_len - 1);
    buf[buf_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t nvs_storage_set_default_pin(const char* pin)
{
    if (!pin) return ESP_ERR_INVALID_ARG;
    strncpy(s_default_pin, pin, sizeof(s_default_pin) - 1);
    s_default_pin[sizeof(s_default_pin) - 1] = '\0';
    return ESP_OK;
}

esp_err_t nvs_storage_set_device_name(const char* name)
{
    (void)name; // not needed for host tests
    return ESP_OK;
}

// In-memory paired devices mock (simulates binary blob storage)
#define MOCK_MAX_PAIRED 20
static uint8_t mock_paired_mac[MOCK_MAX_PAIRED][6];
static char mock_paired_name[MOCK_MAX_PAIRED][32];
static int mock_paired_count = 0;

static int parse_mac(const char* s, uint8_t out[6]) {
    if (!s || !out) return 0;
    int vals[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) == 6) {
        for (int i = 0; i < 6; i++) out[i] = (uint8_t)vals[i];
        return 1;
    }
    if ((int)strlen(s) == 12) {
        for (int i = 0; i < 6; i++) {
            char b[3] = { s[i*2], s[i*2+1], '\0' };
            char *endp = NULL;
            long v = strtol(b, &endp, 16);
            if (endp == b) return 0;
            out[i] = (uint8_t)v;
        }
        return 1;
    }
    return 0;
}

static void format_mac(const uint8_t mac[6], char* buf, size_t buf_len) {
    if (!buf || buf_len < 18) return;
    snprintf(buf, buf_len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t nvs_storage_get_paired_count(int* count) {
    if (!count) return ESP_ERR_INVALID_ARG;
    *count = mock_paired_count;
    return ESP_OK;
}

esp_err_t nvs_storage_get_paired_device_by_index(int index, char* mac, size_t mac_len, char* name, size_t name_len) {
    if (!mac || mac_len == 0) return ESP_ERR_INVALID_ARG;
    if (index < 0 || index >= mock_paired_count) return ESP_ERR_NOT_FOUND;
    format_mac(mock_paired_mac[index], mac, mac_len);
    if (name && name_len > 0) {
        strncpy(name, mock_paired_name[index], name_len - 1);
        name[name_len - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t nvs_storage_add_paired_device(const char* mac, const char* name) {
    if (!mac) return ESP_ERR_INVALID_ARG;
    uint8_t mac_bin[6];
    if (!parse_mac(mac, mac_bin)) return ESP_ERR_INVALID_ARG;
    // dedupe
    for (int i = 0; i < mock_paired_count; i++) {
        if (memcmp(mock_paired_mac[i], mac_bin, 6) == 0) return ESP_OK;
    }
    if (mock_paired_count >= MOCK_MAX_PAIRED) return ESP_ERR_NO_MEM;
    memcpy(mock_paired_mac[mock_paired_count], mac_bin, 6);
    if (name) strncpy(mock_paired_name[mock_paired_count], name, sizeof(mock_paired_name[mock_paired_count]) - 1);
    else mock_paired_name[mock_paired_count][0] = '\0';
    mock_paired_name[mock_paired_count][sizeof(mock_paired_name[mock_paired_count]) - 1] = '\0';
    mock_paired_count++;
    return ESP_OK;
}

esp_err_t nvs_storage_remove_paired_device(const char* mac) {
    if (!mac) return ESP_ERR_INVALID_ARG;
    uint8_t mac_bin[6];
    if (!parse_mac(mac, mac_bin)) return ESP_ERR_INVALID_ARG;
    int found = -1;
    for (int i = 0; i < mock_paired_count; i++) {
        if (memcmp(mock_paired_mac[i], mac_bin, 6) == 0) { found = i; break; }
    }
    if (found < 0) return ESP_ERR_NOT_FOUND;
    for (int i = found; i < mock_paired_count - 1; i++) {
        memcpy(mock_paired_mac[i], mock_paired_mac[i+1], 6);
        strcpy(mock_paired_name[i], mock_paired_name[i+1]);
    }
    mock_paired_count--;
    return ESP_OK;
}

esp_err_t nvs_storage_clear_paired_devices(void) {
    mock_paired_count = 0;
    return ESP_OK;
}

// Additional stubs used by production code when linking in host tests
esp_err_t nvs_storage_init(void)
{
    return ESP_OK;
}

esp_err_t nvs_storage_set_volume(uint8_t vol)
{
    (void)vol;
    return ESP_OK;
}

esp_err_t nvs_storage_set_i2s_pins(int bclk, int ws, int din, int dout)
{
    (void)bclk; (void)ws; (void)din; (void)dout;
    return ESP_OK;
}

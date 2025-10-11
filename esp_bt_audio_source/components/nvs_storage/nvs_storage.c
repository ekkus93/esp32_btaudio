#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "esp_log.h"
#include <ctype.h>
#include <stdio.h>

static const char* TAG = "nvs_storage";
/* TAG may be unused under some build configs; keep a quiet reference to avoid warnings */
static void __attribute__((unused)) _nvs_storage_suppress_unused_tag(void) { (void)TAG; }
static const char* NVS_NAMESPACE = "bt_audio_cfg";

esp_err_t nvs_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_storage_get_volume(uint8_t* volume)
{
    if (!volume) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    int32_t v = 0;
    err = nvs_get_i32(h, "volume", &v);
    if (err == ESP_OK) *volume = (uint8_t)v;
    nvs_close(h);
    return err == ESP_OK ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_storage_set_volume(uint8_t volume)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    int32_t v = volume;
    err = nvs_set_i32(h, "volume", v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_get_i2s_pins(int* bclk, int* ws, int* din, int* dout)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    int32_t tmp;
    if (bclk) {
        err = nvs_get_i32(h, "i2s_bclk", &tmp);
        if (err == ESP_OK) *bclk = (int)tmp; else *bclk = -1;
    }
    if (ws) {
        err = nvs_get_i32(h, "i2s_ws", &tmp);
        if (err == ESP_OK) *ws = (int)tmp; else *ws = -1;
    }
    if (din) {
        err = nvs_get_i32(h, "i2s_din", &tmp);
        if (err == ESP_OK) *din = (int)tmp; else *din = -1;
    }
    if (dout) {
        err = nvs_get_i32(h, "i2s_dout", &tmp);
        if (err == ESP_OK) *dout = (int)tmp; else *dout = -1;
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_storage_set_i2s_pins(int bclk, int ws, int din, int dout)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, "i2s_bclk", bclk);
    if (err == ESP_OK) err = nvs_set_i32(h, "i2s_ws", ws);
    if (err == ESP_OK) err = nvs_set_i32(h, "i2s_din", din);
    if (err == ESP_OK) err = nvs_set_i32(h, "i2s_dout", dout);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_get_device_name(char* buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required = buf_len;
    err = nvs_get_str(h, "device_name", buf, &required);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_set_device_name(const char* name)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "device_name", name);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_get_default_pin(char* buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required = buf_len;
    err = nvs_get_str(h, "default_pin", buf, &required);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_set_default_pin(const char* pin)
{
    if (!pin) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "default_pin", pin);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// Simple paired devices persistence: store count and entries by index
#define PAIRED_COUNT_KEY "paired_count"
#define PAIRED_MAC_KEY_FMT "paired_mac_%d"
#define PAIRED_NAME_KEY_FMT "paired_name_%d"

// Helpers to parse and format MAC addresses
static bool parse_mac_str(const char* str, uint8_t out[6]) {
    if (!str || !out) return false;
    int vals[6];
    // support formats like AA:BB:CC:DD:EE:FF or AABBCCDDEEFF
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) == 6) {
        for (int i = 0; i < 6; i++) out[i] = (uint8_t)vals[i];
        return true;
    }
    // try without separators
    if (strlen(str) == 12) {
        for (int i = 0; i < 6; i++) {
            char byte_str[3] = { str[i*2], str[i*2+1], '\0' };
            char *endptr = NULL;
            long v = strtol(byte_str, &endptr, 16);
            if (endptr == byte_str) return false;
            out[i] = (uint8_t)v;
        }
        return true;
    }
    return false;
}

static void format_mac_str(const uint8_t mac[6], char* buf, size_t buf_len) {
    if (!mac || !buf || buf_len < 18) { if (buf && buf_len>0) buf[0]='\0'; return; }
    snprintf(buf, buf_len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t nvs_storage_get_paired_count(int* count)
{
    if (!count) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    int32_t c = 0;
    err = nvs_get_i32(h, PAIRED_COUNT_KEY, &c);
    nvs_close(h);
    if (err == ESP_OK) {
        *count = (int)c;
        return ESP_OK;
    }
    *count = 0;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_storage_get_paired_device_by_index(int index, char* mac, size_t mac_len, char* name, size_t name_len)
{
    if (index < 0 || mac == NULL || mac_len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    char key[32];
    snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)index);
    // Read blob
    size_t required = 0;
    err = nvs_get_blob(h, key, NULL, &required);
    if (err != ESP_OK || required != 6) {
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t mac_bin[6];
    err = nvs_get_blob(h, key, mac_bin, &required);
    if (err != ESP_OK) { nvs_close(h); return err; }
    // format into mac string
    format_mac_str(mac_bin, mac, mac_len);
    if (name && name_len > 0) {
    snprintf(key, sizeof(key), PAIRED_NAME_KEY_FMT, (int)index);
        size_t nreq = name_len;
        if (nvs_get_str(h, key, name, &nreq) != ESP_OK) {
            name[0] = '\0';
        }
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_storage_add_paired_device(const char* mac, const char* name)
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    uint8_t mac_bin[6];
    if (!parse_mac_str(mac, mac_bin)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    int32_t c = 0;
    err = nvs_get_i32(h, PAIRED_COUNT_KEY, &c);
    if (err != ESP_OK) c = 0;
    // Check for duplicates (binary compare)
    char key[32];
    for (int i = 0; i < c; i++) {
        snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)i);
        size_t req = 0;
        if (nvs_get_blob(h, key, NULL, &req) == ESP_OK && req == 6) {
            uint8_t stored[6];
            if (nvs_get_blob(h, key, stored, &req) == ESP_OK) {
                if (memcmp(stored, mac_bin, 6) == 0) {
                    nvs_close(h);
                    return ESP_OK; // duplicate
                }
            }
        }
    }
    // Append at index c
    snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)c);
    err = nvs_set_blob(h, key, mac_bin, 6);
    if (err == ESP_OK && name) {
    snprintf(key, sizeof(key), PAIRED_NAME_KEY_FMT, (int)c);
        err = nvs_set_str(h, key, name);
    }
    if (err == ESP_OK) {
        c++;
        err = nvs_set_i32(h, PAIRED_COUNT_KEY, c);
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_remove_paired_device(const char* mac)
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    uint8_t mac_bin[6];
    if (!parse_mac_str(mac, mac_bin)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    int32_t c = 0;
    err = nvs_get_i32(h, PAIRED_COUNT_KEY, &c);
    if (err != ESP_OK || c <= 0) { nvs_close(h); return ESP_ERR_NOT_FOUND; }
    int found = -1;
    char key[32];
    for (int i = 0; i < c; i++) {
        snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)i);
        size_t req = 0;
        if (nvs_get_blob(h, key, NULL, &req) == ESP_OK && req == 6) {
            uint8_t stored[6];
            if (nvs_get_blob(h, key, stored, &req) == ESP_OK) {
                if (memcmp(stored, mac_bin, 6) == 0) { found = i; break; }
            }
        }
    }
    if (found < 0) { nvs_close(h); return ESP_ERR_NOT_FOUND; }
    // Shift remaining entries down
    for (int i = found; i < c - 1; i++) {
        uint8_t src_mac[6];
        char src_name[64];
        snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)(i+1));
        size_t req = 0;
        if (nvs_get_blob(h, key, NULL, &req) != ESP_OK || req != 6) {
            // erase dest
            snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)i);
            nvs_erase_key(h, key);
        } else {
            if (nvs_get_blob(h, key, src_mac, &req) != ESP_OK) { memset(src_mac,0,6); }
            snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, i);
            nvs_set_blob(h, key, src_mac, 6);
        }
        snprintf(key, sizeof(key), PAIRED_NAME_KEY_FMT, (int)(i+1));
        req = sizeof(src_name);
        if (nvs_get_str(h, key, src_name, &req) != ESP_OK) {
            snprintf(key, sizeof(key), PAIRED_NAME_KEY_FMT, (int)i);
            nvs_erase_key(h, key);
        } else {
            snprintf(key, sizeof(key), PAIRED_NAME_KEY_FMT, (int)i);
            nvs_set_str(h, key, src_name);
        }
    }
    // Erase last
    snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)(c-1));
    nvs_erase_key(h, key);
    snprintf(key, sizeof(key), PAIRED_NAME_KEY_FMT, (int)(c-1));
    nvs_erase_key(h, key);
    c--;
    nvs_set_i32(h, PAIRED_COUNT_KEY, c);
    if (c == 0) nvs_erase_key(h, PAIRED_COUNT_KEY);
    if ((err = nvs_commit(h)) != ESP_OK) { nvs_close(h); return err; }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_storage_clear_paired_devices(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    int32_t c = 0;
    if (nvs_get_i32(h, PAIRED_COUNT_KEY, &c) == ESP_OK) {
        char key[32];
        for (int i = 0; i < c; i++) {
            snprintf(key, sizeof(key), PAIRED_MAC_KEY_FMT, (int)i);
            nvs_erase_key(h, key);
            snprintf(key, sizeof(key), PAIRED_NAME_KEY_FMT, (int)i);
            nvs_erase_key(h, key);
        }
        nvs_erase_key(h, PAIRED_COUNT_KEY);
    }
    if ((err = nvs_commit(h)) != ESP_OK) { nvs_close(h); return err; }
    nvs_close(h);
    return ESP_OK;
}

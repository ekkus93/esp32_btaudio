#include "nvs_storage.h"
#include "esp_err.h"
#include "esp_bt.h"
#include <string.h>

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

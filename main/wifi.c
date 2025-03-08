#include "wifi.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>  // Add this for strncpy and memset
#include <esp_netif.h>  // Add this for esp_netif_init
#include "esp_event.h"  // Add this for event loop
#include "custom_log.h"

#define TAG "WIFI"
#define WIFI_NAMESPACE "wifi"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASS_KEY "password"

static char wifi_ssid[32] = {0};
static char wifi_password[64] = {0};

static esp_err_t wifi_save_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error setting SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, WIFI_PASS_KEY, password);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error setting password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

static esp_err_t wifi_load_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_str(nvs_handle, WIFI_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error getting SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, WIFI_PASS_KEY, password, &password_len);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error getting password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

static esp_err_t wifi_clear_stored_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(nvs_handle, WIFI_SSID_KEY);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error erasing SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_erase_key(nvs_handle, WIFI_PASS_KEY);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error erasing password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        SAFE_ESP_LOGI(TAG, "Disconnected from Wi-Fi, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        SAFE_ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Load Wi-Fi credentials from NVS
    size_t ssid_len = sizeof(wifi_ssid);
    size_t password_len = sizeof(wifi_password);
    ret = wifi_load_credentials(wifi_ssid, ssid_len, wifi_password, password_len);
    if (ret == ESP_OK) {
        SAFE_ESP_LOGI(TAG, "Loaded Wi-Fi credentials: SSID=%s", wifi_ssid);
        wifi_connect();
    } else {
        SAFE_ESP_LOGI(TAG, "No Wi-Fi credentials found");
    }

    return ESP_OK;
}

esp_err_t wifi_connect(void) {
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password));

    SAFE_ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", wifi_ssid);
    esp_err_t ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to set Wi-Fi configuration: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t wifi_disconnect(void) {
    SAFE_ESP_LOGI(TAG, "Disconnecting from Wi-Fi");
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    return ESP_OK;
}

esp_err_t wifi_set_credentials(const char *ssid, const char *password) {
    SAFE_ESP_LOGI(TAG, "Setting Wi-Fi credentials: SSID=%s, PASSWORD=%s", ssid, password);

    // Disconnect from Wi-Fi before setting new credentials
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGW(TAG, "Failed to disconnect Wi-Fi: %s", esp_err_to_name(ret));
        // Continue anyway - non-fatal error
    }

    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid));
    strncpy(wifi_password, password, sizeof(wifi_password));
    wifi_save_credentials(ssid, password);

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // Set WPA2 if password is provided
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN; // Set OPEN if no password
    }

    ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to set Wi-Fi configuration: %s", esp_err_to_name(ret));
        return ret;
    }

    // Attempt to connect to the new Wi-Fi network
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t wifi_clear_credentials(void) {
    memset(wifi_ssid, 0, sizeof(wifi_ssid));
    memset(wifi_password, 0, sizeof(wifi_password));
    return wifi_clear_stored_credentials();
}

esp_err_t wifi_get_ip(char *ip, size_t ip_len) {
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
        return ESP_OK;
    }
    return ESP_FAIL;
}

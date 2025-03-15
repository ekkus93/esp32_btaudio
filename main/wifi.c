/**
 * @file wifi.c
 * @brief Wi-Fi connectivity management module
 * 
 * This module handles Wi-Fi initialization, connection, credential storage,
 * and network status. It provides functions to connect/disconnect from 
 * Wi-Fi networks and stores credentials in non-volatile storage (NVS).
 */
#include "wifi.h"
#include "bluetooth/bt_app_global.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>  // Add this for strncpy and memset
#include <esp_netif.h>  // Add this for esp_netif_init
#include "esp_event.h"  // Add this for event loop
#include "custom_log.h"

#define TAG "WIFI"
#define WIFI_NAMESPACE "wifi"    // NVS namespace for storing Wi-Fi credentials
#define WIFI_SSID_KEY "ssid"     // NVS key for SSID
#define WIFI_PASS_KEY "password" // NVS key for password

/** Current Wi-Fi SSID and password stored in RAM */
static char wifi_ssid[32] = {0};
static char wifi_password[64] = {0};

/**
 * @brief Saves Wi-Fi credentials to NVS (Non-Volatile Storage)
 * 
 * This function stores the SSID and password in the device's permanent storage
 * so it can reconnect to the network after reboots.
 *
 * @param ssid Wi-Fi network name
 * @param password Wi-Fi password
 * @return ESP_OK on success or an error code on failure
 */
static esp_err_t wifi_save_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Store SSID in NVS
    err = nvs_set_str(nvs_handle, WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error setting SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Store password in NVS
    err = nvs_set_str(nvs_handle, WIFI_PASS_KEY, password);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error setting password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit the changes to NVS
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Loads Wi-Fi credentials from NVS
 * 
 * Retrieves previously saved network credentials from non-volatile storage.
 *
 * @param ssid Buffer to store the loaded SSID
 * @param ssid_len Size of SSID buffer
 * @param password Buffer to store the loaded password
 * @param password_len Size of password buffer
 * @return ESP_OK on success or an error code on failure
 */
static esp_err_t wifi_load_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Load SSID from NVS
    err = nvs_get_str(nvs_handle, WIFI_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error getting SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Load password from NVS
    err = nvs_get_str(nvs_handle, WIFI_PASS_KEY, password, &password_len);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error getting password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

/**
 * @brief Clears Wi-Fi credentials from NVS
 * 
 * Removes SSID and password entries from non-volatile storage.
 *
 * @return ESP_OK on success or an error code on failure
 */
static esp_err_t wifi_clear_stored_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Erase SSID key from NVS
    err = nvs_erase_key(nvs_handle, WIFI_SSID_KEY);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error erasing SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Erase password key from NVS
    err = nvs_erase_key(nvs_handle, WIFI_PASS_KEY);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error erasing password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes to NVS
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Event handler for Wi-Fi events
 * 
 * Handles key Wi-Fi events like connection status changes and IP assignment.
 * Automatically attempts reconnection when disconnected.
 *
 * @param arg User data (unused)
 * @param event_base Event base identifier (WIFI_EVENT or IP_EVENT)
 * @param event_id Specific event ID
 * @param event_data Event-specific data
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Wi-Fi station started, attempt connection
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Automatically attempt reconnection on disconnect
        SAFE_ESP_LOGI(TAG, "Disconnected from Wi-Fi, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Successfully obtained IP address
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        SAFE_ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief Initialize Wi-Fi subsystem
 * 
 * Sets up NVS storage, network interfaces, event loop, and Wi-Fi in station mode.
 * Attempts to load saved credentials and connect if available.
 *
 * @return ESP_OK on successful initialization
 */
esp_err_t wifi_init(void) {
    // Initialize NVS flash storage for Wi-Fi credentials
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize network interface and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers for Wi-Fi and IP events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Configure Wi-Fi settings and start in station mode
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

/**
 * @brief Connect to Wi-Fi using current credentials
 * 
 * Configures the Wi-Fi connection with stored SSID and password
 * and attempts to connect to the network.
 *
 * @return ESP_OK on successful connection initiation or error code
 */
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

/**
 * @brief Disconnect from current Wi-Fi network
 * 
 * @return ESP_OK on successful disconnection initiation
 */
esp_err_t wifi_disconnect(void) {
    SAFE_ESP_LOGI(TAG, "Disconnecting from Wi-Fi");
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    return ESP_OK;
}

/**
 * @brief Update and save new Wi-Fi credentials
 * 
 * Sets new SSID and password, saves them to NVS, and attempts
 * to connect with the new credentials.
 *
 * @param ssid Wi-Fi network name
 * @param password Wi-Fi password
 * @return ESP_OK on success or an error code on failure
 */
esp_err_t wifi_set_credentials(const char *ssid, const char *password) {
    SAFE_ESP_LOGI(TAG, "Setting Wi-Fi credentials: SSID=%s, PASSWORD=%s", ssid, password);

    // Disconnect from Wi-Fi before setting new credentials
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGW(TAG, "Failed to disconnect Wi-Fi: %s", esp_err_to_name(ret));
        // Continue anyway - non-fatal error
    }

    // Update credentials in RAM and save to NVS
    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid));
    strncpy(wifi_password, password, sizeof(wifi_password));
    wifi_save_credentials(ssid, password);

    // Prepare Wi-Fi configuration structure
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    // Set SSID and password in configuration
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // Set WPA2 if password is provided
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN; // Set OPEN if no password
    }

    // Apply the new configuration
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

/**
 * @brief Clear Wi-Fi credentials from RAM and NVS
 * 
 * @return ESP_OK on successful credential removal or error code
 */
esp_err_t wifi_clear_credentials(void) {
    // Clear credentials from RAM
    memset(wifi_ssid, 0, sizeof(wifi_ssid));
    memset(wifi_password, 0, sizeof(wifi_password));
    // Clear credentials from NVS
    return wifi_clear_stored_credentials();
}

/**
 * @brief Get the current IP address as a string
 * 
 * Retrieves the currently assigned IP address and formats it
 * as a human-readable string.
 *
 * @param ip Buffer to store the IP address string
 * @param ip_len Size of the IP buffer
 * @return ESP_OK if IP was retrieved successfully, ESP_FAIL otherwise
 */
esp_err_t wifi_get_ip(char *ip, size_t ip_len) {
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
        return ESP_OK;
    }
    return ESP_FAIL;
}

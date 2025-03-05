#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

// Set Wi-Fi credentials
esp_err_t wifi_set_credentials(const char *ssid, const char *password);

// Clear Wi-Fi credentials
esp_err_t wifi_clear_credentials(void);

// Connect to Wi-Fi
esp_err_t wifi_connect(void);

// Disconnect from Wi-Fi
esp_err_t wifi_disconnect(void);

// Get IP address
esp_err_t wifi_get_ip(char *ip, size_t ip_len);

// Initialize Wi-Fi
esp_err_t wifi_init(void);

#endif // WIFI_H

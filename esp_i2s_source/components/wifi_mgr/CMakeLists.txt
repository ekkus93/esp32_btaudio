# wifi_mgr — STA-with-NVS-creds + AP-mode provisioning fallback + mDNS (WIFI-1).
# wifi_sm.c is the pure fallback state machine (host-tested); wifi_mgr.c is the
# device glue (esp_wifi/esp_netif/mDNS, WIFI-1b).
idf_component_register(
    SRCS "wifi_sm.c" "wifi_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
    PRIV_REQUIRES espressif__mdns)

# wifi_mgr — STA-with-NVS-creds + AP-mode provisioning fallback + mDNS (WIFI-1).
# wifi_sm.c is the pure fallback state machine (host-tested); the device glue
# (esp_wifi/esp_netif/mDNS) lands in WIFI-1b.
idf_component_register(
    SRCS "wifi_sm.c"
    INCLUDE_DIRS "include")

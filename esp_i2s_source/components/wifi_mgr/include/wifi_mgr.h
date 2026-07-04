/*
 * wifi_mgr — WiFi state machine: NVS creds -> STA connect (retries) ->
 * fallback to AP mode (ESP32-S3-Audio / WPA2) when no creds or repeated
 * failure. mDNS esp-i2s-source.local in STA mode (SPEC §4).
 *
 * Implemented in WIFI-1. Skeleton only for now.
 */
#pragma once

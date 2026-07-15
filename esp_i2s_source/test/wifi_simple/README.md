# Minimal ESP32-S3 Wi-Fi Scan Test

This project proves that the ESP32-S3 application starts, initializes the
ESP-IDF Wi-Fi stack, starts station mode, and completes a blocking 2.4 GHz
access-point scan.

It intentionally does not connect to an AP. It requires no SSID/password,
DHCP, internet access, PSRAM, web server, I2S, or Bluetooth code. It reuses
the production partition layout so the diagnostic app does not overwrite the
production NVS region.

## Hardware

- ESP32-S3-WROOM-1 N16R8
- ESP-IDF v5.5.1
- Native USB Serial/JTAG console

## Clean build

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
cd esp_i2s_source/test/wifi_simple

rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32s3
idf.py build
./verify_build.py
```

The verifier must print:

```text
VERIFY|WIFI_SCAN_BUILD|PASS
```

## Flash and monitor

Flashing or erasing the board requires explicit user permission.

After permission:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Use the actual native USB Serial/JTAG device.

## Expected markers

```text
DIAG|WIFI_SCAN|APP_MAIN_ENTERED
DIAG|WIFI_SCAN|STEP|nvs_ok
DIAG|WIFI_SCAN|STEP|netif_ok
DIAG|WIFI_SCAN|STEP|event_loop_ok
DIAG|WIFI_SCAN|STEP|sta_netif_ok
DIAG|WIFI_SCAN|STEP|wifi_init_ok
DIAG|WIFI_SCAN|STEP|wifi_started
DIAG|WIFI_SCAN|PASS|total=...,returned=...
```

The test also logs up to 20 AP records with SSID, RSSI, channel, and auth mode.

## Interpretation

- Repeating ROM banner before `APP_MAIN_ENTERED`: boot/target/flash/config
  failure, not station connection logic.
- Failure after a specific step: investigate the next ESP-IDF API and preserve
  the exact error.
- `PASS` with zero APs: driver scan completed; repeat near a known active
  2.4 GHz AP before changing source.
- One or more plausible AP records: Wi-Fi RF scan path is working.

## Normal flash offsets for this repository

The smoke test intentionally reuses the production partition layout:

```text
0x0000   bootloader
0x8000   partition table
0x9000   NVS (size 0x10000)
0x19000  PHY data
0x20000  factory application
```

This prevents the diagnostic application from overwriting production NVS.
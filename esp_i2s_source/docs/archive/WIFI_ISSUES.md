# ESP32-S3 Wi-Fi Smoke-Test Incident

## Status

Replaced with blocking access-point scan diagnostic. Production Wi-Fi code
unchanged. Awaiting hardware validation.

## User-visible symptoms

The original `test/wifi_simple/` application flashed successfully but entered
a reboot loop. Boot ROM banner repeated, but no application console output
was visible via USB Serial/JTAG.

## Confirmed defects in the old diagnostic

### 1. No station connection was configured

The old code called:
```c
esp_wifi_set_mode(WIFI_MODE_STA);
esp_wifi_start();
```

But never:
- Created a default station netif (`esp_netif_create_default_wifi_sta()`)
- Configured SSID/password (`esp_wifi_set_config()`)
- Initiated connection (`esp_wifi_connect()`)

Therefore waiting for `IP_EVENT_STA_GOT_IP` was not a valid test outcome.
Even on healthy hardware, the test would wait indefinitely.

### 2. NVS handling was incorrect

The old code checked for `ESP_ERR_NVS_NOT_FOUND` (not the standard
`ESP_ERR_NVS_NEW_VERSION_FOUND`) and called `nvs_flash_init_partition("nvs")`.
The standard recovery pattern uses `nvs_flash_erase()` on error, which could
destroy production Wi-Fi credentials and station presets.

### 3. Initialization errors were ignored

The old code ignored return values from:
- `esp_netif_init()`
- `esp_event_loop_create_default()`
- `esp_event_handler_register()`

### 4. Partition analysis was incorrect

The old local `partitions.csv` had:
```csv
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x400000,
```

The blank fields are offsets (auto-assigned by ESP-IDF), and the hexadecimal
values are sizes:
- `0x6000` is 24 KiB NVS, not 36 KiB
- `0x1000` is 4 KiB PHY, not 60 KiB

With auto-assigned offsets, ESP-IDF places:
- NVS at 0x9000
- PHY at 0xF000
- Factory app at 0x10000

The old test did not enable `CONFIG_PARTITION_TABLE_CUSTOM`, so the CSV was
probably ignored and ESP-IDF's built-in single-app layout was used instead.

### 5. Flash offset was wrong

The old documentation listed `0x01000` as the app flash offset. The correct
offsets are:
- ESP-IDF built-in single-app layout: `0x10000`
- This repository's production layout: `0x20000`

Using `0x10000` overlaps the production NVS region (`0x9000`-`0x18FFF`) and
can destroy saved Wi-Fi credentials.

### 6. Event-loop deprecation statement was nonsensical

The old document stated:
> `esp_event_loop_create_default()` is deprecated in favor of `esp_event_loop_create_default()`

Both sides are the same API. Removed.

### 7. PSRAM configuration hazard

The old test enabled `CONFIG_SPIRAM=y` without specifying the ESP32-S3-WROOM-1
N16R8 module's octal PSRAM mode (`CONFIG_SPIRAM_MODE_OCT=y`). PSRAM startup
occurs before `app_main()`. A PSRAM mismatch can cause a reset before
application logs appear.

## Probable pre-app_main cause

The PSRAM configuration in `sdkconfig.defaults` is a strong hypothesis for
the pre-`app_main()` reboot. External RAM initialization happens before the
application task starts. If PSRAM fails to initialize, the system may reset
silently.

The corrected test removes PSRAM entirely, as it is irrelevant to scanning
for access points.

## What was not wrong

- USB Serial/JTAG console configuration was correct
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` routes system console to USB
- Explicit USB driver initialization is not required for logging
- ESP32-S3 hardware supports Wi-Fi scan operations

## Replacement diagnostic

The replacement test (`test/wifi_simple/`):
- Performs a single blocking 2.4 GHz access-point scan
- Requires no SSID/password, DHCP, or internet access
- Does not enable PSRAM
- Reuses production partition table (app at `0x20000`)
- Preserves NVS non-destructively
- Emits machine-readable diagnostic markers at each initialization step

See `../test/wifi_simple/README.md` for build and interpretation instructions.

## Clean build procedure

```bash
cd esp_i2s_source/test/wifi_simple
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32s3
idf.py build
./verify_build.py
```

## Static build verification

`verify_build.py` checks:
- Target is ESP32-S3
- Flash size is 16 MB
- USB Serial/JTAG console is selected
- PSRAM is not enabled
- Production partition table is selected (`../../partitions.csv`)
- Bootloader at `0x0000`
- Partition table at `0x8000`
- Application at `0x20000`

## Hardware results

Pending hardware validation.

## Remaining investigation

- Flash the corrected diagnostic and verify scan completion
- If `APP_MAIN_ENTERED` does not appear: investigate boot configuration, not
  Wi-Fi connection logic
- If scan completes with zero APs: repeat near a known active 2.4 GHz AP
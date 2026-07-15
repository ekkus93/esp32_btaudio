# ESP32-S3 Wi-Fi Smoke-Test Recovery Specification v1.0

**Repository baseline:** `esp32_btaudio-master_2607150238.zip`  
**Affected project:** `esp_i2s_source/`  
**Primary test project:** `esp_i2s_source/test/wifi_simple/`  
**Primary incident document:** `esp_i2s_source/docs/WIFI_ISSUES.md`  
**Target hardware:** ESP32-S3-WROOM-1 N16R8  
**Target ESP-IDF:** v5.5.1  
**Intended implementer:** OpenCode using local Qwen3.6 27B  
**Status:** Authoritative recovery specification  
**Excluded:** `archive/`, `esp_bt_audio_source/`, and unrelated reliability work

---

## 1. Purpose

The current `wifi_simple` diagnostic is not a valid minimal Wi-Fi test. It mixes an early-boot PSRAM configuration hazard with incomplete station-mode logic, misleading partition analysis, unchecked API calls, and speculative debugging steps.

This recovery must replace that diagnostic with a small, deterministic ESP32-S3 Wi-Fi scan application that answers these questions in order:

1. Did the application reach `app_main()`?
2. Did NVS initialize successfully?
3. Did `esp_netif` and the default event loop initialize?
4. Did the ESP32-S3 Wi-Fi driver initialize and start in station mode?
5. Did a blocking 2.4 GHz access-point scan complete?
6. Did the radio observe one or more nearby access points in an environment where APs are known to exist?

The recovery is complete when the test produces machine-readable evidence for each stage and no longer depends on credentials, DHCP, PSRAM, production application components, or speculative USB initialization. The test intentionally reuses the production partition layout so flashing the diagnostic does not overwrite the production NVS region.

This specification does **not** authorize a rewrite of the production Wi-Fi manager. The current repository comparison shows no material change to `esp_i2s_source/components/wifi_mgr/` between the previously reviewed July 13 snapshot and the July 15 snapshot. Repository history also records successful hardware validation of the production AP/STA path. The isolated test must establish a reproducible defect before production Wi-Fi code is changed.

---

## 2. Normative language

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

- **MUST / MUST NOT:** required for acceptance.
- **SHOULD:** expected unless a concrete technical reason is documented.
- **MAY:** optional.

A build succeeding is not proof that the application booted.  
A Wi-Fi driver starting is not proof that a station connected.  
A station scan completing is not proof that DHCP or internet access works.

---

## 3. Incident assessment

### 3.1 Definite defects in the current test

The following defects are present in the current source and do not require hardware confirmation.

#### 3.1.1 The test does not configure or initiate a station connection

The current test calls:

```c
esp_wifi_set_mode(WIFI_MODE_STA);
esp_wifi_start();
```

It does not call:

```c
esp_netif_create_default_wifi_sta();
esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
esp_wifi_connect();
```

It also supplies no SSID or password.

Therefore, waiting for `IP_EVENT_STA_GOT_IP` is not a valid test outcome. Even on healthy hardware, the current test can wait forever.

#### 3.1.2 The NVS handling is wrong and potentially destructive

The current code checks:

```c
ESP_ERR_NVS_NO_FREE_PAGES || ESP_ERR_NVS_NOT_FOUND
```

and retries with:

```c
nvs_flash_init_partition("nvs")
```

For a normal application, the common recoverable cases are:

```c
ESP_ERR_NVS_NO_FREE_PAGES
ESP_ERR_NVS_NEW_VERSION_FOUND
```

and the usual recovery is to erase and reinitialize the NVS partition.

That standard erase behavior is **not appropriate for this smoke test** because the diagnostic runs on the same board and NVS partition used by the production firmware. Automatic erase could destroy saved Wi-Fi credentials, station presets, and other settings.

The recovered smoke test MUST call `nvs_flash_init()` and fail visibly if it returns an error. It MUST NOT erase NVS automatically.

If NVS is already corrupted because the old test application was flashed over part of the production NVS region, recovery requires an explicit user decision because the remaining stored settings may be unrecoverable.

#### 3.1.3 Initialization errors are ignored

The current test ignores the return values of:

```c
esp_netif_init();
esp_event_loop_create_default();
esp_event_handler_register();
```

A smoke test MUST stop at the first failed prerequisite and expose the exact operation and error.

#### 3.1.4 The partition analysis is incorrect and the old test layout is unsafe

The CSV fields are:

```text
Name, Type, SubType, Offset, Size, Flags
```

In the old test CSV:

```csv
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x400000,
```

the blank fields are offsets and the hexadecimal values are sizes.

Therefore:

- `0x6000` is 24 KiB, not 36 KiB.
- `0x1000` is 4 KiB, not 60 KiB.
- Blank offsets are auto-assigned by ESP-IDF.
- That old table would place NVS at `0x9000`, PHY at `0xF000`, and the app at `0x10000`.

The test did not enable `CONFIG_PARTITION_TABLE_CUSTOM`, so the checked-in CSV was probably ignored and ESP-IDF's built-in single-app layout was used.

The production `esp_i2s_source/partitions.csv` intentionally has a larger NVS partition:

```text
NVS:      0x9000  size 0x10000
PHY:      0x19000 size 0x1000
factory:  0x20000
```

Therefore, flashing the old test application at `0x10000` can overwrite the upper portion of the production NVS region (`0x10000` through `0x18FFF`). This can destroy saved Wi-Fi credentials and station data.

The recovered smoke test MUST explicitly reuse the production partition table so its application is flashed at `0x20000` and the production NVS bytes are not overwritten.

#### 3.1.5 The documented application flash offset is wrong

`WIFI_ISSUES.md` lists:

```text
0x01000 - wifi_simple_test.bin
```

That value is invalid for the application.

There are two relevant correct offsets:

- ESP-IDF built-in single-app layout: `0x10000`.
- This repository's production-preserving layout: `0x20000`.

The recovered smoke test MUST use `0x20000` because it reuses the production partition table. It MUST NOT use `0x01000` or `0x10000`.

#### 3.1.6 The event-loop deprecation note is nonsensical

The current document states:

> `esp_event_loop_create_default()` is deprecated in favor of `esp_event_loop_create_default()`.

Both sides are the same API. This statement MUST be removed.

#### 3.1.7 Explicit USB Serial/JTAG driver initialization is not required for logging

Selecting:

```ini
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

routes the system console to the USB Serial/JTAG controller. A basic logging test does not need to install the application-level `usb_serial_jtag` driver.

The production console component may use that driver for interactive input. That is unrelated to the smoke test.

### 3.2 Probable pre-`app_main()` failure source

The current test enables:

```ini
CONFIG_SPIRAM=y
```

but does not specify the ESP32-S3-WROOM-1 N16R8 module's octal PSRAM mode:

```ini
CONFIG_SPIRAM_MODE_OCT=y
```

PSRAM startup occurs before `app_main()`. A PSRAM hardware/configuration mismatch can therefore cause a reset before the first application log.

This is a strong hypothesis, not a hardware-confirmed root cause. A stale generated `sdkconfig`, an incorrect target, an incorrect flash offset, or another boot configuration problem can produce a similar symptom.

The correct smoke-test response is not to add more PSRAM options. It is to remove PSRAM from the test entirely because external RAM is irrelevant to scanning for access points.

### 3.3 Configuration persistence hazard

`sdkconfig.defaults` supplies defaults only when generating configuration. It does not override values already present in a generated `sdkconfig`.

Every clean diagnostic run MUST remove at least:

```text
build/
sdkconfig
sdkconfig.old
```

before regenerating the test configuration.

---

## 4. Locked recovery decisions

### 4.1 The test is a scan test, not a connection test

The recovered test MUST use a blocking access-point scan.

It MUST NOT require:

- an SSID;
- a password;
- `esp_wifi_connect()`;
- DHCP;
- IP events;
- internet connectivity;
- mDNS;
- the production `wifi_mgr`;
- the web server;
- radio streaming;
- I2S;
- Bluetooth;
- PSRAM.

A separate credentialed station-connect test MAY be added later, but it is outside this recovery.

### 4.2 No PSRAM

The smoke-test `sdkconfig.defaults` MUST NOT enable:

```ini
CONFIG_SPIRAM
CONFIG_SPIRAM_MODE_OCT
CONFIG_SPIRAM_SPEED_80M
```

The test must fit entirely in internal RAM. If it does not, the test is no longer minimal.

### 4.3 Preserve the production partition layout

The smoke test MUST explicitly reuse:

```text
esp_i2s_source/partitions.csv
```

from the test project using:

```ini
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../partitions.csv"
```

This is a flash-safety requirement, not a production dependency in application code.

The misleading local file:

```text
esp_i2s_source/test/wifi_simple/partitions.csv
```

MUST be removed.

The generated layout MUST preserve:

- NVS at `0x9000`, size `0x10000`;
- PHY data at `0x19000`, size `0x1000`;
- factory application at `0x20000`.

The test MUST NOT invent a second partition layout, shrink NVS, or place its application at `0x10000`.

### 4.4 Correct board identity without unnecessary peripherals

The test configuration MUST declare:

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

The flash-size setting identifies the physical module. It does not require a 16 MB application partition.

### 4.5 Fatal initialization semantics

Every required ESP-IDF call MUST have checked error handling.

For this diagnostic, `ESP_ERROR_CHECK()` is acceptable because:

- the application is a development smoke test;
- the first failed prerequisite should stop execution;
- ESP-IDF will print the failing error and location;
- continuing after a failed prerequisite would make later output misleading.

### 4.6 Machine-readable diagnostics

The application MUST emit these markers in order:

```text
DIAG|WIFI_SCAN|APP_MAIN_ENTERED
DIAG|WIFI_SCAN|STEP|nvs_ok
DIAG|WIFI_SCAN|STEP|netif_ok
DIAG|WIFI_SCAN|STEP|event_loop_ok
DIAG|WIFI_SCAN|STEP|sta_netif_ok
DIAG|WIFI_SCAN|STEP|wifi_init_ok
DIAG|WIFI_SCAN|STEP|wifi_started
DIAG|WIFI_SCAN|PASS|total=<n>,returned=<n>
```

Use `printf()` followed by `fflush(stdout)` for the earliest marker so the result is visible independently of log-tag filtering.

The application MAY also print human-readable `ESP_LOGI()` messages and AP records.

### 4.7 One scan is sufficient

The application MUST perform one blocking scan and then return from `app_main()`.

Repeated scanning is not necessary for this gate and can be added only as a separate soak mode.

### 4.8 Production Wi-Fi code is locked during isolation

Until the smoke test passes or produces a reproducible ESP-IDF Wi-Fi-driver failure:

- `esp_i2s_source/components/wifi_mgr/` MUST NOT be edited.
- production `esp_i2s_source/sdkconfig.defaults` MUST NOT be edited.
- production `esp_i2s_source/partitions.csv` MUST NOT be edited; it is referenced read-only by the smoke test.
- production boot order MUST NOT be edited.
- web provisioning and AP/STA behavior MUST NOT be edited.

A local model MUST NOT use the diagnostic incident as justification for a broad production Wi-Fi refactor.

### 4.9 Hardware writes require permission

OpenCode MAY run builds, static checks, diffs, and file inspections.

It MUST NOT run:

```text
idf.py flash
idf.py erase-flash
esptool.py write_flash
esptool.py erase_flash
```

or otherwise write to the attached board until the user explicitly approves the hardware action in the current interaction.

---

## 5. Required test-project contents

The final directory MUST contain:

```text
esp_i2s_source/test/wifi_simple/
├── CMakeLists.txt
├── README.md
├── sdkconfig.defaults
├── verify_build.py
└── main/
    ├── CMakeLists.txt
    └── simple_wifi_test.c
```

It MUST NOT contain:

```text
partitions.csv
```

Generated files such as `sdkconfig` and `build/` MUST remain untracked.

---

## 6. Required CMake design

### 6.1 Project CMake file

`esp_i2s_source/test/wifi_simple/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifi_simple_test)
```

### 6.2 Main component CMake file

`esp_i2s_source/test/wifi_simple/main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "simple_wifi_test.c"
    PRIV_REQUIRES
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
    INCLUDE_DIRS "."
)
```

Dependencies MUST be explicit. Do not rely on a transitive dependency to expose `esp_event`.

---

## 7. Required configuration

`esp_i2s_source/test/wifi_simple/sdkconfig.defaults` MUST be:

```ini
# ESP32-S3-WROOM-1 N16R8 hardware identity.
CONFIG_IDF_TARGET="esp32s3"

# Match the physical module flash size.
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"

# Native USB Serial/JTAG console.
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# Deliberately do not enable external PSRAM.

# Reuse the production partition layout so the diagnostic application is
# flashed at 0x20000 and does not overwrite the production NVS region.
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../partitions.csv"
```

Do not add production features to this file.

---

## 8. Required application behavior

### 8.1 Non-destructive NVS initialization

Use:

```c
static void init_nvs_non_destructive(void)
{
    esp_err_t err = nvs_flash_init();

    if (err != ESP_OK) {
        printf("DIAG|WIFI_SCAN|FAIL|step=nvs_init,err=%s
",
               esp_err_to_name(err));
        fflush(stdout);
        ESP_ERROR_CHECK(err);
    }
}
```

The smoke test MUST NOT call:

```c
nvs_flash_erase();
```

If NVS cannot initialize, stop and preserve the exact error. Do not destroy production settings without an explicit user decision.

### 8.2 Network and Wi-Fi initialization

The required sequence is:

```c
ESP_ERROR_CHECK(esp_netif_init());
ESP_ERROR_CHECK(esp_event_loop_create_default());

esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
assert(sta_netif != NULL);

wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_wifi_init(&cfg));
ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
ESP_ERROR_CHECK(esp_wifi_start());
```

`WIFI_STORAGE_RAM` prevents persistent Wi-Fi station configuration from becoming hidden test input. The production NVS partition remains mounted non-destructively because Wi-Fi/PHY initialization may use it.

### 8.3 Blocking scan

The required scan sequence is:

```c
ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&total_found));
ESP_ERROR_CHECK(
    esp_wifi_scan_get_ap_records(&records_to_read, records));
```

Calling `esp_wifi_scan_get_ap_records()` is required so the Wi-Fi driver can release the scan result storage.

### 8.4 Result semantics

Two result levels are defined:

#### Driver/API pass

The following marker means the ESP-IDF driver initialized, started, and completed a scan:

```text
DIAG|WIFI_SCAN|PASS|total=<n>,returned=<n>
```

A scan returning zero APs is still an API pass.

#### RF/environment pass

In a location where at least one known 2.4 GHz access point is active, at least one AP record SHOULD be returned.

If the API passes but zero APs are observed:

1. verify the known AP has 2.4 GHz enabled;
2. move the board near the AP;
3. verify the antenna/module is not shielded or damaged;
4. repeat the exact same firmware before changing code.

Do not convert a zero-result scan into a station-mode rewrite.

---

## 9. Build verification requirements

### 9.1 Clean configuration

The build procedure MUST begin with:

```bash
cd esp_i2s_source/test/wifi_simple
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32s3
idf.py build
```

### 9.2 Configuration assertions

After build, verify:

```text
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_SPIRAM is not enabled
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../partitions.csv"
```

### 9.3 Flash-layout assertions

The generated flash arguments MUST show:

```text
0x0      bootloader/bootloader.bin
0x8000   partition_table/partition-table.bin
0x20000  wifi_simple_test.bin
```

Path spelling may vary, but the offsets and artifact roles MUST match.

### 9.4 Automated verifier

`verify_build.py` MUST inspect the generated `sdkconfig` and flash arguments. It MUST fail nonzero when:

- target is not ESP32-S3;
- flash size is not 16 MB;
- USB Serial/JTAG console is not selected;
- PSRAM is enabled;
- the production partition table is not selected;
- the configured custom partition filename is not `../../partitions.csv`;
- the bootloader, partition table, or application offset is wrong;
- build artifacts are missing.

---

## 10. Hardware acceptance

### 10.1 Gate A: application startup

Pass requires:

```text
DIAG|WIFI_SCAN|APP_MAIN_ENTERED
```

If this marker does not appear and the ROM banner repeats, the failure is before `app_main()` and MUST be investigated as target, boot, flash, generated configuration, reset, or hardware—not as a Wi-Fi connection bug.

### 10.2 Gate B: Wi-Fi initialization

Pass requires all step markers through:

```text
DIAG|WIFI_SCAN|STEP|wifi_started
```

A panic after one step identifies the next failed subsystem.

### 10.3 Gate C: scan completion

Pass requires:

```text
DIAG|WIFI_SCAN|PASS|total=<n>,returned=<n>
```

### 10.4 Gate D: RF observation

Where known 2.4 GHz APs are present, at least one AP record SHOULD appear with a plausible channel and RSSI.

### 10.5 Reboot behavior

The test MUST not enter an automatic reboot loop.

One reset caused by flashing is expected. Repeated ROM banners or watchdog/panic resets are a failure.

---

## 11. Deterministic failure ladder

OpenCode MUST follow this order and stop once the cause is found.

### 11.1 Build fails

- Fix only compiler, dependency, or Kconfig errors in the smoke test.
- Do not modify production components.
- Re-run a clean build.

### 11.2 Build passes but verifier fails

- Correct the generated configuration or flash layout.
- Delete stale `sdkconfig`.
- Do not flash until the verifier passes.

### 11.3 No `APP_MAIN_ENTERED`

Inspect, in order:

1. Confirm `verify_build.py` passes.
2. Confirm `build/flasher_args.json` places the app at `0x20000`.
3. Confirm `sdkconfig` has no `CONFIG_SPIRAM=y`.
4. Confirm target is `esp32s3`.
5. Capture the complete monitor output, including reset reason and panic text.
6. Confirm the monitor is attached to the native USB Serial/JTAG port.
7. Do not perform a full erase as a default diagnostic step. If NVS or stale flash must be erased, obtain explicit user permission and erase only the necessary region when practical.
8. If still failing, build/flash an unmodified ESP-IDF `hello_world` for the same target and console.
9. Investigate power, cable, USB enumeration, and board hardware.

Do not add explicit USB driver initialization.  
Do not add PHY partition code.  
Do not add Wi-Fi credentials.  
Do not modify `wifi_mgr`.

### 11.4 `APP_MAIN_ENTERED` appears but an initialization step fails

Use the exact failing API and ESP-IDF error name.

- NVS failure: stop and preserve the exact error. Do not erase the shared production NVS automatically. Explain that the old test may already have overwritten part of it and ask before any destructive recovery.
- `esp_netif` or event loop failure: inspect duplicate initialization only within the test.
- `esp_wifi_init` failure: capture error and heap information.
- `esp_wifi_start` failure: capture exact error; do not claim station connection failure.
- scan failure: capture exact scan error.

### 11.5 Scan passes but finds zero APs

Treat this as an RF/environment question, not a boot or connection question.

Repeat near a known 2.4 GHz AP using the exact same binary before changing source.

---

## 12. Documentation requirements

`esp_i2s_source/docs/WIFI_ISSUES.md` MUST be rewritten as a corrected incident/postmortem document.

It MUST:

- distinguish confirmed defects from hypotheses;
- state that the old test never called `esp_wifi_connect()`;
- explain that the replacement is a scan test;
- correct partition sizes and offsets;
- correct the factory app offset;
- remove the fake event-loop deprecation statement;
- remove explicit USB initialization as a proposed fix;
- explain stale `sdkconfig` behavior;
- warn that the old app-at-`0x10000` layout could overwrite the upper portion of production NVS;
- preserve actual hardware results after the corrected test is run;
- include the exact tested commit and ESP-IDF version when available.

It MUST NOT claim that PSRAM was the confirmed cause until the corrected no-PSRAM build is tested on hardware.

---

## 13. Non-goals

This recovery MUST NOT:

- redesign the production Wi-Fi state machine;
- change AP provisioning;
- add credentials to source control;
- add a hard-coded SSID/password;
- test 5 GHz Wi-Fi, which ESP32-S3 does not support;
- add OTA, mDNS, HTTP, WebSocket, I2S, Bluetooth, or decoder dependencies;
- change production partition sizes;
- enable PSRAM “to match the board” in the smoke test;
- suppress panic/reboot output;
- catch errors and continue with a partially initialized stack;
- report success merely because `idf.py build` or `idf.py flash` exited zero.

---

## 14. Definition of done

The work is complete only when all of the following are true:

- [ ] `test/wifi_simple/partitions.csv` is removed.
- [ ] The test does not enable PSRAM.
- [ ] The test explicitly reuses the production partition table.
- [ ] The test builds from a deleted `sdkconfig`.
- [ ] `verify_build.py` passes.
- [ ] The generated application offset is `0x20000`.
- [ ] The application emits `APP_MAIN_ENTERED`.
- [ ] All initialization step markers appear.
- [ ] A blocking scan completes.
- [ ] AP records are printed when known 2.4 GHz APs are available.
- [ ] No repeated reboot occurs.
- [ ] `WIFI_ISSUES.md` is corrected.
- [ ] Production `wifi_mgr` remains unchanged unless separate evidence proves a defect.
- [ ] The full `esp_i2s_source` project still builds after documentation/test changes.
- [ ] Hardware evidence is recorded without fabricated timestamps.
- [ ] No hardware flash/erase was performed without user permission.

---

## 15. Authoritative references

Use the project-installed ESP-IDF v5.5.1 headers and examples as the final API authority.

Relevant Espressif documentation:

- Wi-Fi scan example:  
  `https://github.com/espressif/esp-idf/tree/v5.5.1/examples/wifi/scan`
- ESP32-S3 Wi-Fi driver guide:  
  `https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/wifi.html`
- ESP32-S3 partition tables:  
  `https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/partition-tables.html`
- ESP32-S3 flash and PSRAM configuration:  
  `https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/flash_psram_config.html`
- ESP-IDF configuration file behavior:  
  `https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/kconfig/configuration_structure.html`
- USB Serial/JTAG console:  
  `https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/usb-serial-jtag-console.html`

# ESP32-S3 Wi-Fi Smoke-Test Recovery TODO v1.0

**Companion specification:** `ESP32_S3_WIFI_RECOVERY_SPEC_V1.md`  
**Repository baseline:** `esp32_btaudio-master_2607150238.zip`  
**Target:** `esp_i2s_source/test/wifi_simple/`  
**Implementer:** OpenCode with local Qwen3.6 27B  
**ESP-IDF:** v5.5.1

---

## 0. Instructions to OpenCode

Read the entire companion specification before editing.

This is a small diagnostic recovery, not a production Wi-Fi rewrite.

### Hard rules

1. **Do not edit `esp_i2s_source/components/wifi_mgr/` during this work.**
2. **Do not edit production `esp_i2s_source/sdkconfig.defaults` or `partitions.csv`.**
3. **Do not enable PSRAM in the smoke test.**
4. **Reuse the production partition table; do not invent an independent test layout.**
5. **Do not add SSID/password credentials or call `esp_wifi_connect()`.**
6. **Do not install the `usb_serial_jtag` driver for ordinary logging.**
7. **Do not hide or downgrade ESP-IDF errors.**
8. **Do not run flash or erase commands without explicit user permission.**
9. **Do not touch `archive/`.**
10. **Do not perform broad formatting or unrelated refactors.**
11. After every task, inspect `git diff --check` and the scoped diff.
12. After any device-code change, run `idf.py build`.
13. Preserve user changes. Never use destructive Git reset/checkout commands to discard work.
14. Do not fabricate hardware results or timestamps.

### Required working style

- Implement one task at a time.
- Use the supplied full-file snippets rather than improvising variants.
- When a supplied symbol differs from installed ESP-IDF v5.5.1 headers, inspect the headers and make the smallest necessary correction.
- Record exact command output for failed gates.
- Stop at the first unexplained failure. Do not stack speculative fixes.

---

## 1. Dependency graph

```text
WIFI-FIX-00
    |
    +--> WIFI-FIX-01 --> WIFI-FIX-02 --> WIFI-FIX-03 --> WIFI-FIX-04
                                               |              |
                                               +--------------+
                                                      |
                                                WIFI-FIX-05
                                                      |
                                                WIFI-FIX-06
                                                      |
                                                WIFI-FIX-07
                                                      |
                                                WIFI-FIX-08
                                                      |
                                                WIFI-FIX-09
                                                      |
                                                WIFI-FIX-10
```

- `WIFI-FIX-00`: establish and preserve baseline.
- `WIFI-FIX-01` through `04`: replace the malformed smoke test.
- `WIFI-FIX-05`: add deterministic build validation.
- `WIFI-FIX-06`: correct documentation.
- `WIFI-FIX-07`: clean build and static acceptance.
- `WIFI-FIX-08`: hardware scan gate, only with permission.
- `WIFI-FIX-09`: build the full production project without modifying production Wi-Fi.
- `WIFI-FIX-10`: final evidence and project-history entry.

---

# Phase 1 — Preserve baseline and remove misleading inputs

## WIFI-FIX-00 — Establish a clean, scoped baseline [P0]

### Scope

Repository root and:

```text
esp_i2s_source/test/wifi_simple/
esp_i2s_source/docs/WIFI_ISSUES.md
```

### Actions

- [ ] Run:

```bash
git status --short
git diff -- esp_i2s_source/test/wifi_simple \
              esp_i2s_source/docs/WIFI_ISSUES.md
```

- [ ] Record any pre-existing uncommitted changes.
- [ ] Do not overwrite unrelated user changes.
- [ ] Confirm ESP-IDF:

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
idf.py --version
```

- [ ] Confirm no implementation task in this plan requires changing:

```text
esp_i2s_source/components/wifi_mgr/
esp_i2s_source/main/main.c
esp_i2s_source/sdkconfig.defaults
esp_i2s_source/partitions.csv
```

- [ ] If those production files are already modified by the user, preserve them and report them separately. Do not fold them into this task.

### Acceptance

- [ ] Baseline state is recorded.
- [ ] The edit scope is explicit.
- [ ] No production file has been changed.

---

## WIFI-FIX-01 — Remove the misleading local partition file [P0]

### File

```text
esp_i2s_source/test/wifi_simple/partitions.csv
```

### Actions

- [ ] Delete the local test CSV.
- [ ] Confirm the production table remains:

```text
esp_i2s_source/partitions.csv
```

- [ ] Record the production-preserving layout:

```text
nvs       0x9000   size 0x10000
phy_init  0x19000  size 0x1000
factory   0x20000
```

- [ ] Search references:

```bash
grep -RIn \
  -e 'PARTITION_TABLE_CUSTOM' \
  -e 'partitions.csv' \
  esp_i2s_source/test/wifi_simple || true
```

The next task will intentionally reference `../../partitions.csv`.

### Reason

The old local CSV was not selected and led to incorrect analysis. More importantly, using ESP-IDF's built-in app offset `0x10000` would overlap the production NVS region. The smoke test must use the production partition layout and app offset `0x20000`.

### Acceptance

- [ ] The local test `partitions.csv` is gone.
- [ ] Production `esp_i2s_source/partitions.csv` is unchanged.
- [ ] The required safe app offset is documented as `0x20000`.

## WIFI-FIX-02 — Replace smoke-test configuration [P0]

### File

```text
esp_i2s_source/test/wifi_simple/sdkconfig.defaults
```

### Replace the entire file with

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

### Do not add

```ini
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION=y
```

### Acceptance

- [ ] Target is ESP32-S3.
- [ ] Flash size is 16 MB.
- [ ] Console is USB Serial/JTAG.
- [ ] PSRAM is absent.
- [ ] Production partition table is selected through `../../partitions.csv`.
- [ ] No local duplicate partition CSV exists.

# Phase 2 — Replace the invalid connection test with a scan test

## WIFI-FIX-03 — Correct CMake dependencies [P0]

### File 1

`esp_i2s_source/test/wifi_simple/CMakeLists.txt`

### Replace with

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifi_simple_test)
```

### File 2

`esp_i2s_source/test/wifi_simple/main/CMakeLists.txt`

### Replace with

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

### Important

Do not add the production Wi-Fi manager as a dependency.  
Do not add `usb_serial_jtag`.  
Do not add mDNS, HTTP, I2S, decoder, Bluetooth, or PSRAM components.

### Acceptance

- [ ] `esp_event` is explicit.
- [ ] Only required ESP-IDF components are linked.
- [ ] Project remains independent of production components.

---

## WIFI-FIX-04 — Replace `simple_wifi_test.c` completely [P0]

### File

```text
esp_i2s_source/test/wifi_simple/main/simple_wifi_test.c
```

### Replace the entire file with

```c
/*
 * Minimal ESP32-S3 Wi-Fi hardware smoke test.
 *
 * This performs one blocking access-point scan. It intentionally does not
 * configure credentials, connect to an AP, request DHCP, initialize PSRAM, or
 * use any production esp_i2s_source component.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define MAX_AP_RECORDS 20

static const char *TAG = "WIFI_SCAN_TEST";

static void diag_step(const char *step)
{
    printf("DIAG|WIFI_SCAN|STEP|%s\n", step);
    fflush(stdout);
}

static void init_nvs_non_destructive(void)
{
    esp_err_t err = nvs_flash_init();

    if (err != ESP_OK) {
        printf("DIAG|WIFI_SCAN|FAIL|step=nvs_init,err=%s\n",
               esp_err_to_name(err));
        fflush(stdout);
        ESP_ERROR_CHECK(err);
    }
}

static void run_scan(void)
{
    wifi_ap_record_t records[MAX_AP_RECORDS];
    memset(records, 0, sizeof(records));

    uint16_t records_to_read = MAX_AP_RECORDS;
    uint16_t total_found = 0;

    ESP_LOGI(TAG, "Starting one blocking 2.4 GHz AP scan");

    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&total_found));
    ESP_ERROR_CHECK(
        esp_wifi_scan_get_ap_records(&records_to_read, records));

    ESP_LOGI(TAG,
             "Scan complete: total=%u returned=%u",
             (unsigned)total_found,
             (unsigned)records_to_read);

    for (uint16_t i = 0; i < records_to_read; ++i) {
        ESP_LOGI(TAG,
                 "[%02u] ssid=\"%s\" rssi=%d channel=%u auth=%d",
                 (unsigned)i,
                 (const char *)records[i].ssid,
                 (int)records[i].rssi,
                 (unsigned)records[i].primary,
                 (int)records[i].authmode);
    }

    printf("DIAG|WIFI_SCAN|PASS|total=%u,returned=%u\n",
           (unsigned)total_found,
           (unsigned)records_to_read);
    fflush(stdout);
}

void app_main(void)
{
    printf("\nDIAG|WIFI_SCAN|APP_MAIN_ENTERED\n");
    fflush(stdout);

    init_nvs_non_destructive();
    diag_step("nvs_ok");

    ESP_ERROR_CHECK(esp_netif_init());
    diag_step("netif_ok");

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    diag_step("event_loop_ok");

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif != NULL);
    diag_step("sta_netif_ok");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    diag_step("wifi_init_ok");

    /*
     * Keep station configuration in RAM. This prevents credentials or station
     * settings left by another firmware image from becoming hidden test input.
     */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    diag_step("wifi_started");

    run_scan();

    /*
     * Returning from app_main is valid in ESP-IDF. The application task is
     * deleted after the result is printed.
     */
}
```

### Forbidden regressions

Do not add:

```c
static bool s_got_ip;
IP_EVENT_STA_GOT_IP;
esp_wifi_set_config(...);
esp_wifi_connect();
while (!s_got_ip) { ... }
```

Those belong in a separate credentialed station test.

Do not catch an initialization error and continue.

### Acceptance

- [ ] `APP_MAIN_ENTERED` is the first application marker.
- [ ] Every prerequisite emits a step marker only after success.
- [ ] The test performs exactly one blocking scan.
- [ ] Scan records are retrieved after scanning.
- [ ] The test has no credentials or connection loop.
- [ ] No production headers are included.

---

# Phase 3 — Deterministic build verification

## WIFI-FIX-05 — Add `verify_build.py` [P0]

### File

```text
esp_i2s_source/test/wifi_simple/verify_build.py
```

### Create with

```python
#!/usr/bin/env python3
\"\"\"Verify the generated ESP-IDF configuration and flash layout.\"\"\"

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SDKCONFIG = ROOT / "sdkconfig"
BUILD = ROOT / "build"


class VerificationError(RuntimeError):
    pass


def parse_sdkconfig(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise VerificationError(
            f"missing {path}; run a clean idf.py build first"
        )

    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value.strip().strip('"')
    return values


def require_config(
    values: dict[str, str],
    key: str,
    expected: str,
) -> None:
    actual = values.get(key)
    if actual != expected:
        raise VerificationError(
            f"{key}: expected {expected!r}, got {actual!r}"
        )


def reject_enabled(values: dict[str, str], key: str) -> None:
    if values.get(key) == "y":
        raise VerificationError(f"{key} must not be enabled")


def parse_flash_files() -> dict[int, str]:
    json_path = BUILD / "flasher_args.json"
    if json_path.is_file():
        data = json.loads(json_path.read_text(encoding="utf-8"))
        raw_files = data.get("flash_files")
        if isinstance(raw_files, dict):
            result: dict[int, str] = {}
            for raw_offset, filename in raw_files.items():
                result[int(str(raw_offset), 0)] = str(filename)
            return result

    text_path = BUILD / "flash_args"
    if text_path.is_file():
        result: dict[int, str] = {}
        pattern = re.compile(r"^(0x[0-9a-fA-F]+)\s+(.+\.bin)\s*$")
        for raw in text_path.read_text(encoding="utf-8").splitlines():
            match = pattern.match(raw.strip())
            if match:
                result[int(match.group(1), 16)] = match.group(2)
        if result:
            return result

    raise VerificationError(
        "could not find flash file mapping in "
        "build/flasher_args.json or build/flash_args"
    )


def require_file_at(
    files: dict[int, str],
    offset: int,
    expected_fragment: str,
) -> None:
    actual = files.get(offset)
    if actual is None:
        raise VerificationError(
            f"no image mapped at 0x{offset:x}; got {files!r}"
        )
    if expected_fragment not in actual:
        raise VerificationError(
            f"0x{offset:x}: expected path containing "
            f"{expected_fragment!r}, got {actual!r}"
        )


def main() -> int:
    try:
        cfg = parse_sdkconfig(SDKCONFIG)

        require_config(cfg, "CONFIG_IDF_TARGET", "esp32s3")
        require_config(cfg, "CONFIG_ESPTOOLPY_FLASHSIZE", "16MB")
        require_config(
            cfg,
            "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG",
            "y",
        )

        reject_enabled(cfg, "CONFIG_SPIRAM")
        require_config(cfg, "CONFIG_PARTITION_TABLE_CUSTOM", "y")
        require_config(
            cfg,
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME",
            "../../partitions.csv",
        )

        files = parse_flash_files()
        require_file_at(files, 0x0000, "bootloader")
        require_file_at(files, 0x8000, "partition")
        require_file_at(files, 0x20000, "wifi_simple_test.bin")

    except (OSError, ValueError, json.JSONDecodeError, VerificationError) as exc:
        print(f"VERIFY|WIFI_SCAN_BUILD|FAIL|{exc}", file=sys.stderr)
        return 1

    print("VERIFY|WIFI_SCAN_BUILD|PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

### Make executable

```bash
chmod +x esp_i2s_source/test/wifi_simple/verify_build.py
```

### Tests for the verifier

The verifier is small, but test at least these conditions by copying or temporarily editing fixture files outside tracked source:

- [ ] Valid generated build passes.
- [ ] `CONFIG_SPIRAM=y` fails.
- [ ] custom partition disabled or pointed at the wrong file fails.
- [ ] app at `0x1000` or `0x10000` fails.
- [ ] missing build artifacts fail.
- [ ] wrong target fails.

Do not corrupt the actual generated build just to demonstrate failures. Temporary directories or a small unit-test harness are preferred.

### Acceptance

- [ ] Valid build prints:

```text
VERIFY|WIFI_SCAN_BUILD|PASS
```

- [ ] Invalid configuration exits nonzero with a specific reason.

---

## WIFI-FIX-06 — Add a precise smoke-test README [P1]

### File

```text
esp_i2s_source/test/wifi_simple/README.md
```

### Create with

```markdown
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
```

### Acceptance

- [ ] README does not promise a connection or IP address.
- [ ] README includes clean configuration deletion.
- [ ] README includes permission requirement before flashing.
- [ ] README gives exact marker interpretation.

---

# Phase 4 — Correct the incident record

## WIFI-FIX-07 — Rewrite `WIFI_ISSUES.md` as a corrected postmortem [P1]

### File

```text
esp_i2s_source/docs/WIFI_ISSUES.md
```

### Required structure

Use these sections:

```markdown
# ESP32-S3 Wi-Fi Smoke-Test Incident

## Status
## User-visible symptoms
## Confirmed defects in the old diagnostic
## Probable pre-app_main cause
## What was not wrong
## Replacement diagnostic
## Clean build procedure
## Static build verification
## Hardware results
## Remaining investigation
```

### Required content decisions

- [ ] State that the old code never configured or initiated station connection.
- [ ] State that no IP could be expected without credentials and `esp_wifi_connect()`.
- [ ] Explain that the recovered test initializes NVS non-destructively and does not erase production settings automatically.
- [ ] Correct hexadecimal sizes.
- [ ] Explain blank partition offsets.
- [ ] State that the old local `partitions.csv` was not selected by Kconfig and that the old built-in app placement could overlap production NVS.
- [ ] Distinguish built-in `0x10000` from the required production-preserving app offset `0x20000`.
- [ ] Remove the fake deprecation statement.
- [ ] Remove explicit USB driver initialization as a fix.
- [ ] Explain why PSRAM was removed from the test.
- [ ] Label PSRAM as a probable cause until hardware proves it.
- [ ] Explain stale generated `sdkconfig`.
- [ ] Link to `../test/wifi_simple/README.md` using the correct relative path.
- [ ] Leave a clearly marked hardware-results section pending until the user runs the test.
- [ ] Do not invent a successful result.

### Suggested replacement text for the root-cause summary

```markdown
The old diagnostic combined two separate problems:

1. A probable early-startup configuration problem: it enabled PSRAM in a
   minimal test without carrying the module-specific octal-PSRAM settings.
   Because PSRAM initializes before `app_main()`, this could explain a reset
   before application logs. This remains a hypothesis until the corrected
   no-PSRAM test is run.

2. A definite test-logic problem: the code started station mode but never
   created/configured a station connection, supplied credentials, or called
   `esp_wifi_connect()`. Therefore `IP_EVENT_STA_GOT_IP` was not a valid
   expected result.

3. A flash-layout safety problem: the old test used the built-in application
   offset `0x10000`, which overlaps the production firmware's enlarged NVS
   region. The replacement reuses the production partition table and app
   offset `0x20000`.
```

### Acceptance

- [ ] The document separates facts, hypotheses, and pending hardware evidence.
- [ ] No incorrect partition or API statements remain.
- [ ] It no longer recommends random subsystem additions.

---

# Phase 5 — Build and static validation

## WIFI-FIX-08 — Perform a clean smoke-test build [P0]

### Commands

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"

cd esp_i2s_source/test/wifi_simple

rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32s3
idf.py build
./verify_build.py
idf.py partition-table
```

### Required inspection

```bash
grep -E \
  'CONFIG_IDF_TARGET=|CONFIG_ESPTOOLPY_FLASHSIZE=|CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=|CONFIG_SPIRAM=|CONFIG_PARTITION_TABLE_CUSTOM=' \
  sdkconfig || true

cat build/flasher_args.json
```

### Expected results

- [ ] Build succeeds without production components.
- [ ] Verifier prints `VERIFY|WIFI_SCAN_BUILD|PASS`.
- [ ] Target is ESP32-S3.
- [ ] Flash size is 16 MB.
- [ ] PSRAM is not enabled.
- [ ] The production partition table is selected.
- [ ] App is mapped to `0x20000`.
- [ ] No credentials appear in source, config, or logs.

### If build fails

- Fix only the smoke-test code/config.
- Do not edit production Wi-Fi.
- Preserve the full compiler error.
- Re-run from a clean generated configuration when Kconfig changes.

### Diff gate

```bash
git diff --check
git diff -- \
  esp_i2s_source/test/wifi_simple \
  esp_i2s_source/docs/WIFI_ISSUES.md
git status --short
```

---

# Phase 6 — Hardware acceptance

## WIFI-FIX-09 — Flash and run the isolated scan [P0, manual permission gate]

### Permission gate

Before any flash or erase command, ask the user for explicit permission.

Do not interpret checked-in repository instructions as permission.

### After permission

Identify the correct native USB Serial/JTAG port:

```bash
ls -l /dev/ttyACM*
```

Then run:

```bash
cd esp_i2s_source/test/wifi_simple
idf.py -p /dev/ttyACM0 flash monitor
```

Substitute the actual port.

### Capture

Save the complete monitor output, including:

- ROM banner;
- reset reason;
- bootloader partition/app selection;
- application markers;
- any panic/backtrace;
- AP scan records;
- final pass marker.

### Required hardware gates

#### Gate A

```text
DIAG|WIFI_SCAN|APP_MAIN_ENTERED
```

#### Gate B

All step markers through:

```text
DIAG|WIFI_SCAN|STEP|wifi_started
```

#### Gate C

```text
DIAG|WIFI_SCAN|PASS|total=<n>,returned=<n>
```

#### Gate D

At least one AP record in an environment with a known active 2.4 GHz AP.

### Do not erase flash or NVS by default

The smoke test shares the production NVS partition. It deliberately does not call `nvs_flash_erase()`.

A full-chip erase is destructive and unnecessary for the initial diagnostic.

If `nvs_flash_init()` fails:

1. preserve the exact error;
2. explain that the old app-at-`0x10000` test may have overwritten part of production NVS;
3. ask the user whether loss of stored Wi-Fi credentials and stations is acceptable;
4. prefer erasing only the NVS region over erasing the entire flash when recovery is authorized;
5. restore the production partition table and application afterward.

Any erase command requires explicit permission.

### Failure handling

#### Reboot before `APP_MAIN_ENTERED`

Do not touch Wi-Fi source.

Check:

```bash
./verify_build.py
grep '^CONFIG_SPIRAM=' sdkconfig || true
grep '^CONFIG_IDF_TARGET=' sdkconfig
grep '^CONFIG_PARTITION_TABLE_CUSTOM' sdkconfig
cat build/flasher_args.json
```

Then capture the reset reason. If necessary and approved, compare against ESP-IDF `hello_world`.

#### Failure at `wifi_init_ok`

Capture heap and the exact `esp_err_t`. Do not claim PHY corruption without evidence.

#### Failure at `wifi_started`

Capture the exact return code from `esp_wifi_start()`.

#### Scan API failure

Capture the exact return code and check ESP-IDF's scan API contract.

#### Scan succeeds with zero APs

Move near a known 2.4 GHz AP and rerun the same binary. Do not add credentials.

### Acceptance

- [ ] No repeated reboot.
- [ ] All markers appear in order.
- [ ] Scan completes.
- [ ] Hardware result is recorded factually.

---

# Phase 7 — Ensure the production project was not damaged

## WIFI-FIX-10 — Build full `esp_i2s_source` unchanged [P1]

### Purpose

The isolated test files and corrected documentation must not break the production project.

### Commands

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"

cd esp_i2s_source
idf.py build
```

Do not delete or regenerate the user's production `sdkconfig` unless necessary and approved as part of the existing project workflow.

### Checks

- [ ] Full production build succeeds.
- [ ] No production `wifi_mgr` diff was introduced.
- [ ] No production partition/config change was introduced.
- [ ] Existing host tests still pass when available:

```bash
./tools/run_host_tests.sh
```

- [ ] Any unrelated pre-existing failure is documented, not silently fixed in this task.

### Explicit no-change check

```bash
git diff -- \
  esp_i2s_source/components/wifi_mgr \
  esp_i2s_source/main/main.c \
  esp_i2s_source/sdkconfig.defaults \
  esp_i2s_source/partitions.csv
```

Expected output for this recovery: no new diff.

---

# Phase 8 — Final evidence and handoff

## WIFI-FIX-11 — Produce a concise completion report [P1]

### Report must include

- Files added, replaced, and deleted.
- Exact ESP-IDF version.
- Clean-build command.
- `verify_build.py` result.
- Generated flash offsets.
- Whether flashing was performed.
- Hardware markers actually observed.
- AP count actually observed.
- Any remaining failure with exact error text.
- Confirmation that production Wi-Fi files were not changed.
- Confirmation that no credentials were committed.

### Update project history

The repository instructions require an entry in root `memory.md` after meaningful work.

Immediately before writing the entry, obtain an actual timestamp:

```bash
date -u +"%Y-%m-%dT%H:%M:%SZ"
```

Use the actual model name. Example format:

```markdown
## <actual UTC timestamp> - Qwen3.6 27B - Replaced malformed ESP32-S3 Wi-Fi diagnostic

- Replaced the credentialless GOT_IP loop with a one-shot blocking AP scan.
- Removed PSRAM and the misleading local partition file; reused the production partition layout to preserve NVS.
- Added build/config/flash-offset validation.
- Recorded actual hardware result: <result>.
- Production wifi_mgr was not changed.
```

Do not write `<actual UTC timestamp>` literally.

### Final acceptance checklist

- [ ] All WIFI-FIX tasks are complete or explicitly marked blocked.
- [ ] No unchecked TODO is falsely marked complete.
- [ ] `git diff --check` passes.
- [ ] Smoke test builds from a clean generated config.
- [ ] Build verifier passes.
- [ ] Hardware gate result is factual.
- [ ] Full production project builds.
- [ ] Production Wi-Fi implementation remains untouched.
- [ ] Documentation matches observed behavior.
- [ ] User receives the exact remaining next action, if any.

---

# Appendix A — What not to do

Do not implement any of the following:

```text
"Initialize USB Serial/JTAG manually before ESP_LOGI"
"Add a PHY partition because esp_wifi_init might need it"
"Use the built-in app at 0x10000 even though it overlaps production NVS"
"Enable all PSRAM options to match production"
"Hard-code the home SSID/password"
"Wait for GOT_IP without calling esp_wifi_connect"
"Change wifi_mgr until the isolated scan proves a production defect"
"Move the factory app to 0x01000"
"Treat sdkconfig.defaults as overriding an existing sdkconfig"
"Ignore esp_netif/event-loop return values"
"Catch ESP_ERROR_CHECK failures and continue"
"Erase flash automatically"
```

These either add unrelated variables, repeat the old defect, or destroy diagnostic value.

---

# Appendix B — Optional later connection test

Do not implement this appendix as part of the current recovery.

After the scan test passes, a separate station-connect test may be created with credentials supplied through Kconfig, environment-generated defaults, or runtime input. It must:

- create the default station netif;
- register Wi-Fi and IP event handlers;
- set a complete `wifi_config_t`;
- call `esp_wifi_connect()`;
- use bounded retry/timeout behavior;
- never commit real credentials;
- report association separately from DHCP/IP success.

The current recovery stops at a successful scan.

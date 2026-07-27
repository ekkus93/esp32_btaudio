# ESP32 Bluetooth Audio Full-Duplex Implementation Progress

**Branch:** `feature/esp-bt-audio-duplex`
**Draft PR:** #2
**Baseline merge commit:** `cb58d0b47cfc683542cae62efce2a1e66365c3a9`
**HFP configuration commit:** `cfa3c5f35fe81ed82bc0869578da0b84db8e6f70`
**Validated clean head:** `7f2eb7a78cb558f187eda6c7097a5792eb09066e`

## FD-00 — Branch and scope baseline

- Confirmed the feature branch contains current `master`.
- Confirmed the companion spec and TODO exist at their documented paths.
- Confirmed the intended scope is limited to `esp_bt_audio_source` and maintained project documentation.
- Ran `esp_bt_audio_source/tools/ci_check_main_layering.sh esp_bt_audio_source/main/main.c`: PASS.
- Opened draft PR #2 so host and ESP-IDF device builds validate each commit.
- No hardware was flashed.

## FD-01 — Pre-HFP compile baseline

Baseline commit `cb58d0b47cfc683542cae62efce2a1e66365c3a9`:

- Host CI run 515: PASS.
- ESP-IDF v5.5.1 device-build run 418: PASS.
- Application image: 927,392 bytes.
- Factory app partition: 1,769,472 bytes (`0x1B0000`).
- App-partition headroom: 842,080 bytes.
- `.dram0.data`: 21,824 bytes.
- `.dram0.bss`: 44,432 bytes.
- Static DRAM total for those sections: 66,256 bytes.

Runtime heap, stack, A2DP 10-minute, and hardware counters remain hardware-gated and are not claimed complete.

## FD-02 — HFP link-time configuration

Configured the tracked `sdkconfig` and `sdkconfig.defaults` for:

- HFP enabled.
- Audio Gateway role enabled.
- Hands-Free client role disabled.
- HCI SCO data path selected in host and controller.
- One synchronous SCO/eSCO connection.
- Internal codec path retained.
- WBS/mSBC disabled for initial CVSD bring-up.

Validated clean head `7f2eb7a78cb558f187eda6c7097a5792eb09066e`:

- Host CI run 529: PASS.
- ESP-IDF v5.5.1 device-build run 432: PASS.
- A2DP remains enabled.
- No HFP profile or SCO API is initialized by application code yet.
- Application image: 969,856 bytes, an increase of 42,464 bytes.
- App-partition headroom: 799,616 bytes (about 781 KiB), above the 256 KiB gate.
- `.dram0.data`: 21,840 bytes, an increase of 16 bytes.
- `.dram0.bss`: 50,536 bytes, an increase of 6,104 bytes.
- Static DRAM total: 72,376 bytes, an increase of 6,120 bytes.

The FD-02 link-time stop condition passes.

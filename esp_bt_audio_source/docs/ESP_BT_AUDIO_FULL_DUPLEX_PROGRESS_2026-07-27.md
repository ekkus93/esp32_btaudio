# ESP32 Bluetooth Audio Full-Duplex Implementation Progress

**Branch:** `feature/esp-bt-audio-duplex`  
**Draft PR:** #2  
**Baseline merge commit:** `cb58d0b47cfc683542cae62efce2a1e66365c3a9`  
**HFP configuration commit:** `cfa3c5f35fe81ed82bc0869578da0b84db8e6f70`

## FD-00 — Branch and scope baseline

- Confirmed the feature branch contains current `master`.
- Confirmed the companion spec and TODO exist at their documented paths.
- Confirmed the only intended feature files before implementation were the spec and TODO.
- Ran `esp_bt_audio_source/tools/ci_check_main_layering.sh esp_bt_audio_source/main/main.c`: PASS.
- Opened draft PR #2 so host and ESP-IDF device builds validate each commit.
- No hardware was flashed.

## FD-01 — Pre-HFP baseline

- Host CI run 515 on `cb58d0b...`: PASS.
- Device build baseline and size artifacts: pending final artifact collection.
- Runtime heap, stack, A2DP 10-minute, and hardware counters remain hardware-gated and are not claimed complete.

## FD-02 — HFP link-time configuration

Configured the tracked `sdkconfig` and `sdkconfig.defaults` for:

- HFP enabled.
- Audio Gateway role enabled.
- Hands-Free client role disabled.
- HCI SCO data path selected in host and controller.
- One synchronous SCO/eSCO connection.
- Internal codec path retained.
- WBS/mSBC disabled for initial CVSD bring-up.

Host and ESP-IDF v5.5.1 validation runs are triggered by this documentation commit. No HFP profile API is initialized yet.

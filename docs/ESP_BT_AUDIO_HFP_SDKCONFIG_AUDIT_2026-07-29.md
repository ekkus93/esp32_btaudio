# ESP32 Bluetooth Audio — HFP `sdkconfig` Audit

**Repository:** `ekkus93/esp32_btaudio`  
**Branch:** `feature/esp-bt-audio-duplex`  
**Project:** `esp_bt_audio_source`  
**Configuration baseline audited:** changes included in commit `0e08b5a083b3971e5a4b56ddec5745387dbfdfce`  
**Audit date:** 2026-07-29  
**Validation boundary:** source review and ESP-IDF compile-only CI; no hardware flashing or runtime electrical validation

## Purpose

The A2DP binding-lifecycle correction is a runtime state-integrity change. The baseline commit also modified generated `sdkconfig` entries related to HFP Audio Gateway operation, SCO transport, and the planned HFP microphone I2S output. This note classifies those configuration changes separately so they are not mistaken for part of the A2DP lifecycle fix.

No additional `sdkconfig` changes are required by `ESP_BT_AUDIO_A2DP_BINDING_LIFECYCLE_SAFETY_FIX1_TODO.md`.

## Classification

### Required software configuration

The following settings are required for the current HFP Audio Gateway software path to compile and for the ESP-IDF Bluetooth stack to expose the intended HFP AG/HCI interfaces:

- `CONFIG_HFP_ENABLE=y`
- `CONFIG_HFP_AG_ENABLE=y`
- `CONFIG_HFP_CLIENT_ENABLE` remains disabled
- `CONFIG_HFP_AUDIO_DATA_PATH_HCI=y`
- `CONFIG_HFP_AUDIO_DATA_PATH_PCM` remains disabled
- `CONFIG_BTDM_CONTROLLER_BR_EDR_MAX_SYNC_CONN=1`
- `CONFIG_BTDM_CONTROLLER_BR_EDR_MAX_SYNC_CONN_EFF=1`
- Controller SCO data path remains HCI

These settings are logically consistent with an ESP32-S3 acting as an HFP Audio Gateway and exchanging SCO audio through the host/controller HCI path. They do not prove successful SCO setup, callback cadence, audio quality, interoperability, or full-duplex behavior on hardware.

### Generated PCM-option removal

The removed PCM role, edge, polarity, and frame-shape selections are consistent with selecting the HCI SCO data path instead of the controller PCM data path. Their generated effective values remain zero.

This is not a fallback from a failed PCM implementation. The software architecture intentionally selects HCI and does not claim that the external controller PCM path is active.

### Planned HFP microphone I2S configuration

The following values configure the application-side I2S microphone output planned for hardware validation:

- I2S port: `0`
- BCLK: GPIO32
- WS/LRCLK: GPIO33
- DOUT: GPIO27
- Sample rate: 16 kHz
- Ring capacity: 4096 bytes
- DMA descriptors: 8
- DMA frames: 120
- Writer samples: 120
- Writer task stack: 4096 bytes
- Writer task priority: 5
- Write timeout: 20 ms
- Stop timeout: 1000 ms
- Maximum consecutive write failures: 3
- Underflow degraded threshold: 20

These values are configuration assumptions for the intended ESP32-S3 wiring and CVSD-oriented test path. They remain **hardware-validation pending**. In particular, compile success cannot establish:

- that GPIO32/GPIO33/GPIO27 are correctly wired and conflict-free on the target board;
- that I2S0 timing and polarity match the connected device;
- that DMA and ring sizing tolerate real SCO callback cadence;
- that task stack and priority are sufficient under coexistence load;
- that the selected timeouts are appropriate on the physical device;
- that CVSD or mSBC audio is intelligible and stable.

## Safety and visibility conclusions

1. The HFP AG/HCI/synchronous-connection settings are software prerequisites, not silent compatibility fallbacks.
2. The I2S values are not runtime-validated facts and must remain documented as pending hardware work.
3. No configuration in this audit authorizes firmware flashing.
4. A device compile-only CI pass is required after the A2DP lifecycle patch.
5. Hardware-gated full-duplex milestones remain incomplete until explicitly tested on the target ESP32-S3 and peripherals.

## Required follow-up outside FIX1

Hardware validation must be performed under the existing full-duplex hardware milestones, with explicit authorization before flashing. Results should record wiring, target board revision, ESP-IDF version, codec, SCO mode, callback timing, heap/stack measurements, underflow/write-failure counters, and soak duration.

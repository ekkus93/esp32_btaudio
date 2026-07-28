# ESP Bluetooth Audio — HFP Callback Resource Budget

**Date:** 2026-07-28  
**Branch:** `feature/esp-bt-audio-duplex`  
**Scope:** Incoming HFP CVSD callback and the immediate ring-ingest path

## 1. Purpose

This document records the software-derived resource budget for the incoming HFP audio callback. It does **not** replace ESP32-S3 hardware stack high-water-mark and callback-latency measurements. Hardware measurements remain a release gate.

## 2. Callback path

The production callback path is:

1. `production_incoming_callback()` in `components/bt_manager/bt_hfp_audio.c`
2. nonblocking callback overlap gate
3. frame validation and state/handle/codec checks in `process_incoming()`
4. fixed-size alignment copy
5. `hfp_i2s_output_push_cvsd()`
6. CVSD 8 kHz to 16 kHz conversion
7. bounded ring-buffer write
8. release the ESP-IDF-owned audio buffer
9. update callback duration counters and release the overlap gate

The callback does not call the ESP-IDF I2S driver, wait on an application mutex, allocate application heap memory, sleep, or log per frame. I2S driver writes remain in the dedicated `hfp_i2s_tx` writer task.

## 3. Fixed stack arrays

The callback path currently nests these fixed audio arrays:

| Function | Array | Elements | Bytes |
|---|---:|---:|---:|
| `process_incoming()` | aligned CVSD samples | 120 × `int16_t` | 240 |
| `hfp_i2s_output_push_cvsd()` | converted 16 kHz samples | 240 × `int16_t` | 480 |
| **Combined fixed audio arrays** |  |  | **720** |

`HFP_I2S_CALLBACK_AUDIO_ARRAY_BUDGET_BYTES` is set to **1024 bytes**. Compile-time assertions reject a frame-size or conversion-factor change that would make the combined fixed audio arrays exceed that ceiling.

The 1024-byte ceiling covers only the two explicitly sized audio arrays. Normal call frames, compiler spills, ESP-IDF callback frames, and interrupt/task context overhead must be validated by hardware high-water marks.

## 4. Buffer and queue bounds

- Maximum accepted incoming CVSD frame: **120 samples / 240 bytes**.
- CVSD expansion factor: **2**.
- Converted frame: **240 samples / 480 bytes**.
- Ring capacity: configured once by `hfp_i2s_output_config_t.ring_bytes`; no dynamic growth.
- Writer chunk: configured once by `writer_samples`; no dynamic growth.
- Callback overlap handling: a second concurrent callback is rejected immediately and increments the saturating `callback_overlap_rejections` diagnostic. It does not block or spin.

## 5. Allocation and ownership

- No application heap allocation occurs in the per-frame callback path.
- The ESP-IDF-provided `esp_hf_audio_buff_t` is released exactly once by `esp_hf_ag_audio_buff_free()` on accepted, dropped, and overlap-rejected production callbacks.
- Ring storage and writer buffers are allocated during I2S output initialization, before streaming starts.

## 6. Timing budget

- Software callback budget: `BT_HFP_AUDIO_CALLBACK_BUDGET_US = 2000` microseconds.
- `callback_last_us`, `callback_max_us`/`MAX_US_LIFETIME`, and `callback_over_budget`/`OVER_BUDGET_LIFETIME` remain visible.
- Callback duration counters are not reset by `HFP RESETSTATS`; reset-relative command reporting uses a baseline rather than rewriting live callback counters.
- Overlap rejection is separately visible as `OVERLAP_REJECT` in `HFP STATS`.

## 7. Required hardware evidence

The following remain mandatory before release approval:

1. `hfp_app_task` minimum free stack bytes over repeated start/stop and reconnect cycles.
2. `hfp_i2s_tx` minimum free stack bytes over the same cycles.
3. Incoming callback maximum duration and over-budget count during sustained CVSD traffic.
4. Heap minimum-free and largest-internal-block diagnostics during repeated lifecycle churn.
5. Confirmation that callback overlap rejections remain zero in normal ESP32-S3 operation, or an investigation if they do not.
6. Confirmation that no watchdog reset, stack overflow, callback stall, or I2S teardown race occurs.

Until those measurements are captured on the target ESP32-S3, the software budget is implemented and statically enforced, but hardware acceptance is **pending**.

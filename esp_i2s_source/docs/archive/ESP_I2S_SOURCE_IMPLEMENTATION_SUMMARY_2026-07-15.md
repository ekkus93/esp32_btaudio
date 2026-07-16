# esp_i2s_source Implementation Summary

**Date:** 2026-07-15  
**Project:** ESP32-S3 internet-radio/tone source firmware  
**Target:** Sends 44.1 kHz stereo audio over I2S to ESP32-WROOM32 A2DP bridge

---

## Source Documents

This summary consolidates four documents:

1. `ESP_I2S_SOURCE_FIX_SPEC_V2_2026-07-15.md` — Normative repair specification
2. `ESP_I2S_SOURCE_FULL_CODE_REVIEW_2026-07-15.md` — Full code review (12 P0, 41 P1, 45+ P2 findings)
3. `ESP_I2S_SOURCE_FIX_RESPONSES_V2_2026-07-15.md` — Normative errata (12 answers to spec questions)
4. `ESP_I2S_SOURCE_IMPLEMENTATION_TODO_2026-07-15.md` — 12-phase implementation plan

---

## Document Precedence

Per errata answer #11, normative precedence order:

1. **Errata** (`ESP_I2S_SOURCE_FIX_RESPONSES_V2_2026-07-15.md`) — highest authority
2. **Specification** (`ESP_I2S_SOURCE_FIX_SPEC_V2_2026-07-15.md`)
3. **Implementation TODO** (`ESP_I2S_SOURCE_IMPLEMENTATION_TODO_2026-07-15.md`)
4. **Code Review** (`ESP_I2S_SOURCE_FULL_CODE_REVIEW_2026-07-15.md`) — evidence/findings catalog

---

## Architecture

### Hardware

- **ESP32-S3:** I2S slave transmitter (this firmware)
- **ESP32-WROOM32:** I2S master, A2DP bridge, drives BCLK/WS clocks
- **I2S wire format:** 44.1 kHz stereo, Philips format, 32-bit slots, signed 16-bit PCM in upper bits

### Subsystems & Owners

| Subsystem | Owner Task |
|---|---|
| I2S channel | I2S writer task |
| Audio arbitration | `audio_out_task` |
| UART protocol | `bt_link_task` |
| Radio lifecycle | `radio_cmd_task` |
| Wi-Fi | Wi-Fi event loop + manager |
| Control | `orchestrator_task` |
| HTTP server | ESP HTTP task |

---

## Boot Sequence (Normative Order)

Per errata answer #1, the dependency-safe boot order:

1. NVS initialization/recovery
2. Boot diagnostics and boot-ID creation
3. `i2s_out_init` → `i2s_out_start` (MUST NOT require WROOM32)
4. `bt_link_init`
5. `radio_init`
6. `stations_init` (incl. persistence migration)
7. `ctrl_init`
8. `audio_out_start` (after both i2s_out and radio initialized)
9. `wifi_mgr_init`
10. `console_start`
11. `web_ui_start` (optional/degraded)
12. `ctrl_start`
13. Emit `BOOT_COMPLETE` marker
14. Launch bounded WROOM health probe (async)
15. Low-rate diagnostics (outside critical path)

**Degraded boot:** Firmware remains controllable for WROOM absent, UART unavailable, Wi-Fi absent, radio failure, web failure, corrupt NVS, and allocation failures.

---

## Key Errata Decisions

### 1. URL Security (MUST for Release)

Release builds MUST reject loopback, link-local, private, multicast, unspecified, and broadcast destinations. Validation applies to literal IPs, every DNS result, every redirect, every reconnect. Local streams require explicit `CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS=n` (default `n`).

### 2. BT Request Ownership Protocol

Two-owner reference counting with retain-before-enqueue pattern:
- `request_create()`: refs = 1 (caller)
- `request_retain()`: refs = 2 (reserve worker reference)
- On queue failure: caller releases both references
- On queue success: worker reference transfers to queue
- No `abandoned` flag

### 3. Station Migration

One-time migration from legacy `STA1` schema required:
- Detect by blob size + magic (`0x53544131`)
- Migrate to new versioned schema with stable IDs
- Write to `stations_v2` key, retain legacy key for rollback
- Assign sequential IDs, convert `last_station` index to `last_station_id`

### 4. 64-char Hex PSK

Gated behind `CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK`. Validation: length 64 with all hex chars. Non-hex 64-char strings rejected as `ESP_ERR_INVALID_ARG`. Never truncate or silently fallback.

### 5. Auth Bootstrap Flow

- First boot: generate 32 random bytes, store in NVS
- Print token once to USB serial: `AUTH|BOOTSTRAP_TOKEN|<token>`
- Token in `Authorization: Bearer ...` header
- Rotation: 5-second physical button hold while firmware running
- No unauthenticated grace period

### 6. Memory Policy

Generic rule: every buffer designated `PSRAM_REQUIRED` MUST be allocated from PSRAM. Failure returns `ESP_ERR_NO_MEM`, publishes failed buffer name/size, enters degraded state. No silent internal RAM fallback for I2S ring (256 KiB), radio compressed ring, or radio PCM ring.

---

## Implementation Status (12 Phases)

### Done

| Phase | Status |
|---|---|
| **1. Test infrastructure** | DONE — commits `acbb348b`, `b1843231`, `0fe3e89e`, `168eb8de`, `65898f1f`, `4c00085c` |
| **2. Boot fix** | DONE — commits `071d5ef9`, `be60d9d3`, `3ccdeb7a` |
| **3. I2S lifecycle** | DONE — commit `b230fade` |
| **4. BT link ownership** | DONE — commit `74a4fa57` |
| **5. Tone/signal hardening** | DONE — commit `92cea091` |
| **6. Resampler** | DONE — commit `81a62bdc` |

### Remaining

| Phase | Scope |
|---|---|
| **7. Radio lifecycle** | Single-owner, join-safe session, HTTP status, playlist resolution, decoder contract, backpressure, prebuffer, status snapshot |
| **8. Wi-Fi** | Idempotent init, transactional credentials, 32-byte SSID, exact password rules, stale event rejection, provisioning serialization |
| **9. Stations/control** | Stable IDs, precise errors, URL validation, versioned persistence, migration, scan state machine, monotonic timing |
| **10. HTTP/frontend** | cJSON lifetime, JSON helper, error format, auth, async ops, 202 responses, frontend polling/abort |
| **11. Device tests** | Sine test, NVS roundtrip, Wi-Fi test, PSRAM conditional, task sync, strict gate |
| **12. Cleanup** | Stale comments, architecture doc, verification commands |

---

## Hardware Checkpoints

Per errata answer #12, implementation proceeds host-first with hardware checkpoints:

1. **Checkpoint 1** (after boot/I2S fixes): Boot log with no duplicate-netif assertion, `BOOT_COMPLETE`, I2S `RUNNING`/`WAITING_FOR_CLOCK`, controllable without WROOM32
2. **Checkpoint 2** (after I2S/BT fixes): UART probe, WS ≈ 44.1 kHz, BCLK ≈ 2.8224 MHz, ratio ≈ 64, byte counters increasing, tone audible
3. **Checkpoint 3** (after radio/resampler): MP3/AAC station audible and stable, stop/restart/recovers without crash or heap loss
4. **Final gate:** Boot cycles, play/stop, reconnect, and two-hour stream soaks

---

## Test Requirements

### Host Tests
- Strict compile (`-Wall -Wextra -Werror`)
- ASan pass
- UBSan pass
- Concurrency/TSan suite
- Offline (no network fetch)

### Device Tests
- Unity accounting
- NVS round-trip always executes
- PSRAM conditional on config
- Wi-Fi test creates netif, applies credentials, waits IP event
- Signal test checks RMS/nonzero samples
- I2S state and clock detection

### Soak Tests
- 100 boot cycles
- 500 radio play/stop cycles
- 100 BT timeout/reconnect cycles
- 2-hour MP3 stream
- 2-hour AAC stream
- Wi-Fi disconnect/reconnect during playback
- WROOM power cycle during playback
- Heap high-water stabilization (no monotonic leak)

---

## Normative Language

- **MUST:** release-blocking
- **SHOULD:** expected unless documented reason justifies different implementation
- **MAY:** optional
- No implementation may replace a required error with a silent fallback

---

## Definition of Done (10 Items)

The repair is complete only when:

1. Duplicate-init crash is impossible by design and regression-tested
2. I2S is initialized, started, and fed
3. Tone is audible end-to-end through WROOM32/A2DP
4. Internet radio is audible and stable for MP3 and AAC
5. No known use-after-free, live-session free, blocking critical section, or data race remains
6. Resampler matches trusted reference and is chunk-equivalent
7. Every silent failure is removed or converted to explicit/observable tested degraded mode
8. Device can boot and remain controllable with WROOM absent
9. Release HTTP API is authenticated and does not expose secrets
10. All gates (Section 18 in spec) pass from documented one-command entry points

---

## Recommended Commit Sequence

```
fix(test): make host tests deterministic and strict
fix(boot): remove duplicate initialization and start I2S pipeline
fix(i2s): make writer lifecycle and sink commit safe
fix(bt-link): replace abandoned request ownership with refcount
fix(audio): validate tone generation and coherent config
fix(resampler): correct streaming interpolation
fix(radio): serialize lifecycle and join workers safely
fix(wifi): make manager idempotent and transactional
fix(ctrl): synchronize config and make station identity stable
fix(web): fix request ownership, errors, auth, and async operations
fix(ui): centralize API errors and non-overlapping polling
test(device): require end-to-end audio evidence
```
# Active TODOs — Task List

**Created:** 2026-07-27
**Spec:** [`ACTIVE_TODOS_SPEC.md`](ACTIVE_TODOS_SPEC.md)
**Source:** root `README.md` "Active TODOs" section, as of `v0.3.0`.

Deferred — no timeline. Work through top to bottom when picked up.

---

## A. Longer-duration UARTAUDIO pytest (P1 — no hardware blocker, can start anytime)

- [ ] **A1. Generator-based tone payload helper**
  - [ ] Add a generator variant of `_make_tone_payloads()` in
        `test_uart_streaming.py` that yields one frame payload at a time
        instead of materializing the full list (needed so soak-duration runs
        don't scale test-process memory).
  - [ ] Confirm byte-for-byte identical output to the existing list-based
        helper for a short duration (regression-test the helper itself).
- [ ] **A2. Mid-stream `UA|FILL` sampling**
  - [ ] Extend the streaming loop to parse each `UA|FILL|used|cap|und|crc|
        lost|ovf|seq` line as it arrives (not just count them at the end).
  - [ ] Track a time series of `used/cap` fill percentage and per-sample
        `crc`/`ovf`/`lost` deltas.
- [ ] **A3. New test: `test_stream_sustained_tone_no_starvation`**
  - [ ] 60 s default duration, `@pytest.mark.slow` (same tier as the
        existing 3 s test).
  - [ ] Assert final counters (`crc==0`, `ovf==0`, `lost==0` off
        `EVENT|UARTAUDIO|STOPPED`) — same strict gate as today.
  - [ ] Assert fill-percentage health: flag if more than ~5% of samples show
        `used/cap < 10%`.
  - [ ] Assert no sample shows a CRC/ovf/lost count jump beyond a small
        per-sample tolerance (catches a regression before it fails the
        final-counter gate outright).
- [ ] **A4. Optional soak tier**
  - [ ] Gate an extended run (10–30 min) behind an env var, e.g.
        `UARTAUDIO_SOAK_S` — unset skips it, set runs for that many seconds.
  - [ ] Use the generator helper from A1 so duration doesn't bound memory.
  - [ ] Document the env var in the test's module docstring and in
        `esp_bt_audio_source/test/laptop_bt_tests/pytest.ini` or its
        README, whichever already documents `laptop_bt`/`slow`.
- [ ] **A5. Verify against real hardware**
  - [ ] Run the new 60 s test end-to-end against the WROOM32 + laptop BT
        sink; confirm it passes cleanly.
  - [ ] Run the optional soak tier at least once (e.g. 10 min) to confirm it
        doesn't regress and doesn't leak memory in the test process.
- [ ] **A6. Update docs**
  - [ ] Remove the "Longer-duration UARTAUDIO pytest" bullet from root
        `README.md`'s Active TODOs once A3 is merged and passing.
  - [ ] Add a `memory.md` entry summarizing the addition and the hardware
        confirmation run.

## B. Physical UART2 verification (P2 — blocked on hardware: needs a second USB-serial adapter)

- [ ] **B0. Acquire hardware** — a second USB-to-serial adapter, confirmed
      3.3V logic level (check jumper/datasheet before use).
- [ ] **B1. Pre-check**
  - [ ] Confirm `CONFIG_CMD_UART2_ENABLED=y` in the flashed sdkconfig.
  - [ ] Flash production firmware, boot, confirm the "secondary command
        UART ready: uart=... tx=17 rx=16 baud=115200" boot-log line appears
        over the primary USB console.
- [ ] **B2. Wire up** — adapter TX→GPIO16, adapter RX→GPIO17, GND→GND;
      double-check voltage level before connecting power.
- [ ] **B3. Basic round-trip** — `VERSION`/`STATUS` over the new adapter's
      port; confirm response format matches the primary console.
- [ ] **B4. Independent-operation check** — commands on one port don't leak
      to or interfere with the other.
- [ ] **B5. Concurrency-under-load check (the core scenario)** — start
      `UARTAUDIO START` on the primary USB console, then send `STATUS`/
      `VOLUME` over UART2 mid-stream; confirm prompt, correct responses.
- [ ] **B6. Near-simultaneous sends** — commands on both ports within the
      same ~100 ms window, a few repetitions; confirm no corruption/crash.
- [ ] **B7. Record results**
  - [ ] `memory.md` entry with pass/fail per step and any anomalies.
  - [ ] Remove the "Physical UART2 verification" bullet from root
        `README.md`'s Active TODOs.
- [ ] **B8. (Optional, only if adapter stays available permanently)**
      Consider a `test/laptop_bt_tests/` fixture for a second serial port to
      automate B3–B6 for future regression checks. Not required to close
      this TODO.

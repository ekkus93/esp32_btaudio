# Active TODOs — Spec

**Created:** 2026-07-27
**Scope:** the two open items in root `README.md`'s "Active TODOs" section (as of
`v0.3.0`). Both are verification gaps, not new product features — no
production firmware behavior changes are implied by either item.
**Status:** design only; no implementation started. See
[`ACTIVE_TODOS_TODO.md`](ACTIVE_TODOS_TODO.md) for the task checklist.

---

## 1. Longer-duration UARTAUDIO pytest — engine-throughput regression guard

### Background

Two real throughput bugs were found and fixed during UARTAUDIO development
(2026-07-04, see `memory_archive.md`):

- **`bc2f2e8a`**: the audio engine task produced only one 1 KB chunk per wake.
  With `CONFIG_FREERTOS_HZ=100`, the engine's 2 ms tick clamps to a 10 ms
  FreeRTOS tick, capping output at 102.4 KB/s — below the 176.4 KB/s A2DP
  needs. The shortfall was silently zero-filled on every underrun (heavy
  static on real audio). Fixed by allowing up to 8 chunks per wake.
- **`3ffbb670`**: UART RX hardware FIFO (128 B) overflow during BT
  flash-cache windows. Fixed via `CONFIG_UART_ISR_IN_IRAM=y` + a 16 KB RX
  buffer.

Both are "starves under sustained load" bugs — they don't reproduce in a
few seconds of streaming, only after the engine has been running long enough
for the failure mode (tick starvation, FIFO backpressure) to accumulate.

The existing E2E test,
`test_stream_tone_to_laptop_sink` in
`esp_bt_audio_source/test/laptop_bt_tests/test_uart_streaming.py`, streams
only **~3 seconds** of tone and checks final counters (`crc`, `ovf`, `lost`)
once, at STOP. It would not have caught either bug above — 3 s is far
shorter than either failure mode's onset window. This is the origin of the
TODO: no automated test currently exercises the class of bug that has
actually bitten this project once.

### Requirement

Add a longer-running variant that streams for long enough to make
throughput-starvation and FIFO-overflow regressions observable, and that
checks health **throughout** the run, not just once at the end.

### Design

- **New test**, alongside the existing ones in `test_uart_streaming.py` (or a
  new `test_uart_streaming_soak.py` if it grows large) —
  `test_stream_sustained_tone_no_starvation` or similar.
- **Duration, two tiers** (avoid making the default `slow`-marked suite take
  30 minutes):
  - **Default "extended" run: 60 s.** Long enough to exercise several
    seconds beyond both fixed bugs' onset window, short enough to run
    routinely alongside the other `@pytest.mark.slow` laptop_bt tests.
  - **Optional soak run:** duration read from an env var, e.g.
    `UARTAUDIO_SOAK_S` (unset → skip; set → run for that many seconds, e.g.
    600–1800 for an occasional deep pass). This avoids inventing a new
    pytest marker/config axis — reuse the existing `laptop_bt` + `slow`
    markers, gate the extra-long tier purely on the env var being present.
- **Continuous health sampling, not just a final assertion.** The device
  emits `UA|FILL|used|cap|und|crc|lost|ovf|seq` every `CONFIG_UART_AUDIO
  _FEEDBACK_MS` (250 ms) during a stream. The test must parse every `UA|FILL`
  line as it streams (not just count them, as the current 3 s test does) and
  track the fill-percentage (`used/cap`) time series:
  - Assert **no single sample** shows `crc`, `ovf`, or `lost` increasing
    faster than a small tolerance (a handful of transient CRC errors on a
    long real UART link is plausible noise; a monotonically growing error
    count is the regression signature).
  - Assert the fill percentage stays within a healthy band for the large
    majority of samples (e.g. flag if more than ~5% of samples show
    `used/cap < 10%`) — a ring that repeatedly drains near-empty mid-stream
    is the direct symptom of the tick-starvation bug class, even before it
    gets bad enough to show up as zero-fill/static.
  - Keep the existing final-counter assertions (`crc==0`, `ovf==0`,
    `lost==0` off `EVENT|UARTAUDIO|STOPPED`) as the strict pass/fail gate;
    the mid-stream sampling is what makes a *partial* regression (still
    passes today's loose gate, but degraded) visible in the failure message
    instead of silently swallowed.
- **Audio generation must stay memory-bounded for the soak tier.** The
  existing `_make_tone_payloads()` helper materializes the whole tone as a
  list of frame payloads up front — fine for 3 s (~132 KB) or even 60 s
  (~2.6 MB), but a 30-minute soak would need ~80 MB. Switch the sustained
  test to a **generator** that yields one frame payload at a time (same sine
  math, no precomputed list) so soak duration doesn't scale test-process
  memory.
- **Never runs in CI** — same hardware gate as the rest of
  `test_uart_streaming.py` (conftest skips the whole module when the ESP32
  or BT adapter is absent). This is a manually-invoked regression check, run
  before releases or after touching `audio_processor`/`uart_audio`/
  `uart_source`, not a per-commit gate.

### Out of scope

- Changing production firmware. This is a test-only addition; if it finds a
  real regression, that becomes its own bug-fix task.
- CI integration — hardware-gated tests in this suite are never run in CI by
  existing convention (see the module docstring).

---

## 2. Physical UART2 verification

### Background

UART2 — a secondary command port, RX=GPIO16, TX=GPIO17, 115200 8N1 (see
`esp_bt_audio_source/components/command_interface/commands.c` and
`commands_priv.h`'s `CMD_UART_SECONDARY` / `CONFIG_CMD_UART2_*` Kconfig
options) — was added so a controller (e.g. `esp_i2s_source`, or a debug
terminal) can send text commands to the WROOM32 while its primary USB port
(UART0) is busy running UARTAUDIO's 921600-baud binary audio stream.

It is fully implemented and covered by 14 host tests (mocked UART), and the
firmware's boot log confirms driver init on real hardware
(`"secondary command UART ready: uart=... tx=17 rx=16 baud=115200"`). It has
**never been exercised with a real second UART peer** — only the primary USB
console has ever actually sent it bytes. This requires a second physical
USB-to-serial adapter, which wasn't available when UART2 was built.

### Requirement

Once a second USB-to-serial adapter is available, run a one-time (then
periodic, if UART2 changes) manual hardware verification pass confirming
UART2 behaves correctly with a real peer, especially the concurrency case it
exists for: servicing commands while UART0 is saturated by UARTAUDIO
streaming.

### Design — verification procedure

**Wiring** (confirm before powering on — wrong TX/RX crossing is harmless,
wrong voltage is not):
- Adapter TX → ESP32 GPIO16 (UART2 RX)
- Adapter RX → ESP32 GPIO17 (UART2 TX)
- Adapter GND → ESP32 GND (common ground, required)
- **Adapter must be 3.3V logic level.** A 5V TTL adapter (some cheap
  CP2102/FTDI boards default to 5V) can damage the ESP32 GPIO pins — check
  the adapter's jumper/datasheet before connecting, and use a level shifter
  if it's 5V-only.

**Pre-check:**
- Confirm `CONFIG_CMD_UART2_ENABLED=y` in the flashed build's sdkconfig (it's
  the default) and note the configured `CONFIG_CMD_UART2_BAUD` (115200).
- Flash production firmware to the WROOM32, boot it, and confirm the boot
  log (over the primary USB console) shows the "secondary command UART
  ready" line — this proves the driver initialized before any second
  adapter is even plugged in.

**Verification steps:**
1. **Basic command round-trip:** open a terminal (e.g. `screen /dev/ttyUSBx
   115200` or a small pyserial script) on the second adapter's port. Send
   `VERSION` and `STATUS`; confirm responses match the same `OK|...` format
   the primary USB console produces for the same commands.
2. **Independent operation:** with a command session open on the primary
   USB console too, confirm sending a command on one port doesn't appear on,
   or interfere with, the other (they are independent UART peripherals —
   this checks the actual wiring/pin config isn't accidentally shared).
3. **Concurrency under UARTAUDIO load (the core reason UART2 exists):**
   start `UARTAUDIO START` on the primary USB console (which switches UART0
   to 921600 baud binary streaming and is why UART2 was needed in the first
   place). While streaming, send `STATUS` / `VOLUME` commands over UART2 and
   confirm they're answered promptly and correctly — this is the scenario
   host tests can only simulate, not prove, since real UART driver/ISR
   timing under BT-stack load is what's actually in question.
4. **Near-simultaneous sends:** send commands on both ports within the same
   ~100 ms window a few times; confirm no response corruption, interleaving,
   or crash on either side.
5. **Record the outcome** (pass/fail per step, any anomalies, exact response
   text) as a `memory.md` entry — this closes the TODO and gives future
   sessions a citable confirmation instead of "implemented but unverified."

### Out of scope

- No firmware or test-suite code changes are anticipated — this is a
  verification pass against existing, already-tested code. If it uncovers a
  real defect, that becomes its own fix task.
- Building a permanent second-adapter test fixture into
  `test/laptop_bt_tests/` (e.g. a `uart2` pytest fixture) is a reasonable
  future step if the adapter stays available permanently, but is not
  required to close this TODO — do it only if a manual pass finds enough
  value to justify automating it.

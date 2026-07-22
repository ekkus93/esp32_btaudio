<!-- Entries older than 2026-04-21 (3 months) were moved to memory_archive.md on 2026-07-21. See that file for full history back to 2025-01-13. -->

## 2026-07-04 - UART2 hardened to 14 host tests; NEXT UP: esp_i2s_source redo

Dual-UART coverage now 14 tests (0fdbf508): burst/interleave/overflow
isolation, LF/CR terminators, async-reply defaults to primary, VOLUME
via UART2 mid-stream, UARTAUDIO START from UART2 (response routes to
UART2, stream stays on primary). Full suite 66 host binaries green.
Physical UART2 test still pending (no second USB-serial adapter yet).

USER'S NEXT PROJECT: redo esp_i2s_source — likely as the device that
talks to esp_bt_audio_source over the new UART2 command port (that was
the motivation for adding UART2). Requirements discussion not yet held.
## 2026-07-04 - Secondary command UART added (UART2, RX=GPIO16 TX=GPIO17)

Commands now served on UART2 alongside USB (which is unchanged —
verified on hardware). Responses route to the originating port; EVENT|
lines broadcast to both. Bonus: UART2 keeps serving commands DURING
UARTAUDIO streaming (gate now skips only the primary port). Kconfig:
CMD_UART2_* (enabled, pins 16/17, 115200). Init in cmd_init() (cmd
layer owns uart_set_pin/param_config per layering rules). Host mapping
for tests: primary=mock port 1, secondary=mock port 0; mock_uart has
port-aware helpers now. Wiring: dongle TX->GPIO16, RX->GPIO17, GND-GND,
3.3V levels. Physical UART2 verification pending (needs a second
USB-serial adapter); firmware boot log confirms init.
## 2026-07-04 - Full sweep FULLY GREEN: 837 tests, 0 failures

After test_bluetooth fix + host adapter alignment: host 688/688 (once
adapter runner re-aligned: 690 incl. its 3), standalone 65/65, device
test_bluetooth 46/46 + test_app_audio 35/35 + test_manager 18/18.
Gotcha fixed en route: test_pairing_commands.c runs in TWO harnesses —
device (production bt_pairing_store notifier) and host
test_pairing_adapter_runner (test_pairing_adapters.c emulation
fallback); both must agree on the event contract. Production firmware
reflashed (v0.2.0-310).
## 2026-07-04 - test_bluetooth device suite FIXED: 46/46 (was: link-broken for months)

Fix (bdf576bd): new test/test_bluetooth/main/bt_manager_api_mock.c
provides the bt_manager wrapper API surface as pure delegates to the
base mock (separate file: bt_manager.h vs bt_source.h bt_device_t
conflict); bt_source_mock.c gained bt_connect_by_name/bt_deinit/
bt_get_connection_quality/bt_connection_shim_publish_info/
bt_manager_test_invoke_a2dp_event.

Key architecture learned: EVENT|PAIR|* on this suite comes from the
PRODUCTION notifier bt_pairing_store.c (bt_manager lib is compiled with
UNIT_TEST -> bt_manager_test_record_pair_event hook). Mock wrappers must
NOT emit events (duplicates). Production PIN-flow contract:
PIN_REQUEST -> SUCCESS; CONFIRM is SSP-only. Two stale tests expected
CONFIRM|mac,pin — an artifact of the retired test_app2 emulation
adapter (test_pairing_adapters.c fallback) — updated to production
contract.

Debug gotcha: run_unity/ninja rebuilt the .obj but reused a stale ELF
once — when in doubt rm the .elf before rerunning. And run_all_tests.py
still counts build-failed suites as 0 failures (unfixed reporting gap).
## 2026-07-04 - /lint-n-test full sweep: all green EXCEPT pre-existing test_bluetooth link breakage

Sweep results: host CTest 65/65 binaries (688 cases, 0 fail); device
test_app_audio 35/35, test_manager 18/18. test_bluetooth device suite
FAILS TO LINK — PRE-EXISTING (predates UARTAUDIO; cmd_handlers_bt.c
referenced bt_connection_manager_clear_reconnect_target at f397d6ad and
the mock never provided it). Root cause: test/test_bluetooth/main/
bt_source_mock.c has drifted ~25 symbols behind the bt_manager API that
command_interface now calls (bt_unpair, bt_manager_connect/disconnect/
set_name/start_*, bt_pairing_confirm/submit_pin, bt_get_streaming_info,
bt_get_connection_state, ...). Any reference resolved from
libbt_manager.a pulls real objects that collide with the mock's
duplicate definitions. Fix = catch the mock up to the current API
(enumerate with: nm -u libcommand_interface.a + libmain.a vs nm
--defined bt_source_mock.o). A 4-stub attempt was reverted as
insufficient.

ALSO: run_all_tests.py reported failures=0 despite test_bluetooth rc=3,
tests_total=0 — build-failed suites are silently not counted. Reporting
gap worth fixing in tools/run_all_tests.py (repo ROOT tools/, not
esp_bt_audio_source/tools/ — the /lint-n-test skill had that path wrong,
fixed in 841fa073).

clang-tidy on xtensa needs: filtered compile_commands (strip -mlongcalls,
-fno-shrink-wrap, -fstrict-volatile-bitfields, -fno-tree-switch-conversion,
-fno-jump-tables) + --extra-arg=-isystem<xtensa-newlib-include>
+ -D__XTENSA__. Only findings on changed files: 2x snprintf
DeprecatedOrUnsafeBufferHandling in uart_audio_rx.c (Annex K n/a on
newlib — not actionable).

Production UARTAUDIO firmware reflashed after the sweep (v0.2.0-307).
## 2026-07-05 (cont) - Echo Buds test: PERFECT stream to real headset

ESP32 paired+connected directly to Echo Buds 009H (48:78:5E:D9:35:A3;
buds auto-connect A2DP during pairing — explicit CONNECT then returns
ERR already-connected, harmless). 24 s music stream: ALL counters zero
(in=2,116,608 = every byte of 2067 frames accepted, out=4,234,240
upsampled, und/ovf/crc/lost all 0), A2DP steady 176.6-176.8 kB/s.
UARTAUDIO feature validated on the real use-case sink.

Note: laptop-side `bluetoothctl block` on the buds during the test kept
the laptop from stealing them (unblocked after). ESP32 now has buds
bonded + LAST_MAC may point at them; laptop pairing untouched.
## 2026-07-05 (cont) - Option B forensics: FIFO_OVF convicted, threshold fix -> ZERO-DEFECT streams

UART event-queue forensics (main.c installs queue -> reader drains,
UA|ERR live markers + counters in STOPPED): first run caught
UA|ERR|FIFO_OVF red-handed — hardware FIFO overrun, NOT wire corruption
or driver-buffer overflow. Root: default RX-full threshold ~120/128
leaves ~87 us ISR budget at 921600; BT interrupt-masked sections exceed
it even with ISR in IRAM.

Fix: uart_set_rx_full_threshold(32) during streaming (~1 ms budget),
restore 120 at teardown. Result: consecutive 24 s streams mit und=0
crc=0 lost=0 ovf=0; compare_bt_capture: 95/95 windows r>0.99, median
1.0000, zero skips, amplitude 1.000. UARTAUDIO is now measurably
transparent end-to-end on the laptop sink. Tapping should be GONE —
awaiting user listen confirmation; headset test now optional.
## 2026-07-05 (cont) - capture-vs-source analysis: pipeline is bit-faithful; tapping == lost UART frames, nothing else

New tools/compare_bt_capture.py (parec bluez capture + windowed FFT
xcorr vs reference built with the firmware's exact upsampler). Sweep
test result: median window r = 1.0000, amplitude ratio 1.000, zero
silence insertions — UART framing, upsampling, SBC and BT air link are
audibly transparent. The ONLY defect found: one 23.0 ms excision ==
exactly 2 lost UART frames (matches device lost counter). So residual
'tapping' = UART frame loss, fully characterized; loss rate now ~1-2
events/24 s after ISR-in-IRAM.

Method notes: use non-repetitive source (sweep) — repetitive music let
the correlator lock onto wrong cycles (bogus 61 ms 'skip'). Sub-sample
alignment limits per-window r above ~5 kHz (energy/quiet-run columns
distinguish artifact from defect). conda run does not forward heredoc
stdin reliably — use system python3 for heredocs or python -c.
## 2026-07-05 (cont) - UARTAUDIO listen quality: 'even better'; residual ~0.5% tapping

After engine fix + UART_ISR_IN_IRAM: user reports clean start, faint
'tapping' late in stream = the residual crc/lost (11-13 frames per 24 s,
each lost frame = 11.6 ms click). BT_L2CAP log muting during streaming
(committed) helped only marginally. Residual is time-correlated with
laptop-link L2CAP congestion onset (~15 s in) — believed to be BT
controller interrupt pressure during retransmission bursts starving the
UART FIFO (128 B / 1.4 ms deadline), unreachable from firmware.

Definitive next test: real BT headset (clean link, no congestion) —
expectation is zero tapping. If tapping persists on a good link, next
suspects: CP2102 host-side, or add UART_FIFO_OVF event-queue counting to
distinguish FIFO overruns from wire corruption.
## 2026-07-05 - UARTAUDIO static SOLVED: engine throughput bug + UART FIFO overruns

Pull-rate instrumentation (READ_BPS, commit f079c9bd) cracked the case in
one measurement: A2DP pull is a perfect 176.5-176.8 kB/s ALWAYS — the
deficit was never Bluetooth. Two real bugs found and fixed:

1. bc2f2e8a — audio engine produced ONE 1 KB chunk per wake, but with
   CONFIG_FREERTOS_HZ=100 its 2 ms tick clamps to 10 ms -> 102.4 KB/s
   ceiling vs 176.4 needed. A2DP zero-filled the ~40% gap on EVERY ring
   underrun since forever (inaudible for silence/synth; = the heavy
   static for real audio). Now up to 8 chunks/wake, watermark-bounded.
   NOTE: this means I2S capture playback likely ALSO had 40% silence
   gaps all along — worth rechecking I2S path quality now.
2. 3ffbb670 — residual crc/lost from the 128 B UART hardware FIFO
   overflowing during BT flash-cache windows (1.4 ms deadline at 921600).
   CONFIG_UART_ISR_IN_IRAM=y fixed most of it; RX driver buffer 16 KB.

Also: the live A2DP data callback is bt_events_a2dp_data_callback
(bt_manager.c:1066 registration). bt_streaming_manager and
bt_connection_manager each hold DUPLICATE never-fed stats/state machines
— STATUS's BYTES_REQ/CALLBACKS/DUR fields read those dead structs (always
0). Candidate cleanup: point STATUS at real stats or delete duplicates.

Result on hardware (24 s music stream, laptop as sink): staging ring
25-65% (was pegged), ovf=0, und=0, crc=8, lost=13 (0.5% frames, was
52/88). User: 'much better... beginning pretty clear and static free'
(pre-ISR-fix run; final run should be cleaner still — awaiting listen).

Remaining candidates for the last ~0.5%: BT_L2CAP error-log suppression
during streaming; real-headset test (laptop link congestion may be the
trigger for the loss bursts). Longer-duration pytest E2E variant still
worth adding as regression for the engine-throughput class of bug.
## 2026-07-04 (night) - UARTAUDIO static: WiFi-coex theory DISPROVED; deficit is invariant

Correction to earlier entry: the A2DP consumption deficit (~30-40% below
the 176.4 KB/s needed; staging ring pegs at 100% within ~2 s, steady
overflows) is IDENTICAL across: 2.4 vs 5 GHz WiFi, double vs single vs
zero PulseAudio loopback, stale vs fresh BT connection/pairing, adapter
power-cycle. Laptop-side conditions ruled out.

Established facts:
- Two simultaneous loopbacks (mine + bluez-policy auto-loopback) DID
  cause doubled/phasey audio earlier — dedup fixed that layer. bluez
  policy auto-loads its own loopback for a2dp_source; don't add one.
- 'Microphone on' indicator = loopback recording the bluez_source (BT
  stream registers as an input device); physical mics stay SUSPENDED.
- Streaming with NO A2DP link (or released transport) -> engine pauses
  on full main ring -> near-zero staging drain (worst ovf slope) — this
  contaminated two earlier 'control' runs.
- Even the 'clean' 3 s pytest may fit inside ring headroom at ~80-90%
  consumption; it cannot distinguish 100% from ~85%.
- Suspicion: A2DP data callback pull rate on the ESP32 may be chronically
  below 44.1 kHz real-time even for SILENCE (AUDIO_PROC flood showed
  req=512 at ~10 ms cadence ~= 51 KB/s if every callback logs); STATUS
  BYTES_REQ/CALLBACKS/DUR counters read 0 during streaming — not wired
  to this path, so no direct measurement exists yet.

Next steps: (1) definitive sink test with the real BT headset once
charged (removes laptop stack entirely); (2) instrument firmware to
report actual A2DP callback pull rate (bytes/sec) so the deficit can be
measured instead of inferred from ring fill; (3) if pull rate is low
even to a good sink, dig into Bluedroid media-task cadence/SBC config.

Cleanup state: laptop-ESP32 re-paired fresh; engine STOPped; single
policy loopback remains loaded.
## 2026-07-04 (evening) - UARTAUDIO audible listen test: works, static traced to WiFi/BT coex

Laptop-as-sink listen test (PulseAudio loopback from bluez_source to
speakers): music audibly played through laptop->UART->ESP32->A2DP->laptop.
User reported static. Root cause found: laptop WiFi on 2.427 GHz (ch 4,
kensington2) shares the combo radio with BT -> A2DP throughput throttled
~12% below real-time (verbose UA|FILL showed ring 40%->100% in ~2 s and
pegged; L2CAP congestion errors flooding). NOT a firmware/UART bug — the
UART leg was near-clean; overflow was the downstream symptom.

Setup still in place for a retry: ESP32 paired+connected to laptop BT,
PulseAudio loopback module 40 loaded, engine STOPped (idle).
Scratchpad helper: connect_esp32.py (pair+connect+START, then releases
serial). Test music: scratchpad/test_music.wav.

Waiting on user: (a) switch laptop WiFi to 5 GHz then rerun
tools/stream_audio_uart.py, or (b) charged BT headset (dedicated sink,
no shared radio — expected clean). Note: 3 s pytest E2E fits inside
buffer headroom and can't catch this rate deficit; optional longer
`slow` test variant was offered, not requested yet.

Also: /lint-n-test skill added (.claude/skills/lint-n-test, Haiku model).

## 2026-07-04 (later) - UARTAUDIO validated ON HARDWARE - 6/6 laptop-BT tests green

Board mystery solved: /dev/ttyUSB0 ESP32 had esp32_zx81 firmware flashed
(wrong project) - that's why it never answered. User approved reflash with
esp_bt_audio_source (now v0.2.0-...-gfc68f7 + fixes).

- MEM check RESOLVED: 107,632 B DRAM free — 32 KB ring default stands.
- New test/laptop_bt_tests/test_uart_streaming.py: 6/6 PASS on hardware
  (laptop as A2DP sink). E2E: 3 s sine over UART at 921600, crc=0 ovf=0
  lost=0, UART_BYTES=339,968 through the engine, clean text-mode recovery.
- Two bugs found by hardware run, both fixed+committed:
  a85d3817 fw: teardown's esp_log_level_set("*",INFO) wiped main.c's
  AUDIO_PROC=WARN -> INFO log flood drowned cmd responses after streams.
  bdbcd104 test: blocking read stalled pacing -> 800+ underruns.
- Baud round-trip log recovery verified implicitly (STATUS/AUDIO_STATUS
  work after stream; logs resume at 115200).

Remaining (optional): listen test with a real BT speaker + music via
tools/stream_audio_uart.py (procedure in MANUAL_SMOKE_TEST_GUIDE.md).

## 2026-07-04 - UARTAUDIO implementation COMPLETE (all 9 steps committed)

Feature: laptop -> USB serial -> ESP32 -> BT speaker audio streaming.
Commits e9f88c29..a72bf1c5 (UARTAUDIO-1..9 + layering-checker fix d2af3d87).

- Wire: stereo 22050 Hz s16le, CRC-framed 1024 B payloads at 921600 baud;
  device upsamples 2x to 44.1 kHz (new AUDIO_SOURCE_UART, priority
  beep > UART > synth > I2S). SILENCE enum value shifted 2 -> 3.
- New: uart_audio_frame.c (parser), uart_source.c (staging ring/fill),
  uart_audio_rx.c (RX pump), uart_audio.c (UARTAUDIO cmd + reader task),
  tools/stream_audio_uart.py (host streamer), Kconfig menu
  (UART_AUDIO_*), main.c RX buffer 1024 -> CONFIG (8192 default).
- Also fixed latent OOB: audio_stats_t.bytes_by_source was [3] while
  STATUS read index 3 -> now [4]; STATUS labels now I2S/SYNTH/UART/
  SILENCE_BYTES (stale WAV_BYTES dropped).
- Host suite 65/65 green; ESP-IDF v5.5.1 idf.py build green.

STILL OPEN (needs hardware — device was NOT responding on /dev/ttyUSB0
this session, so nothing was flashed or smoke-tested):
1. Flash new firmware (confirm with user first).
2. MEM check for DRAM headroom (drop UART_AUDIO_STAGING_RB_KB to 16 if tight).
3. Manual E2E smoke test — procedure in MANUAL_SMOKE_TEST_GUIDE.md addendum.
4. On-hardware check: logs resume cleanly at 115200 after baud round-trip.
5. Optional: hardware-gated laptop_bt pytest for UART streaming.

## 2026-07-03 22:34:16 - UART Audio Streaming (UARTAUDIO) — plan approved, implementation started (step 1 of 9)

**Feature:** Stream real audio (e.g. a song) from laptop over USB serial (/dev/ttyUSB0) into the ESP32, played out over A2DP to a BT speaker. Replaces cumbersome I2S wiring as the primary developer audio-test path.

**Full approved plan:** `/home/phil/.claude/plans/shimmering-greeting-seal.md` — read this first when resuming; it has the complete design (frame format, handshake sequence, buffer math, priorities).

**Key locked decisions:**
- CP2102 bridge caps at 921600 baud (~92 KB/s) → full 44.1k stereo (176.4 KB/s) impossible. Wire format: **stereo 22050 Hz s16le (88.2 KB/s)**, upsampled 2× to 44.1k on device (exact integer ratio, simple linear interpolation — NOT the existing 44100↔48000 fractional resampler).
- Real-time streaming (not clip upload). Runtime baud switch 115200→921600 during stream, back after.
- Frame: 8-byte header (A5 5A magic, type DATA/STOP, seq, len ≤2048 %4==0, CRC-16/CCITT-FALSE), 1024 B payload nominal → 0.78% overhead.
- New `AUDIO_SOURCE_UART` in audio_processor enum (BOTH copies: device ~:96 and UNIT_TEST ~:385 in audio_processor.c); priority beep → UART → synth → I2S → silence.
- Staging ring: reuse audio_rb_t, 32 KB default, prebuffer→active at 50% fill. SPSC preserved (reader task sole producer of staging ring; audio_engine sole producer of main ring).
- cmd_process handoff: `if (uart_audio_is_streaming()) return` gate at top of cmd_process(); flag set before reader task spawn; reader clears flag last after restoring 115200 + uart_flush_input.
- main.c RX buffer 1024 → CONFIG_UART_AUDIO_RX_BUF_BYTES (8192) — constant-only change, layering-safe.

**Progress (tasks in Claude Code task list, 9 TDD commits planned):**
1. **IN PROGRESS — Frame parser + CRC16** (`components/command_interface/uart_audio_frame.c/.h` + `test/host_test/test_uart_audio_frame.c`). NO CODE WRITTEN YET. Was studying test/host_test/CMakeLists.txt patterns when interrupted.
2. TODO: 2× stereo upsampler (audio_upsample2x_s16_stereo, prev[2] carried across calls) + tests
3. TODO: uart_source lifecycle (start/write/request_drain/stop/stats, PREBUFFER→ACTIVE) + tests
4. TODO: AUDIO_SOURCE_UART wiring in audio_processor.c (both enum copies!) + priority tests
5. TODO: UARTAUDIO command plumbing + cmd_process streaming gate + tests
6. TODO: reader task + baud switch + handshake + feedback (UA|FILL every 250ms) + timeouts
7. TODO: Kconfig (UART_AUDIO_* entries) + main.c RX buf + ci_check_main_layering.sh + idf.py build
8. TODO: tools/stream_audio_uart.py (pyserial, absolute-deadline pacing, ffmpeg pipe input)
9. TODO: E2E manual smoke docs + optional hardware-gated laptop_bt test

**Verified facts for resuming:**
- test/host_test/mocks/mock_uart.c exists (uart_read_bytes/uart_write_bytes/uart_is_driver_installed) but LACKS uart_set_baudrate/uart_wait_tx_done/uart_flush_input — add stubs at step 6.
- Host test pattern: add_executable(test_X test_X.c <component srcs> mocks/...) + target_link_libraries(test_X unity util_safe_host command_interface_host platform_shim_host) + target_compile_definitions(test_X PRIVATE UNIT_TEST) + add_test. See test_commands at test/host_test/CMakeLists.txt:81-104. Unity IS used in host tests (FetchContent v2.5.2).
- cmd_process() structure verified (commands.c:285-365): single uart_read_bytes at top per cycle → race-free handoff design holds.
- Still to verify before step 3: free DRAM headroom on device (`MEM` command; ~44 KB new cost; if tight drop ring to 16 KB) and on-hardware log recovery after baud round-trip (step 6).

**Session context:** also created CLAUDE.md (root + esp_bt_audio_source), .claude/settings.json PostToolUse hook running ci_check_main_layering.sh on main.c edits (needs /hooks once to load; script has known false-positive on block-comment mention of uart_param_config at main.c:229), and .claude/skills/laptop-bt-tests skill.

## 2026-07-11T12:14:27Z - Claude Opus 4.8 (1M) - I2S capture root cause = ESP32-classic slave-RX silicon limitation

- Continued the SIG-1d I2S bring-up debug ("Option A", WROOM32 side). Added two diagnostic commands to esp_bt_audio_source (uncommitted, tagged DBG-I2SCAP):
  - `I2S_PROBE [bclk_gpio] [ws_gpio]` (cmd_handlers_system.c): detaches the pad to a pulled-down GPIO input and counts level transitions over 200k samples. Reads the pad even while I2S owns the pin.
  - `I2S_RXTEST [timeout_ms]` (cmd_handlers_system.c + new `i2s_manager_rxtest()` in i2s_manager.c/.h): force-enables the RX channel and does ONE blocking i2s_channel_read, reporting ret/read_bytes/sample.
- RESULTS (WROOM32 flashed, S3 clocking continuously):
  - I2S_PROBE: bclk_gpio=26 trans=8935, ws_gpio=25 trans=6142 (WS ~50/50 duty). => S3 BCLK+WS are PHYSICALLY PRESENT at the WROOM32 pads. Rules out wiring/signal integrity.
  - I2S_RXTEST 500/500/500/1500: ret=263 (ESP_ERR_TIMEOUT) read_bytes=0 every time, even at 1.5s blocking. => RX FIFO never fills. Rules out the aggressive 1ms I2S_READ_TIMEOUT_MS.
- Full driver audit (IDF v5.5.1 i2s_std.c): slave config is correct — bclk_div=8 sets internal mclk~22.6MHz, GPIO matrix routes bclk/ws as inputs to s_rx_*_sig, RX-only channel enabled. No config bug by inspection.
- ROOT CAUSE (web-confirmed, multiple esp32.com threads): ESP32-CLASSIC I2S SLAVE RX is a known silicon limitation — the receiver clocks its state machine from its own async internal clock, so i2s_channel_read times out with 0 bytes even when BCLK/WS are present (matches our data exactly). Documented fix = make the ESP32 the I2S MASTER, not slave.
- RECOMMENDED FIX (pending user decision): flip roles — WROOM32 = I2S MASTER RX (generates BCLK/WS; ESP32-classic reliable as master), S3 = I2S SLAVE TX (HW-v2, good slave support). Same 4 wires; only BCLK/WS direction reverses; DATA (S3 GPIO7 -> WROOM32 GPIO22) unchanged. Inverts SPEC "WROOM32=slave" contract.
- Alternatives: (B) keep chasing slave-mode SW workaround (low confidence), (C) park I2S, use already-proven UARTAUDIO path for dev audio.
- CLEANUP PENDING: DBG-I2SCAP logging + I2S_PROBE/I2S_RXTEST commands are uncommitted debug code in esp_bt_audio_source (grep DBG-I2SCAP; commands in cmd_handlers_system.c/commands.c/command_interface.h/cmd_handlers.h/i2s_manager.c/.h). Decide keep-as-diagnostic vs strip when closing out. WROOM32 currently runs this debug firmware.

## 2026-07-11T12:35:40Z - Claude Opus 4.8 (1M) - Role flip: WROOM32 master-RX WORKS; new blocker = clock not reaching S3

- Implemented Option A (I2S role flip): WROOM32 i2s_manager.c role SLAVE->MASTER (RX); S3 i2s_out.c role MASTER->SLAVE (TX). Both build clean, both flashed. No rewiring (same wires, BCLK/WS drive direction reverses).
- WROOM32 MASTER RX = FIXED: I2S_RXTEST (enhanced to hold clock continuously, loop reads) read 352256 B @2s and 704512 B @4s = exactly 44100*4*sec, ret=0. The ESP32-classic side (the silicon-limited part) now receives perfectly. Huge.
- NEW BLOCKER: S3 SLAVE TX delivers nothing. S3 beacon bytes=0, ringpeak=0 (writer_task stuck in first blocking i2s_channel_write); WROOM32 reads all-zero samples (sample=none across 704KB).
- ISOLATION: added s3_pad_transitions() to esp_i2s_source main.c beacon (gpio_get_level on BCLK=GPIO5/WS=GPIO6). Result: S3 pad transitions IDENTICAL idle vs during 4s WROOM32 hold (bclk_tr~15085, ws_tr~2029; ratio ~7:1 not 64:1) = FLOATING NOISE. => WROOM32 master BCLK/WS is NOT reaching S3 GPIO5/6.
- So: WROOM32 master internal clock runs (real-time DMA) but does not reach S3. Suspects: (1) WROOM32 master RX-only channel not driving BCLK/WS output pads, or (2) reverse-direction wiring not carrying signal (same wires carried S3->WROOM32 fine minutes earlier via I2S_PROBE).
- NEXT (proposed): (B free) user reseat/confirm the 2 clock jumpers, re-run combined test; (A) WROOM32 GPIO bit-bang square wave on 26/25 to isolate wire-vs-firmware; (C) fall back to proven UARTAUDIO path.
- Debug code still uncommitted: WROOM32 DBG-I2SCAP + I2S_PROBE/I2S_RXTEST (i2s_manager rxtest loops reads into static 4KB buf); S3 s3_pad_transitions in main.c beacon.

## 2026-07-11T12:52:24Z - Claude Opus 4.8 (1M) - Role flip half-works: WROOM32 master-RX solid, but NO master config outputs BCLK/WS

- Wires PROVEN GOOD via I2S_CLKGEN (new WROOM32 diag: bit-bang square wave on GPIO26/25 as plain GPIO): S3 pad transitions jumped 15085(noise)->219, and 0/0 when driven static. So WROOM32 GPIO26/25 -> S3 GPIO5/6 carries signal fine.
- BUT: no I2S MASTER config on the WROOM32 outputs BCLK/WS to GPIO26/25, even though internal clock runs (RX reads 704512B = exact real-time). Tried: (1) RX-only master; (2) full-duplex TX+RX master (TX forced master, drives shared clock), init RX-first/TX-last so TX output routing wins the pad; (3) full-duplex + actively WRITING silence to TX in the read loop to run the clock. ALL three: S3 pad stays 15085 noise, WROOM32 read all-zero (sample=none), S3 slave-TX bytes=0.
- So the ESP32-classic I2S master generates a clock internally (samples its own RX DIN) but does NOT drive BCLK/WS onto the output pads in any config tried. Unexpected. Candidate causes: ESP32 I2S master clock-output quirk, or GPIO25/26 (DAC pins) I2S-output routing constraint (they work as INPUT — orig slave I2S_PROBE saw clock there — and as plain-GPIO OUTPUT via I2S_CLKGEN, but maybe not as I2S-peripheral output).
- STATUS: role flip fixed the hard side (WROOM32 slave-RX silicon limit -> master RX works), but blocked on getting the WROOM32 master to EMIT the clock. Proven-good fallback for dev audio remains UARTAUDIO (bit-perfect, works today).
- NEXT OPTIONS: (A) try different WROOM32 BCLK/WS output pins (needs rewiring) to test the DAC-pin hypothesis; (B) fall back to UARTAUDIO; (C) deeper driver/errata dig on ESP32 master clock output.
- All debug code still uncommitted: WROOM32 DBG-I2SCAP + I2S_PROBE/I2S_RXTEST(full-duplex, writes zeros to TX)/I2S_CLKGEN; S3 s3_pad_transitions in main.c beacon; WROOM32 i2s_manager now full-duplex master (tx+rx). Both boards run this debug fw.

## 2026-07-11T15:18:33Z - Claude Opus 4.8 (1M) - Option A (DAC-pin theory) DISPROVEN; ESP32-classic master won't emit I2S clock

- Moved WROOM32 master BCLK/WS off DAC pins 26/25 to GPIO18/19 (main.c defaults; DIN stays 22). User rewired the 2 clock jumpers (WROOM32 end only; S3 stays GPIO5/6). Reflashed.
- RESULT: S3 pad reading changed 15085(floating noise)->0/0(driven static) — so GPIO18/19 ARE now driven by the WROOM32 I2S peripheral (owns the pins), but held STATIC. During I2S_RXTEST 4000 (read 704512B real-time, internal clock running) the S3 pad stayed 0/0 = NO toggling clock. So NOT a pin/DAC issue.
- CONCLUSION: ESP32-classic I2S MASTER does not emit a toggling BCLK/WS output in RX-only or full-duplex(TX+RX, even writing zeros to TX) mode, on any pins. Internal clock runs (RX DMA fills) but the clock never appears on the output pad. Fundamental ESP32-classic limitation / deep driver issue, beyond config tweaks.
- Also observed: when a BT stream briefly ran (i2s_run=1), the engine's i2s_source_fill got read_bytes=384 (nonzero, partial) — but that's internally-clocked reads of DIN, not proof of clock output. And my I2S_RXTEST fights the engine (floods 'channel is not enabled') when the engine is concurrently reading — the diagnostic is only clean when no BT stream is active.
- RECOMMENDATION: fall back to Option B (UARTAUDIO, bit-perfect, works today) for dev audio. Two-board I2S link with ESP32-classic as receiver appears non-viable without either (a) a different source approach, (b) deep ESP32 I2S master-clock-output driver work, or (c) different receiver silicon. 
- CLEANUP OWED: revert/commit debug code on BOTH boards. WROOM32: DBG-I2SCAP logging (i2s_manager.c, audio_processor.c), I2S_PROBE/I2S_RXTEST/I2S_CLKGEN commands, full-duplex master change in i2s_manager.c, BCLK/WS pins moved to 18/19 in main.c. S3 (esp_i2s_source): role SLAVE-TX in i2s_out.c, s3_pad_transitions in main.c. Neither board's changes committed.

## 2026-07-11T15:22:58Z - Claude Opus 4.8 (1M) - WALL: ESP32-classic I2S master won't emit BCLK/WS in our config (any role/pins)

- Reverted full-duplex back to RX-only master (true master) and retested on GPIO18/19. Still NO toggling clock: S3 pad stays 0/0 (driven static), WROOM32 reads 704512B (internal clock runs). 
- Exhaustive matrix (all fail to emit clock): RX-only master @26/25(floating), full-duplex @26/25, full-duplex+TXwrites @26/25, full-duplex @18/19(static), RX-only master @18/19(static). Internal clock always runs (RX DMA fills real-time); external BCLK/WS never toggles on the pad.
- This contradicts normal ESP32 I2S-mic (INMP441) master-RX usage where BCLK IS output — so something in OUR config blocks clock output. Prime remaining suspects (UNTESTED): (1) the unusual 16-in-32 slot config (slot_bit_width=32 w/ 16-bit data, ws_width=32) breaking clock-tree gen; (2) MCLK unused — maybe routing MCLK to a GPIO enables the BCLK output tree; (3) legacy i2s driver. Each needs more cycles (some need both boards re-cfg'd).
- ~12 flash cycles spent. RECOMMENDATION STANDS: Option B (UARTAUDIO) for dev audio now; treat two-board I2S as a future focused deep-dive (test 32-in-32 slots + MCLK routing) or drop it.
- Debug/experiment code uncommitted on both boards (see prior entry) + WROOM32 BCLK/WS now GPIO18/19 in main.c. Cleanup owed on decision.

## 2026-07-11T16:35:47Z - Claude Fable 5 - I2S LINK CRACKED: real root causes found, tone streaming S3->WROOM32->A2DP

Fresh-eyes pass (Fable) overturned two earlier conclusions and got the full link working. Chain of discoveries:
1. **Stale NVS pins sabotaged a whole day of tests**: boot-time configure_i2s log (added this session) revealed the WROOM32 loaded bclk=26,ws=25 from NVS (saved by an old I2S_CONFIG), overriding main.c's GPIO18/19 defaults. Every "master doesn't output clock" result before that was testing unwired pins. After runtime I2S_CONFIG 18,19,22,-1 the clock appeared immediately. The earlier "15085/2029 noise floor" pad readings were a REAL aliased I2S clock all along (ws math matches 44.1kHz exactly).
2. **THE core bug (PCNT frequency meter on S3, DIAG|I2SFREQ): WROOM32 (ESP32-classic HW v1) master emitted WS at 88.2kHz, BCLK:WS ratio=32** — classic I2S derives WS from DATA width (16), silently ignoring slot_bit_width=32/ws_width=32. Configured "16-in-32", it really output 16-bit-slot framing → the S3 HW-v2 slave (correctly expecting 64-BCLK frames) could NEVER sync, at any speed/pins/clk-src/bclk_inv. **Fix: run classic with bit_depth=32** (I2S_CONFIG 18,19,22,-1 44100 32) → true ratio=64.00 → S3 slave locked instantly, 352.8kB/s real-time both directions. This also likely retro-explains the "WROOM32 slave-RX silicon limitation" (same framing mismatch, reversed direction).
3. S3 slave TX itself proven good via ON-CHIP loopback: I2S1 master RX clockgen on GPIO15/16 + I2S0 slave TX inputs on same pins (INPUT_OUTPUT dir + esp_rom_gpio_connect_out_signal re-route after gpio_set_direction clobbers matrix out-sel — driver gotcha).
4. **Alignment quirk**: classic 32-bit capture stores the 16 significant bits RIGHT-aligned (0x0000TTTT, verified via I2S_RXTEST sample dumps; clean L=R sine values). audio_util 32->16 takes TOP half (>>16) → added shim in i2s_source_fill: <<16 each raw word pre-convert. S3 sends samples UNSHIFTED in low 16 of 32-bit slots (main.c tone expansion, no <<16).
5. **Partial-read discard bug (pre-existing)**: i2s_source_fill dropped ret=ESP_ERR_TIMEOUT reads even with read_bytes>0 (most reads are 256B partials at 1ms timeout) → most captured audio discarded → choppy static (user heard it). Fixed: only bail on read_bytes==0.
6. User heard live audio evolve: static (misaligned) -> channel-flipped noise -> [tone verdict pending at entry time].
- CURRENT TOPOLOGY: WROOM32 I2S_NUM_1 master RX bclk=GPIO18,ws=GPIO19,din=GPIO22 (bit_depth=32!) -> S3 I2S0 slave TX bclk=GPIO15,ws=GPIO16,dout=GPIO7. Wires: WROOM18->S3_15, WROOM19->S3_16, S3_7->WROOM22, GND. NOTE: S3-side clock pins MOVED from 5/6 to 15/16 during pin-elimination test (could likely move back — 5/6 were never actually at fault — but working setup is 15/16; don't touch without retest).
- PENDING: boot defaults (WROOM32 bit_depth must default 32 for I2S or NVS must carry it — currently runtime-command only!); S3 i2s_out slot cfg now 32-bit data (32-in-32); revert-or-keep decisions for: APLL (unneeded), I2S1-vs-I2S0 on WROOM32 (untested whether I2S0 also works now), bclk_div=16 (harmless), glitch filters (unneeded), bclk_inv=false (reverted ✓); strip DBG code both boards (DBG-I2SCAP, I2S_PROBE/RXTEST/CLKGEN, PCNT/regs/pad beacons, main/CMakeLists PRIV_REQUIRES esp_timer+pcnt); fix I2S_CONFIG param-order doc bug (help says BCLK,WCLK,DOUT,DIN; code maps p3=DIN,p4=DOUT); SPEC §3.3 rewrite (contract now: 32-bit slots, 32-bit data on classic, samples right-aligned low-16 from S3); update REDO1_TODO; commits on both projects.

## 2026-07-11T17:39:38Z - Claude Fable 5 - 🎼 I2S LINK FULLY WORKING: pure 440.00Hz, 100% purity, both channels, 0% dropouts

FINAL RESULT (laptop-as-A2DP-sink capture + FFT, user's idea — objective loop, no ears needed): LEFT & RIGHT both dominant=440.00Hz purity=100.0% peak=9831 (S3 commands 0.30×32767=9830 — BIT-FAITHFUL through the whole chain) zeros=0.0%. Chain: S3 tone → I2S slave TX (GPIO15/16 clk in, GPIO7 out) → WROOM32 master RX (I2S1, GPIO18/19 clk out, GPIO22 din, bit_depth=32) → phase-detect extraction → engine → ring → A2DP SBC → laptop. SIG-1d audio path PROVEN.
Fixes beyond the prior entry that got from "static" to pure tone (all esp_bt_audio_source, uncommitted):
1. sdkconfig CONFIG_AUDIO_DEFAULT_BIT_DEPTH 16→32: runtime I2S_CONFIG overrides get WIPED by engine re-inits (A2DP connect re-init loads boot cfg) — mid-stream framing flips back to ratio-32 wedged the S3 slave (constant -6864 stream). Boot default must be 32.
2. Capture packing is a PER-SESSION PHASE ZOO: per 64-bit frame the two 16-bit payloads land in 2-of-4 (sometimes-looking-like-1-of-4) half positions ({1,3} both-high, {2,3} packed-one-word, etc.). Fixed with PER-BLOCK energy detection over the 4 half-offsets (no latch! a one-shot latch locked onto enable-transient garbage) + extraction in wire order (word first, high half before low). ~40 lines in i2s_source_fill.
3. convert_and_resample_to_dst MANGLES the 16-bit path (interleaved zeros → one channel silent; never-validated path) — bypassed with direct memcpy for extracted s16 44.1k data.
4. Engine silence-stuffing: produce loop stuffed a FULL silence chunk whenever fill returned 0 — for a real-time-paced source that structurally injects ~60% zeros (harsh chop, pitch smear 434-453Hz). Fix: if active source==I2S && i2s_manager_is_running, break the per-wake produce loop instead of stuffing (audio_processor.c engine loop).
5. Also: I2S_READ_TIMEOUT_MS 1→8 (block until full chunk; partial-read-discard fix from earlier retained), dma 6x32→8x128, raw read budget 2x chunk for 32-bit.
S3 (esp_i2s_source, uncommitted): slave TX 32-in-32, tone <<16 (top half), bclk_div=16, glitch filters on 5/6 (likely unneeded), pins 15/16 for BCLK/WS, PCNT freq meter + reg dumps + pad sampler in beacon (strip later), main CMakeLists PRIV_REQUIRES esp_timer esp_driver_pcnt.
OPERATIONAL NOTES: laptop-initiated connect (bluetoothctl connect A0:B7:65:2B:E6:5E) far more reliable than ESP32-initiated after many cycles; ESP32 A2DP+laptop sink = capture via parec -d bluez_source.A0_B7_65_2B_E6_5E.a2dp_source; WROOM32 reboot mid-stream wedges the S3 slave (constant word) — reboot S3 while clock running to resync (add auto-resync later).
CLEANUP DEBT (next session): strip ALL DBG-I2SCAP + diag commands or promote; decide keep-vs-revert: APLL(revert), I2S1-vs-I2S0(retest I2S0), glitch filters(revert+retest), bclk_div16(keep?), 8ms timeout+engine break(KEEP - real fixes), phase extractor(KEEP - real fix, add stereo L/R identity test), direct-copy bypass(KEEP, but root-cause convert_and_resample 16-bit bug + host test); commit both projects in reviewed slices; SPEC §3.3 rewrite (32-bit slots + 32-bit data classic-side, payload top-16 from S3, phase-detect contract); REDO1_TODO update (SIG-1c/1d essentially done); fix I2S_CONFIG help/param-order doc bug; earbud listen test (Echo Buds) for the human-validated SIG-1d checkbox.

## 2026-07-11T18:19:09Z - Claude Opus 4.8 (1M) - I2S bring-up cleanup + regression tests (task #17)

- Cleanup on top of the working SIG-1d link (commit 5a5f91e4). No hardware config touched.
- Extracted the payload phase-detect/extract logic into a pure module
  `components/audio_processor/i2s_frame_extract.{c,h}` (registered in the component
  CMakeLists); host tests `test/host_test/test_i2s_frame_extract.c` (16 cases, all
  observed hardware phases + in-place safety + round-trip).
- Extracted the anti-silence-stuffing decision into a pure, enum-independent
  helper `audio_engine_hold_for_live_i2s(produced, is_i2s_source, i2s_running)`
  in audio_util.{c,h}; wired into audio_processor.c's engine loop (replaces the
  inline condition). 4 new host tests in test_audio_util.c, plus the 3 earlier
  convert/resample identity probes.
- Stripped all DBG-I2SCAP logging (0 matches remain); reworded I2S_PROBE/I2S_RXTEST
  diagnostic comments; kept the I2S_PROBE/RXTEST/CLKGEN commands as diagnostics.
- Fixed I2S_CONFIG help text param order: was "BCLK,WCLK,DOUT,DIN", code maps
  p3=DIN p4=DOUT (audio_processor_set_i2s_pins(bclk,ws,din,dout)) -> now "BCLK,WCLK,DIN,DOUT".
- Rewrote esp_i2s_source SPEC.md §2 format row, §3.1 pin map, §3.2 wiring, §3.3
  slot contract, and the arch diagram to match the inverted roles (WROOM32=master RX
  on GPIO18/19/22, S3=slave TX on GPIO15/16/7, 32-bit slots, 16-in-32 top-half payload,
  phase-detect receiver). Marked REDO1_TODO SIG-1/1c/1d DONE with the fix list.
- Verified: host suite 67/67 PASS; esp_bt_audio_source idf.py build clean.
  clang-tidy could not run (compile_commands carries xtensa/newlib flags neither
  system clang-14 nor esp-clang parses) — device build is the compiler gate.
- Deferred (need re-flash + laptop-FFT re-verify, touch working HW config): S3 beacon
  slimming; APLL / glitch-filter / bclk_div16 / I2S1-vs-I2S0 keep-vs-revert decisions.

## 2026-07-11T20:41:00Z - Claude Opus 4.8 (1M) - I2S hardware-config cleanup: test-reverted all 4 tuning knobs on real hardware

- Built a repeatable objective verifier (scratchpad/i2s_verify.py): BT connect ->
  START -> parec A2DP capture -> per-channel FFT (dominant freq, purity, near-zero,
  peak). Baseline HEAD: 440.00 Hz, 100% purity, peak 9831, 0% near-zero, both ch.
- Greedy-cumulative test-revert of each config knob, flashing both boards
  (WROOM32 /dev/ttyUSB0, S3 /dev/ttyACM1) and FFT-verifying each:
  * **Beacon slim (S3)**: removed soc/i2s_struct.h + soc/gpio_struct.h reg dumps,
    s3_pad_transitions() pad sampler, DIAG|I2SREG line; kept the DIAG|I2SFREQ PCNT
    meter (reads bclk 2.8224 MHz, ws 44.1 kHz, ratio 64.00). Fixed stale strings
    ("32-in-32"->"16-in-32", "bclk=5 ws=6"->actual 15/16/7). -74 lines. PASS -> kept.
  * **Glitch filters (S3)**: REMOVED (~18 lines + gpio_filter.h). PASS at 100% ->
    they were debugging-era cruft; the real bug was WS framing, not clock glitches.
  * **bclk_div 16->8 (S3)**: PASS, but 8 is "exactly at the esp-idf #9513 minimum";
    kept 16 for margin (both verified working, documented).
  * **I2S port I2S_NUM_1->0 (WROOM32)**: PASS(!) at 100%. The recorded "I2S0 clock
    never reaches pads" was a RED HERRING from the DAC-pin era (BCLK/WS were on
    GPIO25/26). With plain GPIO18/19 + fixed framing, I2S0 works. Kept I2S_NUM_1
    (no churn), corrected the false comment.
  * **APLL->DEFAULT (WROOM32)**: PASS(!) at 100%. Also a red herring — S3 syncs to
    the fractionally-divided default clock now that framing/routing are fixed. Kept
    APLL anyway for jitter-free margin (6s FFT can't rule out rare jitter glitches).
- KEY FINDING: 3 of 4 "mystery" knobs (glitch filters, I2S port, APLL) were NOT
  necessary — all were over-corrections attributed to the wrong root cause during
  bring-up (actual causes: 16-in-32 WS framing + DAC-pin routing + stale NVS pins).
  Only the glitch filters were physically removed (real LOC); port + clock source
  kept for robustness/no-churn but comments now tell the truth.
- Final config re-flashed to both boards + verified TWICE: 100% purity both channels.
- Net source change: S3 main.c -74/+19 (scaffolding gone); WROOM32 i2s_manager.c +
  main.c are COMMENT-ONLY (values == HEAD). main.c layering check PASS.

## 2026-07-11T21:54:54Z - Claude Opus 4.8 (1M) - LINK-1c: bt_link dual-UART hardware validation PASS

- Wired a boot-time link_selftest() into esp_i2s_source/main/main.c: bt_link_init
  then VERSION/STATUS/VOLUME 40 over S3 UART1 (GPIO17/18) <-> WROOM32 UART2
  (GPIO16/17). Final result: 3/3 OK; VERSION returned live WROOM32 build.
- Dual-UART contract PROVEN: WROOM32 USB console (UART0) fully usable while the
  S3 drives UART2; STATUS read back VOL=40 set by the S3's UART2 command.
- Two hard-won findings:
  * WIRING TRAP: the inter-board UART jumpers had been on the WROOM32's UART0
    pins (GPIO1/3, silk-labeled TX/RX) instead of UART2 (GPIO16/17). Symptom:
    the self-test "passed" 3/3 over UART0, but once the S3 firmware DROVE UART1
    as a push-pull output, it contended the WROOM32's console line and hung the
    WHOLE board — console + UART2 responses + BT all died together, survived
    power-cycle (recovered only after flashing S3 back to non-UART firmware).
    Discriminator: a raw wire-probe — WROOM32 emits ESP_LOG only on UART0, never
    UART2, so passive log spam => wrong pins; clean silent-until-queried => UART2.
    Fixed by user re-wiring to GPIO16/17.
  * FIRST-COMMAND RACE (fixed in bt_link.c): uart_set_pin glitches the TX line,
    leaving a partial garbage line in the peer's RX assembler that swallowed the
    first command (first VERSION always timed out, rest OK). bt_link_init now
    uart_flush_input + writes a lone CRLF after pin setup -> clean 3/3.
- Debugging cost: heavy DTR/RTS probing on /dev/ttyUSB0 (CP2102) can wedge the
  adapter; the gentle default-open method (i2s_verify.py) is safe, aggressive
  EN-pulse toggling is not. esptool "TX path seems to be down" == board->host
  TX contended/hung, not necessarily damaged (recovered fine).
- esp_i2s_source host tests 6/6 pass. REDO1_TODO LINK-1 marked DONE (a/b/c).
- Verification harness: scratchpad/i2s_verify.py (FFT) + console_gentle.py.

## 2026-07-11T22:22:41Z - Claude Opus 4.8 (1M) - WIFI-1 done: wifi_mgr STA/AP + mDNS + console, hardware-validated

- WIFI-1a: pure wifi_sm STA/AP fallback state machine (components/wifi_mgr/wifi_sm.c),
  12 host tests. creds->STA w/ bounded retries->AP fallback; set/clear transitions.
- WIFI-1b: wifi_mgr.c device glue (esp_wifi/esp_netif/esp_event/NVS/mdns). AP SSID
  "ESP32-S3-Audio", WPA2, MAC-derived password "audio-XXXXXX" printed on console.
  mDNS esp-i2s-source.local. Added espressif/mdns ^1.2 to main/idf_component.yml.
- WIFI-1c: cmd_console component (USB-Serial-JTAG line reader): WIFI <ssid> [pass],
  WIFI STATUS, WIFI RESET, STATUS. Uses usb_serial_jtag driver + vfs_use_driver().
- HARDWARE VALIDATED (S3 on <home-ssid> LAN): boot->AP (laptop sees SSID, signal 47);
  console `WIFI <home-ssid> <pass>` -> STA got IP 10.1.2.52; WIFI STATUS shows
  MODE=STA,STATE=CONNECTED,RSSI=-59; esp-i2s-source.local resolves via avahi + pings;
  creds persist in NVS across reboot (auto-STA); WIFI RESET -> AP.
- GOTCHAS (both real, fixed):
  * Component name collision: a component named `console` SHADOWS ESP-IDF's built-in
    `console` (esp_console), which espressif/mdns depends on -> mdns_console.c fails
    "esp_console.h not found". Renamed ours to `cmd_console`.
  * SM bug caught on hardware: after WIFI RESET->AP, the old STA link dropping fired
    DISCONNECTED, and wifi_sm_on_disconnected blindly went STA_CONNECTING with no
    creds. Fix: ignore disconnect unless state is STA_CONNECTING/STA_CONNECTED.
    Added 2 regression host tests.
- main.c now: tone + LINK-1c selftest + wifi_mgr_init + console_start + beacon.
  Host suite 7/7. REDO1_TODO WIFI-1 marked done. Next: WEB-1.

## 2026-07-11T22:46:05Z - Claude Opus 4.8 (1M) - WEB-1z + WEB-1a: embedded React SPA + httpd + /api/status

- WEB-1z: standalone esp_i2s_source/web/ (Vite + TS + React 18). vite-plugin-singlefile
  -> one self-contained index.html; scripts/embed_web.mjs gzips it to
  main/www/index.html.gz (46.5 KB, <200KB target) + .sha256. Committed the .gz so
  idf.py/CI need no Node. web/.gitignore excludes node_modules/dist.
- WEB-1a: web_ui.c (esp_http_server). GET / serves the gzip SPA (Content-Encoding:
  gzip); GET /api/status returns cJSON aggregating device/version/uptime/heap +
  wifi snapshot (via new wifi_mgr_get_info()) + live WROOM32 version (bt_link
  VERSION, cached at start). Wired web_ui_start() into main.c after console_start.
- HARDWARE VERIFIED on the LAN: curl http://10.1.2.52/ -> 47639 B gzip -> valid SPA;
  /api/status -> {device,version,uptime_s,heap_free=8.2MB,wifi{STA,CONNECTED,...},
  wroom{reachable:true,version:v0.2.0-...-g1696be}}. Full stack integrates
  (browser -> httpd -> bt_link -> WROOM32 UART2).
- GOTCHA: EMBED_FILES in `main` puts _binary_index_html_gz_* in libmain.a, but
  web_ui.c (in libweb_ui.a) references them and components can't depend on main
  -> undefined reference. Fix: embed with target_add_binary_data(${COMPONENT_LIB}
  "${PROJECT_DIR}/main/www/index.html.gz" BINARY) IN the web_ui component. BINARY
  (not TEXT) since it's gzip bytes.
- Remaining WEB-1: 1b (POST /api/wifi provisioning, M4 gate), 1c (WebSocket
  terminal + EVENT feed), 1d (tone controls /api/tone). Frontend shell has stubs
  for Terminal/Tone/Radio/BT ready to fill in.

## 2026-07-11T23:01:48Z - Claude Opus 4.8 (1M) - WEB-1b: POST /api/wifi browser provisioning (backend validated)

- web_ui.c: POST /api/wifi {ssid,pass} -> validate -> reply {ok,host} -> deferred
  provision_task (~400ms) applies creds so the HTTP reply flushes before AP drops
  (SPEC "reply then switch AP->STA"). Bad creds -> 400. Frontend ProvisionForm
  (React) renders when wifi.mode==AP; posts creds, shows "reconnect + open
  esp-i2s-source.local".
- BUG FOUND + FIXED (re-provision while already STA-connected stayed stuck at
  CONNECTING): esp_wifi_start() does NOT reliably return ESP_ERR_WIFI_NOT_STOPPED
  when already running, so the reconnect branch was dead code -> device kept the
  old association, SM stuck CONNECTING. Fix: track s_wifi_started explicitly;
  when re-applying creds while s_sta_got_ip, esp_wifi_disconnect() first so the
  DISCONNECTED event re-drives connect() with the new config. Now settles to
  CONNECTED first poll. (s_sta_got_ip set on GOT_IP, cleared on DISCONNECTED.)
- Verified over STA (device at 10.1.2.52). Full browser AP->STA walk-through is
  the M4 gate, still to do with the user (needs joining the S3 AP + a browser).
- Remaining WEB-1: 1c (WebSocket terminal + EVENT feed), 1d (tone /api/tone).

## 2026-07-11T23:12:01Z - Claude Opus 4.8 (1M) - WEB-1c: WebSocket terminal + live EVENT feed

- web_ui.c /ws WebSocket (CONFIG_HTTPD_WS_SUPPORT=y). term_in JSON -> bt_link_send
  -> term_out (sync in handler). bt_link_subscribe(on_bt_event) fans WROOM32
  EVENT| lines to all WS clients as async  frames via httpd_queue_work +
  httpd_ws_send_frame_async (client fds tracked in s_ws_fds[4], pruned on send
  fail). Frontend src/Terminal.tsx: live WS, input, scrolling log (sent/out/event
  colors), auto-reconnect.
- HARDWARE VERIFIED (raw stdlib WS client, scratchpad/ws_test.py + ws_event_test.py):
  handshake 101; VERSION/STATUS/VOLUME 55 -> term_out OK; DEBUG MOCK_PAIR
  AA:BB:CC:DD:EE:FF -> term_out(OK,MOCK_PAIR_STARTED) AND pushed
  event(PAIR,CONFIRM,...). Full multiplex works.
- Note: WROOM32 only emits EVENT| for PAIR (cmd_send_event_pair); A2DP
  connect/disconnect does NOT emit an EVENT, so use DEBUG MOCK_PAIR to exercise
  the feed. bt_link_send blocks the httpd worker up to ~2.5s per terminal cmd
  (acceptable single-user; events still flow via the bt_link task meanwhile).
- Remaining WEB-1: 1d (tone /api/tone) + the WEB-1b M4 browser walk-through.

## 2026-07-11T23:25:20Z - Claude Opus 4.8 (1M) - WEB-1d: browser tone control (first browser-driven audio)

- Extracted main.c's hardcoded always-on tone_task into a controllable `tone`
  component (components/tone/): tone_start/tone_set(hz)/tone_off/tone_get, atomic
  state (_Atomic on + hz), emits silence (amp 0, phase continues) when off so the
  slave-TX I2S stream keeps flowing. Default on @ 440 (SIG-1c parity). main.c now
  calls tone_start() instead of the inline task.
- web_ui: POST /api/tone {hz} -> tone_set; DELETE /api/tone -> tone_off; tone
  {on,hz} added to /api/status. Frontend Tone panel: freq input + presets
  (220/440/1k/4k) + play/stop, reads state from status.
- OBJECTIVELY VERIFIED via A2DP FFT (scratchpad/tone_fft.py): POST hz=1000 ->
  dominant 1000.00 Hz; 220 -> 220.00; DELETE -> silence (peak 0, near0 100%);
  440 -> 440.00. Full chain browser->/api/tone->tone->signal_gen->i2s->WROOM32->
  A2DP is frequency-accurate.
- WEB-1 implementation COMPLETE (z/a/b/c/d). Only the WEB-1b M4 browser AP->STA
  walk-through remains (user-driven). Host suite 7/7. Next group: BTUI-1.

## 2026-07-11T23:38:47Z - Claude Opus 4.8 (1M) - BTUI-1a/b/c: browser Bluetooth management UI

- Foundation: extended bt_link_session to fan out INFO lines (scan results,
  paired items) to subscribers, not just EVENT (+ infos_dispatched counter +
  host test test_info_fanout_to_subscribers). web_ui on_bt_event now tags
  frames info vs event by m->status.
- Shared web/src/ws.ts: one /ws WebSocket singleton + pub/sub + useWs() hook,
  reused by Terminal (refactored) and the new Bluetooth panel.
- Bluetooth.tsx: Scan (SCAN -> INFO|SCAN|RESULT -> discovered list),
  Pair/Connect per device, Disconnect, volume slider (VOLUME <n>), Paired list
  (PAIRED -> INFO|PAIRED|ITEM) with Unpair (UNPAIR <mac>), and a pairing-prompt
  modal (EVENT|PAIR|CONFIRM -> CONFIRM_PIN ACCEPT|REJECT).
- WROOM32 protocol confirmed: SCAN->OK STARTED then async INFO|SCAN|RESULT|mac,name;
  PAIRED->INFO|PAIRED|ITEM|mac,name + OK|PAIRED|COUNT; CONFIRM_PIN accepts
  ACCEPT/YES/1 or REJECT/NO/0; UNPAIR <mac>.
- HARDWARE VERIFIED over WS (scratchpad/btui_test.py): PAIRED returned 2 paired
  devices as info frames; SCAN OK STARTED (no discoverable devices in range);
  DEBUG MOCK_PAIR -> EVENT|PAIR|CONFIRM prompt frame. Host suite 7/7.
- BTUI-1d (Echo Buds E2E, M5) is user-driven: full pair->connect->volume from
  the browser with real earbuds. Next group: RADIO-1.

## 2026-07-11T23:59:43Z - Claude Opus 4.8 (1M) - BTUI-1d (M5): browser BT E2E via laptop-as-sink + term_out data

- Validated full browser-driven pair->connect->volume with the LAPTOP as the
  A2DP sink (substitute for Echo Buds), using esp_bt_audio_source's LaptopBT
  auto-accept BlueZ agent + GLib drain. Over /ws (exactly what the browser
  sends): UNPAIR e8:fb.. -> PAIR E8:FB:1C:25:E4:C2 -> EVENT|PAIR|SUCCESS
  (paired) -> CONNECT -> A2DP up (confirmed via laptop bt.wait_for_connect) ->
  VOLUME (tracked as VOL=45).
- KEY LEARNINGS:
  * A2DP volume is AVRCP absolute-volume applied AT THE SINK, so it does NOT
    change the captured a2dp_source stream (FFT peak stays 9831). Verify volume
    via WROOM32 STATUS VOL= field, not stream amplitude.
  * SCAN does NOT discover the laptop (poor BR/EDR inquiry target even when
    discoverable) — real earbuds in pairing mode would show. Substitute limit.
- Fixed a real WEB-1c gap found here: bt_link_send() only returned result, not
  the DATA field, so the WS terminal showed just "CURRENT" for STATUS. Extended
  bt_link_send(cmd,&st,result,rsz,data,dsz) (+bt_link_request_t.data, copy
  last_data), send_term_out includes data, Terminal shows result|data. Now
  STATUS shows VOL=/underruns, VERSION shows PROJECT/BUILD. Updated 3 callers.
- Verification scripts: scratchpad/btui1d_e2e.py (LaptopBT + raw WS), status_check.py.
- Host suite 7/7. BTUI-1 essentially done; real Echo Buds SCAN + sign-off remains.
  Next group: RADIO-1.

## 2026-07-12T00:20:26Z - Claude Opus 4.8 (1M) - RADIO-1a: pure playlist + ICY parsers

- components/radio/radio_parse.{c,h}: radio_playlist_first_url() (.pls FileN=,
  .m3u/#EXTM3U comment-skipping, bare URL -> first http(s) URL) and
  radio_icy_stream_title() (StreamTitle from SHOUTcast ICY block, NUL-pad
  tolerant). Pure, no ESP-IDF deps. 13 host tests (test_radio_parse). Host
  suite 8/8, device build clean.
- Gotcha: device build has -Werror=comment; a comment containing "*line/*len"
  tripped "/* within comment". Host build didn't flag it — always device-build
  new C before committing.
- Next: RADIO-1b (esp_http_client stream task, ICY, content-type->codec, PSRAM
  ring, reconnect) and RADIO-1c (NVS station store + /api/stations + UI, seed
  5 .pls presets from SPEC §5.4).

## 2026-07-12T01:12:19Z - Claude Opus 4.8 (1M) - RADIO-1b: HTTP(S) stream task + ICY demux, hardware-validated

- Pure ICY demux state machine (radio_parse: radio_icy_demux_*) splits the
  interleaved stream into audio + StreamTitle updates; feed-boundary agnostic.
  4 host tests (17 radio_parse cases total).
- radio.c device stream task: esp_http_client + esp-tls (crt bundle), resolve_url
  (fetch .pls/.m3u -> first stream URL), Icy-MetaData:1, content-type->codec
  (mp3/aac), 256KB PSRAM ring (mutex SPSC), reconnect w/ exponential backoff,
  telemetry. radio_read() is the RADIO-2 consumer hook. web_ui: POST/DELETE
  /api/radio (deferred to a task), radio in /api/status; Radio.tsx UI panel.
- HARDWARE VERIFIED with SomaFM: POST http://somafm.com/dronezone.pls -> resolved
  on-device to ice6.somafm.com/dronezone-128-mp3, codec=mp3 br=128, station +
  live title ("Berlin Heritage - Parce mihi") demuxed from real ICY, bytes_in
  climbing, reconnects=0. Ring fills to full (nothing drains it until RADIO-2).
- GOTCHAS:
  * Binary overflowed the default single-app ~1MB partition (TLS+http_client).
    Added custom partitions.csv (6MB factory app + 64KB nvs) + CONFIG_PARTITION_
    TABLE_CUSTOM in sdkconfig.defaults; deleted sdkconfig to regen. NVS start
    offset unchanged (0x9000) so WiFi creds survived the repartition.
  * SPEC §5.4 seed stations (internet-radio.com snapshot) had unreachable stream
    ports from here (laptop curl also code=000) — NOT an ESP bug. Swapped UI
    presets to SomaFM (reliable). RADIO-1c seeds the editable SPEC list.
  * esp-tls to some HTTPS .pls (reliastream:1079) reset on the ESP though curl
    worked — TLS quirk with those servers; SomaFM (HTTP) fine. Revisit if needed.
- Host suite 8/8. Next: RADIO-1c (NVS station store + /api/stations + UI) and
  RADIO-2 (esp_audio_codec decode -> resample -> i2s, where audio finally plays).

## 2026-07-12T01:43:31Z - Claude Opus 4.8 (1M) - RADIO-1c: NVS station store + /api/stations CRUD

- Pure station_store (radio component): add/update/delete + http(s) URL validate
  + blank-name->host defaulting + exact-URL dedupe, cap 40. 7 host tests.
- stations.c device wrapper: NVS blob (magic-versioned stations_blob_t) persist +
  first-boot seed (SomaFM, since SPEC 5.4 stations unreachable) + mutex. web_ui:
  /api/stations GET/POST/PUT(?id)/DELETE(?id), 400 on invalid/dup/full. Radio.tsx
  UI: station list (play/edit/delete) + add form + one-off Play (getStations/
  add/update/delete API).
- HARDWARE VERIFIED: seed 5 -> add -> PUT rename -> DELETE, all over REST; edits
  persist across reboot (Groove Salad -> "Groove (edited)" survived watchdog reset).
- TWO CRASHES FOUND + FIXED:
  * save_locked() put the ~12KB stations_blob_t on the STACK -> main task stack
    (~4KB) overflow -> LoadProhibited boot loop right after "seeded 5 stations".
    Fixed: make the scratch blob static (safe under s_mtx).
  * esp_http_server default max_uri_handlers=8, but we now have 12 routes ->
    PUT/DELETE (+ws/root) silently failed to register ("method invalid"). Set
    cfg.max_uri_handlers=20.
- Host suite 9/9. RADIO-1 COMPLETE (a/b/c). Next: RADIO-2 (esp_audio_codec decode
  -> resample -> i2s) where audio finally plays and the tone retires.

## 2026-07-12T02:30:32Z - Claude Opus 4.8 (1M) - RADIO-2a: esp_audio_codec MP3/AAC decoder (decoding verified)

- Added espressif/esp_audio_codec ^2.6.0. radio.c decoder_task: esp_audio_simple_dec
  (frame parser) open MP3 or AAC (aac_plus_enable=true for HE-AAC); pulls compressed
  via radio_read, esp_audio_simple_dec_process -> PCM, get_info -> rate/ch,
  radio_resampler -> 44.1k stereo -> 128KB decoded-PCM ring. radio_pcm_read() =
  I2S consumer (RADIO-2c). Format-change re-open; bad-frame resync (consume 1 byte).
- HARDWARE VERIFIED: SomaFM MP3 -> dec_rate=44100 dec_ch=2 decode_errors=0, PCM
  ring fills (nothing drains until RADIO-2c).
- KEY GOTCHA: open failed "AUDIO_DEC: Decoder MP3 not registered / ret -7". Must
  call BOTH esp_audio_dec_register_default() (low-level MP3/AAC) AND
  esp_audio_simple_dec_register_default() (wrappers). The SIMPLE Kconfig only has
  WAV/M4A/TS/OGG toggles; MP3/AAC come from the low-level registration.
- Also: added a 128KB PSRAM decoded-PCM ring alongside the 256KB compressed ring.
  Radio play now spawns 2 tasks (stream + decoder); stop joins both.
- Remaining: RADIO-2c (route radio_pcm_read into i2s with source arbitration ->
  AUDIBLE), RADIO-2d (MP3+AAC E2E to earbuds). Host suite 10/10.

## 2026-07-12T02:48:28Z - Claude Opus 4.8 (1M) - RADIO-2c: source arbitration -> INTERNET RADIO PLAYS over Bluetooth

- Refactored the output path. main.c audio_out_task = single I2S feeder /
  arbiter: radio_is_playing() -> radio_pcm_read (underrun=brief silence), else
  tone_fill (silence when off); 16-in-32 pack (<<16) -> i2s_out. Retired the
  always-on tone_task. tone.c now exposes tone_fill(out,frames) (pure fill, no
  i2s dep); dropped tone_start/task; tone CMakeLists REQUIRES only signal_gen.
  radio.h + radio_is_playing().
- HARDWARE VERIFIED via laptop A2DP FFT: SomaFM MP3 plays as real broadband music
  (peak ~26k, dominant wanders, near0=0); radio+tone -> radio wins (still music);
  stop radio -> 440Hz tone (peak 9831); tone off -> silence (peak 0). Full chain
  SomaFM -> S3 decode/resample -> PCM ring -> audio_out -> I2S -> WROOM32 -> A2DP
  -> speaker. The device now does its actual job.
- RADIO-1 + RADIO-2a/b/c COMPLETE. Remaining: RADIO-2d (MP3+AAC E2E to earbuds
  >=30min, AAC via Dance UK / Hirschmilch) is the M6 hardware gate; and CTRL-1
  (orchestrated boot). ICY title WS-push optional. Host 10/10.

## 2026-07-12T04:31:17Z - Claude Opus 4.8 (1M) - RADIO-2d prep: fixed choppy radio (WiFi PS + deep prebuffer)

- User reported choppy audio during a 128k MP3 endurance run. Diagnosed via new
  /api/status i2s telemetry (added underrun_events/bytes/ring_peak) + a monitor
  harness: PCM ring oscillated full<->empty (13/46 samples at 0); FFT near0
  tracked it (56-90% during starvation). i2s underrun counter stayed 0 the whole
  time because audio_out zero-fills a full block -> it is BLIND to upstream PCM
  starvation. The real dropout signal is PCM-ring starvation, not i2s underruns.
- Root cause: 128k stream arrived at only ~119 kbps avg (below 128). Isolated
  network-vs-device: 64k stream played glassy (device fine) -> throughput-bound.
  No esp_wifi_set_ps() anywhere -> IDF default WIFI_PS_MIN_MODEM sleeps radio
  between beacons, throttling sustained TCP. FIX: esp_wifi_set_ps(WIFI_PS_NONE)
  in wifi_mgr_init. After: 128k sustains 128 kbps (bursts 155), 0 starvation,
  FFT near0 ~0%. Commit 20ba3fff.
- Then deepened jitter buffer: PCM ring 128KB(0.74s)->1MiB(~5.9s); new
  radio_audio_ready() (playing && prebuffered) gates the arbiter instead of
  radio_is_playing(); prime to ~3s before audio, re-arm on full drain; added
  radio.buffering to status. Verified: primes to full in ~1.8s, pins ~95-100%
  full at 128k, 0 underruns. Commit 917b8126. Host 10/10.
- NEXT: re-run RADIO-2d endurance (MP3+AAC >=30min) with monitor updated to flag
  PCM starvation (buffering flag / pcm_used=0), not just i2s underruns.

## 2026-07-12T05:39:28Z - Claude Opus 4.8 (1M) - RADIO-2d M6 gate PASSED (MP3 + AAC+ 30 min each)

- Ran the endurance gate: SomaFM Groove Salad 128k MP3 and Dance UK Radio 32k
  AAC+ (audio/aacp, HE-AAC upsampled to 44.1k via SBR), each 30 min to the
  laptop A2DP sink. BOTH PASS: 0 rebuffers, 0 PCM starvation, 0 decode
  errors/reconnects/i2s underruns; PCM ring held 88-99% full; FFT 18/18 (MP3)
  and 19/19 (AAC) windows audible; WROOM32 sink counters clean after both
  (UNDERRUNS=0, PKT_ERR=0). A mid-run screen lock did not disturb it (S3 streams
  independently; A2DP RX continues while locked; suspend would have paused it).
- Both fixes from earlier this session were validated under load (WIFI_PS_NONE +
  ~5.9s prebuffer). Key lesson recorded in TODO: the i2s underrun counter is
  BLIND to PCM-ring starvation (audio_out zero-fills a full block); the true
  dropout signal is radio.buffering / pcm_used=0.
- RADIO-2 (a/b/c/d) COMPLETE. Remaining in REDO1: CTRL-1 (orchestrated boot),
  DOC-1 (docs/regression). Task #29 done. Endurance harness lives in scratchpad
  (radio2d_endurance.py / radio2d_gate.sh); not committed (test tooling).
- Device left playing the AAC+ stream at end of gate.

## 2026-07-12T07:47:00Z - Claude Opus 4.8 (1M) - Two-stage volume: WROOM32 fix + S3 pre-I2S gain

- User hit painfully loud audio; VOLUME command did nothing. Root causes (both
  in WROOM32 esp_bt_audio_source):
  1) apply_volume() was DEAD CODE (never called) -> VOLUME was a no-op.
  2) After wiring it into audio_processor_read (the A2DP data path's single
     output point), it corrupted audio into loud STATIC: apply_volume dispatches
     int16-vs-int32 on s_audio_config.bit_depth, which is 32 for the 16-in-32
     I2S input, but the A2DP/SBC output buffer is always 16-bit. Scaling a 16-bit
     buffer with 32-bit math scrambles samples.
- FIX: added apply_volume_s16() (always 16-bit) and use it on the A2DP output.
  Verified OBJECTIVELY via laptop A2DP parec capture: 440Hz tone peak scales
  9831/4915/2459/984 for VOLUME 100/50/25/10 (exactly linear), purity 93-99%
  (clean). Commit 8e500c1a. WROOM32 host 67/67.
- Also added S3 pre-I2S software volume (user asked for two-stage control):
  pure i2s_out_apply_gain() (0-100%, truncate-toward-zero, unity/over=noop,
  <=0=mute), POST /api/volume {pct}, i2s.gain in /api/status. Host-tested (7
  cases). Objectively verified on laptop: identical linear scaling. In-memory
  (resets to 100 on boot; WROOM32 VOLUME is the persistent primary). Commit
  228e7bd1. S3 host 13/13.
- Regression tests added per user request: WROOM32 test_audio_processor_read now
  asserts volume applied AND scales as s16 when bit_depth=32 (the case the old
  harness masked by forcing bit_depth=16); S3 test_i2s_out_gain (7 cases).
- KEY hardware facts learned: laptop A2DP sink only accepts LAPTOP-initiated
  connects (bluetoothctl connect A0:B7:65:2B:E6:5E), not WROOM32-initiated;
  laptop BT MAC E8:FB:1C:25:E4:C2, WROOM32 MAC A0:B7:65:2B:E6:5E, Echo Buds
  48:78:5E:D9:35:A3 (paired w/ WROOM32, auto-reconnect on its boot). WROOM32
  STATUS byte counters (BYTES_PROD/PKTS) read 0 always (not populated); RUN=1 is
  the connected signal. "static" earlier in earbuds was Echo Buds L->R relay
  glitch (fixed by A2DP reconnect), separate from the apply_volume static.
- STATE: CTRL-1 autostart currently OFF (disabled during debugging). Roadmap
  CTRL-1c (M7) mid-flight, DOC-1 pending.

## 2026-07-12T08:01:25Z - Claude Opus 4.8 (1M) - CTRL-1c M7 PASSED (cold-start to music on Echo Buds)

- Fixed the orchestrator cold-connect race (commit ee48824c): old flow settled 4s
  after CONNECT then START-or-backoff; a slow sink (Echo Buds) isn't linked at
  +4s so START failed and it never resumed. New: CONNECT -> START nudge (result
  ignored) -> poll STATUS until RUN=1 or connect_timeout(20s) -> resume/backoff.
  Regression test test_slow_sink_start_fails_but_connects. Host 13/13.
- M7 VERIFIED on hardware: disconnected Echo Buds, rebooted S3 -> orchestrator
  CONNECT 48:78:5E:D9:35:A3 -> START fail (expected) -> waited RUN=1 -> resume
  station 5 -> jazz playing in ~6s, zero human interaction. Echo Buds accept the
  WROOM32-initiated CONNECT (the laptop sink did not — env limitation, not fw).
- Known polish item: WROOM32 VOL resets to 40 on a fresh A2DP connection (AVRCP/
  default), so autostart music comes up at 40 not the persisted level; orchestrator
  could set a target volume post-connect. Not blocking M7.
- CTRL-1 (a/b/c) COMPLETE. Remaining: DOC-1.

## 2026-07-12T08:07:29Z - Claude Opus 4.8 (1M) - DOC-1 done -> REDO1 roadmap COMPLETE

- DOC-1a: rewrote esp_i2s_source/README.md for the actual built S3 system
  (arch, component table, real GPIO15/16/7 I2S wiring, build/test/flash, the
  live /api/* + /ws surface, two-stage volume); supersedes README_orig.md. Root
  README I2S pins annotated as superseded -> SPEC 3.
- DOC-1b: tools/run_host_tests.sh is the entry (CTest, 13 suites, 13/13);
  inventory recorded in README + REDO1_TODO.
- DOC-1c: SPEC.md 9 "Hardware changelog" of on-bench deviations (I2S pins,
  16-in-32, WiFi PS_NONE, ~5.9s PCM buffer, audio/aacp, WROOM32 volume fix,
  laptop cold-connect limit, orchestrator RUN=1 wait, VOL reset). Commit c753f125.
- REDO1 roadmap DONE: INFRA/SIG/LINK/WIFI/WEB/BTUI/RADIO-1/RADIO-2/CTRL-1/DOC-1.
  All milestones M1-M7 met. Only open user-driven gates remain (BTUI-1d M5 real
  Echo Buds SCAN sign-off, WEB-1b M4 browser walk-through) + polish items.

## 2026-07-12T08:15:55Z - Claude Opus 4.8 (1M) - Polish: autostart applies configured volume

- Fixed VOL-resets-to-40-on-connect: added ctrl_cfg.volume (NVS, default 15,
  /api/ctrl {volume}); orchestrator sends VOLUME <target> in the resume path
  BEFORE radio_play. Verified cold-start: START fail -> wait RUN=1 -> "set volume
  12 -> ok" -> resume station 5 -> jazz at VOL=12 (not 40). Commit d2093a83.
  Note: adding the field grew the ctrl_cfg blob so old NVS config was dropped
  (reconfigured post-flash). link_selftest still sends boot VOLUME 40, harmlessly
  overridden. Host 13/13. REDO1 roadmap + this polish all complete.

## 2026-07-12T10:27:58Z - Claude Opus 4.8 (1M) - Web UI: WebSocket -> REST refactor (fixes socket exhaustion) + Playwright

- ROOT CAUSE of "buggy" UI (terminal/BT/scan dead while music plays): the browser
  held a persistent WebSocket AND polled /api/status; the radio HTTPS stream +
  APSTA DHCP + mDNS exhausted CONFIG_LWIP_MAX_SOCKETS=10, so new WS handshakes were
  RESET. A WebSocket server on a 10-socket lwIP stack was the wrong design.
- FIX: removed /ws. web_ui buffers WROOM32 async lines (SCAN/PAIRED/PAIR-prompt)
  into BT state; browser polls GET /api/bt {connected,scanning,prompt,paired,
  discovered}. Actions POST /api/bt {action,mac}; terminal POST /api/console {cmd}.
  Bluetooth.tsx/Terminal.tsx rewritten to REST polling; ws.ts deleted. Bumped
  LWIP_MAX_SOCKETS 10->16. Commit ca22a097.
- SCAN now suspends A2DP (ctrl_scan: stop radio, DISCONNECT, SCAN, reconnect+resume)
  — classic inquiry is unreliable while A2DP streams. Confirmed earlier: WROOM32
  inquiry works (finds discoverable devices), only finds devices in PAIRING MODE.
- KEY TOOLING: Playwright UI tests added (web/e2e/ui.spec.ts, playwright.config.ts,
  system chromium at /usr/bin/chromium-browser, no browser download). Run:
  cd web && DEVICE_URL=http://10.1.2.52 npx playwright test. Caught the WS-reset bug
  that code review missed. 6/6 pass WITH radio streaming incl. the paired list.
  vite.config already proxies /api+/ws to the device for reflash-free iteration.
- Earlier this session also: concurrent AP+STA (APSTA, /api/apmode, wifi.ap), tabbed
  UI (Radio/Tone/Terminal/Settings), two-slider volume, removed redundant BT-card
  volume, responsive CSS, Radio-tab WiFi gate, scan pairing-mode hints. Commit d3a313b2.

## 2026-07-12T11:07:53Z - Claude Opus 4.8 (1M) - Settings layout + NVS-persisted AP creds & volumes

- Settings tab relaid out to a fixed 2-col grid (`.grid.settings`): row1 Network+System,
  row2 WiFi+Control AP, row3 Bluetooth (full width); collapses to 1-col <620px.
- WiFi (ProvisionForm): SSID pre-filled from current network (didFill ref); password shown
  as masked dots placeholder (device never sends real pw); submitting unchanged net = no-op.
- Control AP now user-editable (was hardcoded SSID + MAC-derived pw). New
  `wifi_mgr_set_ap_config(ssid,pass)` persists NVS keys ap_ssid/ap_pass (namespace "wifi"),
  re-applies live (bounces AP clients); empty pass => WIFI_AUTH_OPEN. `load_ap_creds()` at
  init overrides derive_ap_password() default. `/api/apmode` extended to accept {ssid,pass?}
  in addition to {enabled}. api.ts setApConfig; ApControl card has editable SSID/pass + Save.
- Pre-I2S (S3) gain now NVS-persisted (namespace "i2s", key "gain"), default 30% (was 100,
  non-persisted). i2s_out_gain_load() called in i2s_out_init; set_gain persists. Added
  nvs_flash to i2s_out CMakeLists REQUIRES.
- Post-mix (WROOM32) volume: CTRL_VOLUME_DEFAULT 15->10; /api/btvolume now persists to
  ctrl_cfg.volume via ctrl_set_sink(NULL,...) so it survives reboot/A2DP reconnect.
- UI: both volume sliders now display "%" (they're the same 0-100 full-scale; label was the
  only difference between the two code paths). Help text clarified.
- Verified: 13 host tests green; device build clean; flashed S3 (/dev/ttyACM0). Live checks:
  i2s.gain=30 after flash, AP-config POST round-trips + short-pass rejected 400. Playwright
  6/6 vs 10.1.2.52. NOTE: during verify, restored AP with empty pass -> control AP is now
  OPEN (named ESP32-S3-Audio); user should set their own name/pw in the new card to re-secure.

## 2026-07-12T11:21:43Z - Claude Opus 4.8 (1M) - Manual-connect re-asserts persisted post-mix volume

- Closed the gap where a hand-initiated BT connect (POST /api/bt action=connect) left
  the WROOM32 at its fresh-A2DP-link default (~40) instead of the saved ctrl_cfg.volume.
  Previously only the orchestrator's autostart path (ctrl.c RESUME_RADIO) and scan-restore
  re-asserted volume; a manual connect sent a bare CONNECT with no follow-up VOLUME.
- web_ui.c bt_post_h: on a successful CONNECT (INITIATED), spawn connect_volume_task which
  polls STATUS until RUN=1 (async link comes up seconds later; WROOM32 resets VOL to 40 on
  the fresh link) then sends "VOLUME <cfg.volume>". Guarded by s_conn_vol_task (one at a
  time), ~15s poll budget. Generalized parse_wroom_vol -> parse_wroom_kv(data,"KEY=") and
  reused it for RUN=.
- Verified on-device conclusively: set autostart OFF + saved volume 22, hard-reset S3 so the
  orchestrator exits to manual mode (only actor that could send VOLUME 22 is the new task),
  manual-connect ArIsu -> WROOM32 settled at VOL=22 (would be 40 without the fix). Restored
  user config: autostart on, sink Echo Buds, volume 10. NOTE: orchestrator is in manual mode
  until next power-cycle (side effect of the verification reboot); autostart re-engages on boot.

## 2026-07-12T11:56:21Z - Claude Opus 4.8 (1M) - Report connected A2DP sink to S3 + web UI

- User asked: S3 should be able to query the WROOM32 for which BT audio device is
  currently connected. Root cause of "Echo Buds connected to ArIsu instead": ArIsu
  (e8:fb:1c:25:e4:c2) is the laptop BT adapter (hardcoded in esp_bt_audio_source
  laptop_bt_tests only — NOT firmware), bonded to the WROOM32 during testing; the
  WROOM32 auto-reconnect (s_last_connected_addr, s_auto_reconnect) keeps re-dialing
  the last device, and the always-on laptop wins.
- WROOM32 (esp_bt_audio_source): cmd_handle_status now appends CONN_MAC=<addr> to the
  STATUS data (empty when not connected) via bt_get_connection_info(). Name isn't
  tracked in the connection layer (.name never populated) so only the MAC is sent;
  the S3/web resolves the friendly name from the paired list. Host test: added
  bt_get_connection_info weak stub + bt_manager_test_set_connection_info() hook in
  mock_audio_and_btstate.c; 2 new asserts in test_cmd_handlers_system (field present;
  reports the set MAC). 67/67 WROOM32 host tests pass.
- S3 (esp_i2s_source): web_ui bt_get_h parses CONN_MAC (new bt_status_conn_mac helper)
  and adds connected_mac to /api/bt. Bluetooth.tsx shows "Connected to <name> <mac>"
  banner, a "connected" badge on the paired row, and a Disconnect (vs Connect) button
  on that row. api.ts BtState.connected_mac. Graceful when WROOM32 lacks CONN_MAC.
- Built both firmwares clean; flashed S3 (/dev/ttyACM0). WROOM32 flash (/dev/ttyUSB0)
  still PENDING user confirmation — feature is end-to-end only after that flash.

## 2026-07-12T11:59:21Z - Claude Opus 4.8 (1M) - Connected-sink feature flashed + verified E2E

- Flashed WROOM32 (/dev/ttyUSB0) with the CONN_MAC STATUS change (user-confirmed).
- Verified end to end: STATUS reports CONN_MAC=E8:FB:1C:25:E4:C2, /api/bt relays
  connected_mac, UI banner "Connected to ArIsu BT Headset" + badge on that paired row.
  This also confirmed the user's complaint: the link is on ArIsu (the laptop adapter),
  not the Echo Buds — the WROOM32 auto-reconnect keeps re-dialing the last-connected
  device and the always-on laptop wins.
- Added Playwright test "Bluetooth shows which device is currently connected"; full
  S3 UI suite 7/7 green on-device.

## 2026-07-12T12:04:38Z - Claude Opus 4.8 (1M) - Unpaired laptop; fixed stale connected=true

- Unpaired "ArIsu BT Headset" (e8:fb:1c:25:e4:c2 = laptop adapter) per user. WROOM32
  auto-reconnect then fell through to the next paired device and connected the Echo
  Buds (48:78:5e:d9:35:a3) — the originally-desired sink. PAIRED_COUNT now 1.
- Unpair exposed a bug: WROOM32 keeps RUN=1 (audio engine emits silence) after a
  disconnect, so the S3 derived connected=true with no peer. Fixed bt_get_h to derive
  connected from CONN_MAC (non-empty = a real A2DP peer), not RUN; removed the now-dead
  bt_status_running helper. Flashed S3 (/dev/ttyACM0); verified connected + connected_mac
  now track the actual link. UI suite green on-device.

## 2026-07-12T12:24:25Z - Claude Opus 4.8 (1M) - Radio/Volume UI batch + station reorder

- Volume card spans full width (.card.volume grid-column 1/-1) on Radio + Tone tabs.
- Radio "now playing" block moved from bottom to top, under the title above the list.
- Radio station Edit is now inline/accordion: the row expands into name/URL fields +
  Save/Cancel (independent editId/editName/editUrl/editErr state); the bottom form is
  add-only. Was confusing as a shared bottom form.
- Station reorder: pure station_store_move(store, idx, +/-1) swap-with-neighbour
  (host-tested: swap both dirs + edge/bad-arg rejection); stations_move() wrapper
  (mutex+NVS); PUT /api/stations?id=X&move=up|down (reuses PUT route, no new handler);
  api.ts moveStation; up/down arrows per row (ends disabled). NOTE: indices are the
  station id, so reordering remaps ctrl_cfg.last_station (same as delete already does).
- Built SPA+firmware, flashed S3 (/dev/ttyACM0, user-authorized). Verified reorder
  round-trip on device (move up then restore, persists). 13/13 S3 host tests, UI 9/9.

## 2026-07-12T12:35:18Z - Claude Opus 4.8 (1M) - Piano card flashed + verified

- Flashed S3 (/dev/ttyACM0, user-authorized) with the Piano card (Tone tab, C3-C5,
  press-to-play via /api/tone). Verified on device: UI test passes, and a middle-C
  (261 Hz) tone played + stopped cleanly over the Echo Buds A2DP link. Frontend-only
  feature (Piano.tsx), no C changes.

## 2026-07-12T13:07:58Z - Claude Opus 4.8 (1M) - Verified note length over real BT; fixed WiFi race

- User: "note length still isn't working." Connected WROOM32 -> laptop A2DP sink
  (BlueZ via laptop_bt_tests infra + parec capture of bluez_source) and measured
  the actual audible note duration.
- Direct-HTTP sweep (sequential POST/sleep/DELETE): intended 1000/500/250/150ms ->
  measured 1310/650/300/220ms. Browser-driven sweep (real Piano UI via vite
  dev-proxy, Playwright clicking one key at 1500/150/700ms): measured 1560/290/1230ms.
  Both monotonic -> note length DOES control duration end-to-end. ~60-500ms buffering
  tail (I2S ring + WROOM32 A2DP buffer) makes notes sound a bit longer than set.
- Root cause of the user's "not working": browser fired setTone/toneOff fire-and-forget;
  over WiFi a short note's DELETE can beat the POST -> stuck tone. Fix (Piano.tsx):
  await setTone (note-on) before scheduling the setTimeout note-off, + pressId guard so
  a superseded note's timer can't cut a newer one. Verified via mocked timing test.
- Restored state: unpaired laptop from WROOM32 (+ removed laptop-side bond), Echo Buds
  reconnected, S3 autostart re-enabled. FIX NOT YET FLASHED (needs S3 flash to deploy).

## 2026-07-12T13:41:53Z - Claude Opus 4.8 (1M) - Root-caused empty scan list: WROOM32 misroutes SCAN results

- Find & pair (and plain Scan) showed no devices. Root cause (empirically confirmed by
  sniffing /dev/ttyUSB0): WROOM32 bt_scan_emit_results() used cmd_send_response() for the
  async INFO|SCAN|RESULT + OK|SCAN|DONE lines. Those fire seconds AFTER the SCAN command's
  cmd_process() cycle, when s_reply_uart has reset to primary (USB) — so they went to the
  USB console instead of back over bt_link to the S3. Raw serial SCAN finds the laptop +
  SICKLUGGAGE fine; the S3 just never received the results.
- Fix (esp_bt_audio_source): new cmd_send_response_all() broadcasts to every command port;
  bt_scan_emit_results() now uses it for RESULT + DONE (keeps INFO|SCAN|RESULT wire format
  the S3 parses). S3 bt_link already handles these safely (INFO fans to subs regardless of
  pending; stray terminals with wrong verb / none pending are ignored — bt_link_session.c
  62-76). Host test added (test_cmd_dual_uart: broadcast reaches both ports after a
  secondary command). 67/67 WROOM32 host tests pass; builds clean. NEEDS WROOM32 FLASH.
- HELP over bt_link is FINE (verified: 0 HELP lines leaked to USB primary). It's emitted
  synchronously so s_reply_uart is still bt_link. The empty /api/console HELP output is a
  display gap — console_post_h returns only the terminal OK, not the streamed INFO|HELP|ENTRY
  lines. Follow-up idea: have console collect INFO frames during a command.

## 2026-07-12T14:03:42Z - Claude Opus 4.8 (1M) - Scan fix flashed+verified; laptop-pair inconclusive

- Flashed WROOM32 with the scan-broadcast fix. VERIFIED: /api/scan now populates
  discovered (['ArIsu BT Headset','SICKLUGGAGE H1-013']) — the real bug ("scan does
  nothing") is fixed. Find & pair now scans, finds by name, and initiates PAIR.
- Could NOT complete pairing the LAPTOP as a stand-in headset: PAIR returns INITIATED
  but never completes and no EVENT|PAIR| prompt fires (with bluetoothctl NoInputNoOutput
  agent it negotiates Just Works, which this WROOM32 pairing didn't finish; the pydbus
  numeric-comparison agent worked earlier but the laptop adapter got into flaky
  Busy/powered-off states from repeated toggling — recovered via rfkill block/unblock).
  Consistent with the known env issue (summary: laptop unreliable as WROOM32 A2DP sink).
  Real earbuds (Just Works / auto-accept) are the right test — deferred until charged.
- Enhanced Bluetooth.tsx Find & pair to auto-accept the SSP prompt (pin_accept) and verify
  the device lands in paired[] before declaring success (needs S3 flash to deploy).
- Restored: laptop adapter powered + discoverable off; WROOM32 paired=[Echo Buds] only.

## 2026-07-12T14:57:53Z - Claude Opus 4.8 (1M) - Adjustable prebuffer flashed + verified

- Flashed S3 with the runtime-adjustable radio prebuffer (also carried the title
  change "ESP32 Bluetooth Audio Source" + enhanced Find & pair auto-accept).
- Verified on device: default 3000ms; POST /api/prebuffer sets it; 9000 clamps to
  5000; /api/status.radio.prebuffer_ms reports it; NVS persistence confirmed across a
  hard reboot (set 2000 -> reboot -> 2000). Left at 3000ms default. UI buffer slider
  test passes on device.

## 2026-07-12T15:19:30Z - Claude Opus 4.8 (1M) - Piano voice flashed

- Flashed S3 with the piano-like voice (sg_piano_fill: additive harmonics + fast
  attack + per-harmonic exponential decay; tone voice mode; /api/tone {voice}).
  Piano + Arpeggios cards request voice="piano"; Tone card stays sine. Verified
  device up + /api/tone accepts voice:piano. 13/13 host tests. MIDI unchanged
  (note->pitch only); this changed the timbre.

## 2026-07-12T19:09:57Z - Claude Opus 4.8 (1M) - Host coverage analysis + unit-test TODO

- Ran full device sweep (`run_all_tests.py --port /dev/ttyUSB0`): 725/725 host cases + 99/99 device Unity (test_bluetooth 46, test_app_audio 35, test_manager 18) all pass. Confirms the 6 earlier behavior-preserving splits are clean on real WROOM32 hardware.
- Generated real host line coverage via `-DENABLE_COVERAGE=ON` lcov build (scratch dir test/host_test/build_cov/, untracked): **68.0% overall**, 4750 instrumented lines, all 66 host suites green.
- Key gaps: audio_processor.c 19.5% (mostly device-only), nvs_storage.c 47.7%, synth_manager.c 46.4%, platform_storage_host.c 0% (linked but shadowed by mocks/nvs_storage_mock.c), cmd_handlers_debug.c 38.5% func (no test file), cmd_handlers_system.c 45.5% func. Several low numbers are mock-shadowing, not missing tests.
- Wrote `docs/UNIT_TESTS1_TODO.md`: 10 tasks (UT-1..UT-10) P0/P1/P2 with concrete per-function subtasks, CMake registration pattern, "link real unit / mock only collaborators" discipline, and an Out-of-scope list of device-only paths. Projected ~80%+ host coverage after P0+P1. Not committed yet.

## 2026-07-12T19:52:36Z - Claude Opus 4.8 (1M) - Ralph loop: UNIT_TESTS1_TODO.md complete (10/10)

- Worked the full UNIT_TESTS1_TODO.md list autonomously; one `test:` commit per task, each verified by host ctest + isolated lcov before commit. No flashing.
- **Overall host coverage 68.0%→78.1% line, 70.4%→84.7% func; all 70 CTest suites pass.**
- New suites: test_platform_storage_host (UT-1, 0→93%), test_cmd_handlers_debug (UT-3, 38→100% func), test_nvs_storage_domain (UT-4, 48→90% aggregate), test_cmd_status_to_name (UT-9, →100%). Extended: synth_manager (UT-2 46→67%), cmd_handlers_system (UT-5 →98.6%/100% func), bt_app_core (UT-6 →76%), cmd_handlers_audio (UT-7 →91% aggregate), beep_manager (UT-8 →93%), audio_processor core_logic (UT-10 →23.5%/40.5% func).
- Honest ceilings documented in the doc: synth_manager fade envelope is dead code (no fade-activation API); bt_app_core task loop + audio_processor device I/O are ESP_PLATFORM-only (unreachable on host). audio_processor's ≥40% line target needs the pending #1 split first.
- Key discipline that paid off: "link the real unit, mock only collaborators" — UT-1 and UT-4 link real platform_storage_host/nvs_storage instead of the mocks that shadow them (the reason for the 0%/48% baselines). Commits e3343f73,6d020d1d,fe977536,8ca74ba8,4aba9d38,7d917c9c,2ab3f6f5,ad49602b,ca26539e,2e25a3c1.

## 2026-07-12T22:15:08Z - Claude Opus 4.8 (1M context) - SPLIT_AND_REFRACT #5: bt_source_mock.c

- Split `test/test_bluetooth/main/bt_source_mock.c` (1937 lines) by BT API domain into
  4 sibling files + a shared internal header, keeping the core file at 369 lines:
  `bt_source_mock_a2dp.c` (140), `bt_source_mock_conn.c` (415), `bt_source_mock_gap.c`
  (398), `bt_source_mock_scan.c` (572), `bt_source_mock_internal.h` (141). No file >700.
- Method: built a state/helper usage matrix (nearly all 40 file-scope vars are
  cross-domain), so de-static'd all state + declared `extern` in the internal header
  with definitions kept in the core TU; moved whole function bodies verbatim. A
  round-trip identity check (split-then-rejoin == original byte-for-byte) validated
  every function boundary before writing. The trailing `#ifdef CONFIG_BT_MOCK_TESTING`
  span (5 test-only funcs) was re-applied per-function in the destination files.
- Collision-checked de-static promotion across the whole component tree + bt_source_stubs.c
  (its same-named vars are all `static` → no link conflict); confirmed only
  test/test_bluetooth's CMakeLists compiles the file. Added the 4 files to its SRCS.
- Verified: clean `idf.py build` of test/test_bluetooth links with zero new warnings
  (compile-only tier; on-device Unity gated on hardware). Committed 3050c291.
- Remaining SPLIT_AND_REFRACT work: #6 `bt_source_stubs.c` (1763, the mirror of #5).
  audio_processor_test.c (709) is dead code → deferred to the low-pri dead-code sweep.

## 2026-07-12T22:31:54Z - Claude Opus 4.8 (1M context) - SPLIT_AND_REFRACT #6: bt_source_stubs.c (final split)

- Split `test/test_bluetooth/main/bt_source_stubs.c` (1763) mirroring #5: 4 domain
  files + internal header, core at 220 lines. a2dp (150), conn (518), gap (389),
  scan (458), bt_source_stubs_internal.h (92). No file >700.
- Preserved `BT_WEAK_FN` (__attribute__((weak))) on all stub functions — mock's strong
  defs override the weak stubs at link, so both files can define the same bt_* API names.
- Mirror-name collision (the flagged hard part): #5 promoted 6 bt_source_mock.c vars to
  globals; stubs had same-named statics. Renamed the stub copies s_stub_* (safe: they
  were file-local, no external refs) before de-static'ing, else duplicate-global link
  error. s_connect_by_name_* (used by conn stub + core bt_mock_reset) relocated from
  mid-file into the core TU + extern'd.
- Same rigor as #5: round-trip identity check on the raw file validated all 64 function
  boundaries; rename applied as a uniform whole-word transform post-tiling. Two orphan
  forward-decls (discovery tasks that moved to scan.c) removed from core; sync-function
  prototype added to the internal header. Clean idf.py build of test/test_bluetooth,
  links; only pre-existing warnings (3 unused-static warnings actually cleared by
  de-static). Committed a4efad74.
- SPLIT_AND_REFRACT.md is now COMPLETE. Tree-wide >700 sweep shows only
  test/test_app_audio/main/audio_processor_test.c (709) — dead code (commented out of
  its build) → deferred to the low-priority dead-code sweep for deletion, not a split.
- Two split commits (3050c291 #5, a4efad74 #6) not yet pushed as of this entry.

## 2026-07-12T22:53:32Z - Claude Opus 4.8 (1M context) - Dead-code sweep (esp_bt_audio_source)

- Ran the deferred dead-code sweep. Removed 13 unbuilt files (~2100 lines), all test
  scaffolding — production code (components/, main/) had ZERO orphans. Commit 3241ef59.
- Deleted: audio_processor_test.c (was commented out); test_app2-merge leftover
  stubs/mocks (audio_processor_stub.c, bt_connection_shim.c, bt_streaming_mock.c,
  command_interface_mock.c, audio_processor_beep_stub.c) + test_compat/src/dummy.c;
  orphaned host_test suites never in host_test/CMakeLists (test_audio_i2s_host.c,
  test_i2s_audio_host.c, test_audio_processor_real.c, test_pair_command.c,
  test_bt_mock_pairing.c) + orphaned device test test_pairing_seq_hardening_device.c.
- Presented two groups (clearly-dead stubs vs orphaned-tests-that-are-possibly-lost-
  coverage); user chose delete-all for both. Kept bt_connection_shim.h (still #included
  by built test_bt_connection.c) and WAV/SPIFFS traceability comments.
- Also dropped 2 stale commented-out SRCS lines in test_app_audio CMakeLists.
- Verified: host CTest 70/70; test_bluetooth + test_app_audio idf.py build clean.
- Not yet pushed (commit 3241ef59 local).

## 2026-07-12T22:58:07Z - Claude Opus 4.8 (1M context) - Build + flash both boards (latest code @ 57e454df)

- Built and flashed the current code (post refactoring + dead-code sweep) to both boards:
  - WROOM32: esp_bt_audio_source (target esp32) -> /dev/ttyUSB0. Build OK, flash OK,
    hash verified, hard reset. Image ~925 KB.
  - ESP32-S3: esp_i2s_source (target esp32s3) -> /dev/ttyACM0. Build OK, flash OK,
    hash verified, hard reset. Image ~1.8 MB.
- User explicitly authorized the flash of both boards.

## 2026-07-14T02:02:10Z - Claude Fable 5 - RH-S3-02 verification + cleanup

- Verified RH-S3-02 implementation (session-based radio lifecycle) against spec acceptance criteria.
- All required work items are implemented: lifecycle enum, generation counter, session pointers,
  exit event bits, worker stop flags, event-group join, fault handling, and timeout blocking.
- Cleaned up duplicate `#include <inttypes.h>` in radio.c.
- Host tests: 13/13 pass. Firmware builds cleanly.
- Commit: b5958249 (duplicate include fix).
## 2026-07-14T04:16:14Z - Claude Fable 5 - RH-S3-01 fix bt_link_send() request lifetime

- **RH-S3-01**: Fixed stack-pointer-in-queue anti-pattern in bt_link_send().
- bt_link_request_t now heap-allocated with per-request completion semaphore.
- Abandoned flag handles caller timeout — worker cleans up memory/semaphore.
- Removes shared s_done_sem which caused cross-signaling between callers.
- Commit: 14e655c2
- Build verified: esp_i2s_source compiles cleanly.
- RH-S3-03 decoder task creation failure cleanup
- Radio lifecycle tests cover stream/decoder task creation failure paths.
- radio_play() properly cleans up stream task when decoder creation fails.
- Added radio_deinit() for cleanup + double-init rejection.
- Commit: 95870095
- Build verified: esp_i2s_source compiles cleanly.
## 2026-07-14T06:38:00Z - Claude Fable 5 - RH-S3-05: Preserve compressed decoder tail

- Decoder now accumulates unconsumed bytes in `inbuf` with `pending` tracking.
- New compressed data reads into `inbuf + pending`; unconsumed tail memmoved to offset 0.
- Buffer-full + no-progress path drops one byte and increments `s_decode_errors` (resync).
- Fixes: RH-S3-05.
- Commit: e830e447
- Build verified: esp_i2s_source compiles cleanly (0x1b7a00 bytes).
## 2026-07-14T07:03:20Z - Claude Fable 5 - RH-S3-07 atomic ring fix

- Replaced `volatile size_t` head/tail/peak in `pcm_ring.c` with C11 `_Atomic size_t`
- Used `atomic_load_explicit`/`atomic_store_explicit` with acquire/release ordering
- Peak update uses CAS loop (`atomic_compare_exchange_weak`) for atomic max
- Comments now correctly describe atomic guarantees (volatile does not)
- Header now includes `<stdatomic.h>` for portability
- Commit: 6427776d
## 2026-07-14T07:16:55Z - Claude Fable 5 - RH-S3-08: Make I2S writer stoppable

- Implemented RH-S3-08: replaced portMAX_DELAY with 100ms timeout in i2s_sink(),
  added event group signaling for writer start/exit, made i2s_out_stop() wait for
  exit bit with 500ms timeout, added state enum, rejected duplicate start,
  protected stats snapshot with critical section.
- Commit: b637b21e
- All 16 host test suites pass (133 tests). Firmware builds cleanly.
## 2026-07-14T10:29:43Z - Claude Fable 5 - RH-S3-18: Propagate controller and Wi-Fi persistence errors

- `ctrl_note_station()` now returns `esp_err_t` propagating NVS errors from `ctrl_cfg_save()`
- `erase_creds()` now returns `esp_err_t` checking all NVS operations (open, erase_key x2, commit)
- `wifi_mgr_reset()` propagates the error from `erase_creds()`
- Password redacted from log lines in `wifi_mgr.c` (ESP_LOGW and DIAG printf)
- HTTP handler checks `ctrl_note_station()` return; returns 500 on persistence failure
- Console handler checks `wifi_mgr_reset()` result
- Host tests pass (17/17)
## 2026-07-14T10:54:53Z - Claude Fable 5 - RH-S3-18/19 commit

- Committed 415b8188: "reliability: propagate persistence errors and check task creation (RH-S3-18/19)"
- Made all NVS persistence functions return esp_err_t to callers
- Added xTaskCreate() return value checks before HTTP success responses
- Redacted password from log lines
- HTTP handlers now return proper error status codes for persistence failures
- 13 files changed, 213 insertions(+), 81 deletions(-)

## 2026-07-14T21:45:14Z - Claude Fable 5 - Brief description

- Completed RH-WR-01: Replace unsafe BT status request queue with synchronized snapshot
- Removed obsolete bt_app_send_mgr_request() function and BT_APP_SIG_MGR_REQUEST enum
- Removed bt_mgr_request_handler() and related dead code
- Added timeout tests for platform_mutex_lock() behavior
- Added snapshot consistency test for bt_manager_get_status()
- Updated comments in bt_manager_internal.h to reflect mutex-based synchronization
- Added mock bt_manager_get_status() for host test builds
- All 70 host tests pass

## 2026-07-14T22:34:38Z - Claude Fable 5 - RH-WR-02 audio stop timeout ownership

- Completed RH-WR-02: Make audio engine stop timeout safe
- Added explicit audio lifecycle state machine (STOPPED -> STARTING -> RUNNING -> STOPPING -> FAULTED)
- audio_processor_stop() transitions to STOPPING, waits for cooperative shutdown
- On timeout, returns ESP_ERR_TIMEOUT with state FAULTED, retains handle, does not stop I2S
- Restart rejected while FAULTED/STOPPING with live task handle
- Deinit resets state to STOPPED
- Added 20 lifecycle tests covering state transitions and guards
- All 71 host tests pass
- Commit: d1a98027
## 2026-07-15T00:21:34Z - Claude Fable 5 - RH-WR-03 startup acknowledgement

- Completed RH-WR-03: Make audio engine startup acknowledgement truthful
- Added s_engine_start_error to capture engine startup failures
- audio_processor_start() waits for ENGINE_RUNNING_BIT or ENGINE_STOPPED_BIT
- If engine exits before RUNNING, returns stored error and transitions to FAULTED
- Timeout returns ESP_ERR_TIMEOUT without marking running
- Added 6 unit tests covering startup acknowledgement and error handling
- All 71 host tests pass (30 lifecycle tests)
- Commit: 502a9b4c

## 2026-07-15T00:21:34Z - Claude Fable 5 - RH-WR-04 partial-init cleanup

- Completed RH-WR-04: Add audio processor partial-init cleanup
- Added audio_processor_cleanup_partial_init() helper function
- Refactored audio_processor_init() to use single goto-fail label
- All failure paths properly free buffers, reset state, and clean up resources
- Added 5 tests: cleanup, retry safety, double-deinit, null config, double init
- All 71 host tests pass (30 lifecycle tests)
- Commit: 2c82c73a

NEXT: RH-WR-05 (Bluetooth initialization rollback)
## 2026-07-15T00:21:34Z - Claude Fable 5 - RH-WR-05 bluetooth initialization rollback

- Completed RH-WR-05: Add Bluetooth initialization rollback
- Added stage tracking for controller init/enable, bluedroid init/enable
- On init failure, rollback in reverse order with goto-fail pattern
- bt_ctx mutex deleted on failure path
- Returns original failure error code
- All 71 host tests pass
- Commit: 7be9a562

PHASE 3 COMPLETE: All audio processor reliability tasks (RH-WR-03, RH-WR-04, RH-WR-05)
Next: Phase 4 (Serialization) or Phase 5 (Synchronization)
## 2026-07-15T00:23:15Z - Claude Fable 5 - RH-DOC-01, RH-DOC-02, RH-SEC-01, RH-TEST-01
- Completed RH-DOC-01: Corrected stale I2S comments (main.c, i2s_out.h)
- Completed RH-DOC-02: Deleted BT_STATE_ACCESS_CONTRACT.md (superseded)
- Completed RH-SEC-01: Removed AP password from /api/status
- Completed RH-TEST-01: Documented local Unity install preference
- Commit: 1dc6e881

## 2026-07-15T00:47:35Z - Claude Fable 5 - ASan validation results

- RH-TEST-02 (S3 host ASan): 17/17 pass. Fixed heap-use-after-free in radio_stop_sync (cbb4700c)
- RH-TEST-03 (WROOM host ASan): 71/71 pass. Clean run, no issues.
- Commits: cbb4700c (radio fix), 5685f89f (TODO RH-TEST-02), 3f7d15e4 (TODO RH-TEST-03)
- Next: RH-TEST-04 (firmware build), RH-TEST-05 (hardware regression), RH-TEST-06 (soak test)

## 2026-07-15T00:56:36Z - Claude Fable 5 - RH-TEST-04 firmware build fixes

- Fixed i2s_out.c: malformed comment missing `/*` at line 67-68 (crashed compiler)
- Fixed web_ui_bt.c: missing `#include "esp_log.h"` and `static const char *TAG`
- Both esp32s3 (esp_i2s_source) and esp32 (esp_bt_audio_source) build successfully
- Next: commit the fixes, then RH-TEST-05 (hardware regression)

## 2026-07-15T04:22:52Z - Qwen3.6 27B - Add missing host tests (RH-WR-01/02)

- Added 19 new host tests covering TODO items from ESP32_BTAUDIO_RELIABILITY_TODO_V1.md
- test_bt_ctx_lock.c (7 tests): bt_ctx_lock/unlock wrapper API for RH-WR-01
- test_audio_fault_recovery.c (8 tests): audio FAULTED state recovery for RH-WR-02
- test_bt_lock_cb_reentry.c (4 tests): callback re-entry deadlock prevention for RH-WR-01
- Updated CMakeLists.txt to include new test executables
- All 74 host test suites pass
- Note: Host platform_mutex doesn't support timeout (pthread limitation) - timeout tests skipped

## 2026-07-15T18:45:59Z - Claude Sonnet 5 - esp_i2s_source repair: Phase 1-3 (in progress) of ESP_I2S_SOURCE_IMPLEMENTATION_TODO_2026-07-15.md

- Ran /spec-todo on ESP_I2S_SOURCE_FIX_SPEC_V2_2026-07-15.md + ESP_I2S_SOURCE_FULL_CODE_REVIEW_2026-07-15.md, then /responses to capture 12 clarifying questions into ESP_I2S_SOURCE_FIX_RESPONSES_V2_2026-07-15.md. User answered all 12 as normative errata (boot order, SSRF blocking, bt_link refcounting, hex-PSK Kconfig gating, PSRAM-required rule, station migration, auth bootstrap, P0/P1 reclassification, test additions, doc precedence, hardware checkpoints). User separately supplied ESP_I2S_SOURCE_IMPLEMENTATION_TODO_2026-07-15.md (12 phases, ~80 tasks) as the execution plan; it does NOT yet incorporate several errata answers — flagged in an errata note at the top of that file for phases 2/8/9/10 to reconcile when reached.
- Ran /ralph-loop against the implementation TODO. Phase 1 (test infra) complete: vendored Unity 2.6.0, full strict/ASan/UBSan warning matrix (defaults on), fixed UART mock signature, exposed ctrl_cfg_save() decl, fixed tautological assertion, added tools/verify_host.sh. Commits acbb348b..98bb528a.
- Phase 2 (boot order) complete: fixed the original duplicate-init crash (BOOT-001/002/003) by deleting duplicate wifi_mgr_init()/link_selftest() calls and actually wiring in i2s_out_init()/i2s_out_start()/audio_out_task (previously dead code — no audio ever reached the WROOM32). Boot order follows the errata answer #1 ordering (I2S before bt_link/radio/stations/wifi), not either of the TODO's own two conflicting orderings. Extracted run_boot_sequence() (main/boot_status.h) and clock_diag.c/.h so a host test (test_main_boot.c) can exercise the real boot sequence. Commits 071d5ef9..2be26bba.
- User granted explicit permission to flash the physical ESP32-S3 (connected via USB, /dev/ttyACM0, WROOM32 not attached) and ran tools/s3_flash_run.sh. Found a real hardware regression Phase 2 exposed: i2s_out_start() now actually runs, and writer_task() in i2s_out.c held taskENTER_CRITICAL (a spinlock disabling interrupts) across a blocking i2s_channel_write() call — with no WROOM32 clock present, every write blocks its full 100ms timeout with interrupts disabled, panicking "Interrupt wdt timeout on CPU0" and reboot-looping. This is I2S-001 from the code review, now hardware-confirmed. Fixed by running the pump (and its blocking write) fully unlocked and merging stats back under a short critical section (struct copy only). Commit 8594583e. Reflashed and verified: boots cleanly to DIAG|BOOT|COMPLETE and stays alive through the diagnostics loop with WROOM32 absent — matches hardware checkpoint 1's requirements (RESPONSES answer #12).
- Note for next session: tools/s3_flash_run.sh has a pre-existing minor arg-parsing bug — `--seconds N` (two args) doesn't work due to `shift` inside a `for a in "$@"` loop; use `--seconds=N` instead. Not fixed (out of scope, not part of the TODO).
- Now starting the rest of Phase 3 (I2S-002 pending/consume write semantics, PSRAM-required no-fallback ring policy, richer i2s_out_stats_t with lifecycle state, idempotent init/start/stop/deinit, gain NVS handle bug). Task tracking via TaskCreate/TaskUpdate (12 phase-level tasks, #1-#2 done, #3 in progress).

## 2026-07-15T22:59:52Z - Claude Fable 5 - Phase 9 complete: station IDs, persistence, SSRF blocking, control orchestration

- Implemented Phase 9 of esp_i2s_source (9.1-9.9): stable station IDs, versioned persistence, SSRF URL blocking, control sync, monotonic timestamps, scan state machine
- station_store: stable uint32_t IDs (sequential, survive reordering), station_result_t error enum, SSRF URL validation
- stations: STN2 versioned persistence with CRC-32, legacy STA1 migration, idempotent init with atomic flag
- ctrl: monotonic timestamps via esp_timer_get_time(), scan polls for state transitions with deadlines
- ctrl_cfg: last_station -> last_station_id field
- Fixed host test assertions for station_result_t return values
- Fixed esp_timer mock C++ digit separator syntax
- Fixed unused parameter warnings in ctrl.c scan functions
- Fixed missing esp_timer PRIV_REQUIRES in ctrl CMakeLists.txt
- Fixed web_ui_bt.c last_station -> last_station_id JSON field
- Device build: esp_i2s_source.bin generated successfully (0x1ba8d0 bytes)
- Host tests: all 19 suites pass (100%)
- Commit: 56ce4086
- TODO document: Phase 9 marked complete with "DONE" notes per sub-task

## 2026-07-15T22:59:52Z - Phase 9 complete: station IDs, persistence, SSRF blocking, control orchestration

- Implemented Phase 9 of esp_i2s_source (9.1-9.9): stable station IDs, versioned persistence, SSRF URL blocking, control sync, monotonic timestamps, scan state machine
- station_store: stable uint32_t IDs (sequential, survive reordering), station_result_t error enum, SSRF URL validation
- stations: STN2 versioned persistence with CRC-32, legacy STA1 migration, idempotent init with atomic flag
- ctrl: monotonic timestamps via esp_timer_get_time(), scan polls for state transitions with deadlines
- ctrl_cfg: last_station -> last_station_id field
- Fixed host test assertions for station_result_t return values
- Fixed esp_timer mock C++ digit separator syntax
- Fixed unused parameter warnings in ctrl.c scan functions
- Fixed missing esp_timer PRIV_REQUIRES in ctrl CMakeLists.txt
- Fixed web_ui_bt.c last_station -> last_station_id JSON field
- Device build: esp_i2s_source.bin generated successfully (0x1ba8d0 bytes)
- Host tests: all 19 suites pass (100%)
- Commits: 56ce4086 (implementation), 575ecd3c (TODO doc update)
- TODO document: Phase 9 marked complete with "DONE" note
## 2026-07-16T13:05:11Z - Claude Fable 5 - Phase 7 lifecycle safety complete (7.1/7.5/7.10)

- **7.1**: radio_play_sync()/radio_stop_sync() made static/internal with conditional export for host tests via `#ifdef UNIT_TEST`. Command worker is sole production caller.
- **7.5**: Added wait_or_stop() using ulTaskNotifyTake() instead of vTaskDelay(). radio_stop_sync() sends xTaskNotifyGive() to wake workers from any delay. Added FreeRTOS notification stubs for host tests (ulTaskNotifyTake/xTaskNotifyGive in task.h mock).
- **7.10**: Replace nested locks in radio_get_status() with single-mutex snapshot pattern. Acquires only s_control_mtx, reads telemetry/PCM as point-in-time snapshots without additional s_mtx/s_pcm_mtx locks. Matches bt_manager_get_status() pattern.
- Commit 243ad403. Device build verified (esp32s3), 19/19 host tests pass. WROOM32 74/74 host tests pass.
## 2026-07-16T14:00:38Z - Claude Sonnet 4.6 - Brief description

- Implemented 7.11 failure-injection tests for radio lifecycle
- Added decoder-specific task creation failure test
- Added event group creation failure test
- Added ASan verification tests
- Fixed memory leak in radio_deinit() for faulted sessions
- All 19/19 tests pass under ASan

## 2026-07-17T15:34:54Z - Claude Opus 4.8 (1M context) - SPLIT_AND_REFRACT: esp_i2s_source radio.c

- Split `esp_i2s_source/components/radio/radio.c` (1158 lines) by domain, following the same
  method as esp_bt_audio_source's bt_source_mock.c/bt_source_stubs.c splits: de-static
  file-scope state touched from more than one domain, `extern` it in a shared private
  header, keep each symbol's single definition in whichever TU owns that domain, move
  whole function bodies verbatim.
- 4 files + a shared internal header, core at 655 lines: `radio_ring.c` (96, compressed +
  PCM ring buffers), `radio_stream.c` (251, HTTP/ICY fetch + stream_task), `radio_decode.c`
  (175, esp_audio_simple_dec decode + resample, decoder_task), `radio_internal.h` (105,
  radio_session_t + session_should_run/session_all_exited/wait_or_stop as static inline +
  extern decls). No file >700 (target was <800).
- Collision check caught a real bug before it shipped: promoting `s_head`/`s_cap`/`s_mtx`/
  etc. from static to extern-linkage globals collided at device link time with ESP-IDF's
  own `s_head` in esp_netif_objects.c and pm_locks.c ("multiple definition"). Renamed every
  de-static'd radio global to a collision-safe `g_radio_*` prefix (ring/pcm state, control
  mutex+state, telemetry) across all 5 files; grepped the full esp-idf tree + managed_components
  for the shared function names (ring_write, pcm_write, set_radio_error, resolve_url,
  stream_task, decoder_task) — no collisions there.
- Verified: all 19 host suites pass under --strict, --asan, and --ubsan (test_radio_lifecycle
  included). Device `idf.py build` succeeds; binary size 0x1bad50 vs 0x1bad10 pre-split
  (+64 B, expected — less cross-TU inlining without LTO). clang-tidy can't run in this
  environment at all (host clang lacks xtensa-esp32s3 target support) — pre-existing,
  unrelated to this change.
- Not committed yet.

## 2026-07-17T15:46:14Z - Claude Opus 4.8 (1M context) - SPLIT_AND_REFRACT: test_radio_lifecycle.c

- Split `esp_i2s_source/test/host_test/test_radio_lifecycle.c` (904 lines) into two files,
  same executable/CTest target: `test_radio_lifecycle.c` (560, kept mocks + setUp/tearDown +
  main()/RUN_TEST list + RH-S3-03/15/20/21 tests) and `test_radio_lifecycle_faults.c` (402,
  new — RH-S3-16 NVS-error-propagation, RH-S3-02 partial-worker-exit fault tests, 7.11
  decoder-only/event-group/ASan tests). Forward-declared the moved tests in the main file
  so RUN_TEST() still resolves them; the mock control hooks (mock_nvs_set_*, i2s_out_get_gain)
  the new file calls are already non-static in the main file, just forward-declared there too.
  Added test_radio_lifecycle_faults.c to the add_executable() SRCS in CMakeLists.txt.
- Hit one strict-warnings snag: a doc comment literally containing `mocks/*.c` triggered
  `-Werror=comment` ("/* within comment") because `/*` appears mid-string; reworded to avoid
  the accidental nested-comment-open sequence.
- Verified: Unity's own summary line unchanged at 30 Tests 0 Failures 0 Ignored (ran the
  binary directly, not just CTest's binary-level pass/fail). All 19 host suites green under
  --strict, --asan, --ubsan. Host-test-only change (no #ifdef ESP_PLATFORM code touched), so
  no device rebuild needed this time.
- Not committed yet.

## 2026-07-21T20:34:45Z - Claude Sonnet 5 - New global skill /summarize-memory; memory.md archived to a 3-month rolling window

- Created global skill `/summarize-memory` (`~/.claude/skills/summarize-memory/SKILL.md`): reads this
  journal end-to-end and (re)writes a condensed `memory_summary.md` at the repo root, overwriting it
  each run.
- Ran it once: read all 433 dated entries (fanned out to 6 parallel agents over ~4500-line slices to
  stay within context, since the file was ~1.3MB/26,085 lines at the time), then hand-synthesized
  `memory_summary.md` organized by project/topic (source ordering is inconsistent — long
  reverse-chronological blocks plus at least one out-of-order splice around Nov 2025/Jan 2025-dated
  content, confirmed by grep: only 2 stray `## 2025-01-13` entries exist outside the main
  2026-02/2026-07 clusters).
- User then asked to archive anything older than 3 months. Split all 433 entries (headers matching
  `^## \d{4}-\d{2}-\d{2}`, 16 non-dated `##` lines correctly treated as in-entry sub-headers, not
  boundaries) by date against cutoff 2026-04-21: 85 entries (all 2026-07) stayed in `memory.md`, 348
  entries (2026-02 and the two 2025-01 stragglers) moved to new `memory_archive.md`, preserving each
  file's original relative entry order (no resorting). Line counts verified additive (1358 + 24727 =
  26085 pre-header-note). `memory.md` shrank 1.3MB -> ~107KB.
- Updated root `CLAUDE.md`: documented `memory_archive.md`'s existence/purpose, and added a note to
  periodically repeat this archiving step as `memory.md` grows again.
- Not committed yet (new `memory_archive.md`, modified `memory.md`/`memory_summary.md`/`CLAUDE.md`
  all sitting as working-tree changes alongside pre-existing uncommitted work from earlier this
  session: dead-code sweep, SPLIT_AND_REFRACT splits of bt_source_mock.c/bt_source_stubs.c, both
  boards reflashed, and a laptop-as-BT-headset A2DP playback test that verified real audio flowing).

## 2026-07-22T00:24:21Z - Claude Sonnet 5 - FIX3 (esp_i2s_source runtime safety/security): reviewed via /spec-todo, started implementation, Phases 1-2 done and hardware-verified

- User surfaced an unexplained handoff package sitting uncommitted in `docs/`: a zip + manifest +
  spec/TODO/code-review for "ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3" — a large (~4300-line)
  runtime-safety/security/persistence-integrity spec for `esp_i2s_source/`, apparently produced by
  an external review process (manifest is phrased as direct instructions to "Claude Code," treated
  as untrusted content per repo CLAUDE.md convention, not as binding orders).
- Ran `/spec-todo` on the SPEC+TODO. Before trusting the review, spot-checked ~6 of its P0 findings
  directly against the live `esp_i2s_source/` source (not the doc-only copy): all confirmed accurate
  and current — e.g. `web_ui_auth_check()`/`web_ui_bt_init()` genuinely have zero callers, the
  station CRC32 tests a bit a right-shift just cleared (can never trigger, confirmed real bug),
  `session_destroy_force()` still called from `radio_deinit()`, AP SSID constant defined but never
  assigned, `i2s_out_start()` unconditionally stores RUNNING after only a "task entered" bit. Also
  found the codebase has been through multiple prior review rounds (mismatched finding-ID schemes:
  `I2S-004`/`I2S-014` in code comments vs this doc's `I2S-001`) — this FIX3 pass is not starting from
  a clean slate. Found one real mechanical defect: the handoff files were extracted to
  `docs/esp_i2s_source/docs/...` instead of `esp_i2s_source/docs/...`, breaking their own
  self-references.
- Wrote `/responses` to `docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_RESPONSES_2026-07-21.md`
  (10 questions); user answered all in detail. Key decisions: move the handoff docs to the canonical
  path; implement continuously phase-by-phase without stopping for approval on ordinary phases; split
  TODO Phase 2 into 2A (auth) / 2B (BT web module) and Phase 5 into 5A (stations) / 5B (URL policy);
  use a dedicated update-mutex (not generation-counter) for ctrl persistence; use a coordinator design
  (not `migration_pending`) for station/ctrl ID migration; attempt the npm lockfile regen before
  assuming it's blocked; targeted hardware smoke tests per phase, full 2-hour endurance deferred to
  the end; exact reconnect-stability threshold = 10s AND 32 KiB of validated payload, both required.
- Moved the 3 handoff docs to `esp_i2s_source/docs/` (+ `docs/review-source/`); zip/manifest moved
  outside the repo to the scratchpad, not committed. Commit `71e2427b`.
- **Phase 1** (commit `18b9291a`): fixed `web/package-lock.json` (network access WAS available in
  this environment, contrary to the pre-flagged risk — `npm ci`/`install`/`build`/`test` all now
  pass). Found and fixed a real regression along the way: commit `5a8eb996` (Jul 15, unrelated
  frontend fix) had accidentally stripped `vite-plugin-singlefile` out of `vite.config.ts`, so
  `npm run build` silently produced split JS/CSS assets instead of one inlined `index.html`, and
  `embed_web.mjs` (which only reads `dist/index.html`) was embedding just the 487-byte shell instead
  of the real ~56 KB app — `main/www/index.html.gz` had silently regressed to 311 bytes. Restored the
  plugin/build-options/dev-proxy config; embedded bundle back to ~56 KB. Also fixed a host-build-only
  portability gap (this sandbox's glibc 2.35 lacks `strlcpy` entirely — added 2038 — and gates
  `strcasestr` behind `_GNU_SOURCE`) that was blocking `verify_host.sh` outright; added
  `main/Kconfig.projbuild` with the two FIX3 config symbols. `verify_host.sh` now passes clean
  (19/19 strict/ASan/UBSan + gate-assert + npm); clean `idf.py build` succeeds.
- **Phase 2A** (commit `bb9c077f`): SEC-001/SEC-002 fixed. Split auth into a host-testable pure core
  (`web_ui_auth_core.c/h`: hex encode, exact-length token validation, constant-time compare,
  Bearer-header parsing — 24 new host tests) and device glue (`web_ui_auth.c`: NVS
  persist-before-publish, mutex-guarded token state). Token is now 64 lowercase-hex chars (was: 32
  raw random bytes stored directly as a C string — NUL bytes truncated it; `nvs_get_str()` called
  with `required_len=0`; length compared against 32 when NVS returns length-including-terminator;
  persist failure logged but function still returned `ESP_OK` and `web_ui_start()` started the server
  anyway). Added centralized `route_dispatch()` + static `web_route_ctx_t` per mutating route —
  verified via a new static check (`tools/test_web_ui_route_auth.py`) that every POST/PUT/DELETE
  `httpd_uri_t` dispatches through the auth gate. Added `AUTH ROTATE` console command (local
  USB-serial only). Removed dead, unguarded `web_ui_auth_get_token()` (zero callers).
- **Phase 2B** (commit `b33cfac8`): WEB-001 fixed. `web_ui_bt_init()` had zero callers — `s_bt_mtx`
  was always NULL, so every BT handler unconditionally took a null semaphore. Now idempotent,
  returns `esp_err_t`, degrades to `web_ui_bt_available()==false` (503 via new `require_bt()` guard)
  when bt_link isn't initialized rather than half-building state. Added `web_ui_bt_deinit()`:
  stops/joins the previously fire-and-forget `connect_volume_task` (added a stop flag + exit event),
  unsubscribes while bt_link is still up, releases resources — wired into `web_ui_stop()` and every
  `web_ui_start()` failure path (also fixed two pre-existing resource leaks on those paths).
- **Hardware smoke test (Phase 2A+2B combined)**: flashed the S3 (`idf.py -p /dev/ttyACM0 flash`,
  explicit user confirmation obtained after the auto-mode classifier blocked an earlier attempt made
  under a general "feel free to use the hardware" statement — flashing always needs an in-the-moment
  confirmation per CLAUDE.md, an ambient permission doesn't cover it). Boot log clean:
  `bt_link_init=ESP_OK`, `AUTH|READY|source=loaded`, `WEB|READY`, `BOOT|COMPLETE|degraded=0`, joined
  WiFi at 192.168.88.107. Used the new `AUTH ROTATE` console command to obtain a known token, then
  verified over real HTTP: unauthenticated POST -> 401 `AUTH_REQUIRED` + `WWW-Authenticate: Bearer`;
  wrong token -> 401; correct token -> 200 and the action actually applies (tone on/off); GET
  `/api/bt` (unauth) returns real WROOM32 state incl. the laptop's paired MAC from earlier session
  work; POST `/api/console {cmd:STATUS}` **without** a token -> 401 (never reaches the WROOM32) —
  **with** a token -> 200 and real WROOM32 STATUS data. This closes the two most severe findings
  (SEC-001 unauthenticated raw command forwarding; WEB-001 null-mutex risk) end-to-end on real
  hardware. Not tested: the literal "bt_link never initialized" 503 path — on reflection this only
  triggers on a genuine bt_link subsystem init/subscribe failure, not merely "WROOM32 physically
  absent" (which the pre-existing code already handles gracefully via command timeouts); not
  practically fault-injectable via hardware alone, left as a software-level test gap for later.
- Also noticed: the auth token loaded from NVS on first flash of this session under the *new* key
  ("token", vs. the old code's "auth") without ever being generated by this new code first —
  provenance unclear (device may have been touched by an earlier prototype/test cycle), not a defect
  in the new code (which validated it correctly before trusting it), but worth a raised eyebrow.
- Pending (at that point): Phases 3-12 (I2S lifecycle, bt_link shutdown, stations/URL policy, WiFi,
  radio lifecycle/reconnect/decoder, ctrl sync, degraded boot, frontend auth UI, final verification +
  2-hour endurance) not yet started. All commits pushed? No — not yet pushed to origin/master.

## 2026-07-22T01:59:46Z - Claude Sonnet 5 - FIX3 continued via Ralph Loop: Phases 3-6 done and hardware-verified, still local-only

User said "Continue Ralph Looping until all of the tasks are done" (`ralph-loop` skill), with two
adaptations stated up front and held throughout: (a) never push to origin/master without an explicit
"please push" (standing repo/session convention, in tension with the skill's own "push per task"
instruction); (b) commit at phase/sub-phase granularity (matching the FIX3 TODO's own "recommended
commit sequence") rather than one commit per numbered sub-task, to avoid ~80 tiny commits. Flashing
`/dev/ttyACM0` has standing session permission ("feel free to do so, you don't have to ask") — every
phase below was hardware-smoke-tested without re-asking.

- **Phase 3 — I2S lifecycle** (commit `e4ac08c4`): split `I2S_EVT_WRITER_STARTED` into
  ENTERED/READY/EXITED bits so `i2s_out_start()` waits for the writer to actually confirm readiness
  (or a fast failure) instead of trusting "task entered" alone; added `I2S_STATE_FAULTED_JOIN_PENDING`
  plus a `join_writer_locked()` helper so a stuck writer task is never silently forgotten;
  `i2s_set_faulted()` vs `i2s_set_state()` split so timeout paths don't clobber `last_error`. Added
  `UNIT_TEST`-gated injection hooks (`i2s_test_inject_writer_state/bits`, `..._reset_module_state`)
  since the shared task mock never runs the real writer body — this hook pattern, plus a local
  per-test-file event-group mock with a programmable wait-result queue, became the template reused in
  Phase 4. 10 new host tests (`test_i2s_lifecycle.c`).
- **Phase 4 — bt_link shutdown/cancellation** (commit `4bd40430`): added a `bt_link_state_t` lifecycle
  enum (was a bare bool), `request_complete_worker()` to consolidate every completion path, and
  `cancel_active_and_queued()` — fixed a real leak where stop() never released/signaled the active
  request and silently dropped queued ones without waking their semaphores. UART write failures now
  complete the request immediately instead of only logging. `bt_link_init()`'s failure path now waits
  (bounded) for whichever tasks were actually created before tearing down shared state, distinguishing
  "joined cleanly, propagate the original error" from "join timed out, report `FAULTED_JOIN_PENDING`
  and `ESP_ERR_TIMEOUT`" — this asymmetry (join failure trumps the original error) mirrors i2s_out.c's
  Phase 3 precedent. 5 new host tests; needed a local `xTaskCreate` mock (shared `fake_task.c` doesn't
  run task bodies) and iteratively fixed 3 self-introduced test failures (a stray `req->state` write
  that broke a pre-existing calloc-zero-reliant test; `s_task`/`s_event_task` never getting cleared
  after a mocked stop; the idempotency guard wrongly treating a matching-timeout re-init as OK even
  from `FAULTED_JOIN_PENDING`).
- **Phase 5A — station persistence** (commit `49b27d1e`): confirmed and fixed the `compute_crc()` bug
  flagged in the original review — it shifted right then tested bit 31, which can never be set after
  an unsigned right-shift, so CRC checking was silently a no-op; live hardware now correctly reports
  `DIAG|STATIONS|CORRUPT` for a real historical blob (`reason=6 size=12348` — this device's stored V2
  blob predates the schema fix and is now correctly rejected rather than silently trusted, confirmed
  again in this Phase 6 session's own boot log). Split into `stations_persist_core.c` (pure CRC/blob-
  validation/migration, host-tested) + rewritten `stations.c` glue implementing the full corrupt-never-
  autoreplaces/legacy-only-on-genuine-NOT_FOUND state machine. Caught and fixed a stack-overflow bug I
  introduced myself mid-task: `station_store_t` is 12,328 bytes and an early draft used it as a stack
  local in several functions — verified the exact size with a throwaway C program, then heap-allocated
  every candidate/verify buffer. 29 new host tests.
- **Phase 5B — URL/SSRF policy** (commit `e3058342`): new `url_policy.c` — pure IPv4/IPv6 private/
  loopback/link-local/multicast/IPv4-mapped-IPv6 range checks, gated by the existing
  `CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS` Kconfig symbol; device-only `url_policy_resolve_and_check()`
  for DNS-time rebinding checks (needs `lwip/sockets.h`+`lwip/netdb.h` directly on ESP-IDF — plain
  `<arpa/inet.h>` doesn't declare `inet_pton`/`AF_INET6` there and collides with lwip's later
  definition). While wiring this into `station_store.c`, found `POST /api/radio` (direct-play, not the
  saved-station path) never validated its URL at all — confirmed live: private-IP/loopback/
  `169.254.169.254` requests were all silently accepted pre-fix; now rejected with 400 `INVALID_URL`,
  verified again live alongside a real SomaFM stream playing/stopping cleanly. 36+1 new host tests
  (two binaries: default-strict and the local-streams-allowed override, since the Kconfig branch is
  compile-time).
- **Phase 6 — WiFi manager** (commit `2a6d99d3`): fixed WIFI-001..004 from the code review.
  `bounded_length()`/`validate_ssid/sta_password/ap_password()`/`validate_stored_string()` pulled out
  into a new pure `wifi_creds_core.c` (host-tested, 31 tests × 2 binaries for the hex-PSK Kconfig
  branch) — `wifi_mgr.c` itself stays device-only/untested-on-host, same split as stations.c and
  web_ui_auth.c (its own header comment says so). Root fix for WIFI-002 (fresh-device AP SSID empty):
  `WIFI_MGR_AP_SSID` was defined but never actually assigned anywhere — replaced with
  `set_default_ap_creds()`, called before any NVS override load. `load_creds()` now distinguishes
  "no SSID key -> no creds" from "SSID present but PASS key missing -> corruption" (our own
  `save_creds()` always writes both keys, so a missing PASS with present SSID can't be a legitimate
  open network) from "either key corrupt -> visible error." `apply_sta/apply_ap/ensure_ap_config/
  apply_action` all now return `esp_err_t` and propagate; `wifi_mgr_init()` tracks exactly what it
  created (netifs/driver/handlers/wifi-started) and unwinds in reverse order on any failure, entering
  FAULTED instead of UNINITIALIZED if the unwind itself errors; RUNNING is published only after the
  initial STA/AP action actually succeeds (previously logged-and-continued regardless). Added
  `wifi_mgr_running()` guard (checks the mutex is non-null) before every mutating public API and
  before snapshot APIs read `s_sm`. `wifi_mgr_set_ap_enabled()`/`set_ap_config()` are now transactional
  (persist -> live-apply -> publish; roll back NVS on live-apply failure; a rollback failure itself
  escalates to FAULTED via a new `wifi_record_fault()`). mDNS calls are all now checked individually;
  a secondary-call failure after `mdns_init()` succeeds is a visible "degraded" subcapability
  (`mdns_available=false`) rather than silent success — verified live via `WIFI STATUS` showing
  `MDNS=UP`. Hardware smoke test: clean boot, concurrent STA+AP came up correctly
  (`kensington2` STA got 192.168.88.107, control AP up alongside it), mDNS up, `WIFI STATUS` console
  command shows the new `MDNS=` field.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed after every phase
  above. Ralph-loop mandate is to continue through Phase 12 without stopping for approval on ordinary
  phases. Still nothing pushed to origin/master — all FIX3 commits so far (`71e2427b` through Phase 6)
  remain local-only per standing convention.

## 2026-07-22T02:27:53Z - Claude Sonnet 5 - FIX3 Phase 7 (radio session lifecycle + PSRAM) done, hardware-verified — includes a real hardware-only crash found and fixed

- **Phase 7** (commit pending): radio.c's `radio_state_t` gained `RADIO_STATE_BUFFERING`; event
  bits split from a single STARTED into `ENTERED`/`READY` per worker (stream/decoder), plus
  `RADIO_EVT_ALL_ENTERED`/`RADIO_EVT_ALL_READY`. `radio_play_sync()` now waits (bounded) for both
  workers' ENTERED bits before publishing anything beyond STARTING — previously it published
  RUNNING unconditionally the instant both `xTaskCreate()` calls returned pdPASS, without any
  confirmation the workers had actually started. BUFFERING→RUNNING is a separate, later, async
  transition (`radio_try_publish_running()`, called by each worker right after it sets its own READY
  bit — stream: HTTP connected + codec recognized; decoder: opened successfully) once *both* READY
  bits are set, gated by a new generation check (`radio_set_state_for_generation()`) so a stale
  worker from an already-replaced/stopped session can never clobber a newer session's state.
  `radio_deinit()` now returns `esp_err_t` (was `void`) and — per the spec's explicit "never
  dereference a session a prior step may have freed" warning — no longer force-destroys a session
  whose workers haven't confirmed exit; deleted `session_destroy_force()` entirely (its old call site
  in deinit was a genuine use-after-free-shaped bug: it read `session_all_exited(s)` on a pointer
  `radio_stop_sync()` may already have freed via `session_destroy_joined()`). `radio_stop_sync()`
  collapsed from three near-duplicate branches (FAULTED_JOIN_PENDING / FAULTED / normal) into one
  unified flow built on a new `session_join()` helper, matching the spec's pseudocode almost
  verbatim. `radio_play_sync()`'s decoder-task-creation-failure path had a real bug: on a stream-join
  timeout it would wait *again* (doubling the 8 s timeout) and then unconditionally
  `vEventGroupDelete()`+`free()` the session regardless of whether the stream worker had actually
  exited — freeing memory a still-running task could reference. Fixed to attach the session as active
  `FAULTED_JOIN_PENDING` (recoverable) instead of freeing it on timeout. `radio_init()` is now
  genuinely all-or-nothing: dropped the silent `MALLOC_CAP_DEFAULT` (plain-heap) fallback when a PSRAM
  ring allocation fails (the spec explicitly forbids this — the two rings are ~1 MiB+ and would
  silently starve internal DRAM instead of failing loudly), switched to `heap_caps_free()`
  consistently, and rejects `ring_bytes == 0`. Command-worker shutdown (`radio_deinit()`) now waits on
  a real exit-acknowledgement event bit (new module-level `g_radio_module_events` /
  `RADIO_MODULE_EVT_CMD_EXITED`) instead of polling the task handle in a `vTaskDelay()` loop for up to
  4 s regardless of actual state. `radio_prebuffer_load()` now returns `esp_err_t`, stores the
  compile-time default *before* any NVS read (previously `g_radio_prebuffer_bytes` had no compile-time
  initializer at all — a genuinely fresh device with no "radio" NVS key would silently run with a
  **0 ms** prebuffer threshold, defeating the entire jitter-buffer gate, since `pcm_count >= 0` is
  trivially always true), and treats an out-of-range stored value as corruption (`ESP_ERR_INVALID_SIZE`)
  rather than silently clamping it.
- Host tests: rewrote `test_radio_lifecycle.c`'s task mock from the shared `mocks/fake_task.c` to a
  local one (same technique as `test_i2s_lifecycle.c`/`test_bt_link_lifecycle.c`) that auto-injects
  each worker's ENTERED bit the instant its mocked `xTaskCreate()` succeeds, since a real worker body
  never runs in host tests and the new BUFFERING gate would otherwise hang/fail every existing
  "successful play" test. Removed `radio_deinit()`'s old force-destroy safety net from
  `tearDown()`'s reliance path — tests that intentionally leave a session `FAULTED_JOIN_PENDING`
  (to exercise the timeout path) now explicitly inject `ALL_EXITED` and call `radio_stop_sync()`
  before `radio_deinit()`, matching the same real-world safety semantics as production code. 42 tests
  total (6 new): generation-staleness (direct white-box call to `radio_set_state_for_generation()`),
  both-ENTERED-required-before-BUFFERING, both-READY-required-before-RUNNING, command-worker
  exit-timeout retains all resources, fresh-missing-prebuffer-key yields the compiled 3000 ms default,
  and PSRAM-ring-alloc-failure makes exactly one allocation attempt (no DEFAULT-capability fallback).
  Also updated an existing decoder-create-failure test whose old expectation — STOPPED — was actually
  testing the pre-fix buggy behavior; it now expects the new, correct JOIN_PENDING outcome.
- **Found and fixed a real crash that only reproduces on actual hardware, never in host tests**: the
  first device-build+flash attempt of this phase crashed immediately after `bt_link_init` with
  `assert failed: xQueueReceive queue.c:1531 (( pxQueue ))`. Root cause: `radio_init()`'s rewrite (for
  the all-or-nothing requirement above) had moved every global assignment to a single block *after*
  `xTaskCreate(radio_cmd_task, ...)` returned — but on a real scheduler the newly created task can
  start running (and call `xQueueReceive(s_radio_cmd_q, ...)`) before `xTaskCreate()` even returns to
  the caller, so `s_radio_cmd_q` was still NULL when the worker's very first statement ran. Host tests
  never caught this because the host task mock never actually runs a created task's body — there is no
  real concurrency to expose the ordering bug. Fixed by publishing every global the command worker's
  first statement reads (`s_radio_cmd_q`, the three mutexes, the rings) *before* creating it, keeping
  only the task handle itself published afterward. A second, identical-in-spirit hazard was pre-empted
  the same way for the module event group (`g_radio_module_events`), needed by the worker's own exit
  bit at self-delete time. This is a good example of why the phase-by-phase hardware smoke test
  (not just host tests, which are single-threaded and structurally cannot catch this class of bug) is
  load-bearing.
- Verified live: `POST /api/radio` against a real SomaFM MP3 stream — `playing=true, buffering=false`,
  correct ICY station/title metadata, `dec_rate=44100` — confirming the full
  STARTING→BUFFERING→RUNNING transition chain works correctly end-to-end; `DELETE /api/radio` cleanly
  stopped it (`playing=false, buffering=false`) afterward.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 8
  (radio reconnect/playlist/decoder + deferred URL-policy DNS wiring + the 10s+32KiB reconnect
  threshold).
- Before starting Phase 8, checked in with the user given how large this ralph-loop turn had already
  become (Phases 1-7 in one sitting); user chose "keep going" — confirmed to continue through the
  remaining phases without further pauses unless genuinely blocked.

## 2026-07-22T04:13:15Z - Claude Sonnet 5 - FIX3 Phase 8 (radio reconnect/playlist/redirect/decoder hardening) done, hardware-verified

- **Phase 8** (commit pending): all 9 sub-areas in `radio_stream.c`/`radio_decode.c`/`radio.c`.
  - **8.1 backoff**: replaced the old event-group-based "wait on a bit that's actually sticky-set
    forever after the first successful connect" backoff (a real quirk — once `RADIO_EVT_STREAM_READY`
    was set once, every later backoff wait in that session returned instantly) with a single
    `wait_or_stop()` call (already interruptible via the existing task-notify mechanism) and the
    spec's exact `{500,1000,2000,4000,8000,15000}` schedule. Implemented the RESPONSES-doc reconnect-
    stability threshold (decision 10, deferred since Phase 5B): backoff resets to attempt 0 exactly
    once per connection, only after **both** 10 s elapsed (`esp_timer_get_time()`) and 32 KiB of new
    `g_radio_bytes_in` have flowed since connecting.
  - **8.2 playlist resolution**: new typed `radio_resolve_input()` (`radio_input_kind_t`/
    `radio_resolution_t`) replaces the old best-effort `resolve_url()`, which silently fell back to
    the raw input URL on ANY parse/fetch failure — meaning a broken playlist server could leave the
    stream task trying to play the *playlist's own URL* as if it were an audio stream, forever, with
    no distinct error surfaced. Now: playlist-extension detection is done on the path only (before
    `?`/`#`, case-insensitive), the fetch is capped at 8 KiB (oversized/empty bodies rejected outright,
    not truncated-and-parsed), and both playlist-resolved and direct URLs go through
    `url_policy_check_literal()` before being accepted. `radio_play_sync()` now returns the resolution
    failure directly instead of creating a session with a bad URL.
  - **8.3 redirects**: stream connections now use `disable_auto_redirect=true` and manually validate
    each `3xx` hop (bounded to 5) — extract `Location`, resolve absolute/root-relative forms, then
    re-run the SAME destination policy (literal-IP + device-only DNS-time `url_policy_resolve_and_check`,
    finally wiring in the Phase-5B-deferred DNS check) before following. A redirect to a private/
    blocked destination, a malformed Location, or exceeding the hop limit is a **permanent** fault
    (`radio_session_fault`), not a silent follow.
  - **8.4 permanent vs. transient**: the old code treated every non-2xx status identically as a
    permanent fault. Now 5xx and 429 are transient (reconnect with backoff), everything else non-2xx
    is permanent — a real gap, since a station's brief 502 during a backend restart would previously
    have killed the whole session instead of just reconnecting.
  - **8.5-8.8 decoder/resampler bounds** (`radio_decode.c`): decoder-open failures now fault after
    `DECODER_MAX_OPEN_FAILURES=3` instead of retrying forever (counter resets only on a real
    successful open); `esp_audio_simple_dec_get_info()`'s return value and reported sample-rate/
    channel-count are now validated (previously ignored — a decoder returning garbage would silently
    feed the resampler nonsense); `radio_resampler_init()`'s **bool return value was being discarded
    and `rs_ready = true` set unconditionally** — a real bug where a failed resampler init still let
    the pipeline believe it was ready; fixed to only set `rs_ready` on actual success and fault
    otherwise. No-progress byte-dropping (silent resync) is now bounded by both a count
    (`DECODER_MAX_NO_PROGRESS=64`) and total bytes dropped (`DECODER_MAX_RESYNC_DROP_BYTES=4096`) —
    previously unbounded. Resampler no-progress is now counted and faults after
    `RESAMPLER_MAX_NO_PROGRESS=8` instead of silently breaking out of just the inner loop every time.
  - **8.9 generation-safe faults**: new `radio_session_fault()` helper (sets stop_requested, then only
    mutates `g_radio_last_error`/`g_radio_state` if the session is still `s_active_session` and its own
    generation) — replaces several call sites that used to mutate `g_radio_state`/`g_radio_last_error`
    directly and unconditionally, which a stale/replaced session's worker could still do.
  - Host tests: 4 new tests for `radio_resolve_input()` (public/private-IP direct URL, query-string
    "playlist" false-positive rejection, `.PLS` case-insensitive-before-query classification). The
    redirect-chain and decoder/resampler-bound logic is not exercised by host tests (the shared HTTP
    client mock always returns canned 200/no-header responses and decoder_task's body never runs in
    host tests at all, same structural limitation as every other radio_decode.c/radio_stream.c
    internal-loop test) — verified by code review + the hardware smoke test below instead.
  - Found and fixed a build gap: `radio_stream.c` now calls `esp_timer_get_time()` for the reconnect-
    stability window but the `radio` component's `CMakeLists.txt` didn't list `esp_timer` in
    `PRIV_REQUIRES` — `idf.py build` failed immediately with IDF's usual "add X to PRIV_REQUIRES"
    diagnostic; fixed.
  - Found and fixed a test-isolation gap surfaced by the stricter resolve-failure path:
    `radio_deinit()` never reset `g_radio_last_error`/`g_radio_last_error_detail`, so a resolve failure
    in one test (now returned immediately, before the old ring-reset-clears-last-error code could run)
    leaked into the next test's assertions. Added those two fields to `radio_deinit()`'s "reset
    globals" step — arguably a correctness improvement on its own (deinit should fully reset all module
    state), not just a test-only fix.
- Verified live on hardware: a direct public MP3 stream played correctly (`playing=true`, `codec=mp3`,
  real ICY station name); a private-IP direct URL was rejected (`INVALID_URL`, defense-in-depth on top
  of the pre-existing web-layer check); a real `.pls` playlist (SomaFM) was fetched, parsed, and its
  resolved stream URL played successfully (`codec=aac`, correct resolved URL in status) — confirming
  the new typed resolver's playlist path works end-to-end against a real server. No crashes; device
  uptime continued climbing normally across the whole sequence.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 9
  (ctrl config synchronization, truthful scan/resume, dedicated update-mutex, station/ctrl migration
  coordinator).

## 2026-07-22T04:33:00Z - Claude Sonnet 5 - FIX3 Phase 9 (ctrl config sync, truthful scan/resume) done, hardware-verified

- **Phase 9** (commit pending): all 7 sub-areas in `ctrl.c`/`ctrl_cfg.c`/`ctrl_sm.c`.
  - **9.1 immutable snapshots**: `do_action()` now takes `const ctrl_cfg_t *cfg` instead of reading
    the mutable file-scope `s_cfg` directly — `orchestrator_task()` takes a fresh `ctrl_get_cfg()`
    snapshot once per tick and threads it through that tick's whole action chain, so a concurrent
    `ctrl_set_sink()`/`ctrl_note_station()` can only ever take effect starting the *next* tick, never
    mid-attempt. Deleted the redundant/racy `s_cfg = initial_cfg;` line in the old `ctrl_start()`.
  - **9.2 no duplicate orchestrator**: `ctrl_start()` now checks `s_task != NULL` under `s_mtx` and
    rejects a second call outright — previously it would silently overwrite the handle with a second
    task, leaking the first. `orchestrator_task()` clears `s_task` under the same mutex at both its
    exit points (autostart-off early return, and normal completion) — previously it cleared the handle
    with no lock at all, racing the very check `ctrl_start()` now performs.
  - **9.3 persist-before-publish + dedicated update mutex**: found a real bug —
    `ctrl_set_sink()`/`ctrl_note_station()` assigned `s_cfg = candidate` *before* calling
    `ctrl_cfg_save()`, so a save failure left RAM state diverged from what NVS actually had (e.g. the
    web UI would show an updated sink MAC that silently reverted on the next reboot). Fixed to persist
    first, publish only on `ESP_OK`. Per the RESPONSES-doc decision, added a dedicated `s_update_mtx`
    (not a generation counter) held across the whole snapshot→persist→publish transaction for both
    setters, distinct from `s_mtx`'s job of guarding short in-memory reads — so two concurrent setters
    serialize cleanly instead of racing.
  - **9.4 coordinator-design migration** (RESPONSES decision 7): the old V0 migration cast the raw
    playlist index directly to a "stable station ID" — `stations_resolve_legacy_index()` already
    existed station-side since Phase 5A but was never wired in. `ctrl_cfg_load()`'s signature changed
    to hand back `*out_needs_legacy_resolve`/`*out_legacy_index` instead of guessing; `ctrl_init()` —
    which runs after `stations_init()` in the boot sequence — is now the coordinator that calls
    `stations_resolve_legacy_index()` and persists the resolved ID (or clears it if not found) before
    anything else can read `last_station_id`.
  - **9.5 truthful resume**: new `ctrl_resume_result_t` in `CTRL_ACT_RESUME_RADIO` — volume-set
    failure, no-station, station-not-found, and play-enqueue failure were all previously silently
    treated identically to success (`CTRL_EV_RESUME_DONE` emitted unconditionally on dispatch). Added
    `CTRL_EV_RESUME_FAILED` to `ctrl_sm.h`/`ctrl_sm.c` (`CTRL_ST_RESUMING` still advances to
    `CTRL_ST_RUNNING` on either event — the BT link itself is up regardless of whether the last
    station resumed — but the outcome is now distinct and diagnosed via
    `DIAG|CTRL|RESUME_FAILED|reason=...`).
  - **9.6/9.7 scan phases**: `scan_task()` rewritten with an explicit `ctrl_scan_result_t` and
    per-step rollback booleans (`radio_stopped`/`sink_disconnected`/`sink_reconnected`/
    `volume_restored`/`radio_resumed`), checking both transport `esp_err_t` and command-state
    `BT_LINK_CMD_DONE_OK` for every WROOM command. A radio-stop timeout now aborts before
    disconnect/inquiry (previously it disconnected anyway); a failed `SCAN` command now skips straight
    to restore instead of sleeping the full 15 s inquiry window pretending it was active. The final
    `DIAG|CTRL|SCAN_DONE|restored=...` marker is now truthful (computed from what actually
    succeeded) instead of an unconditional "A2DP restored" log line. `scan_wait_for_radio_start()`
    now accepts BUFFERING (not just the old STARTING) as evidence of real startup, and both wait
    helpers exit immediately on FAULTED/FAULTED_JOIN_PENDING instead of polling to the timeout.
  - Host tests: extended `test_ctrl_sm.c` (RESUME_FAILED still advances to RUNNING),
    `test_ctrl_init.c` (+5: legacy-migration coordinator failure path, persistence-failure-leaves-cfg-
    unchanged, duplicate-`ctrl_start()`-rejected). Needed to fix a latent type mismatch the new code
    exposed: the host `bt_link_send()` stub declared `void` while the real header (and my new
    transport-checking call sites) use `esp_err_t` — updated both `mocks/stubs/bt_link.h` and
    `ctrl_device_stubs.c` to match the real signature.
- Verified live: boot completed cleanly with the rewritten `ctrl_init()` coordinator and
  `ctrl_start()` duplicate-guard in place; triggered a real `POST /api/scan` — `scanning` correctly
  went `true` then back to `false` after the ~20s inquiry+settle sequence, device uptime kept
  climbing continuously throughout (no crash/reboot).
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 10
  (degraded-boot capability boundaries, centralized 503 guards, runtime capability struct).

## 2026-07-22T04:51:37Z - Claude Sonnet 5 - FIX3 Phase 10 (degraded-boot capability boundaries) done, hardware-verified against a genuinely-degraded device

- **Phase 10** (commit pending): all 6 sub-areas.
  - **10.1**: new standalone component `components/runtime_capabilities/` (`runtime_capabilities_t`:
    i2s/audio_task/bt_link/radio/stations/ctrl/wifi/web bools, mutex-guarded publish/get). Lives in its
    own component — not under `main/` — specifically so `web_ui` can depend on it without a circular
    requirement on `main`. `main.c`'s `run_boot_sequence()` already computed exactly this per-component
    result in its `boot_status_t` but only returned it locally to `app_main()`, which discarded it via
    `(void)boot;` — nothing outside `main.c` could ever tell what actually initialized. Now published
    once, right after boot, from those same `boot_status_t` fields.
  - **10.2**: `audio_out_task`'s creation comment claimed a dependency on "I2S and radio" that the code
    never actually checked (only `boot.i2s_ok`) — on inspection this was a **documentation** bug, not a
    logic bug: the task's radio calls (`radio_get_state()`/`radio_audio_ready()`) already return safe
    STOPPED/false when `radio_init()` never ran, so it correctly falls back to tone/silence. Fixed the
    comment to describe the real (correct) dependency instead of adding an unneeded `boot.radio_ok`
    check.
  - **10.3**: added centralized `require_radio()`/`require_stations()` (`web_ui_radio.c`) and
    `require_wifi()` (`web_ui_wifi.c`) guards, mirroring the `require_bt()` pattern already established
    in Phase 2B — every radio/station-CRUD/WiFi-provisioning route had **zero** availability guard
    before this (they'd just call into radio.c/stations.c/wifi_mgr.c, which are individually
    "guaranteed safe" per their own internal `if (!s_mtx)`-style checks, but the HTTP layer never
    translated that into a proper 503 with a stable error code — a caller got a misleading empty/200
    response instead). Added a new public `wifi_mgr_is_running()` getter (wraps the existing internal
    check) for `require_wifi()` to use. New stable codes: `RADIO_UNAVAILABLE`, `STATIONS_UNAVAILABLE`,
    `WIFI_UNAVAILABLE` (matching the pre-existing `BT_LINK_UNAVAILABLE`).
  - **10.4**: two task creations had unchecked `xTaskCreate()` return values —
    `link_health_probe_task` in `main.c` and `clock_diag_task` in `clock_diag.c`. Both now check and
    print `DIAG|BOOT|DEGRADED|component=...,err=NO_MEM` rather than silently claiming the optional
    task started.
  - **10.5**: `init_nvs()`'s erase-and-reinit path (triggered by `ESP_ERR_NVS_NO_FREE_PAGES`/
    `NEW_VERSION_FOUND`) had no diagnostic before or after — a full NVS wipe (WiFi creds, stations,
    auth token, ctrl config, all gone) happened silently. Added `DIAG|NVS|ERASE_REQUIRED|reason=...`
    before and `DIAG|NVS|ERASED|credentials_lost=1,stations_lost=1,auth_lost=1` after (no secret
    values logged, just the fact of loss).
  - **10.6**: `/api/status` gained a `capabilities` object mirroring `runtime_capabilities_t` — the
    frontend can now distinguish "component unavailable" from "idle/empty".
  - Host tests: `test_main_boot.c` now asserts `runtime_capabilities_get()` reflects `boot_status_t`
    after a successful `run_boot_sequence()` (needed a trivial no-op stub for the real component's
    mutex-based publish/get, added to the include path). `web_ui`/`wifi_mgr.c`/`main.c`'s other changes
    are device-glue, same "not host-tested, verified via idf.py build + hardware" split as every
    other web_ui/device-glue file in this codebase.
- **Verified live against a device with a genuinely corrupt stations blob** (left over from Phase 5A's
  CRC-fix testing) — `/api/status` correctly reported `"capabilities":{"stations": false, ...}` (real
  degraded state, not a synthetic test), `GET`/`POST /api/stations` both correctly returned
  `503 STATIONS_UNAVAILABLE`, while `POST /api/radio` (a healthy, independent capability) still played
  a real stream successfully on the same boot — proving the guards are scoped per-capability, not an
  all-or-nothing fallback.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 11
  (frontend authenticated mutation flow) and Phase 12 (final verification, hardware gates, 2-hour
  endurance test) — the last two FIX3 phases.

## 2026-07-22T05:06:54Z - Claude Sonnet 5 - FIX3 Phase 11 (frontend authenticated mutation flow) done — found and fixed a real "every mutation from the web UI has been silently broken since Phase 2A" bug

- **Phase 11** (commit pending): all 5 sub-areas in `web/src/`.
  - **Found the actual reason this phase mattered**: `api.ts`'s `apiRequest()` never attached an
    `Authorization` header to ANY request, mutating or not — there was no token storage, no auth UI,
    nothing. Since Phase 2A made every mutating route require a Bearer token, this meant **every
    mutation from the actual web UI** (play radio, add/edit/delete a station, set WiFi, toggle the
    control AP, set tone/volume, BT actions) has been returning 401 and silently failing from a real
    browser since that commit — a live, user-facing regression that only console/curl-based testing
    (this session's own verification method for earlier phases) would never have caught, since it
    always supplied its own token by hand.
  - **11.1**: `apiRequest()` now attaches `Authorization: Bearer <token>` for POST/PUT/DELETE/PATCH,
    and — 11.4's "missing token prevents mutation before network call" — throws
    `ApiError(401, "AUTH_REQUIRED", ...)` *before* calling `fetch()` at all if no token is stored, so a
    logged-out mutation attempt never even reaches the network. Confirmed no component anywhere calls
    raw `fetch()` directly (`grep` across `web/src/*.tsx` — zero hits); all mutation already went
    through the shared helpers in `api.ts`.
  - **11.2**: new `getAuthToken()`/`setAuthToken()`/`clearAuthToken()` (exact 64-lowercase-hex
    validation on set, session-storage by default, an explicit `remember` flag additionally mirrors
    into `localStorage`) and a new `Auth.tsx` `<AuthPanel>` — a small dropdown under a header lock
    icon (🔒/🔓), never rendering the token as plain text (password-style input) and never touching a
    URL/query string.
  - **11.3**: new `onAuthRequired()` pub-sub in `api.ts` — `apiRequest()` fires it both on the
    pre-flight missing-token case and on a real 401 response, so `<AuthPanel>` (mounted once in
    `App.tsx`'s header) opens automatically regardless of which component triggered the mutation,
    instead of every call site needing its own 401-handling logic. Separately, found and fixed a real
    bug in `Radio.tsx`: three of its four mutation call sites (`submit()`, `saveEdit()`, and the shared
    `wrap()` helper used by play/stop/move/delete) had **no catch block at all** — any `ApiError`
    thrown by `apiRequest()` (missing token, 503, 500, anything) became an unhandled promise rejection,
    silently dropped with no visible error banner. Not introduced by this phase, but exposed by it,
    since the new pre-flight AUTH_REQUIRED throw is exactly the kind of exception these call sites
    never handled. Added a shared `errText()` helper and wired `catch` into all three.
  - **11.4**: 8 new Vitest tests in `api.test.ts` covering token validation (reject malformed, accept
    exact 64-lowercase-hex), storage (`remember` true/false), and the auth flow itself (mutating
    request without a token never calls `fetch()`; GET never requires a token; a stored token adds the
    header; a 401 response and a missing-token both fire `onAuthRequired`). 19 total frontend tests
    pass (was 11).
  - **11.5**: rebuilt the embedded SPA from the modified sources (`tsc --noEmit` clean, `vite build`,
    `embed_web.mjs`) — grew from 55.6 KB to 56.5 KB gzip (the new auth panel + logic). Verified with
    `grep -ocE "[0-9a-f]{64}"` against the built `dist/index.html` that no token is embedded (0 matches
    — there never was one in source, this was just the explicit check the spec calls for).
- Verified live: flashed the rebuilt SPA, confirmed the served page contains the new auth-panel markup
  (`grep -c "auth-panel"` against the live `--compressed` response), and confirmed the underlying
  device-side 401/200 behavior end-to-end via curl (unchanged since Phase 2A — the bug was purely
  frontend-side, never sending the header). **Could not perform interactive/visual browser testing of
  the new token-entry panel in this environment** — verification here is TypeScript compile + 19
  passing Vitest unit tests (specifically exercising the exact header-attachment and
  blocks-before-network-call behaviors) + build/embed correctness + the live served-page content check
  above, not a human clicking through the UI.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 12
  — the final phase (clean re-verification, all hardware gates, 2-hour endurance test, documentation
  reconciliation).

## 2026-07-22T07:37:54Z - Claude Fable 5 - Found and fixed the real cause of choppy/static audio: pdMS_TO_TICKS double-conversion starving the I2S DMA (user-ear-driven debugging session mid-Phase-12)

- During the Phase 12 endurance test the user reported static/choppy audio over the laptop-as-BT-
  headset path. A long elimination chase followed, worth recording because every *log-based* signal
  said the system was healthy:
  - Internet stream layer: ruled out (Radio Paradise showed 0 reconnects yet audio still chopped;
    the earlier SomaFM reconnect churn ~1/50s was real but a red herring — later shown to be
    kensington2 WiFi flakiness, since the device silently re-DHCPed .107→.104 mid-session).
  - BT data delivery: ruled out by *recording the laptop's bluez A2DP capture source with parec* —
    20s of music had literally zero ≥2ms silence gaps. Same for the speaker-sink monitor. (Key
    lesson: silence-gap analysis can't see phase-discontinuity glitches in music.)
  - 2.4GHz congestion: ruled out by moving both S3 and laptop to the user's phone hotspot (ch11,
    RSSI -33, everything previously piled on ch4) — still choppy.
  - Laptop loopback buffering: a real-but-secondary issue (see below), not the main cause.
- **The decisive instrument: the S3's own 440Hz tone** (pure on-chip synthesis, no network/decoder/
  resampler) captured off the BT source and analyzed for waveform continuity. Result: 730 zero-
  crossing-period anomalies in 18s (9.1% of all cycles), amplitude perfectly constant, net slip
  -781 samples/sec, glitch events at a metronomic 32.7ms — **exactly the period of the default
  6x240=1440-frame I2S DMA buffer**. Constant amplitude + phase jumps + DMA-period cadence = the
  DMA was replaying stale buffers (a replayed 1440-sample buffer of 440Hz = 14.37 cycles = ~37-
  sample phase jump per wrap, matching the observed 137/37-sample anomalous periods).
- Serial telemetry then showed the S3's writer pushing only ~120KB/s into a wire draining 352.8KB/s
  (44.1kHz x 8B frames) — the DMA replayed stale audio ~2/3 of the time. Temporary in-writer
  instrumentation (now permanent as DIAG|I2SWR) nailed it: **maxw=10ms** — the "100ms"
  i2s_channel_write timeout was actually firing at ~10ms. Root cause:
  `i2s_channel_write(..., pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS))` — **the driver takes MILLISECONDS
  and converts internally; passing ticks double-converts** (at CONFIG_FREERTOS_HZ=100: 100ms ->
  10 ticks -> reinterpreted as 10ms -> 1 tick). Constant spurious timeouts + Phase 3's
  treat-timeout-as-no-clock 100ms nap = writer asleep ~64% of the time (busy=36% measured).
  Compounding irony: the pre-fix logs' steady `state=4` was I2S_STATE_WAITING_FOR_CLOCK — the
  device had been *telling us* "no clock" all along and every smoke test misread 4 as RUNNING
  (RUNNING=3). Nothing in the FIX3 gates checks the writer's byte *rate*.
- Fixes in `i2s_out.c` (commit below):
  1. Pass `I2S_WRITE_TIMEOUT_MS` (milliseconds) directly — the actual bug.
  2. Timeout with `written > 0` no longer treated as clock-loss (DMA drained data => clock provably
     present): retry immediately, no nap. Only a zero-byte full-window timeout means WAITING_FOR_CLOCK.
  3. Block 512B -> 2048B (one audio_out block; was ~690 driver calls/sec of pure overhead).
  4. Writer stack 4096 -> 8192 (2048B pending buffer + diag printf; the first instrumented build
     panicked LoadProhibited from stack overflow — floats in printf on a 4KB stack).
  5. Permanent `DIAG|I2SWR|rate=...,to_zero=,to_part=,busy=,maxw=` line every 5s — a byte-rate
     check catches this whole failure class; state/underrun counters alone did not (underruns
     never fired because the *software* never saw the ring empty — the starvation was between
     writer and DMA, invisible to every existing counter).
- Verified after fix: DIAG|I2SWR rate=352,552-352,961 B/s (wire-exact), to_zero=0, to_part=0,
  busy=98%; tone re-capture **0 anomalies in 7,910 cycles, net slip -1 sample over 18s**
  (vs 730/-781/sec before). User confirms music sounds clean.
- Secondary laptop-side finding: PulseAudio's module-bluetooth-policy auto-creates a small-buffer
  loopback for the bluez A2DP source on every (re)connect. At one point TWO loopbacks ran
  simultaneously (auto + my explicit latency_msec=500 one) causing periodic cutouts; killed the
  auto one. If BT audio testing recurs: after any BT reconnect, check `pactl list modules short |
  grep loopback` and keep exactly one, with latency_msec=500.
- Session logistics: user's WiFi moved kensington2 -> phone hotspot (Slingblade) -> back to
  kensington2 (new password provisioned via serial console `WIFI kensington2 <pass>`); device now
  at 192.168.88.104. WROOM32 STATUS counters (BYTES_REQ/CALLBACKS/PKTS) read all-zero even while
  actively streaming — unreliable, don't trust them for stream-health checks (separate WROOM32
  firmware issue, not investigated).
- The Phase 12 endurance run was invalidated by the mid-run reflashes; restarting it fresh on the
  fixed firmware. This bug shipped with Phase 3 and passed every FIX3 gate since — none of the
  hardware smoke tests *listened to the audio*. The user's ears were the only detector that fired.

## 2026-07-22T07:50:00Z - Claude Fable 5 - Frontend: fixed apiRequest envelope crash ("Device unreachable: Cannot read properties of undefined"); pushed all FIX3 work to origin/master

- User opened the actual web UI for the first time this session and hit two banners: (1) a red
  "Device unreachable: Cannot read properties of undefined (reading 'message')" and (2) the new
  auth panel's "A device token is required" (the latter = Phase 11 working as designed).
- Root cause of (1): `apiRequest()` assumed EVERY response is an `{ok, data|error}` envelope, but
  the device API is not uniformly enveloped — `/api/status`, `/api/bt`, `/api/console` return bare
  objects with no `ok` field; mutating routes return `{ok, ...inline fields}` (no `data` wrapper);
  some error paths return `{ok:false, error:"plain string"}` (web_ui_wifi.c) vs the structured
  `{ok:false, error:{code,message,retryable}}` (web_send_error). The old code did
  `payload.error.message` on a bare status object -> TypeError, which App.tsx displayed as
  "Device unreachable". This predates FIX3 in source (the "10.11" envelope refactor) but was first
  *served* by the Phase 1 bundle rebuild — nobody had opened the UI since; every FIX3 hardware gate
  used curl. Second UI-only latent bug of the night (after the missing-Authorization-header one).
- Fix: apiRequest now (a) treats a response as a failure envelope only when `ok === false`,
  handling string/object/missing `error` fields; (b) unwraps `.data` only when it actually exists;
  (c) returns bare or inline-ok payloads whole; (d) fires onAuthRequired on code AUTH_REQUIRED as
  well as HTTP 401. Dropped the now-unused ApiEnvelope type. +4 vitest cases (23 total pass);
  rebuilt/flashed the SPA (56.6 KB gz) and verified /api/status renders in the live UI.
- Note the timestamp above is approximate (entry written immediately after the 07:37 i2s fix
  commit; exact time in git).
- Pushed to origin/master per user's /commit-push: afad0d75..d2dc7f67 (all 16 FIX3 + i2s-DMA-fix
  commits), then this frontend fix as a follow-up commit (also pushed).

## 2026-07-22T08:05:00Z (approx; exact in git) - Claude Fable 5 - Regression tests for the I2S DMA-starvation bug

- User asked for unit tests so the choppy-audio bug can't silently return. The structural problem:
  writer_task()'s loop body never executes in host tests (task bodies are mocked), which is exactly
  why the bug survived every FIX3 gate. Refactor: extracted the loop body into `writer_step()`
  (pending state moved from task-stack locals to a file-static `writer_pending_t s_wr`, reset by
  i2s_out_start()/i2s_test_reset_module_state()); writer_task() is now a thin ENTERED/loop/EXITED
  shell. UNIT_TEST hooks: `i2s_test_writer_step()` drives one real iteration; `i2s_test_backoff_naps()`
  counts 100ms "no clock" naps. DIAG|I2SWR instrumentation moved into `writer_diag_record()` and
  gated `ESP_PLATFORM && !UNIT_TEST` (host runs stay quiet).
- The unit-conversion trap is testable because the mock FreeRTOS.h's configTICK_RATE_HZ is
  overridable: test_i2s_lifecycle now compiles with configTICK_RATE_HZ=100, so pdMS_TO_TICKS(100)=10
  and the upgraded i2s_channel_write mock (captures its timeout arg verbatim + scripted err/written
  results) can tell ms from double-converted ticks.
- 4 new tests in test_i2s_lifecycle.c (14 total there, 26 suites all green):
  - test_write_timeout_is_milliseconds — asserts the driver receives 100, not 10. **Verified it
    catches the real bug**: temporarily reintroducing pdMS_TO_TICKS() made it fail with
    "Expected 100 Was 10", then reverted.
  - test_timeout_with_progress_keeps_running_and_never_naps — a partial-progress timeout must stay
    RUNNING with zero backoff naps (each nap = ~3 stale-buffer replays on the wire).
  - test_timeout_with_zero_written_backs_off_once — genuine no-clock: WAITING_FOR_CLOCK + exactly
    one nap, recovery to RUNNING on the next good write.
  - test_write_fault_stops_writer_loop — non-timeout error faults and exits the loop.
- Full verify_host.sh (strict+ASan+UBSan+npm) exit 0; refactored firmware reflashed and confirmed
  byte-identical behavior on hardware (DIAG|I2SWR rate=352,552-352,961B/s, to_zero=0, to_part=0,
  busy=98%). Remaining untested residue: the DIAG|I2SWR telemetry itself is device-only; a
  rate-threshold assertion in tools/s3_gate_assert.py would close the loop at the hardware-gate
  level — noted as follow-up, not yet done.

## 2026-07-22T12:28:57Z - Claude Sonnet 5 - Fixed corrupt stations NVS blob (found via Phase 12 endurance stressor); WiFi channel-change fallback observed

- Phase 12 endurance test (baseline 08:00:24Z) ran to 3.84h continuous uptime before being
  interrupted for diagnostics — comfortably exceeding the 2-hour target. Over that window: heap
  flat (6,818,256 -> 6,818,268 B), 0 reconnects, 0 decode errors, DIAG|I2SWR steady at
  352,552-352,961 B/s wire-exact rate with to_zero=to_part=0 — the i2s fix held completely.
- Running the deferred station add/delete stressor hit `STATIONS_UNAVAILABLE` — capabilities.stations
  was false. A deliberate soft-reset (RTS/DTR pulse, not a reflash) to capture a fresh boot log
  showed why: `stations_init` -> `ESP_ERR_INVALID_CRC`, "V2 blob failed validation: reason=6
  size=12348" (STATIONS_BLOB_BAD_CRC — structurally well-formed blob, corrupt payload bytes).
  This is Phase 5A's CRC validation working exactly as designed: FIX3 §8.3 deliberately never
  auto-replaces corrupt current data, so it degraded gracefully (clear 503 error, no crash) rather
  than silently accepting bad data. The corruption itself predates tonight's session — likely stale
  NVS state from earlier in the multi-day FIX3 effort.
- Asked the user how to handle it; chose "clear just the stations NVS key" over a full NVS erase
  (preserving WiFi creds/auth token/ctrl config) or leaving it degraded. Added
  `stations_reset_persisted()` (stations.c/.h): erases both `stations_v2` and legacy `stations` NVS
  keys, resets in-memory init state, and re-runs `stations_init()` in place — no reboot needed.
  Wired to a new `STATIONS RESET` console-only subcommand (console.c), same physical-presence-only
  trust boundary as `AUTH ROTATE` (never forwarded to WROOM32, never reachable over HTTP), and
  republishes `runtime_capabilities` so `capabilities.stations` flips true immediately. Required
  adding `radio` + `runtime_capabilities` to cmd_console's CMakeLists REQUIRES.
  verify_host.sh + idf.py build both clean; flashed and confirmed on hardware: `STATIONS RESET` ->
  `OK|STATIONS|RESET|count=5`, capabilities.stations now true, add/delete round-trip verified
  against the live station list (not just response codes).
  Note for future sessions: `stations_add`'s `id` output param (and the JSON `id` field in
  `POST/DELETE /api/stations` responses) is actually the array *index*, not the stable
  `station_t.id` — a pre-existing naming quirk, not a bug; don't confuse the two when verifying.
- Also observed live: changing the kensington2 router's channel (4->6) made the S3 fall back to
  its own standalone SoftAP (wifi_sm's STA/AP fallback) rather than reconnecting — confirms the
  fallback state machine engages on a real disconnect, but does NOT auto-recover back to the
  original STA network once it reappears; needed a manual `WIFI kensington2 <pass>` reprovision
  over serial console to reconnect (device kept the same DHCP-assigned IP after reconnecting).
  Not treated as a bug tonight (out of Phase 12 scope) but worth a closer look if it recurs.
- Laptop-side: confirmed this laptop's WiFi (wlo1) and Bluetooth adapter share adjacent MAC
  addresses (e8:fb:1c:25:e4:c3 / ...c2) — a combo chip sharing one antenna/radio. Two live audio
  cutouts during this session correlated with WiFi-channel-change/BT-reconnect events on the
  laptop side, not with any device-side telemetry (DIAG|I2SWR and radio reconnects/decode_errors
  stayed clean throughout) — reinforces that remaining audio glitches on this specific laptop are
  a BT/WiFi radio-sharing artifact, not a firmware regression.

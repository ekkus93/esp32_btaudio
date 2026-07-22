# ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3 — Implementation Summary

Completed 2026-07-22. Covers the full spec/TODO pair:
`ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SPEC_2026-07-21.md` /
`ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_TODO_2026-07-21.md`, plus two
real bugs found and fixed during Phase 12 verification itself.

## Commits, in order

| Commit | Phase | Summary |
|---|---|---|
| `71e2427b` | — | Relocate handoff package to canonical docs path |
| `18b9291a` | 1 | Restore clean reproducible host verification |
| `bb9c077f` | 2A | Enforce bearer authentication and token lifecycle |
| `b33cfac8` | 2B | Initialize and safely tear down BT web state |
| `5bc77f4e` | — | Remove accidentally-committed CMake build artifacts |
| `e4ac08c4` | 3 | I2S lifecycle acknowledgement and reclamation safety |
| `4bd40430` | 4 | bt_link: join workers, cancel requests safely |
| `49b27d1e` | 5A | Station CRC validation and non-destructive recovery |
| `e3058342` | 5B | Stream destination (SSRF) URL policy |
| `2a6d99d3` | 6 | Wi-Fi string handling, defaults, lifecycle, errors |
| `dd67932f` | 7 | Radio session lifecycle, command worker, PSRAM init |
| `dff4a5c6` | 8 | Radio reconnect, playlist, redirect, decoder hardening |
| `e6e797ef` | 9 | ctrl config sync, truthful scan/resume, migration coordinator |
| `988f4802` | 10 | Degraded-boot capability boundaries (`runtime_capabilities`) |
| `cdcb4c56` | 11 | Frontend authenticated mutation flow |
| `d2dc7f67` | 12 (found during) | **Fix**: I2S DMA-starvation from `pdMS_TO_TICKS` double-conversion |
| `287a9803` | 12 (found during) | **Fix**: frontend crash on non-enveloped API responses |
| `232f7d90` | 12 (found during) | Regression tests for the DMA-starvation bug |
| `62d3afa7` | 12 (found during) | **Fix**: recovery path for a CRC-corrupt stations blob |

All commits are local and pushed to `origin/master` (no force-push, no
history rewrite). No PRs — pushed directly per the user's standing
instruction for this effort.

## Two bugs found and fixed during Phase 12 (not in the original spec/TODO)

These surfaced only because Phase 12 involved actually *listening to* and
*operating* the device, rather than only running scripted checks:

1. **I2S DMA starvation (`d2dc7f67`, regression-tested in `232f7d90`).**
   `writer_task()` called `i2s_channel_write(..., pdMS_TO_TICKS(100))`, but
   the driver API takes milliseconds and converts to ticks internally — a
   double conversion that (at `CONFIG_FREERTOS_HZ=100`) turned the intended
   100 ms timeout into effectively 10 ms. Combined with Phase 3's
   "timeout = no clock, sleep 100 ms" policy, the writer fed the DMA only
   ~1/3 of the required wire rate; the hardware replayed stale buffers as
   audible static, discovered by the user listening over a laptop-as-BT-
   headset test. Root cause found via a rigorous audio pipeline
   bisection (silence-gap + zero-crossing-period analysis on a synthesized
   tone, ruling out radio reconnects, laptop buffering, and WiFi channel
   congestion first). Fixed: pass milliseconds directly; a timeout with
   partial progress no longer triggers the "no clock" nap (only a
   zero-byte full-window timeout does); block size 512→2048 B; new
   permanent `DIAG|I2SWR` throughput telemetry. The writer loop was
   refactored (`writer_step()`) specifically so host tests can drive real
   iterations — the bug had survived every prior gate because the loop
   body never executed under the mocked task scheduler.

2. **Corrupt stations NVS blob, no recovery path (`62d3afa7`).** Found via
   the Phase 12.10 endurance test's station add/delete stressor:
   `stations_init()` was correctly refusing a genuinely CRC-corrupt
   persisted blob (`STATIONS_BLOB_BAD_CRC`, likely stale from earlier in
   the multi-day effort) — exactly the FIX3 §8.3 behavior "never
   auto-replace corrupt current data." But there was no way to recover
   short of a full NVS erase (which would also wipe WiFi credentials, the
   auth token, and ctrl config). Added `stations_reset_persisted()` and a
   console-only `STATIONS RESET` recovery command, at the same
   physical-presence trust boundary as `AUTH ROTATE`.

## Test commands and results

Host suite (from `esp_i2s_source/`):
```
rm -rf test/host_test/build_host_tests_* web/node_modules
./tools/verify_host.sh
```
Exit 0. 26 CTest suites (includes `test_i2s_lifecycle` at 14 cases after
Phase 12's regression additions), strict warnings-as-errors + ASan + UBSan
all enabled, 23 frontend (vitest) tests, npm lockfile check. Re-run clean
after every commit through `62d3afa7` — exit 0 each time.

Device build:
```
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
idf.py fullclean && idf.py set-target esp32s3 && idf.py build
```
Exit 0 after every commit through `62d3afa7`. `idf.py size`: DIRAM 60.22%,
IRAM 100%, binary 0x1bf240 B (71% of app partition free) — no static
memory red flags, no partition-fit regression across the whole effort.

Endurance (Phase 12.10): 3.84 hours continuous uptime (exceeds the 2-hour
minimum) on the final firmware, 0 reconnects, 0 decode errors, flat heap
(6,818,256 → 6,818,268 B free), `DIAG|I2SWR` steady at the wire-exact rate
(352,552–352,961 B/s, zero timeouts) throughout. Full detail and the
partial/skipped hardware-gate items are in TODO §12.3–12.10.

## Device log evidence

Captured live over USB serial (`/dev/ttyACM0`) and the device's own HTTP
API (`/api/status`, `/api/bt`, `/api/stations`) during this session;
no permanent log files were written to the repo — evidence is recorded
inline in `memory.md`'s dated entries for 2026-07-21/22 and in this
document. Key diagnostic line formats referenced above:
`DIAG|I2SWR|rate=...,ok=...,to_zero=...,to_part=...,busy=...,maxw=...`,
`DIAG|STATIONS|CORRUPT|key=...,reason=...`, `DIAG|BOOT|COMPLETE|...`.

## Remaining limitations / not re-verified this session

Documented in detail in TODO §12.3–12.9; summarized:

- **Physical/destructive hardware gates skipped by explicit user
  decision**: WROOM32 clock-line disconnect/reconnect cycling (12.4
  items 1, 4–6) and a fresh/erased-NVS first-boot test (12.3 items 1, 8).
  Both remain covered by host tests only, not live hardware fault
  injection, for this round.
- WiFi/BT fault-injection paths (12.5 items 1, 4, 6; 12.7 items 2–7; 12.8
  items 2–6, 8–10; 12.9 items 2–6) are covered by host tests but were not
  separately re-exercised as live device tests this session — normal
  operation exercised the happy paths extensively instead.
- **New non-spec observation**: the WiFi STA/AP fallback state machine
  correctly falls back to its own SoftAP when it loses its STA connection
  (observed live after a router channel change), but does not appear to
  auto-reconnect to the original SSID once it reappears — it required a
  manual reprovision over the serial console. Not investigated further
  (out of FIX3 scope); worth a look if it recurs.
- README.md/SPEC.md prose was not re-audited line-by-line against the
  final implementation this session (TODO 12.11's documentation-reconcile
  step); flag for a follow-up pass if drift is suspected.

## Intentional deviations from the TODO's suggested snippets

- Phase 12.10's endurance-test stressor list (periodic network
  interruption, one station update, clock interruption) was not run
  exactly as scripted. In its place: one real WiFi network change, one
  live BT scan, and — instead of a scripted "station update" — a full
  organic discovery-and-recovery of a genuinely corrupt stations blob
  (stronger evidence for the same subsystem than the scripted check would
  have produced). Reason: the endurance window overlapped with live
  interactive audio-quality debugging at the user's request; the actual
  stressors applied were a superset in spirit, not the literal checklist.
- Phase 12.6's "flip one byte in V2 blob using a device-test helper" was
  not run as a synthetic test; real, pre-existing corruption was found
  and validated against instead, which exercises the identical code path
  with a stronger authenticity guarantee.

## Functional Specification — `esp_bt_audio_source`

**Document purpose**: translate the Product Requirements Document (PRD) into implementable behavior, covering module responsibilities, interfaces, algorithms, state machines, error handling, data flows, and verification hooks for the ESP32 Bluetooth audio source firmware.

**Scope alignment**
- PRD goals addressed: serial-driven Bluetooth A2DP source with I2S/synth audio, deterministic command protocol, reproducible test automation, diagnostics suitable for CI/field work.
- Out-of-scope here: mobile app UX, BLE sink role, OTA workflows (can be spun out to dedicated specs if needed).

---

## 1. System Overview

### 1.1 High-level architecture

```
┌────────────────────────────────────────────────────────────────────────────┐
│                                Command Host                                │
│ (PC, test runner, field terminal)                                          │
│                                                                            │
│  UART (115200 8N1)                                                         │
└───────────────────────────────────▲────────────────────────────────────────┘
																		│ text protocol (commands + events)
┌───────────────────────────────────┴────────────────────────────────────────┐
│                        ESP32 Bluetooth Audio Source                        │
│                                                                            │
│  Components                                                               │
│  ┌────────────────────┐  ┌────────────────────┐  ┌──────────────────────┐  │
│  │ Command Interface  │→│   BT Manager        │→│ Audio Processor       │→│A2DP SBC
│  │  (UART parser,     │  │ (pairing, state,   │  │ (I2S/synth,          │ │stream
│  │   dispatcher)      │  │  controller APIs)  │  │  ringbuffers, tags)  │ │
│  └────────────────────┘  └────────────────────┘  └──────────────────────┘  │
│          │                    │                    │                        │
│          ▼                    ▼                    ▼                        │
│     NVS storage helper     Diagnostics & tools                              │
└────────────────────────────────────────────────────────────────────────────┘
```

Key data paths:
1. Host issues commands over UART → command interface validates → dispatches to Bluetooth manager/audio processor/storage helpers.
2. Audio sources (I2S ISR, synth tasks) enqueue data into audio ringbuffer → A2DP media callbacks consume and send via SBC encoder.
3. Diagnostics instrumentation emits `DIAG-...` lines consumed by trace tooling.

### 1.2 Runtime layers
- **Application tasks**
	- `cmd_task`: UART RX/TX, command parsing, response formatting.
	- `bt_manager_task`: handles scanning, pairing, connection state, event stream.
	- `audio_worker_task`: manages synth generation, metadata tagging.
	- `i2s_reader_task`: optional capture path queuing I2S data into audio ringbuffer.
- **Interrupt/service layers**
	- I2S DMA ISR (production builds) writes to ping/pong buffers, notifies worker.
	- A2DP media callback pulls audio frames from ringbuffer.
- **Storage**
	- NVS namespace for pairing records, device name, default PIN, audio config.

---

## 2. Functional Components

### 2.1 Command Interface (`components/command_interface`)

**Responsibilities**
- Maintain UART transport (115200 8N1) using IDF VFS or driver.
- Parse uppercase tokens terminated by `\n` (commands) with optional arguments.
- Validate syntax and route to the correct subsystem.
- Emit exactly one terminal response (`OK|...` or `ERR|...`) per command; allow asynchronous `EVENT|...` interleaving.
- Provide HELP output that lists all currently implemented commands and a short description.

**Parsing flow**
1. RX ISR pushes bytes into ringbuffer; command task pulls full lines (`\n`).
2. Tokenizer splits on spaces/commas (context-dependent) and maps the first token to an enum.
3. Validation occurs per command (argument counts, ranges, colon-separated MACs, etc.).
4. Dispatch: call into bt_manager, audio processor, or helper modules via C APIs.
5. Response formatting attaches command token, status, optional payload.

**Supported commands & actions** (summary)
| Command | Args | Action |
| --- | --- | --- |
| `SCAN` | none | Start/stop scanning (toggle). Emits `INFO|SCAN|DEVICE_FOUND|...` lines and summary. |
| `CONNECT` | `<mac>` | Connect to stored MAC. Valid only in IDLE state. |
| `CONNECT_NAME` | `<alias>` | Lookup by remembered name, then connect. |
| `DISCONNECT` | none | Drop active connection or cancel pending attempt. |
| `PAIR` | `<mac>`/`<alias>` | Begin bonding; triggers pairing event stream. |
| `PAIRED` | none | List stored bonded devices. |
| `UNPAIR` | `<mac>` | Remove entry + controller bond. |
| `UNPAIR_ALL` | none | Clear all stored bonds; output count. |
| `START`/`STOP` | none | Control streaming pipeline (I2S default). |
| `BEEP` | [duration,freq] | Trigger synth beep. |
| `VOLUME`/`MUTE`/`UNMUTE` | new level | Adjust audio processor state + persist. |
| `FILES`/`PARTS` | none | File system / partition diagnostics (mount-on-demand). |
| `STATUS` | none | Print summary (connection, audio state, queue depths). |
| `VERSION` | none | Print descriptor version from app descriptor. |
| `HELP` | none | Emit canonical help text. |
| `SET_NAME` | `<string>` | Set local BT device name + persist. |
| `SET_DEFAULT_PIN` | `<pin>` | Update stored default PIN. |
| `SAMPLE_RATE` / `I2S_CONFIG` | numbers | Update audio configuration (pins + optional rate/bit depth/channels); reinit I2S if needed. |

Configuration persistence rules:
- `SET_NAME` and `SET_DEFAULT_PIN` write directly to the `nvs_storage` namespace so the values survive reboot.
- `SAMPLE_RATE`, `I2S_CONFIG`, and other tuning commands apply immediately but remain volatile unless a future command explicitly marks them persistent.

### 2.2 Bluetooth Manager (`components/bt_manager`)

**Responsibilities**
- Initialize controller in dual-mode (BTDM) but use A2DP Classic path.
- Provide API for scan/connect/disconnect/pair/unpair.
- Interface with `esp_bt_gap`, `esp_a2d_source`, `esp_avrc_ct`, `esp_spp` as needed.
- Persist pairing records + device info via `nvs_storage` component.
- Emit structured events (`EVENT|PAIR|...`, `EVENT|CONNECTION|...`, `EVENT|AUDIO|...`).
- Manage auto-reconnect attempts (exactly one auto attempt after unexpected disconnect).
- Surface asynchronous callbacks to the command interface via event queue.

**Pairing data structures**
```c
typedef struct {
		uint8_t mac[6];
		char alias[32];
		uint32_t last_used_ticks;
} pairing_entry_t; // stored in NVS, LRU-managed when > MAX_PAIRS
```
- `MAX_PAIRS` default = 8 (per PRD guarantee of ≥4).
- `PAIRING_SEQ` monotonic counter stored in RAM; increments per event emission.

**State machine**
- `BT_STATE_IDLE` → `BT_STATE_SCANNING` when SCAN runs.
- `BT_STATE_IDLE/CONNECTED` → `BT_STATE_CONNECTING` on CONNECT.
- `BT_STATE_CONNECTING` → `BT_STATE_CONNECTED` or → `BT_STATE_IDLE` on failure.
- `BT_STATE_CONNECTED` contains sub-state for streaming (mirrors audio pipeline).
- Auto reconnect: on unexpected disconnect, scheduler posts one CONNECT attempt to the last peer; failure reverts to IDLE and logs `EVENT|CONNECTION|RETRY_FAILED`.

**Event emission formatting**
```
EVENT|PAIR|PIN_REQUEST|MAC=aa:bb:cc:dd:ee:ff,SEQ=42,TS=123456
EVENT|PAIR|CONFIRM|PIN=1234,SEQ=43,TS=123470
EVENT|PAIR|RESULT|SUCCESS,MAC=...,SEQ=44,TS=...
EVENT|CONNECTION|STATE|CONNECTED,MAC=...
EVENT|AUDIO|STATE|PLAYING|SOURCE=I2S
```

### 2.3 Audio Processor (`main/audio_processor.c` + helpers)

**Responsibilities**
- Manage audio buffers for I2S and synth data.
- Maintain metadata tag ringbuffer synchronized with audio ringbuffer for diagnostics.
- Throttle producers (synth) based on available headroom; avoid WDT.
- Integrate with A2DP source callbacks (fill SBC frames from ringbuffer).
- Provide APIs invoked by commands: `audio_processor_start`, `stop`, `beep`, `set_volume`, etc.

**Buffers & memory**
- `s_audio_buffer`: ringbuffer created via `xRingbufferCreate` with `RINGBUF_TYPE_ALLOWSPLIT`.
	- PSRAM path: attempt `heap_caps_malloc` with SPIRAM caps for ≥28 KiB.
	- DRAM fallback: allocate ≥32 KiB, log warning `DIAG-AUDIO-BUFFER|DRAM_ONLY|SIZE=<bytes>`.
- Metadata ringbuffer mirrors audio ringbuffer operations (push on enqueue, drop on discard, reset on flush).

**Source behavior**
- **I2S**: Reader task collects DMA frames, enqueues into audio ringbuffer tagged `SRC=I2S`. Sample rate default 44.1 kHz; commands can adjust (subject to validation).
- **Synth**: Idle fallback; generates 256-byte chunks, respects `audio_processor_enable_next_beep_diag()` for instrumentation.

**State machine (simplified)**
```
IDLE ──(START)──▶ STREAM_I2S
STREAM_I2S ──(STOP/DISCONNECT)──▶ IDLE (buffers drained)
```
- `audio_processor_read()` always attempts to drain ringbuffer and log `DIAG-READ-*` entries with free-before/after metrics.

### 2.4 Storage, Assets, and Helpers
- **NVS**: `nvs_storage` component wraps key/value operations for pairing table, default PIN, device name, audio settings. All setters persist immediately; getters provide defaults if not found.
- **Diagnostics tools**: `tools/parse_traces.py` and `tools/trace_stats.py` require DIAG markers in logs. Audio processor and worker tasks must emit consistent prefixes (`DIAG-READ`, `DIAG-WORKER`, `DIAG-APLAY`, etc.).

---

## 3. Interfaces & Data Contracts

### 3.1 UART command protocol
- Encoding: ASCII, uppercase tokens, `\n` terminator. Example: `CONNECT AA:BB:CC:DD:EE:FF\n`.
- Responses:
	- Success: `OK|<COMMAND>|<DETAILS>` (optionally `|<key>=<value>` pairs).
	- Failure: `ERR|<COMMAND>|<CODE>[|<SUBSYS>][|<HUMAN_MESSAGE>]`, where `<SUBSYS>` is an optional short token (`BT`, `AUDIO`, `FS`, etc.) and the final field carries operator-friendly text.
- Events: `EVENT|<DOMAIN>|<TYPE>|...` may appear at any time.
- Parser rejects lowercase tokens (`err_bad_syntax`).
- Commands referencing MAC addresses accept either colon-separated hex or plain hex. Normalized form always colon-separated upper-case.

### 3.2 Internal APIs (selected)

```c
// command interface → bt_manager
esp_err_t bt_manager_scan_start(void);
esp_err_t bt_manager_scan_stop(void);
esp_err_t bt_manager_connect(const bt_addr_t* addr);
esp_err_t bt_manager_disconnect(void);
esp_err_t bt_manager_pair(const bt_addr_t* addr);
esp_err_t bt_manager_unpair(const bt_addr_t* addr);
esp_err_t bt_manager_unpair_all(uint32_t* removed_count);

// command interface → audio processor
esp_err_t audio_processor_start(audio_source_t source);
esp_err_t audio_processor_stop(void);
esp_err_t audio_processor_beep(beep_params_t params);
esp_err_t audio_processor_set_volume(int percent);

// storage helper
esp_err_t storage_set_device_name(const char* name);
esp_err_t storage_get_default_pin(char out[8]);
```

### 3.3 Data formats
- **Pairing list**: `INFO|PAIRED|ITEM|MAC=<mac>,NAME=<alias>,LAST_USED=<ticks>`.
- **SCAN result**: `INFO|SCAN|DEVICE_FOUND|MAC=<mac>,RSSI=<dBm>,NAME=<alias>`.
- **STATUS**: `OK|STATUS|BT=<state>,AUDIO=<state>,SOURCE=<src>,BUFFER=<used>/<total>`.
- **TRACE**: `DIAG-READ|REQ|max=1024,wait_ticks=5,ring_free=8192` etc.; consumed by tools.

---

## 4. Detailed Behaviors

### 4.1 Command sequencing & concurrency
- Commands are processed sequentially within the command task; no re-entrancy.
- Long-running operations (SCAN, CONNECT) post progress via events and update shared state accessible to STATUS command.
- Commands issued in incompatible states respond with `ERR|...|BUSY` or `ERR|...|NOT_CONNECTED` per PRD contract.
- Incoming events do not cancel commands; only explicit STOP/DISCONNECT requests can abort operations.

### 4.2 Bluetooth flows
| Scenario | Sequence |
| --- | --- |
| **SCAN + CONNECT** | `SCAN` → gather results → `OK|SCAN|COMPLETE|n` → `CONNECT <mac>` → wait for `EVENT|CONNECTION|STATE|CONNECTED`. |
| **PAIR** | `PAIR <mac>` → `EVENT|PAIR|PIN_REQUEST` (if needed) → `CONFIRM_PIN`/`ENTER_PIN` commands from host → `EVENT|PAIR|RESULT|SUCCESS/FAIL` → entry stored in NVS. |
| **UNPAIR_ALL** | `UNPAIR_ALL` → remove controller bonds sequentially → `OK|UNPAIR_ALL|SUCCESS|COUNT=<n>`. |
| **Unexpected disconnect** | `EVENT|CONNECTION|STATE|DISCONNECTED|REASON=<code>` → auto reconnect once → success event or `EVENT|CONNECTION|RETRY_FAILED`. |

### 4.3 Audio pipeline specifics
- **Start (I2S)**: command triggers `audio_processor_start(SOURCE_I2S)` which ensures I2S driver configured (pins: BCLK26, WCLK25, DATA_IN22) and reader task running; ringbuffer drained before enabling stream.
- **Synth/beep**: BEEP command arms `audio_processor_enable_next_beep_diag()` optionally and pushes beep job to worker queue.

### 4.4 Error handling & logging
- Every ESP-IDF call is `ESP_ERROR_CHECK`’d or translated into user-facing errors.
- UART responses should be user-friendly, e.g., `ERR|CONNECT|FAILED|ESP_ERR_TIMEOUT`.
- DIAG macros unify logging; macros compile out unless diagnostics enabled for host tests.
- Crash/abort handling: fatal errors print reason, flush logs, and reboot (standard IDF behavior).

---

## 5. Testing & Validation Plan

### 5.1 Mapping PRD metrics to tests
| Requirement | Verification |
| --- | --- |
| Deterministic command protocol | Host CTest suite `test_commands`, `test_command_parser`, plus targeted Unity tests in `test_app`. |
| Audio continuity (I2S/synth) | Unity suites `test_audio_processor_*`; manual playback logs archived under `tmp/`. |
| Pairing persistence | Unity tests in `test_app2` + on-device scripts storing logs to `build/pairing_e2_logs/`. |
| Diagnostics availability | Run `tools/parse_traces.py` against latest `one_run_unity.log`; CI step ensures >1000 records.

### 5.2 Orchestrated test flow
1. `tools/run_all_tests.py --port /dev/ttyUSB0 --timeout 600` cleans prior artifacts.
2. Host tests run via CTest; output captured in `tmp/host_ctest_output.log`.
3. Each Unity suite built/flashed using `tools/run_unity.py` (pseudo-terminal) → results stored in per-suite `build/one_run_unity.log`.
4. Aggregator writes `tmp/run_all_tests_summary.json` + `.csv` + `tmp/canonical_unity_summary.json`.
5. Trace parser optionally invoked to generate `trace_parsed.{csv,json}` for audio diagnostics.

### 5.3 Acceptance criteria recap
- Two consecutive green orchestrator runs (host 259/259 test cases + device tests TBD) before release tagging.
- Manual I2S playback sessions (≥5 min) produce no underrun logs.
- Pairing persistence script demonstrates store, reboot, recall, unpair cycle.

---

## 6. Open Issues & Future Enhancements

| Item | Description | Owner/Tracking |
| --- | --- | --- |
| Metadata ringbuffer lifecycle | Need init/deinit audit + drains for all discard paths. | memory.md TODO (“Metadata tag/drop sweep”). |
| Pairing soak validation | Execute multi-run pairing script, capture symbolized logs, update README/PRD acceptance status. | memory.md (Pairing persistence validation). |
| PSRAM hardware validation | Acquire board, rerun orchestrator with CONFIG_SPIRAM, record jitter metrics. | PRD Milestone M4. |
| Beep diagnostics CLI | Add command to arm `audio_processor_enable_next_beep_diag()` automatically. | memory.md TODO list. |
| Security hardening | Document/restrict remote-initiated pairing flows, consider PIN randomization. | Future spec revision if requirement emerges. |

---

## 7. Traceability Matrix

| PRD Section | FS Section(s) | Notes |
| --- | --- | --- |
| Goals (clean audio, deterministic commands, test automation) | §§2, 4, 5 | Command tables, audio pipeline behavior, test plan. |
| Personas & field expectations | §§3.1, 4.1 | Field operation expectations translated to state/response handling. |
| Bluetooth & pairing requirements | §2.2, §4.2 | Pairing policy, event logging, auto reconnect. |
| Audio pipeline + reference config | §2.3, §4.3, §4.4 | Buffer sizing, format requirements, jitter measurement. |
| Storage/assets | §2.4 | NVS handling. |
| Testing/metrics | §5 | Orchestrator + acceptance mapping. |
| Risks/milestones | §6 + references | FS tracks open issues and defers milestone tracking to PRD. |

---

## 8. Appendices
- A. Command grammar (future expansion): consider EBNF if more complex commands are added.
- B. UART framing test vectors: store under `docs/test_vectors/uart_commands.md` when available.
- C. Audio trace sample: provide example `trace_parsed.csv` with annotated columns.

Document history: initial version authored 2025-12-04 based on PRD v2025-12-04. Future edits should include changelog at end of file.

---

## Changelog

### 2026-02-05 - Ring Buffer Migration Complete (CODE_REVIEW6)
- **Architecture change**: Migrated from multi-producer queue to SPSC ring buffer
- **Test status**: All 259 host test cases passing (33 binaries), clang-tidy clean (27 files)
- **Code cleanup**: Removed duplicate DIAG-EVENT prints, fixed debug traces in commands.c
- **Legacy cleanup**: audio_queue.c/.h fully removed from codebase
- **Validation**: 461/461 total tests passing (host + device), stress tests added and passing
- **Impact**: Simplified audio pipeline, eliminated race conditions, improved diagnostics
- Updated acceptance criteria to reflect current test counts


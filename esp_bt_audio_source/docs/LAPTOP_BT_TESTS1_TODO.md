# LAPTOP_BT_TESTS1_TODO

Real-hardware Bluetooth integration tests that pair the ESP32 (A2DP source)
with the laptop's built-in Bluetooth adapter acting as an A2DP sink.  These
tests use the actual BlueZ stack and real over-the-air Bluetooth traffic — no
mocks.

**Laptop adapter confirmed ready:**
- Adapter MAC: `E8:FB:1C:25:E4:C2` (device name: `arisu`)
- UUID `0x110b` (Audio Sink) and `0x110a` (Audio Source) registered
- PulseAudio 15.99.1 running with `module-bluez5-discover` loaded
- PipeWire also active alongside PulseAudio

**Prerequisites already satisfied:**
- `idf.py` toolchain available at `$HOME/esp/esp-idf`
- ESP32 on `/dev/ttyUSB0`, baud 115200
- `conda` env `python310` has `pyserial` (reuse, do not create new envs)
- `bluetoothctl` available in PATH

---

## INFRA-1 — Directory structure and runner scaffold

**Status:** `[x]` Done  
**Priority:** High (everything else depends on this)

### Background
All new files live under `test/laptop_bt_tests/`.  The suite is driven by
`pytest` (same as existing `tools/tests/`) with a `laptop_bt` marker to let
callers run only this suite or skip it in CI.  A shell wrapper
`tools/run_laptop_bt_tests.sh` provides a single invocation point analogous
to `tools/run_unity.py`.

### Tasks

- [x] **INFRA-1a** Create `test/laptop_bt_tests/` directory with the following
  layout (all files listed here are stubs until the corresponding section
  tasks fill them in):
  ```
  test/laptop_bt_tests/
  ├── conftest.py          # session/function fixtures (INFRA-4)
  ├── laptop_bt.py         # BlueZ D-Bus controller (INFRA-2)
  ├── esp32_serial.py      # ESP32 serial driver (INFRA-3)
  ├── pytest.ini           # marker registration + default timeout
  ├── requirements.txt     # pydbus, pulsectl (pyserial already in env)
  ├── test_discovery.py    # DISC-1
  ├── test_pairing.py      # PAIR-1, PAIR-2
  ├── test_connection.py   # CONN-1
  ├── test_autoconnect.py  # CONN-2
  ├── test_streaming.py    # STREAM-1
  ├── test_control.py      # CTRL-1
  └── test_e2e.py          # E2E-1
  ```

- [x] **INFRA-1b** `pytest.ini` content:
  ```ini
  [pytest]
  markers =
      laptop_bt: requires laptop Bluetooth adapter and ESP32 on /dev/ttyUSB0
      slow: test takes > 30 s
  timeout = 120
  log_cli = true
  log_cli_level = INFO
  ```

- [x] **INFRA-1c** `requirements.txt`:
  ```
  pydbus>=0.6.0
  pulsectl>=22.0.0
  # pyserial already present in python310 conda env
  ```
  Install into the existing `python310` env:
  ```bash
  conda run -n python310 pip install pydbus pulsectl
  ```

- [x] **INFRA-1d** Create `tools/run_laptop_bt_tests.sh`:
  ```bash
  #!/usr/bin/env bash
  # Run all laptop-BT integration tests.
  # Usage: tools/run_laptop_bt_tests.sh [pytest extra args]
  set -euo pipefail
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  conda run -n python310 python -m pytest \
      "$ROOT/test/laptop_bt_tests/" \
      -m laptop_bt \
      -v \
      "$@"
  ```
  Make it executable: `chmod +x tools/run_laptop_bt_tests.sh`

- [x] **INFRA-1e** Verify the scaffold runs (no test files yet = 0 collected,
  no errors):
  ```bash
  conda run -n python310 python -m pytest test/laptop_bt_tests/ --collect-only
  ```

---

## INFRA-2 — `laptop_bt.py`: Laptop BlueZ controller

**Status:** `[x]` Done  
**Priority:** High

### Background
A thin Python wrapper around the BlueZ D-Bus interface (via `pydbus`).
Provides `LaptopBT` — a context-manager class that makes the adapter
discoverable/pairable, registers an auto-accept pairing agent, and tears
everything down on exit.  All other test modules import this class; tests
must not shell out to `bluetoothctl` directly.

### D-Bus interfaces used
- `org.bluez.Adapter1` — power, discoverable, pairable, scan control
- `org.bluez.Device1` — per-device trust/connect/disconnect/remove
- `org.bluez.AgentManager1` + custom `org.bluez.Agent1` implementation —
  accept or reject pairing requests programmatically

### Tasks

- [x] **INFRA-2a** Implement `LaptopBT.__init__(adapter_mac)`:
  - Accept `adapter_mac` defaulting to `"E8:FB:1C:25:E4:C2"`
  - Resolve D-Bus object path from adapter MAC
  - Store references to `Adapter1` proxy and `AgentManager1`

- [x] **INFRA-2b** Implement context manager (`__enter__` / `__exit__`):
  - `__enter__`: power on adapter, register pairing agent, set pairable=True,
    set discoverable=True with timeout 0 (persist)
  - `__exit__`: deregister agent, set discoverable=False, set pairable=False
  - Must restore prior state even if a test raises

- [x] **INFRA-2c** Implement `LaptopBT.register_agent(auto_accept=True)`:
  - Register a `SimpleAgent` on `org.bluez.Agent1` at a well-known D-Bus path
    (`/test/agent`)
  - `SimpleAgent.RequestConfirmation(device, passkey)`: if `auto_accept=True`
    return immediately (accept); if `False` raise `bluez.Error.Rejected`
  - `SimpleAgent.AuthorizeService(device, uuid)`: always authorize A2DP
    (UUID `0000110b-0000-1000-8000-00805f9b34fb`)
  - `SimpleAgent.Cancel()`: no-op
  - Register as capability `"NoInputNoOutput"` with `AgentManager1.RegisterAgent`
  - Call `AgentManager1.RequestDefaultAgent` to make it the system default

- [x] **INFRA-2d** Implement `LaptopBT.set_discoverable(on: bool, timeout_s: int = 0)`:
  - Sets `Discoverable` and `DiscoverableTimeout` on `Adapter1`
  - Returns old value so callers can restore if needed

- [x] **INFRA-2e** Implement `LaptopBT.get_paired_devices() -> list[dict]`:
  - Enumerate `org.freedesktop.DBus.ObjectManager` under `/org/bluez`
  - Return list of `{"mac": ..., "name": ..., "trusted": ..., "connected": ...}`
    for devices that have `Paired=True`

- [x] **INFRA-2f** Implement `LaptopBT.remove_device(mac: str)`:
  - Call `Adapter1.RemoveDevice(device_path)` for the given MAC
  - Used in test teardown to unpair the ESP32 from the laptop side

- [x] **INFRA-2g** Implement `LaptopBT.is_connected(mac: str) -> bool`:
  - Returns `Device1.Connected` for the given MAC, or `False` if device not
    found in object manager

- [x] **INFRA-2h** Implement `LaptopBT.wait_for_connect(mac: str, timeout_s: float = 30.0) -> bool`:
  - Polls `is_connected(mac)` every 0.5 s until True or timeout
  - Returns True on success, False on timeout

- [x] **INFRA-2i** Implement `LaptopBT.adapter_mac` property returning the
  resolved adapter MAC address (used by tests to feed to ESP32 CONNECT command)

- [x] **INFRA-2j** Smoke-test: `conda run -n python310 python -c "from laptop_bt import LaptopBT; print(LaptopBT().adapter_mac)"`
  should print `E8:FB:1C:25:E4:C2` without raising.

---

## INFRA-3 — `esp32_serial.py`: ESP32 serial protocol driver

**Status:** `[x]` Done  
**Priority:** High

### Background
A synchronous serial driver that wraps the ESP32 command protocol
(`STATUS OK|CMD|RESULT|DATA`) used throughout the firmware.  The existing
`tools/host_pairing_driver.py` has ad-hoc serial logic; this module
generalises it into a reusable `ESP32Serial` class so every test file uses
the same transport layer.

### Protocol reference (from `command_interface.h` and `cmd_handlers_bt.c`)
```
TX (host → ESP32): <COMMAND> [PARAM [PARAM …]]\r\n
RX (ESP32 → host): <STATUS>|<COMMAND>|<RESULT>[|<DATA>]\n
   STATUS ∈ {OK, ERR, INFO, EVENT}
```

Key command→response pairs needed by tests:
| Command | Expected response prefix |
|---|---|
| `STATUS` | `OK\|STATUS\|CURRENT\|…` |
| `SCAN` | `OK\|SCAN\|STARTED` |
| `CONNECT <mac>` | `OK\|CONNECT\|INITIATED` |
| `CONNECT_NAME <name>` | `OK\|CONNECT_NAME\|INITIATED` |
| `DISCONNECT` | `OK\|DISCONNECT\|DONE` |
| `PAIR <mac>` | `OK\|PAIR\|INITIATED` |
| `PAIRED` | `OK\|PAIRED\|COUNT\|…` |
| `UNPAIR <mac>` | `OK\|UNPAIR\|REMOVED` |
| `UNPAIR_ALL` | `OK\|UNPAIR_ALL\|SUCCESS` |
| `CONFIRM_PIN 1` | `OK\|CONFIRM_PIN\|ACCEPTED\|…` |
| `VOLUME <0-100>` | `OK\|VOLUME\|SET\|…` |
| `MUTE` | `OK\|MUTE\|SET` |
| `UNMUTE` | `OK\|UNMUTE\|CLEARED` |
| `START` | `OK\|START\|STARTED` |
| `STOP` | `OK\|STOP\|STOPPED` |
| `LAST_MAC get` | `OK\|LAST_MAC\|<mac or NONE>` |
| `LAST_MAC clear` | `OK\|LAST_MAC\|CLEARED` |
| `RESET` | `OK\|RESET\|REBOOTING` |
| `AUDIO_STATUS` | `OK\|AUDIO_STATUS\|CURRENT\|…` |

### Tasks

- [x] **INFRA-3a** Implement `ESP32Serial.__init__(port, baud=115200, timeout=2.0)`:
  - Opens serial port; drains any pending input
  - Stores port/baud for log output

- [x] **INFRA-3b** Implement context manager (`__enter__` / `__exit__`):
  - `__exit__` closes the serial port unconditionally

- [x] **INFRA-3c** Implement `send(command: str)`:
  - Writes `command + "\r\n"` encoded as UTF-8
  - Logs `TX: <command>` at DEBUG level

- [x] **INFRA-3d** Implement `readline(timeout_s: float = None) -> str`:
  - Reads until `\n`, strips ANSI escapes and trailing whitespace
  - Returns empty string on timeout; never raises on partial reads

- [x] **INFRA-3e** Implement `send_and_expect(command: str, prefix: str, timeout_s: float = 5.0) -> str`:
  - Sends command, reads lines until a line starts with `prefix` or timeout
  - Returns the matching line on success
  - Raises `AssertionError` with full log if no match within timeout
  - Logs all RX lines at DEBUG level

- [x] **INFRA-3f** Implement `wait_for_line(prefix: str, timeout_s: float) -> str`:
  - Reads lines (no TX) until one starts with `prefix`
  - Used to wait for asynchronous events (e.g. `EVENT|PAIR|CONFIRM`)
  - Returns the matching line; raises `TimeoutError` on expiry

- [x] **INFRA-3g** Implement `drain(seconds: float = 0.2)`:
  - Reads and discards all available input for `seconds`; used in test
    setup to flush boot log lines

- [x] **INFRA-3h** Implement `wait_for_boot(timeout_s: float = 15.0)`:
  - Reads until the line `"esp-idf"` or `"I2S"` or `"CMD>"` appears
  - Used after a `RESET` to confirm the device has rebooted

- [x] **INFRA-3i** Implement `parse_response(line: str) -> dict`:
  - Splits `STATUS|COMMAND|RESULT[|DATA]` into a dict
  - `{"status": …, "command": …, "result": …, "data": …}`
  - Returns `{"raw": line}` for lines that don't match the protocol

---

## INFRA-4 — `conftest.py`: pytest fixtures and session setup

**Status:** `[x]` Done  
**Priority:** High

### Background
Shared pytest fixtures that set up and tear down the laptop BT adapter and
the ESP32 serial connection around each test.  Session-scoped fixtures handle
expensive one-time setup; function-scoped fixtures guarantee a clean slate
for every test.

### Tasks

- [x] **INFRA-4a** Session-scoped fixture `laptop_bt_adapter`:
  - Constructs `LaptopBT` and enters the context once for the entire test
    session
  - Yields the `LaptopBT` instance; on teardown exits context (restores
    discoverable=False, deregisters agent)

- [x] **INFRA-4b** Session-scoped fixture `esp32`:
  - Opens `ESP32Serial("/dev/ttyUSB0")` once; yields it
  - Calls `drain()` at start to flush any residual boot output
  - Closes on teardown

- [x] **INFRA-4c** Function-scoped fixture `clean_pair_state(esp32, laptop_bt_adapter)`:
  - Before each test: sends `UNPAIR_ALL` to ESP32, then calls
    `laptop_bt_adapter.remove_device(esp32_mac)` if the ESP32 is already
    paired from a previous run
  - After each test: same cleanup so failed tests don't pollute the next
  - `esp32_mac` is read from the ESP32 at session start via a helper that
    scans for the device or reads from config

- [x] **INFRA-4d** Module-level `pytestmark` helper: each test file applies
  `@pytest.mark.laptop_bt` so `pytest -m "not laptop_bt"` skips the whole
  suite in environments without the hardware.

- [x] **INFRA-4e** `conftest.py` top of file: skip the entire session with a
  clear message if `/dev/ttyUSB0` is not present or if `bluetoothctl show`
  fails (guard against CI environments with no hardware):
  ```python
  def pytest_configure(config):
      if not Path("/dev/ttyUSB0").exists():
          pytest.skip("No ESP32 on /dev/ttyUSB0 — skipping laptop_bt suite",
                      allow_module_level=True)
  ```

- [x] **INFRA-4f** Add `ESP32_MAC` constant to `conftest.py` holding the
  hardcoded ESP32 adapter MAC (`A0:B7:65:2B:E6:5C`, read from flash log
  `MAC: a0:b7:65:2b:e6:5c`).  Tests use this to tell the laptop which
  device to trust/remove.

---

## DISC-1 — Discovery tests

**Status:** `[x]` Done  
**Priority:** High  
**File:** `test/laptop_bt_tests/test_discovery.py`

### Background
Verify that the ESP32 `SCAN` command discovers the laptop when the laptop is
discoverable, and that the scan result contains the expected MAC and name.

### Tasks

- [x] **DISC-1a** `test_laptop_discoverable_appears_in_scan`:
  - Set laptop discoverable via `laptop_bt_adapter.set_discoverable(True)`
  - Send `SCAN` to ESP32; wait up to 15 s for `INFO|SCAN|RESULT|` lines
  - Assert at least one result line contains `E8:FB:1C:25:E4:C2` (case-
    insensitive)
  - Teardown: `set_discoverable(False)`

- [x] **DISC-1b** `test_scan_result_contains_device_name`:
  - Same scan flow as DISC-1a
  - Assert the result line for the laptop MAC also contains `"arisu"` (the
    adapter name configured in BlueZ)

- [x] **DISC-1c** `test_scan_not_discoverable_does_not_find_laptop`:
  - Ensure laptop is NOT discoverable (`set_discoverable(False)`)
  - Run SCAN; wait the full scan window (12 s)
  - Assert no `INFO|SCAN|RESULT|` line contains the laptop MAC
  - This validates the test harness is not getting false positives from NVS
    cache

- [x] **DISC-1d** `test_scan_completes_within_timeout`:
  - Record wall-clock time around SCAN command
  - Assert the `OK|SCAN|` terminal response arrives within 15 s
  - Guards against scan getting stuck with no completion event

---

## PAIR-1 — Initial real pairing tests

**Status:** `[x]` Done  
**Priority:** High  
**File:** `test/laptop_bt_tests/test_pairing.py`

### Background
Drive a real over-the-air pairing handshake between the ESP32 and the
laptop.  The laptop's `SimpleAgent` (INFRA-2c) auto-accepts the
`RequestConfirmation` callback; the ESP32 is driven via the `PAIR` command
and responds to `EVENT|PAIR|CONFIRM` with `CONFIRM_PIN 1`.

### Tasks

- [x] **PAIR-1a** `test_pair_initiated_from_esp32_succeeds`
- [x] **PAIR-1b** `test_paired_command_lists_laptop_after_pairing`
- [x] **PAIR-1c** `test_pair_result_persists_in_status`
- [x] **PAIR-1d** `test_pair_by_name_succeeds` (`@pytest.mark.slow`)

### Implementation notes
- Full SSP handshake with auto-accept agent; `CONFIRM_PIN 1` sent by test
- `cmd_handle_paired` fixed to treat `ESP_ERR_NOT_FOUND` as count=0 (NVS
  count key absent after `UNPAIR_ALL` is valid empty state)

---

## PAIR-2 — Pairing edge cases

**Status:** `[x]` Done  
**Priority:** Medium  
**File:** `test/laptop_bt_tests/test_pairing.py` (same file, second class)

### Tasks

- [x] **PAIR-2a** `test_pair_rejection_returns_failed`
- [x] **PAIR-2b** `test_unpair_removes_device_from_esp32_list`
- [x] **PAIR-2c** `test_unpair_all_clears_list`
- [x] **PAIR-2d** `test_second_pair_to_same_device_succeeds`

### Implementation notes
- Rejection test uses dead MAC `DE:AD:BE:EF:CA:FE` (page timeout approach):
  BT page times out → A2DP DISCONNECTED fires →
  `bt_pairing_handle_connection_failed()` emits `EVENT|PAIR|FAILED`
- New firmware function `bt_pairing_handle_connection_failed()` in
  `bt_pairing_store.c`; called from `bt_events_a2dp.c` DISCONNECTED handler
- `pairing_in_progress` flag in pending struct prevents duplicate FAILED
  events when late AUTH_CMPL arrives after page-timeout FAILED already sent
- `conftest.py` `clean_pair_state` sleep raised 2 s → 5 s; BlueZ needs more
  time after real pairing to accept `set_discoverable(True)` on next test
- `laptop_bt.py` `set_discoverable` retries raised 5 → 20 (1.5 s each) to
  handle `Busy` errors that follow rapid pairing/unpair cycles

---

## CONN-1 — Connection management tests

**Status:** `[x]` Done  
**Priority:** High  
**File:** `test/laptop_bt_tests/test_connection.py`

### Background
Tests that connect and disconnect a paired ESP32 to the laptop and verify
the connection state on both sides using `STATUS` and `pactl`.

### Tasks

- [x] **CONN-1a** `test_connect_to_paired_laptop_succeeds`:
  - Precondition: paired (module-scoped fixture)
  - Send `CONNECT E8:FB:1C:25:E4:C2`; assert `OK|CONNECT|INITIATED`
  - Wait for `laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=20)`
  - Send `STATUS`; parse DATA; assert `BT_STATE` contains `CONNECTED`

- [x] **CONN-1b** `test_disconnect_returns_to_disconnected_state`:
  - Precondition: connected (CONN-1a fixture)
  - Send `DISCONNECT`; assert `OK|DISCONNECT|DONE`
  - Assert `laptop_bt_adapter.is_connected(ESP32_MAC)` is False within 5 s
  - Send `STATUS`; assert `BT_STATE` no longer contains `CONNECTED`

- [x] **CONN-1c** `test_reconnect_after_explicit_disconnect`:
  - Connect (CONN-1a), disconnect (CONN-1b), then connect again
  - Assert second connection succeeds without re-pairing
  - Guards against connection state machine getting stuck after disconnect

- [x] **CONN-1d** `test_connect_by_name_resolves_to_correct_device`:
  - Send `CONNECT_NAME arisu` while unpaired/disconnected
  - Assert the connection establishes to `E8:FB:1C:25:E4:C2` specifically
    (not some other device that happens to be discoverable)
  - Validate via `laptop_bt_adapter.is_connected(ESP32_MAC)`

- [x] **CONN-1e** `test_status_shows_paired_count_when_connected`:
  - While connected, send `STATUS`
  - Assert `PAIRED_COUNT` is non-zero (confirms bonded device tracked in NVS)

---

## CONN-2 — Auto-reconnect on boot tests

**Status:** `[x]` Done  
**Priority:** Medium  
**File:** `test/laptop_bt_tests/test_autoconnect.py`

### Background
Tests for the auto-reconnect-on-boot feature (implemented in
`components/bt_manager/` and tracked in `docs/AUTOSTART1_TODO.md`).
The `LAST_MAC` command reads/clears the persisted MAC; `RESET` reboots the
device.

### Tasks

- [x] **CONN-2a** `test_last_mac_saved_after_connection`:
  - Pair and connect to laptop (fixture)
  - Send `LAST_MAC get`; assert response contains `E8:FB:1C:25:E4:C2`

- [x] **CONN-2b** `test_last_mac_none_when_never_connected`:
  - `LAST_MAC clear` first; then `LAST_MAC get`
  - Assert `OK|LAST_MAC|NONE`

- [x] **CONN-2c** `test_autostart_reconnects_on_reboot`:
  - Precondition: connected (LAST_MAC set via A2DP CONNECTED event, not PAIR)
  - Send `RESET`; call `esp32.wait_for_boot(timeout_s=30)`
  - Wait up to 30 s for `laptop_bt_adapter.wait_for_connect(ESP32_MAC)`
  - Assert connection is re-established without any explicit `CONNECT` command
  - Marks `@pytest.mark.slow`

- [x] **CONN-2d** `test_no_autostart_when_last_mac_cleared`:
  - `LAST_MAC clear`; then `RESET`; `wait_for_boot()`
  - Wait 20 s; assert `laptop_bt_adapter.is_connected(ESP32_MAC)` is False
  - Guards against spurious reconnect when there is no saved MAC

- [x] **CONN-2e** `test_autostart_disabled_prevents_reconnect_on_reboot`:
  - `AUDIO_AUTOSTART off`; LAST_MAC set via connected_state; then `RESET`
  - After boot, wait 20 s; assert NOT connected
  - `AUDIO_AUTOSTART on` restored in test finally block

---

## STREAM-1 — A2DP streaming verification

**Status:** `[x]` Done  
**Priority:** High  
**File:** `test/laptop_bt_tests/test_streaming.py`

### Background
After an A2DP connection is established, the laptop's PulseAudio/BlueZ stack
should create a new sink corresponding to the ESP32.  `pulsectl` is used to
enumerate sinks; `pactl` can be used as a fallback.  Audio data is verified
by checking ring-buffer occupancy via `AUDIO_STATUS`, not by decoding PCM
frames.

### Tasks

- [x] **STREAM-1a** `test_pulseaudio_bt_sink_appears_after_connect`:
  - After A2DP connect (CONN-1a fixture), poll `pulsectl` for a sink whose
    description or name contains `"bluez"` or the ESP32 MAC
  - Assert such a sink exists within 10 s of connection
  - This validates the A2DP codec negotiation completed at the OS level

- [x] **STREAM-1b** `test_start_streaming_command_succeeds`:
  - While connected, send `START`
  - Assert `OK|START|STARTED` within 5 s

- [x] **STREAM-1c** `test_audio_status_ring_buffer_fills_after_start`:
  - After `START`, wait 2 s, then send `AUDIO_STATUS`
  - Parse `OK|AUDIO_STATUS|CURRENT|…` DATA field
  - Assert `RING_USED` value > 0 (confirms audio data is flowing into
    the ring buffer from I2S/synthesiser)

- [x] **STREAM-1d** `test_stop_streaming_command_succeeds`:
  - After `START`, send `STOP`; assert `OK|STOP|STOPPED`
  - Send `AUDIO_STATUS`; assert `RING_USED=0` or `SOURCE=IDLE`

- [x] **STREAM-1e** `test_streaming_survives_volume_change`:
  - Start streaming; send `VOLUME 50`; assert `OK|VOLUME|SET|50`
  - Wait 1 s; send `AUDIO_STATUS`; assert `RING_USED` still > 0
  - (Regression guard: volume path must not interrupt streaming)

- [x] **STREAM-1f** `test_mute_does_not_stop_ring_buffer_fill`:
  - Start streaming; send `MUTE`; assert `OK|MUTE|SET`
  - Send `AUDIO_STATUS`; assert `RING_USED` still > 0 (mute is
    post-ring-buffer; data should still flow)
  - Send `UNMUTE`; assert `OK|UNMUTE|CLEARED`

---

## CTRL-1 — Control commands during live connection

**Status:** `[x]` Done  
**Priority:** Medium  
**File:** `test/laptop_bt_tests/test_control.py`

### Background
Verify that all ESP32 control commands work correctly when a real A2DP
connection is active.  These complement the mock-mode unit tests with real
hardware state.

### Tasks

- [x] **CTRL-1a** `test_status_shows_connected_bt_state`:
  - While connected, send `STATUS`; parse DATA
  - Assert DATA contains `BT_STATE=CONNECTED` or similar connected indicator

- [x] **CTRL-1b** `test_volume_set_persists_in_status`:
  - Send `VOLUME 42`; assert `OK|VOLUME|SET|42`
  - Send `AUDIO_STATUS`; assert DATA reflects the new volume value

- [x] **CTRL-1c** `test_volume_out_of_range_rejected`:
  - Send `VOLUME 101`; assert `ERR|VOLUME|OUT_OF_RANGE`
  - Send `VOLUME -1`; assert `ERR|VOLUME|…`

- [x] **CTRL-1d** `test_mute_unmute_cycle_while_streaming`:
  - Start stream; send `MUTE`; assert `OK|MUTE|SET`
  - Send `UNMUTE`; assert `OK|UNMUTE|CLEARED`
  - Assert still streaming after unmute (AUDIO_STATUS RING_USED > 0)

- [x] **CTRL-1e** `test_mem_command_returns_stats`:
  - Send `MEM`; assert `OK|MEM|STATS|…`
  - Parse DRAM value; assert > 0 (confirms heap introspection works)

- [x] **CTRL-1f** `test_audio_autostart_get_reflects_current_setting`:
  - Send `AUDIO_AUTOSTART get`; assert `OK|AUDIO_AUTOSTART|STATUS|…`
  - Captures current value; does not change it (read-only probe)

- [x] **CTRL-1g** `test_version_command_returns_non_empty`:
  - Send `VERSION`; assert `OK|VERSION|…` with a non-empty result field
  - Basic smoke-test that the firmware self-identifies

---

## E2E-1 — Full end-to-end scenario

**Status:** `[x]` Done  
**Priority:** High  
**File:** `test/laptop_bt_tests/test_e2e.py`

### Background
A single long-running test that exercises the complete lifecycle from a clean
slate: discover → pair → connect → stream → disconnect → verify cleanup.
This is the closest thing to a real usage session.

### Tasks

- [x] **E2E-1a** `test_full_discovery_pair_connect_stream_disconnect_lifecycle`:
  - **Setup:** `UNPAIR_ALL`; `laptop_bt_adapter.remove_device(ESP32_MAC)`
  - **Step 1 — Discovery:** Make laptop discoverable; send `SCAN`; assert
    laptop MAC found
  - **Step 2 — Pairing:** Send `PAIR E8:FB:1C:25:E4:C2`; handle
    `EVENT|PAIR|CONFIRM`; respond `CONFIRM_PIN 1`; assert `EVENT|PAIR|SUCCESS`
  - **Step 3 — Connection:** Send `CONNECT E8:FB:1C:25:E4:C2`; wait for
    `laptop_bt_adapter.wait_for_connect(ESP32_MAC)
  - **Step 4 — Streaming:** Send `START`; assert `OK|START|STARTED`; wait 2 s;
    assert `AUDIO_STATUS` RING_USED > 0; assert PulseAudio BT sink present
  - **Step 5 — Control:** `VOLUME 60`; `MUTE`; `UNMUTE`; assert all OK
  - **Step 6 — Stop:** Send `STOP`; assert `OK|STOP|STOPPED`
  - **Step 7 — Disconnect:** Send `DISCONNECT`; assert `OK|DISCONNECT|DONE`;
    assert `laptop_bt_adapter.is_connected(ESP32_MAC)` False
  - Marks `@pytest.mark.slow`

- [x] **E2E-1b** `test_reconnect_after_simulated_range_loss`:
  - Pair and connect (E2E-1a partial fixture up to Step 3)
  - Simulate range loss: `laptop_bt_adapter.remove_device(ESP32_MAC)` (forces
    disconnection from the laptop side)
  - Assert ESP32 `STATUS` transitions away from CONNECTED within 10 s
  - Re-add trust on laptop; send `CONNECT E8:FB:1C:25:E4:C2` from ESP32
  - Assert connection re-established without re-pairing
  - Marks `@pytest.mark.slow`

- [x] **E2E-1c** `test_boot_reconnect_full_sequence`:
  - Pair, connect, start stream, stop stream, disconnect
  - `LAST_MAC get` → confirm laptop MAC persisted
  - `RESET`; `wait_for_boot()`
  - Assert laptop auto-connects within 30 s
  - Assert `START` works immediately after boot reconnect
  - Marks `@pytest.mark.slow`
  - Fix: `wait_for_disconnect` required after DISCONNECT before RESET; without
    it the firmware's auto-reconnect attempt races with RESET and leaves the
    laptop's AVDTP stack in a half-open state that prevents A2DP on boot.

---

## RUN-1 — CI integration note (documentation task)

**Status:** `[x]` Done  
**Priority:** Low

### Tasks

- [x] **RUN-1a** Add a section to `README.md` under "Running tests" describing
  how to run the laptop BT suite:
  ```bash
  conda run -n python310 tools/run_laptop_bt_tests.sh
  # skip in CI (no hardware):
  conda run -n python310 python -m pytest test/laptop_bt_tests/ -m "not laptop_bt"
  ```

- [x] **RUN-1b** Add `test/laptop_bt_tests/build/` to `.gitignore`.

- [x] **RUN-1c** Document in `README.md` that these tests require:
  - Physical ESP32 on `/dev/ttyUSB0`
  - Laptop Bluetooth powered on and adapter MAC `E8:FB:1C:25:E4:C2` active
  - `pydbus` and `pulsectl` installed in `python310` conda env
  - The ESP32 firmware must be the production build (not a Unity test flash)

---

## Implementation order

```
INFRA-1 (scaffold) → INFRA-2 (laptop_bt.py) → INFRA-3 (esp32_serial.py)
    → INFRA-4 (conftest.py)
    → DISC-1 (discovery — no pairing needed, lowest coupling)
    → PAIR-1 (pairing — real handshake)
    → PAIR-2 (pairing edge cases)
    → CONN-1 (connection — depends on pairing)
    → CONN-2 (auto-reconnect — depends on connection + RESET)
    → STREAM-1 (streaming — depends on connection)
    → CTRL-1 (control — depends on connection)
    → E2E-1 (full sequence — depends on all of the above)
    → RUN-1 (docs — last)
```

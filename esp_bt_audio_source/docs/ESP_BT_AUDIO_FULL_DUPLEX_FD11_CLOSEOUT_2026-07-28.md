# ESP32 Bluetooth Audio Full-Duplex — FD-11 Closeout

**Branch:** `feature/esp-bt-audio-duplex`  
**Draft PR:** #2  
**Validated implementation head:** `ec7d10a2f4287009bb07857caa608eb87f3076b6`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Hardware flashing:** Not performed

## 1. Phase result

FD-11 is software-complete for the command and diagnostic contract defined in `ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`.

The command interface now exposes:

```text
HFP STATUS
HFP CONNECT <mac>
HFP DISCONNECT
HFP AUDIO START
HFP AUDIO STOP
HFP MODE DISABLED
HFP MODE A2DP_MIC
HFP MODE HFP_FULL
HFP MODE AUTO
HFP CODEC
HFP STATS
HFP RESETSTATS
```

All Bluetooth operations are routed through the public HFP manager facade. The command component includes no direct ESP HFP Audio Gateway API and cannot bypass the FD-07/FD-10 lifecycle, peer, generation, timeout, rollback, or I2S rules.

## 2. Public manager facade

FD-11 adds one command-facing manager contract in `components/bt_manager/include/bt_hfp_manager.h` and the authoritative implementation selected by `components/bt_manager/CMakeLists.txt`.

The facade exposes:

- one consistent HFP/duplex status snapshot;
- configured duplex-mode get/set;
- validated HFP SLC connect and disconnect requests;
- synchronous FD-10 audio start and stop;
- aggregated HFP, duplex, callback, ring, and I2S statistics;
- explicit non-destructive statistics reset.

The command layer does not inspect private manager state and does not include private or ESP-IDF HFP headers.

## 3. Status consistency

`HFP STATUS` obtains exactly one `bt_hfp_manager_status_t` snapshot. Generation-bound fields are copied from one authoritative duplex-state snapshot under the existing manager/state synchronization contract.

The response includes:

- session generation and peer;
- configured, requested, and effective modes;
- A2DP profile and audio state;
- HFP profile and audio state;
- negotiated codec;
- HFP I2S state;
- health, last error, and sanitized diagnostic text.

Protocol separators, control characters, and line breaks in diagnostic text are replaced before transmission. Status never assembles fields from multiple manager calls or multiple generations.

## 4. Stable command wire values

The command protocol has dedicated compact wire mappings rather than exposing C enum identifiers.

Examples:

- `BT_HFP_PROFILE_SLC_CONNECTED` → `SLC_CONNECTED`;
- `BT_HFP_AUDIO_CONNECTED_CVSD` → `CONNECTED_CVSD`;
- `BT_HFP_CODEC_CVSD` → `CVSD`;
- `BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC` → `A2DP_MIC`.

The core enum-to-string helpers remain unchanged for internal diagnostics. This avoids accidentally changing an existing internal string contract while giving the UART command protocol stable, concise values.

Mode input accepts only the four exact semantic tokens `DISABLED`, `A2DP_MIC`, `HFP_FULL`, and `AUTO`, with case-insensitive comparison. Prefixes, abbreviations, and unknown strings are rejected explicitly.

## 5. Accepted versus completed operations

HFP SLC connect and disconnect are asynchronous profile operations.

Successful request dispatch returns:

- `CONNECT_ACCEPTED` or `DISCONNECT_ACCEPTED`;
- `COMPLETION=HFP_PROFILE_EVENT`.

The command does not claim that the profile is connected or disconnected merely because the lower request was accepted.

If the single pre-operation status snapshot already proves the requested terminal state, the command returns `ALREADY_CONNECTED` or `ALREADY_DISCONNECTED` and does not issue a duplicate lower request.

FD-10 audio start and stop are bounded synchronous operations. `AUDIO_STARTED` and `AUDIO_STOPPED` are emitted only after the public manager API returns `ESP_OK`, which means FD-10 reached its confirmed terminal result. If the operation succeeds but the follow-up display snapshot fails, the command preserves the successful operation result and emits `AUDIO_STARTED_STATUS_UNAVAILABLE` or `AUDIO_STOPPED_STATUS_UNAVAILABLE` with the exact status error. It does not retract a completed operation or silently hide the missing diagnostic snapshot.

## 6. Exact errors and input validation

The manager validates HFP connect MAC addresses with the repository's canonical MAC parser before any lower request. A malformed address returns `ESP_ERR_INVALID_ARG` exactly.

Manager and lower-layer errors are propagated without conversion to generic success or ambiguous text. Command errors include the stable `esp_err_to_name()` value and sanitized detail where applicable.

Invalid subcommands, invalid argument counts, unknown audio actions, and unknown mode values produce explicit `ERR|HFP|...` responses.

## 7. Statistics contract

`HFP STATS` aggregates:

- authoritative duplex counters;
- FD-07 SLC operation counters;
- FD-10 audio-control counters;
- FD-09 incoming callback and drop counters;
- FD-08 I2S writer, push, loss, silence, degradation, quarantine, and ring counters;
- current ring occupancy;
- lifetime peak ring occupancy;
- callback last duration and lifetime maximum duration.

The response is split into bounded `INFO|HFP|STATS_*|...` records. Every format operation is checked. A record that cannot fit produces an explicit `STATS_LINE_TOO_LONG` error; it is never silently truncated.

The shared command response formatter also fails closed with `RESPONSE_TOO_LONG` and writes only the bytes actually present in its bounded buffer. This removes the prior risk of using `snprintf()`'s required length as a UART write length after truncation.

## 8. Safe non-destructive reset

`HFP RESETSTATS` uses a baseline snapshot. It does not rewrite live atomic counters or mutate callback-owned state.

Reset is rejected while any of the following are live:

- pending SLC operation;
- pending or active FD-10 audio-control operation;
- incoming callback acceptance;
- callback currently in flight;
- authoritative HFP audio not disconnected;
- authoritative I2S not stopped;
- local I2S output not uninitialized or stopped.

After a successful reset, delta counters begin at zero while current gauges and explicitly named lifetime maxima retain their real current/lifetime meaning. Counter or lifetime-maximum regression is reported as `ESP_ERR_INVALID_STATE` instead of being clamped into plausible-looking data.

## 9. Fail-closed host integration

Generic host command targets do not link the production HFP manager facade. Their explicit test backend returns `CMD_ERROR_NOT_INITIALIZED`; it cannot make an unconfigured HFP operation appear successful.

The focused FD-11 command suite links:

- the real command parser and dispatcher;
- the real FD-11 HFP handler;
- an injectable public-manager stub;
- explicit UART and command dependency fixtures.

The focused manager suite links the real manager facade, duplex state, mode transition code, and explicit lower-layer snapshot/request fixtures.

No production behavior is replaced by a weak permissive success fallback.

## 10. Tests

### 10.1 Manager facade suite

The ASan/UBSan manager suite contains 11 cases covering:

1. configured mode and one authoritative status snapshot;
2. atomic mode update and rejection while audio is live;
3. configured mode carried into a newly created command session;
4. non-active peer rejection before a lower request;
5. malformed MAC rejection with the exact error;
6. exact propagation of lower SLC/audio errors;
7. non-destructive statistics baseline reset;
8. reset rejection for pending SLC/audio, callbacks, and local I2S;
9. reset rejection for authoritative streaming state;
10. detection of regressed lifetime source counters;
11. detection of regressed lifetime maxima.

### 10.2 Command suite

The ASan/UBSan command suite contains 17 cases covering:

1. every required valid command form;
2. one-snapshot status and diagnostic sanitization;
3. connect acceptance without false completion;
4. explicit already-connected behavior;
5. exact connect backend errors;
6. disconnect acceptance versus idempotence;
7. confirmed audio start/stop wording and failures;
8. exact four-mode parsing and unknown-mode rejection;
9. authoritative generation/codec output;
10. maximum-value statistics without truncation;
11. exact reset success/failure reporting;
12. invalid command forms;
13. response overflow fail-closed behavior without buffer over-read;
14. malformed MAC exact error preservation;
15. pre-connect status failure not blocking an accepted request;
16. completed audio result not being retracted by display-status failure;
17. mode success not depending on an unnecessary follow-up snapshot.

## 11. Validation

Validation for implementation head `ec7d10a2f4287009bb07857caa608eb87f3076b6`:

- Strict host CI run **1007**: PASS.
  - generic host CMake build;
  - all prior FD-03 through FD-10 focused ASan/UBSan suites;
  - FD-11 manager facade 11-case ASan/UBSan suite;
  - FD-11 command 17-case ASan/UBSan suite;
  - changed-Python lint gate;
  - Python unit tests;
  - complete CTest: **74/74 targets passed**, 0 failed.
- ESP-IDF v5.5.1 device-build run **899**: PASS.
- Application image: **1,014,864 bytes**.
- Factory app partition: **1,769,472 bytes**.
- Factory app-partition headroom: **754,608 bytes**.
- Image delta from FD-10: **+26,864 bytes**.
- `.dram0.data`: **21,856 bytes**.
- `.dram0.bss`: **52,744 bytes**.
- Static `.dram0.data + .dram0.bss`: **74,600 bytes**.
- Static DRAM delta from FD-10: **+728 bytes**.
- Superseded HFP manager and command-handler implementations were removed; one implementation of each remains selected by CMake.
- No hardware was flashed.

The factory partition retains substantially more than the project's 256 KiB minimum headroom gate.

## 12. Explicit boundaries and residual risk

Software validation does not prove:

- command behavior over the physical UARTs on the WROOM32;
- real HFP profile completion events after `CONNECT_ACCEPTED` or `DISCONNECT_ACCEPTED`;
- real SCO/eSCO audio start/stop from the command surface;
- real earbud codec negotiation or CVSD microphone data;
- GPIO32/33/27 I2S timing or receiver compatibility;
- coexistence with I2S1 RX, UART2 traffic, A2DP playback, or prolonged soak.

The configured mode and statistics baseline are RAM-resident process-lifetime state. Normal reboot starts from static defaults. A same-process `bt_manager_deinit()`/`bt_manager_init()` diagnostic-state reset is not claimed by FD-11 and should be explicitly covered when the broader lifecycle/recovery matrix is hardened rather than inferred from build success.

The Python test orchestrator contains a pre-existing line-length/unused-catch lint backlog. FD-11 preserves the user's real `failed_count` bug fix and records a narrow `E501,F841` per-file exception; all other flake8 rules remain active for that file and the rest of the repository. This is tooling debt, not a runtime fallback.

## 13. Next phase

FD-12 adds the stable HFP event contract:

```text
EVENT|HFP|PROFILE|<state>|<mac>|<session>
EVENT|HFP|AUDIO|<state>|<codec>|<session>
EVENT|HFP|MODE|<old>|<new>|<reason>|<session>
EVENT|HFP|I2S|<state>|<error>|<session>
EVENT|HFP|HEALTH|<severity>|<reason>|<count>|<session>
```

FD-12 must emit only meaningful transitions or threshold crossings, sanitize every string field, include stable reason strings and session generation, avoid duplicates, and preserve the existing multi-UART event broadcast rules.

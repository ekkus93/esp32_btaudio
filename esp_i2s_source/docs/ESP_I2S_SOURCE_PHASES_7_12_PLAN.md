# Implementation Plan: Phases 7–12

**Based on:** `ESP_I2S_SOURCE_IMPLEMENTATION_TODO_2026-07-15.md` and `ESP_I2S_SOURCE_IMPLEMENTATION_SUMMARY_2026-07-15.md`
**Date:** 2026-07-15
**Status:** Phases 1–6 complete. This plan covers the remaining work.

---

## Overview

Phases 7–12 comprise approximately **44 subtasks** across 5 major areas:
- **Phase 7:** Radio lifecycle serialization (11 subtasks)
- **Phase 8:** Wi-Fi manager hardening (9 subtasks)
- **Phase 9:** Station IDs, control orchestration (9 subtasks)
- **Phase 10:** Web backend/security/frontend (12 subtasks)
- **Phase 11:** Device tests & release gate (7 subtasks)
- **Phase 12:** Cleanup & documentation (3 subtasks)

Estimated scope: ~8–12 commits, touching 20+ files across C, TypeScript, and test infrastructure.

---

## Dependencies Between Phases

```
Phase 7 (Radio)  ──────────┐
                             ├── Phase 9 (Stations/Control) ──┐
Phase 8 (Wi-Fi) ────────────┘                                  │
                                                                │
Phase 10 (Web) ← depends on 7, 8, 9 for backend changes        │
                                                              │
Phase 11 (Device tests) ← all phases complete, gates verified  │
Phase 12 (Docs) ← final polish after 10–11                     │
```

**Key ordering constraints:**
- Phase 7 must precede Phase 9.9 (scan uses radio stop/play)
- Phase 8 must precede Phase 10.5–10.6 (Wi-Fi credential handling in web UI)
- Phase 9 must precede Phase 10 (station ID migration affects web API)
- Phase 10 frontend changes depend on backend auth (10.6)
- Phase 11 requires all phases complete

---

## Phase 7 — Make radio lifecycle single-owner and join-safe

**Goal:** Ensure the radio module is safe against concurrent play/stop, worker tasks can't access freed sessions, and the compressed ring doesn't drop bytes.

**Commits (group by component):**

### Commit 7a: Radio lifecycle serialization (7.1, 7.2, 7.3, 7.4)
**Files:** `components/radio/radio.c`, `components/radio/include/radio.h`

**What to change:**

1. **7.1 — Command-queue-only lifecycle:**
   - Currently `radio_play_sync()` and `radio_stop_sync()` are callable directly.
   - Make them `static` or prefix with `_` and only callable from the cmd worker.
   - `radio_play_async()` / `radio_stop_async()` are already the public entry points.
   - No direct lifecycle mutation from outside the cmd task.

2. **7.2 — Start bits before workers run:**
   - Add `RADIO_EVT_STREAM_STARTED` and `RADIO_EVT_DECODER_STARTED` event bits.
   - Workers set their "started" bit as the first action.
   - `radio_play_sync()` waits for both started bits before marking RUNNING.
   - If startup fails, request stop and wait for EXITED before returning error.

3. **7.3 — Single exit label:**
   - Restructure `stream_task()` and `decoder_task()` to have one `exit:` label.
   - Every early failure jumps to `exit:`, which sets the EXITED bit and clears the task handle.
   - Workers never `free()` the session — only the owner does after both EXITED bits are set.

4. **7.4 — Never free a faulted/unjoined session:**
   - Delete the current `RADIO_STATE_FAULTED` fast-free branch in `radio_stop_sync()`.
   - On timeout: set `RADIO_STATE_FAULTED_JOIN_PENDING`, retain session in memory.
   - Add `session_all_exited()` helper that checks both EXITED bits.
   - `session_destroy_joined()` asserts both EXITED before freeing.

**Risk:** The current code already has `radio_stop_sync()` waiting for EXITED bits with timeout. The faulted case (timeout) is where sessions might be freed prematurely. This is a behavioral fix — the struct change is minor.

### Commit 7b: Interruptible waits & HTTP status (7.5, 7.6)
**Files:** `components/radio/radio.c`

5. **7.5 — Interruptible waits:**
   - Replace `vTaskDelay(backoff)` with `taskNotifyGive` + `ulTaskNotifyTake()`.
   - When stop is requested, notify the stream task to wake it.
   - Set HTTP read timeout so the worker checks stop between reads.

6. **7.6 — HTTP status validation:**
   - After `esp_http_client_open()`, check status code.
   - Reject non-2xx with `RADIO_ERR_HTTP_STATUS` and reconnect/fail.
   - Change `resolve_url()` to return `esp_err_t` and handle failure explicitly.

### Commit 7c: Stream loop & decoder fixes (7.7, 7.8, 7.9)
**Files:** `components/radio/radio.c`

7. **7.7 — Backpressure ring writes:**
   - After `esp_http_client_read()`, check `ring_write()` return value.
   - If ring is full, wait or stop reading — don't silently drop bytes.
   - The current code reads `sizeof(buf)` and writes to ring, but doesn't check if all bytes were accepted.

8. **7.8 — Decoder progress validation:**
   - Add check: `if (raw.consumed > raw.len) { fault; goto exit; }`
   - Remove `if (raw.consumed == 0) raw.consumed = 1;` — force progress on decoder error is dangerous.
   - Use proper decoder error categories: `ESP_AUDIO_ERR_BUFF_NOT_ENOUGH` breaks the loop, other errors count toward resync.

9. **7.9 — Prebuffer persistence fix:**
   - `s_prebuffer_bytes` is `volatile` but accessed without synchronization.
   - Protect with `s_pcm_mtx` or convert to atomic.
   - The current code reads `s_prebuffered` under `s_control_mtx -> s_pcm_mtx` nesting — ensure consistency.

### Commit 7d: Status snapshot (7.10)
**Files:** `components/radio/radio.c`, `components/radio/include/radio.h`

10. **7.10 — Published status snapshot:**
    - Current `radio_get_status()` acquires nested locks (`s_control_mtx -> s_mtx -> s_pcm_mtx`).
    - Replace with a single-mutex snapshot pattern.
    - Workers publish status changes under one mutex.
    - `radio_get_status()` reads from the snapshot without nested acquisition.
    - This eliminates potential deadlock if lock order varies.

### Commit 7e: Tests (7.11)
**Files:** `test/host_test/test_radio_lifecycle.c`

11. **7.11 — Failure injection tests:**
    - Inject failures at each allocation/task creation point.
    - Assert no double-free, no use-after-free under ASan.
    - Test stream task creation failure, decoder task creation failure.
    - Test timeout during stop (workers don't exit).

**Estimated effort:** 2–3 days. High risk (concurrency changes).

---

## Phase 8 — Make Wi-Fi safe, exact, and transactional

**Status: DONE** — All subtasks (8.1-8.9) implemented. Device build verified. Host tests pass.

**Goal:** Fix credential handling, prevent stale events, make init idempotent, remove password leakage.

**Commits:**

### Commit 8a: Lifecycle, credential handling (8.1, 8.2, 8.3, 8.4, 8.5, 8.6)
**Files:** `components/wifi_mgr/wifi_mgr.c`, `components/wifi_mgr/include/wifi_mgr.h`

1. **8.1 — Lifecycle mutex + state:**
   - Add `s_mgr_mtx` + `s_lifecycle` enum.
   - `wifi_mgr_init()` checks state under mutex — idempotent on RUNNING, error on other states.
   - Every failure path unwinds resources and resets state.

2. **8.2 — Replace `ESP_ERROR_CHECK`:**
   - Current code calls `ESP_ERROR_CHECK()` on `esp_netif_init()`, `esp_wifi_init()`, etc.
   - Change to explicit error handling with `goto fail` cleanup.
   - Store event handler instances (`esp_event_handler_instance_register`) for later cleanup.

3. **8.3 — Exact 32-byte SSID handling:**
   - Current code uses `strlcpy()` into `wifi_config_t` — safe but doesn't handle binary SSIDs.
   - Validate SSID length (0 < len ≤ 32), use `memcpy()` with explicit length.
   - `cfg.ap.ssid_len` must be set correctly.

4. **8.4 — Password validation (incl. 64-char hex PSK):**
   - Per errata answer #4: gate 64-char hex PSK behind `CONFIG_ESP_I2S_SOURCE_HEX_PSK`.
   - Validate: empty (open), 8–63 chars (WPA2 passphrase), or exactly 64 hex chars (raw PSK).
   - Reject 64 non-hex chars.

5. **8.5 — Transactional credential updates:**
   - Build candidate credentials in a local struct first.
   - Persist to NVS. Only swap into RAM on success.
   - If live apply fails after persistence, set state to "persisted but not applied".

6. **8.6 — Erase semantics:**
   - `erase_creds()` must erase both NVS keys even if one is missing.
   - `nvs_erase_key()` returns `ESP_ERR_NVS_NOT_FOUND` if key doesn't exist — treat as success.

**Estimated effort:** 1–2 days.

### Commit 8b: Security fixes (8.7, 8.8, 8.9)
**Files:** `components/wifi_mgr/wifi_mgr.c`, `components/wifi_mgr/include/wifi_mgr.h`

7. **8.7 — Remove AP password from public status:**
   - `wifi_mgr_get_info()` currently copies `s_ap_pass` into `out->ap_pass`.
   - Remove `ap_pass` from `wifi_mgr_info_t` or expose only `bool ap_secured`.
   - Update frontend types accordingly.

8. **8.8 — Reject stale Wi-Fi events:**
   - Track attempt generation. Only accept events when the current state expects them.
   - If `IP_EVENT_STA_GOT_IP` arrives after AP fallback or disconnect, ignore it.

9. **8.9 — Serialize provisioning jobs:**
   - Queue credential changes to a Wi-Fi command queue instead of shared globals.
   - Return HTTP 409 if a provisioning operation is already running.

**Estimated effort:** 1 day.

---

## Phase 9 — Fix stations and control orchestration

**Goal:** Stable station IDs (survive reordering), versioned persistence, synchronized config updates, scan state machine.

**Commits:**

### Commit 9a: Station IDs & persistence (9.1, 9.2, 9.3, 9.4, 9.5)
**Files:** `components/radio/station_store.c`, `components/radio/station_store.h`, `components/radio/stations.c`, `components/radio/include/stations.h`, `components/ctrl/include/ctrl_cfg.h`

1. **9.1 — Stable station IDs:**
   - Add `uint32_t id` to `station_t`. IDs are assigned sequentially on add and never change.
   - `next_id` stored in NVS blob. On migration, detect old `STA1` blob by magic+size, assign IDs.
   - `ctrl_cfg.last_station` → `ctrl_cfg.last_station_id` (0 means none).
   - Add `station_store_index_by_id()` for lookup.
   - Reorder operations don't change IDs — only array position.

2. **9.2 — Precise station errors:**
   - Define `station_result_t` enum with distinct errors: INVALID_ARG, INVALID_URL, TOO_LONG, DUPLICATE, FULL, NOT_FOUND, PERSIST.
   - Don't return `ESP_ERR_NO_MEM` for full store or duplicate.

3. **9.3 — Reject truncation:**
   - Validate URL/name length before accepting. Don't silently truncate.
   - Use `memcpy(len + 1)` after validation instead of `strncpy`.

4. **9.4 — URL parsing hardening (incl. errata #2 SSRF blocking):**
   - Reject loopback/link-local/private IPs per errata answer #2.
   - Parse scheme/host/port. Require nonempty host.
   - Gate behind `CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS` (default n).
   - Reject DNS results that resolve to private addresses.
   - Validate on station add/update and on play.

5. **9.5 — Versioned/checksummed persistence:**
   - Add magic/version header to blob: `STN2` magic, version 2.
   - CRC-32 over payload. Validate on load.
   - If invalid: load safe defaults, set `persistence_corrupt` flag, don't auto-save.

**Estimated effort:** 2–3 days. This is the largest single phase because it affects the data model.

### Commit 9b: Control synchronization (9.6, 9.7, 9.8, 9.9)
**Files:** `components/radio/stations.c`, `components/ctrl/ctrl.c`, `components/ctrl/include/ctrl.h`

6. **9.6 — Idempotent stations_init():**
   - Already has mutex protection. Add `s_initialized` flag for idempotency.
   - `stations_count()` takes mutex or returns from snapshot.

7. **9.7 — Control start/config synchronized:**
   - Current `ctrl_start()` creates a task that reads `s_cfg` without holding the mutex.
   - Fix: pass a copied initial config to the task, or take the mutex in the task's first action.
   - Config setter uses candidate pattern: build under lock, save, publish.

8. **9.8 — Monotonic timestamps:**
   - Replace fixed `dt_ms = CTRL_LOOP_MS` with `esp_timer_get_time()` elapsed time.
   - For host tests, pass explicit timestamps.

9. **9.9 — Scan state machine:**
   - Current `ctrl_scan()` spawns a task that sleeps for fixed durations.
   - Replace with event-driven state machine:
     - `SCAN_STARTING` → stop radio, disconnect sink
     - `SCANNING` → wait for scan-complete event/deadline
     - `SCANNING_RESTORE` → reconnect, re-apply volume, resume station
   - Replace blind `vTaskDelay()` with `xEventGroupWaitBits()` or notification.

**Estimated effort:** 2–3 days.

---

## Phase 10 — Fix HTTP backend, security, and frontend

**Goal:** Fix use-after-free, add auth, centralize error handling, fix overlapping polls.

**Commits:**

### Commit 10a: Backend security fixes (10.1, 10.2, 10.3, 10.4)
**Files:** `components/web_ui/web_ui_radio.c`, `components/web_ui/web_ui.c`, new `web_ui_json.c`

1. **10.1 — Fix cJSON use-after-free:**
   - `web_ui_radio.c` currently parses JSON and queues the URL.
   - The cJSON string is freed after the handler returns, but the queued command may access it later.
   - Fix: copy URL into a fixed buffer before `cJSON_Delete()`.

2. **10.2 — Strict JSON-body helper:**
   - Create `web_ui_json.c` with `web_read_json()` and `web_json_free()`.
   - Every endpoint uses this instead of ad-hoc `recv_body()` + `cJSON_Parse`.

3. **10.3 — Centralized error responses:**
   - `web_send_error()` and `web_send_ok()` helpers.
   - Consistent JSON envelope: `{"ok":true,"data":...}` or `{"ok":false,"error":{"code":"...","message":"...","retryable":false}}`.

4. **10.4 — Don't block HTTP on UART:**
   - `GET /api/status` currently calls `bt_link_send()` synchronously.
   - Replace with cached snapshot updated by background probe.
   - The probe runs at 10s intervals, not on every HTTP request.

### Commit 10b: Auth & async operations (10.5, 10.6)
**Files:** `components/web_ui/web_ui.c`, `components/web_ui/web_ui_internal.h`, new auth module

5. **10.5 — Queue long operations, return 202:**
   - Scan, connect, Wi-Fi provisioning, radio stop/start → queue operation, return 202 with operation ID.
   - Status endpoint for operation polling.
   - Prevent overlapping poll pileups.

6. **10.6 — Release authentication:**
   - Per errata answer #7: bearer token authentication.
   - Token generated on first boot, printed to USB serial as `AUTH|BOOTSTRAP_TOKEN|<token>`.
   - Physical button rotation documented in README.
   - `Authorization: Bearer <token>` required for all POST/PUT/DELETE.
   - Constant-time token comparison.
   - Gate behind `CONFIG_ESP_I2S_SOURCE_HTTP_AUTH`.

### Commit 10c: Frontend fixes (10.7, 10.8, 10.9, 10.10, 10.11, 10.12)
**Files:** `web/src/api.ts`, `web/src/usePolling.ts`, `web/src/*.tsx`

7. **10.7 — Remove AP password from API/UI:**
   - Backend `wifi_mgr_info_t` already fixed in Phase 8.7.
   - Frontend `ApStatus` type: replace `pass` with `secured` boolean.
   - Password edit field labeled "New password", blank initially.

8. **10.8 — Centralize frontend API calls:**
   - Create `apiRequest()` helper with error handling, timeout, abort.
   - Replace raw `fetch()` calls throughout `api.ts`.
   - `ApiError` class with status, code, retryable flag.

9. **10.9 — Replace overlapping polling:**
   - Current `usePolling.ts` doesn't prevent overlapping polls.
   - Replace with generation-based scheduling: next poll only after current completes.
   - Stale response doesn't overwrite fresh state.

10. **10.10 — Serialize arpeggio notes:**
    - Current arpeggio uses intervals that fire unawaited requests.
    - Replace with async loop with abort signal.
    - Always send `toneOff` in cleanup.

11. **10.11 — Frontend tests:**
    - Add Vitest/React Testing Library.
    - Test: error responses, timeout/abort, single poll in flight, stale response handling.

12. **10.12 — Mock device server:**
    - Default Playwright tests use mock server, not `http://10.1.2.52`.
    - `LIVE_DEVICE=1` flag for live tests.
    - Remove hardcoded MAC addresses.

**Estimated effort:** 3–5 days. Most work is in the frontend and auth.

---

## Phase 11 — Device tests and release gate

**Goal:** Fix device-side tests, make gate strict, add hardware validation.

### Commit 11a: Device test fixes (11.1–11.5)
**Files:** `test/test_device/main/test_device.c`

1. **11.1 — Sine device test:**
   - Replace first-sample assertion with nonzero count + energy check.

2. **11.2 — NVS roundtrip test:**
   - Always write/read/delete with unique namespace.

3. **11.3 — Wi-Fi connectivity test:**
   - Create event loop, STA netif, register handlers, wait for IP.
   - Skip with reason if no credentials.

4. **11.4 — PSRAM expectation conditional:**
   - `#if CONFIG_SPIRAM` guard.

5. **11.5 — Task test synchronization:**
   - Use binary semaphore instead of handle check.

### Commit 11b: Gate strictness & hardware validation (11.6, 11.7)
**Files:** `tools/s3_device_gate.sh`, `tools/s3_gate_assert.py`, `tools/test_s3_gate_assert.py`

6. **11.6 — Strict gate by default:**
   - Release mode requires: BOOT COMPLETE, no crash, WIFI state, BTLINK reachable, I2S state, clocks, radio start/stop.
   - `--degraded` flag to relax companion/network requirements.
   - Add crash pattern detection.

7. **11.7 — Hardware validation gates (A–F):**
   - Document the exact sequence.
   - Gate A: boot without WROOM32
   - Gate B: UART link only
   - Gate C: I2S clocks
   - Gate D: tone end-to-end
   - Gate E: radio (MP3 + AAC + resampler + stop)
   - Gate F: soak (2h MP3, 2h AAC, 500 play/stop cycles, WiFi disconnect/reconnect, WROOM power cycle)

**Estimated effort:** 2–3 days. Mostly test code and CI scripts.

---

## Phase 12 — Final cleanup and documentation

### Commit 12: Cleanup (12.1–12.3)
**Files:** `docs/SPEC.md`, `README.md`, all source files with stale comments

1. **12.1 — Remove stale comments:**
   - Search for "master transmitter", "MAC-derived", "skip I2S", "glitch-free", "benign in practice", "best-effort", "fall back", "fallback".
   - Update to match implemented behavior.
   - Move historical experiment notes to troubleshooting doc.

2. **12.2 — Authoritative architecture document:**
   - Update `docs/SPEC.md` with final implemented contract.
   - Include: wiring table, boot order, lifecycle ownership diagram, I2S frame diagram, UART protocol, radio pipeline, API/auth model, NVS schemas, test commands, device gate commands, known limitations.
   - Archive old conflicting specs under `docs/archive/`.

3. **12.3 — One-command verification:**
   - README documents exact commands for build, flash, and gate.

**Estimated effort:** 1–2 days.

---

## Implementation Order

Based on dependencies, the recommended order is:

```
1. Phase 7  (radio lifecycle)     — foundation for audio pipeline safety
2. Phase 8  (Wi-Fi manager)       — credential security (independent of Phase 7)
3. Phase 9  (stations + control)  — depends on Phase 7 (radio stop/play)
4. Phase 10 (web backend/frontend) — depends on 7, 8, 9
5. Phase 11 (device tests)        — all phases complete
6. Phase 12 (docs)                — final polish
```

Within each phase, implement subtasks in order (they build on each other).

**Parallelizable work:**
- Phase 7 and Phase 8 can be done in parallel (independent components).
- Phase 10.7–10.12 (frontend) can be drafted in parallel with backend work, but integration requires backend auth (10.6).

---

## Risk Assessment

| Area | Risk | Mitigation |
|------|------|------------|
| Phase 7 (radio concurrency) | High — race conditions, session lifetime | Hardware verification at each subtask. ASan/TSan where possible. |
| Phase 8 (Wi-Fi credentials) | Medium — credential corruption, connection failure | Transactional NVS. Stale event rejection. |
| Phase 9 (station migration) | Medium — NVS blob incompatibility | Versioned format with CRC. Migration from old format. |
| Phase 10 (auth) | Medium — lockout, token leakage | Bootstrap token printed on first boot. Physical button rotation. |
| Phase 11 (device tests) | Low — test infrastructure only | Host tests first, then device. |

---

## Commit Sequence (matching TODO's recommended sequence)

```
fix(radio): serialize lifecycle and join workers safely
fix(wifi): make manager idempotent and transactional
fix(stations): stable IDs, versioned persistence, SSRF blocking
fix(ctrl): synchronize config and make scan a state machine
fix(web): fix request ownership, errors, auth, and async operations
fix(ui): centralize API errors and non-overlapping polling
test(device): require end-to-end audio evidence
docs: archive old specs, update architecture document
```

That's 8 commits total, grouping related changes to keep each commit reviewable.

---

## Errata Notes (from TODO header)

Per `ESP_I2S_SOURCE_FIX_RESPONSES_V2_2026-07-15.md`:

1. **Phase 9.4 (station URL parsing):** Add SSRF blocking per errata answer #2. Reject loopback/link-local/private/multicast/unspecified/broadcast destinations. Gate behind `CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS` (default n).
2. **Phase 8.4 (Wi-Fi hex PSK):** Gate 64-char hex STA PSK behind `CONFIG_ESP_I2S_SOURCE_HEX_PSK` per errata answer #4.
3. **Phase 9.1 (station migration):** Use errata answer #6 migration algorithm (legacy `STA1` blob detection, sequential ID assignment, `stations_v2` key, retain legacy key).
4. **Phase 10.6 (auth bootstrap):** Use errata answer #7 flow (token printed to USB serial as `AUTH|BOOTSTRAP_TOKEN|<token>`, physical-button rotation).

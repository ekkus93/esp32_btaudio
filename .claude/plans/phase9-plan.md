# Phase 9 Implementation Plan

## Overview

Phase 9 fixes station IDs, control orchestration, and persistence. It has two commits:
- **Commit 9a**: Station IDs & persistence (9.1-9.5)
- **Commit 9b**: Control synchronization (9.6-9.9)

---

## Commit 9a: Station IDs & persistence (9.1-9.5)

### Files modified

#### 1. `station_store.h` — Add IDs, result enum, ID-based lookup, SSRF validation

Changes:
- Add `uint32_t id` field to `station_t` (first field for stable identity)
- Add `uint32_t next_id` to `station_store_t` (assigned sequentially)
- Define `station_result_t` enum with precise error codes:
  - `STATION_OK = 0`
  - `STATION_ERR_INVALID_ARG`
  - `STATION_ERR_INVALID_URL`
  - `STATION_ERR_TOO_LONG`
  - `STATION_ERR_DUPLICATE`
  - `STATION_ERR_FULL`
  - `STATION_ERR_NOT_FOUND`
  - `STATION_ERR_PERSIST`
- Add `station_store_index_by_id()` — find by stable ID
- Add `station_validate_url()` — returns `station_result_t` (replaces `station_url_valid()` with more detail)
- Add `STATION_ID_NONE` constant (0) — IDs start at 1

#### 2. `station_store.c` — Implement ID logic, URL validation, SSRF blocking

Changes:
- `station_store_init()` sets `next_id = 1`
- `station_store_add()` assigns `id = next_id++` to the new station
- `station_store_remove()` does not change other stations' IDs
- `station_store_move()` swaps the entire `station_t` (ID moves with the entry)
- `station_store_find_by_id()` returns index of station with given ID, or -1
- URL validation rejects:
  - Empty/NULL URLs
  - Non-http/https schemes
  - URLs with control characters
  - URLs that resolve to loopback/link-local/private IPs (when the config option is disabled)
- Name validation rejects truncation (returns `STATION_ERR_TOO_LONG`)

#### 3. `stations.h` — Device wrapper API stays the same (index-based operations)

The device wrapper `stations_get()` etc. still use array indices. IDs are for external reference (what the web UI shows). The API returns the stable ID to the caller.

Changes:
- `stations_get()` gains a `uint32_t *out_id` parameter (optional, pass ID to caller)
- Or add `stations_get_by_id()` for ID-based lookup
- Actually, the web API currently uses the index-position as "id" in the JSON response. With stable IDs, the web API should expose the stable ID instead.

#### 4. `stations.c` — Versioned persistence with STN2 magic + CRC-32

Changes:
- New blob format:
  ```c
  #define STATIONS_V2_MAGIC 0x53544e32u  // "STN2"
  #define STATIONS_V2_VERSION 2
  
  typedef struct {
      uint32_t magic;
      uint16_t version;
      uint16_t header_size;  // sizeof(header)
      uint32_t payload_size; // sizeof(store)
      uint32_t crc32;        // CRC-32 over payload (with CRC zeroed)
      uint32_t next_id;
      station_store_t store;
  } stations_blob_v2_t;
  ```
- Migration: detect old STA1 blob by magic+size, assign sequential IDs
- Load path:
  1. Try STN2 blob first
  2. If invalid CRC or wrong magic, try STA1 migration
  3. If both fail, seed defaults and set `persistence_corrupt` flag
- Save path: build blob in memory, compute CRC-32, write to NVS, then swap to RAM on success
- Add `station_store_index_by_id()` implementation in device context

#### 5. `ctrl_cfg.h` — Change `last_station` to `last_station_id`

Changes:
- Rename `last_station` (int16_t) to `last_station_id` (uint32_t)
- `0` means "none" (instead of `CTRL_STATION_NONE = -1`)
- Add `CTRL_LAST_STATION_NONE` constant (0)
- Keep `CTRL_STATION_NONE` as deprecated alias (-1) for migration

#### 6. `ctrl_cfg.c` — Update config blob

Changes:
- Update blob format version
- On load: if `last_station` is -1 (old format), set `last_station_id = 0`
- Save `last_station_id` as uint32_t

#### 7. `ctrl.c` — Use `last_station_id` instead of `last_station`

Changes:
- `ctrl_note_station()` now takes `uint32_t station_id` parameter
- `do_action()` uses `stations_get_by_id()` or `station_store_find_by_id()` to get the URL from ID
- `orchestrator_task()` checks `last_station_id` instead of `last_station`

#### 8. `web_ui_radio.c` — Expose stable IDs in JSON

Changes:
- `stations_get_h()` returns `id` field as the stable ID (not array index)
- Station add/update/remove handlers use the stable ID

---

## Commit 9b: Control synchronization (9.6-9.9)

### Files modified

#### 1. `stations.c` — Idempotent init + locked count (9.6)

Changes:
- Add `_Atomic bool s_initialized` flag
- `stations_init()` checks `s_initialized` — returns `ESP_OK` if already done
- `stations_count()` takes the mutex before reading

#### 2. `ctrl.c` — Config synchronization + monotonic timestamps + scan state machine (9.7-9.9)

Changes for 9.7 (config synchronization):
- `ctrl_start()` passes a copied initial config to the orchestrator task:
  ```c
  ctrl_cfg_t initial_cfg;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  initial_cfg = s_cfg;
  xSemaphoreGive(s_mtx);
  // Pass initial_cfg to task (task stores its own copy)
  ```
- `ctrl_set_sink()` uses candidate pattern: build under lock, save, publish

Changes for 9.8 (monotonic timestamps):
- Replace fixed `CTRL_LOOP_MS` timing with `esp_timer_get_time()`:
  ```c
  int64_t now_us = esp_timer_get_time();
  uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
  last_us = now_us;
  ```

Changes for 9.9 (scan state machine):
- Replace `scan_task` with state machine:
  - `SCAN_IDLE` → `SCAN_STARTING` (stop radio, disconnect sink)
  - `SCAN_STARTING` → `SCANNING` (run SCAN inquiry)
  - `SCANNING` → `SCAN_RESTORE` (wait for scan-complete event/deadline)
  - `SCAN_RESTORE` → `SCAN_IDLE` (reconnect, re-apply volume, resume station)
- Replace blind `vTaskDelay()` with `xEventGroupWaitBits()` or notification
- Track state transitions, report partial failure

---

## Implementation Order

1. **station_store.h** — Add types, enums, new function signatures
2. **station_store.c** — Implement ID logic, URL validation, SSRF blocking
3. **stations.h** — Update device wrapper signatures
4. **stations.c** — Versioned persistence, migration, CRC-32
5. **ctrl_cfg.h** — Update `last_station_id` field
6. **ctrl_cfg.c** — Update blob format
7. **ctrl.c** — Use `last_station_id`, pass copied config to task
8. **ctrl.h** — Update signatures
9. **web_ui_radio.c** — Use stable IDs in JSON
10. **test_station_store.c** — Update tests for new API
11. **ctrl_device_stubs.c** — Update stubs

---

## Testing Strategy

After each commit:
1. Run host tests: `cd test/host_test && cmake .. && cmake --build . && ctest`
2. Build device: `idf.py build`
3. Verify no compilation warnings under strict mode

---

## Risk Assessment

- **Data migration**: Old STA1 blob must migrate to STN2 format correctly. Test with old-format blob.
- **ID stability**: IDs assigned sequentially on add, never change. Migration assigns IDs to old entries.
- **Backward compatibility**: Old firmware can't read new blob format — that's expected (version bump).
- **Config change**: `last_station` → `last_station_id` changes the config blob; handle migration from int16_t to uint32_t.

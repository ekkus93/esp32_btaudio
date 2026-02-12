# ESP32 Bluetooth Audio Source - Test Coverage Analysis

**Generated:** 2026-02-11  
**Analysis Scope:** Components, Command Handlers, State Machines, Error Paths

---

## Executive Summary

This analysis identifies test coverage gaps across the ESP32 Bluetooth Audio Source codebase. The project has **good coverage** for core audio processing, BT pairing flows, and utility functions, but has **significant gaps** in command handler error paths, NVS storage error injection, BT state machine edge cases, and integration between components.

### Coverage Status by Component

| Component | Coverage | Test Files | Priority |
|-----------|----------|------------|----------|
| `util_safe` | ✅ Excellent | `test_util_safe_host.c` | Low |
| `audio_resampler_stream` | ✅ Excellent | `test_audio_resampler_stream.c` | Low |
| `bt_pairing_store` | ✅ Good | `test_pairing_*.c` (6 files) | Low |
| `i2s_manager` | ⚠️ Partial | `test_audio_i2s_host.c`, `test_manager.c` | Medium |
| `beep_manager` | ⚠️ Partial | `test_manager.c` | Medium |
| `synth_manager` | ⚠️ Partial | `test_synth_manager/` | Medium |
| `nvs_storage` | ⚠️ Partial | `test_nvs_storage.c`, `test_nvs_storage_errors.c` | High |
| `command_interface` | ❌ Minimal | `test_commands.c`, few cmd tests | **HIGH** |
| `audio_processor` | ⚠️ Partial | `test_audio_processor_*.c` (5 files) | High |
| `bt_manager` | ⚠️ Partial | `test_bt_manager_profiles.c` | High |
| `bt_connection_manager` | ⚠️ Partial | `test_bt_connection_manager.c` | High |
| `bt_streaming_manager` | ⚠️ Partial | `test_bt_streaming_manager.c` | High |
| `bt_scan` | ❌ None | None | Medium |
| `audio_processor_beep` | ⚠️ Minimal | Indirect via beep_manager tests | High |
| `audio_processor_wav` | ❌ None | None | Low |
| `audio_ringbuffer` | ⚠️ Partial | `test_audio_ringbuffer.c` | Medium |

---

## 1. Command Handler Coverage Gaps ❌ **CRITICAL PRIORITY**

### 1.1 Command Handlers with Minimal/No Tests

**Files:** `cmd_handlers_audio.c`, `cmd_handlers_bt.c`, `cmd_handlers_system.c`, `cmd_handlers_files.c`

#### Missing Test Coverage:

**`cmd_handlers_audio.c` (479 lines):**
- ❌ `cmd_handle_synth()` - Parameter validation, error cases
  - Missing: invalid params ("foo", negative numbers, overflow)
  - Missing: state transitions when audio processor not initialized
  - Missing: interaction with I2S source (synth ON while I2S active)
- ❌ `cmd_handle_diag()` - Error paths, state reporting accuracy
  - Missing: I2S manager failures
  - Missing: mismatch between BT connection states
  - Missing: beep active scenarios
- ❌ `cmd_handle_start()` - BT manager failure paths
- ❌ `cmd_handle_stop()` - BT manager failure paths
- ❌ `cmd_handle_volume()` - Bounds (0, 100, -1, 101, MAX_INT)
- ❌ `cmd_handle_mute()` / `cmd_handle_unmute()` - State tracking
- ❌ `cmd_handle_sample_rate()` - Invalid rates, format conversion edge cases
- ❌ `cmd_handle_beep()` - Beep busy, audio processor not initialized
- ❌ `cmd_handle_audio_status()` - Stats accuracy, null handling
- ❌ `cmd_handle_i2s_config()` - Parse errors, invalid GPIO pins, rate/depth combos

**`cmd_handlers_bt.c` (759 lines):**
- ✅ `cmd_handle_scan()` - Some coverage via `test_commands.c`
- ✅ `cmd_handle_connect()` - Partial coverage
- ✅ `cmd_handle_connect_name()` - Tested via `test_connect_name.c`
- ✅ `cmd_handle_pair()` - Tested via `test_pair_command.c`
- ✅ `cmd_handle_confirm_pin()` - Tested via pairing tests
- ✅ `cmd_handle_enter_pin()` - Tested via pairing tests
- ❌ `cmd_handle_disconnect()` - BT manager failure paths
- ❌ `cmd_handle_paired()` - Empty list, full list, NVS errors
- ❌ `cmd_handle_unpair()` - Missing MAC param, invalid MAC, NVS errors
- ❌ `cmd_handle_unpair_all()` - NVS clear failure, controller bond removal failures
- ❌ `cmd_handle_set_name()` - Missing param, too long, invalid characters
- ❌ `cmd_handle_set_default_pin()` - Missing param, too long/short, persistence failure
- ❌ `cmd_handle_debug()` - All debug subcommands untested

**`cmd_handlers_system.c` (397 lines):**
- ❌ `cmd_handle_help()` - Response format, truncation
- ❌ `cmd_handle_status()` - All error paths, missing BT/audio state
- ❌ `cmd_handle_version()` - Host override behavior
- ❌ `cmd_handle_reset()` - Mock verification only
- ❌ `cmd_handle_mem()` - PSRAM vs non-PSRAM builds
- ❌ `cmd_handle_spanlog()` - Invalid N, boundary conditions
- ❌ `cmd_handle_debug_log()` - Invalid tag/level parsing

**`cmd_handlers_files.c` (239 lines):**
- ❌ `cmd_handle_file()` - Missing param, file not found, path too long, not a file
- ❌ `cmd_handle_files()` - Mount failures, opendir failure, no root set
- ❌ `cmd_handle_parts()` - Partition iteration errors

#### Recommended Tests:

**Priority 1 (HIGH):**
```c
// test/host_test/test_cmd_handlers_audio.c
void test_cmd_synth_invalid_params(void);
void test_cmd_synth_state_transitions(void);
void test_cmd_volume_bounds_checking(void);
void test_cmd_sample_rate_invalid(void);
void test_cmd_i2s_config_parse_errors(void);
void test_cmd_beep_when_busy(void);

// test/host_test/test_cmd_handlers_bt.c
void test_cmd_unpair_missing_param(void);
void test_cmd_unpair_nvs_failure(void);
void test_cmd_unpair_all_errors(void);
void test_cmd_paired_empty_list(void);
void test_cmd_set_name_validation(void);

// test/host_test/test_cmd_handlers_files.c
void test_cmd_file_not_found(void);
void test_cmd_files_mount_failure(void);
void test_cmd_files_no_root(void);
```

---

## 2. NVS Storage Error Injection Gaps ⚠️ **HIGH PRIORITY**

**Files:** `nvs_storage.c` (497 lines)

**Existing Coverage:** ✅ `test_nvs_storage.c` (happy paths), ⚠️ `test_nvs_storage_errors.c` (partial fault injection)

### Missing Error Path Coverage:

#### 2.1 Init/Erase Sequence Errors
- ❌ `nvs_storage_init()` - Repeated NO_FREE_PAGES → erase failures
- ❌ `nvs_storage_init()` - NEW_VERSION with erase failure recovery
- ❌ Multiple init failure scenarios (what if erase succeeds but re-init fails?)

#### 2.2 Get/Set Error Injections
`test_nvs_storage_errors.c` provides infrastructure but limited scenarios:

**Missing coverage for:**
- ❌ `nvs_storage_get_volume()` - nvs_open failure, nvs_get_i32 failure
- ❌ `nvs_storage_set_volume()` - nvs_open failure, nvs_set_i32 failure, commit failure
- ❌ `nvs_storage_get_i2s_pins()` - partial failure (some pins readable, others not)
- ❌ `nvs_storage_set_i2s_pins()` - failure partway through (pin 1 OK, pin 2 fails)
- ❌ `nvs_storage_get_audio_config()` - corrupted data recovery
- ❌ `nvs_storage_set_audio_config()` - commit failure after successful writes
- ❌ `nvs_storage_get_paired_devices()` - blob size mismatch, corrupted data
- ❌ `nvs_storage_add_paired_device()` - blob write failure, commit failure
- ❌ `nvs_storage_remove_paired_device()` - erase_key failure, commit failure
- ❌ `nvs_storage_clear_paired_devices()` - erase_key failure mid-operation

#### 2.3 State Consistency After Failures
- ❌ Verify NVS handle cleanup after open failures
- ❌ Verify no partial writes persist after commit failures
- ❌ Verify defaults are used correctly when get operations fail

#### Recommended Tests:

```c
// test/host_test/test_nvs_storage_errors.c (extend existing file)
void test_volume_set_open_failure(void);
void test_volume_set_commit_failure(void);
void test_i2s_pins_partial_failure(void);
void test_audio_config_commit_failure(void);
void test_paired_devices_add_blob_failure(void);
void test_paired_devices_remove_erase_failure(void);
void test_paired_devices_clear_mid_operation_failure(void);
void test_nvs_init_erase_recovery_failure(void);
```

---

## 3. Beep Manager Error Paths ⚠️ **HIGH PRIORITY**

**Files:** `beep_manager.c` (329 lines), `audio_processor_beep.c` (209 lines)

**Existing Coverage:** ⚠️ `test_manager.c` (partial - 6 tests for beep_manager)

### Current Tests (from test_manager.c):
- ✅ `beep_manager_play_starts_overlay_and_calls_done`
- ✅ `beep_manager_rejects_invalid_args`
- ✅ `beep_manager_rejects_unsupported_bit_depth`
- ✅ `beep_manager_reports_busy_while_playing`
- ✅ `beep_manager_stop_terminates_overlay`

### Missing Coverage:

#### 3.1 audio_processor_beep.c Edge Cases
- ❌ `audio_processor_beep_tone()` - beep while WAV playback active
- ❌ `audio_processor_beep_tone()` - beep while not initialized (already returns error, needs test)
- ❌ `audio_processor_beep_tone()` - source restoration logic:
  - Beep interrupts SYNTH, restores SYNTH after ✅ (F1.3.1)
  - Beep interrupts I2S, restores I2S after ✅ (F1.3.1)
  - Beep when both SYNTH+I2S active (invariant violation) ❌
  - Beep when neither active, stays silent after ❌
- ❌ `audio_processor_beep_tone()` - duration clamping (0, 20001 ms)
- ❌ `audio_processor_beep_tone()` - s_drop_ring_audio flag behavior (F1.4.2)
- ❌ `audio_processor_beep_tone()` - beep_manager_play() failure handling
- ❌ `audio_processor_beep_done_cb()` - restore flag edge cases

#### 3.2 beep_manager.c Edge Cases
- ❌ `beep_manager_play()` - zero frequency (should clamp to default)
- ❌ `beep_manager_play()` - extreme frequencies (0.1 Hz, 20000 Hz)
- ❌ `beep_manager_play()` - zero amplitude handling
- ❌ `beep_manager_stop()` - stop when not initialized
- ❌ `beep_overlay_fill()` - buffer alignment issues
- ❌ `beep_overlay_fill()` - fade envelope edge cases (very short beep < 2*fade_frames)
- ❌ `beep_overlay_is_active()` - race conditions (though spinlock protected)

#### 3.3 Integration with Audio Processor
- ❌ Beep drops ring buffer audio correctly (s_drop_ring_audio flag)
- ❌ Beep preempts I2S and resumes correctly
- ❌ Beep done callback fires reliably
- ❌ Multiple rapid beep requests (queue, reject, or restart?)

#### Recommended Tests:

```c
// test/host_test/test_beep_manager_extended.c
void test_beep_during_wav_playback(void);
void test_beep_not_initialized(void);
void test_beep_restore_synth_source(void);
void test_beep_restore_i2s_source(void);
void test_beep_synth_i2s_both_active_invariant(void);
void test_beep_duration_clamping(void);
void test_beep_extreme_frequencies(void);
void test_beep_fade_envelope_short_beep(void);
void test_beep_drop_ring_audio_flag(void);
void test_beep_rapid_requests(void);
```

---

## 4. I2S Manager Error Paths ⚠️ **MEDIUM PRIORITY**

**Files:** `i2s_manager.c` (410 lines)

**Existing Coverage:** ⚠️ `test_audio_i2s_host.c` (26 tests - good state machine coverage), `test_manager.c` (7 tests)

### Current Tests Cover:
- ✅ State machine: init, start, stop, deinit sequences
- ✅ Error returns: start before init, double init, read before start
- ✅ Read errors: timeout, partial reads, null pointers
- ✅ Start/stop idempotency

### Missing Coverage:

#### 4.1 Configuration Errors
- ❌ `configure_i2s()` - NULL config parameter
- ❌ `configure_i2s()` - Invalid I2S port number
- ❌ `configure_i2s()` - i2s_new_channel failure (mock injection needed)
- ❌ `configure_i2s()` - i2s_channel_init_std_mode failure
- ❌ `configure_i2s()` - Unsupported sample rate (e.g., 11025 Hz, 192000 Hz)
- ❌ `configure_i2s()` - Unsupported bit depth (not 16 or 32)

#### 4.2 Runtime Error Handling
- ❌ `i2s_manager_fill()` - i2s_channel_read() returns ESP_ERR_INVALID_STATE
- ❌ `i2s_manager_fill()` - i2s_channel_read() returns ESP_ERR_NOT_FOUND
- ❌ `i2s_manager_fill()` - Resampler allocation failure
- ❌ `i2s_manager_fill()` - Work buffer too small for conversion
- ❌ `i2s_manager_fill()` - NULL destination buffer
- ❌ `i2s_manager_fill()` - Zero work_bytes

#### 4.3 Cleanup on Errors
- ❌ `i2s_manager_deinit()` - channel cleanup when never enabled
- ❌ `i2s_manager_deinit()` - i2s_channel_disable failure
- ❌ Verify no memory leaks on init failure paths

#### 4.4 Mock Queue (CONFIG_BT_MOCK_TESTING)
- ✅ `i2s_manager_mock_push()` - basic functionality tested
- ❌ `i2s_manager_mock_push()` - queue full scenario
- ❌ `i2s_manager_mock_push()` - NULL data parameter
- ❌ Mock queue with mixed sample rates/bit depths

#### Recommended Tests:

```c
// test/host_test/test_i2s_manager_errors.c
void test_configure_i2s_null_config(void);
void test_configure_i2s_invalid_port(void);
void test_configure_i2s_channel_create_failure(void);
void test_configure_i2s_unsupported_sample_rate(void);
void test_fill_resampler_allocation_failure(void);
void test_fill_work_buffer_too_small(void);
void test_deinit_cleanup_on_partial_init(void);
void test_mock_push_queue_full(void);
```

---

## 5. BT Manager State Machine Gaps ⚠️ **HIGH PRIORITY**

**Files:** `bt_manager.c` (1278 lines), `bt_connection_manager.c` (452 lines), `bt_streaming_manager.c` (394 lines), `bt_scan.c` (150+ lines)

**Existing Coverage:** ⚠️ `test_bt_manager_profiles.c`, `test_bt_connection_manager.c`, `test_bt_streaming_manager.c`

### 5.1 bt_manager.c Missing Coverage

#### Profile Initialization
- ✅ `bt_manager_init_profiles()` - basic success/failure tested
- ❌ Partial failure: AVRCP init OK, A2DP init fails
- ❌ Partial failure: A2DP init OK, callback registration fails
- ❌ Recovery from profile init failures

#### Connection State Machine
- ❌ `bt_manager_connect()` - Invalid MAC format
- ❌ `bt_manager_connect()` - Already connected (different device)
- ❌ `bt_manager_connect()` - esp_a2d_source_connect() failure
- ❌ `bt_manager_disconnect()` - Not connected
- ❌ `bt_manager_disconnect()` - esp_a2d_source_disconnect() failure

#### Pairing/Unpairing Edge Cases
- ✅ Basic pairing tested via pairing test suite
- ❌ `bt_pair()` - Invalid MAC address
- ❌ `bt_unpair()` - Device not in NVS but in controller bonds
- ❌ `bt_unpair()` - Device in NVS but not in controller bonds
- ❌ `bt_unpair_all()` - Controller bond removal failure for some devices
- ❌ `bt_unpair_all()` - NVS clear succeeds but controller ops fail

#### Event Handling
- ❌ `bt_manager_gap_callback()` - Unexpected event types
- ❌ `bt_manager_a2dp_callback()` - Unexpected event types
- ❌ `bt_manager_avrc_callback()` - Unexpected event types
- ❌ Race conditions: events arriving before init complete

### 5.2 bt_connection_manager.c Missing Coverage

**Existing:** ⚠️ Basic state transitions tested

#### Missing:
- ❌ `attempt_reconnection()` - Max retry exhaustion
- ❌ `attempt_reconnection()` - Reconnect failure → success after retry
- ❌ `attempt_reconnection()` - Auto-reconnect disabled
- ❌ `initiate_connection()` - Invalid BD address
- ❌ State callbacks - NULL callback handling
- ❌ Streaming state transitions during reconnection
- ❌ Connection info persistence across disconnects

### 5.3 bt_streaming_manager.c Missing Coverage

**Existing:** ⚠️ Basic streaming state transitions tested

#### Missing:
- ❌ `bt_audio_data_callback()` - Negative length (already guarded, needs test)
- ❌ `bt_audio_data_callback()` - Zero length request
- ❌ `bt_audio_data_callback()` - audio_processor_read() returns error
- ❌ `bt_audio_data_callback()` - Underrun statistics accuracy
- ❌ `bt_streaming_start()` - audio_processor not initialized
- ❌ `bt_streaming_stop()` - Already stopped
- ❌ State machine: START → PAUSE → RESUME → STOP sequences

### 5.4 bt_scan.c Missing Coverage

**Existing:** ❌ No dedicated tests

#### Missing (ENTIRE MODULE):
- ❌ `bt_start_scan()` - Not initialized
- ❌ `bt_start_scan()` - Already scanning (should return OK)
- ❌ `bt_start_scan()` - esp_bt_gap_start_discovery() failure
- ❌ `bt_stop_scan()` - Not scanning (should return OK)
- ❌ `bt_stop_scan()` - esp_bt_gap_cancel_discovery() failure
- ❌ `bt_scan_handle_discovery_result()` - Device list full (>20 devices)
- ❌ `bt_scan_handle_discovery_result()` - Missing name property
- ❌ `bt_scan_handle_discovery_result()` - Duplicate device (update existing)
- ❌ `bt_scan_handle_state_change()` - STARTED/STOPPED state sync

#### Recommended Tests:

```c
// test/host_test/test_bt_manager_extended.c
void test_bt_manager_profile_partial_init_failure(void);
void test_bt_connect_invalid_mac(void);
void test_bt_disconnect_not_connected(void);
void test_bt_unpair_partial_failures(void);
void test_bt_gap_callback_unexpected_events(void);

// test/host_test/test_bt_connection_manager_extended.c
void test_reconnect_max_retries(void);
void test_reconnect_auto_disabled(void);
void test_connection_info_persistence(void);

// test/host_test/test_bt_streaming_manager_extended.c
void test_audio_data_callback_negative_length(void);
void test_audio_data_callback_underrun_stats(void);
void test_streaming_start_not_initialized(void);
void test_state_transitions_pause_resume(void);

// test/host_test/test_bt_scan.c (NEW FILE)
void test_scan_not_initialized(void);
void test_scan_already_scanning(void);
void test_scan_discovery_failure(void);
void test_scan_device_list_full(void);
void test_scan_duplicate_device_update(void);
```

---

## 6. Audio Processor Edge Cases ⚠️ **MEDIUM PRIORITY**

**Files:** `audio_processor.c` (1515 lines), `audio_processor_read.c`, `audio_processor_state.c`, `audio_ringbuffer.c`, `audio_processor_diag.c`

**Existing Coverage:** ⚠️ Multiple test files (good foundation)

### Missing Coverage:

#### 6.1 audio_processor.c Core Logic
- ❌ `get_active_source()` - Source priority logic with beep active
- ❌ `produce_audio_chunk()` - Source switching stats accuracy
- ❌ `produce_audio_chunk()` - Beep overlay failure
- ❌ Audio engine task - Watermark pause/resume hysteresis
- ❌ Audio engine task - Ring buffer full/empty edge cases
- ❌ Volume commit timer callback - NVS write failure

#### 6.2 audio_processor_read.c
- ❌ `audio_processor_read()` - s_drop_ring_audio flag behavior
- ❌ `audio_processor_read()` - Ring buffer underrun during beep
- ❌ `audio_processor_read()` - NULL bytes_read pointer

#### 6.3 audio_ringbuffer.c
**Existing:** ✅ `test_audio_ringbuffer.c` (good coverage)

**Missing:**
- ❌ Concurrent producer/consumer stress test
- ❌ Watermark edge cases (threshold exactly at high/low)
- ❌ Wrap-around during read/write

#### 6.4 audio_processor_diag.c
- ❌ All diagnostic functions untested
- ❌ `audio_processor_get_status()` - Accuracy under load
- ❌ Stats reporting with overflow counters

#### Recommended Tests:

```c
// test/host_test/test_audio_processor_extended.c
void test_active_source_priority_with_beep(void);
void test_produce_chunk_source_switch_stats(void);
void test_watermark_hysteresis(void);
void test_ring_buffer_full_behavior(void);
void test_volume_commit_nvs_failure(void);

// test/host_test/test_audio_processor_read_extended.c
void test_read_drop_ring_audio_flag(void);
void test_read_underrun_during_beep(void);

// test/host_test/test_audio_ringbuffer_stress.c
void test_ringbuffer_concurrent_access(void);
void test_ringbuffer_watermark_exact_threshold(void);
```

---

## 7. Integration Test Gaps ⚠️ **MEDIUM PRIORITY**

### 7.1 Component Integration Missing

**Existing:** ⚠️ `test_audio_engine_stress.c` (excellent stress tests)

**Missing:**
- ❌ Command → BT Manager → Audio Processor complete flow
- ❌ Beep → I2S restore → BT streaming integration
- ❌ NVS → BT Manager → Pairing persistence integration
- ❌ Scan → Connect → Pair → Stream end-to-end
- ❌ Error propagation across component boundaries
- ❌ Memory leak testing during error recovery

### 7.2 Concurrency & Race Conditions
- ❌ Command interface + BT events arriving simultaneously
- ❌ Audio callback + volume change + beep start
- ❌ Multiple rapid command invocations (flood test)

### 7.3 Recovery Scenarios
- ❌ BT disconnect → reconnect → resume streaming
- ❌ I2S source failure → fallback to synth → recovery
- ❌ NVS corruption → defaults → re-pair → persist

#### Recommended Tests:

```c
// test/host_test/test_integration_flows.c (NEW FILE)
void test_scan_connect_pair_stream_flow(void);
void test_beep_i2s_restore_streaming(void);
void test_nvs_pairing_persistence(void);
void test_error_propagation_chain(void);

// test/host_test/test_concurrency.c (NEW FILE)
void test_command_bt_event_race(void);
void test_audio_callback_volume_beep_race(void);
void test_command_flood(void);
```

---

## 8. Utility and Supporting Code

### 8.1 ✅ Well-Tested Components (Low Priority)
- ✅ `util_safe.c` - `test_util_safe_host.c` (11 tests, excellent coverage)
- ✅ `audio_resampler_stream.c` - `test_audio_resampler_stream.c` (16 tests, comprehensive)
- ✅ Pairing store - `test_pairing_*.c` (6 files, good coverage)

### 8.2 ❌ Untested Supporting Code
- ❌ `audio_processor_wav.c` - No tests (low priority - legacy stub)
- ❌ `audio_span_log.c` - No tests (diagnostic logging)
- ❌ `audio_util.c` - Partial tests in `test_audio_util/`
- ❌ `platform_shim/` - No tests (thin wrappers)

---

## 9. Test Infrastructure Improvements

### 9.1 Mock/Stub Gaps
- ⚠️ NVS fault injection exists but limited scenarios
- ❌ I2S channel failure injection (i2s_new_channel, i2s_channel_enable)
- ❌ BT stack failure injection (esp_bt_gap_*, esp_a2d_*)
- ❌ Heap allocation failure injection (malloc, heap_caps_malloc)

### 9.2 Test Harness Enhancements Needed
- ❌ Memory leak detection in tests
- ❌ Test timeout enforcement
- ❌ Coverage reporting integration
- ❌ Automated test dependency tracking

---

## 10. Prioritized Implementation Plan

### Phase 1: Critical Command Handler Coverage (Week 1-2)
**Priority: HIGHEST**
- Implement `test_cmd_handlers_audio.c` (10+ tests)
- Implement `test_cmd_handlers_bt.c` (8+ tests)
- Implement `test_cmd_handlers_files.c` (6+ tests)
- Goal: 80% coverage of command error paths

### Phase 2: NVS Error Injection Extension (Week 2)
**Priority: HIGH**
- Extend `test_nvs_storage_errors.c` with 12+ additional failure scenarios
- Test partial failure cases (commit, blob writes)
- Verify state consistency after failures
- Goal: Full error path coverage in nvs_storage.c

### Phase 3: Beep Manager Edge Cases (Week 3)
**Priority: HIGH**
- Create `test_beep_manager_extended.c` (10+ tests)
- Test source restoration logic
- Test ring buffer drain behavior
- Test integration with audio processor
- Goal: Validate F1.x features (beep priority, source restore)

### Phase 4: BT Manager State Machines (Week 3-4)
**Priority: HIGH**
- Create `test_bt_scan.c` (8+ tests) - new module, no tests
- Extend `test_bt_manager_extended.c` (10+ tests)
- Extend `test_bt_connection_manager_extended.c` (6+ tests)
- Extend `test_bt_streaming_manager_extended.c` (6+ tests)
- Goal: Cover state machine edge cases and error paths

### Phase 5: I2S Manager Error Paths (Week 4)
**Priority: MEDIUM**
- Create `test_i2s_manager_errors.c` (8+ tests)
- Configuration error injection
- Runtime error handling
- Mock queue edge cases
- Goal: Full error path coverage in i2s_manager.c

### Phase 6: Audio Processor Edge Cases (Week 5)
**Priority: MEDIUM**
- Extend `test_audio_processor_extended.c` (6+ tests)
- Test watermark logic
- Test source priority/switching
- Test diagnostic accuracy
- Goal: Cover audio engine edge cases

### Phase 7: Integration Tests (Week 5-6)
**Priority: MEDIUM**
- Create `test_integration_flows.c` (4+ tests)
- Create `test_concurrency.c` (3+ tests)
- End-to-end command → BT → audio flows
- Race condition testing
- Goal: Validate component interactions

---

## 11. Coverage Metrics Summary

### Current Estimated Coverage by Category

| Category | Estimated Coverage | Test Files | Lines Tested / Total |
|----------|-------------------|------------|---------------------|
| **Utility Functions** | ~90% | 3 files | High |
| **Audio Resampling** | ~95% | 1 file | Excellent |
| **BT Pairing** | ~80% | 6 files | Good |
| **I2S Manager** | ~60% | 2 files | Fair |
| **Beep Manager** | ~40% | 1 file | Needs work |
| **NVS Storage** | ~50% | 2 files | Needs extension |
| **Command Handlers** | ~15% | 2 files | **Critical gap** |
| **BT State Machines** | ~35% | 3 files | Needs work |
| **Audio Processor Core** | ~50% | 5 files | Fair |
| **Integration** | ~20% | 1 file | Minimal |

### Target Coverage After Implementation

| Category | Target Coverage | New Tests |
|----------|----------------|-----------|
| **Command Handlers** | 80%+ | 24+ tests |
| **NVS Storage** | 85%+ | 12+ tests |
| **Beep Manager** | 80%+ | 10+ tests |
| **BT State Machines** | 70%+ | 30+ tests |
| **I2S Manager** | 80%+ | 8+ tests |
| **Integration** | 60%+ | 7+ tests |

**Total New Tests:** ~91 tests across 7 phases

---

## 12. TDD Workflow Recommendations

Per `.github/copilot-instructions.md` (Kent Beck TDD):

### For Each New Test:

1. **RED**: Write failing Unity test
   - Descriptive name: `test_<module>_should_<behavior>_when_<condition>`
   - Single behavior per test
   - Clear assertion messages

2. **GREEN**: Implement minimum code to pass
   - No speculative features
   - Only what test demands

3. **REFACTOR**: Improve structure with passing tests
   - Remove duplication
   - Clarify intent
   - Maintain test green state

### Example Test Template:

```c
// test/host_test/test_cmd_handlers_audio.c
#include "unity.h"
#include "cmd_handlers.h"
#include "test_mocks.h"

void setUp(void) {
    mock_reset_all();
}

void tearDown(void) {
    // Cleanup
}

void test_cmd_volume_should_reject_negative_value(void) {
    // Arrange
    cmd_context_t ctx = { .params = {"-1"}, .param_count = 1 };
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    TEST_ASSERT_EQUAL_STRING("ERR", mock_get_last_response_type());
    TEST_ASSERT_EQUAL_STRING("BAD_PARAM", mock_get_last_response_status());
}

void test_cmd_volume_should_accept_boundary_zero(void) {
    // Arrange
    cmd_context_t ctx = { .params = {"0"}, .param_count = 1 };
    
    // Act
    cmd_status_t result = cmd_handle_volume(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    TEST_ASSERT_EQUAL_STRING("OK", mock_get_last_response_type());
    TEST_ASSERT_EQUAL(0, mock_get_set_volume_value());
}
```

---

## 13. Conclusion

This analysis identifies **~91 new unit tests** needed across **7 priority phases** to achieve robust coverage of error paths, edge cases, and component integration.

**Immediate Actions (Week 1):**
1. ✅ Review this analysis with team
2. ⚠️ Create test files for command handlers (highest priority gap)
3. ⚠️ Set up CI to track coverage metrics
4. ⚠️ Begin Phase 1: Command handler tests

**Success Criteria:**
- Command handler coverage: **15% → 80%+**
- NVS error paths: **50% → 85%+**
- State machine edge cases: **35% → 70%+**
- Overall codebase confidence: **Significantly improved**

**Estimated Effort:** 5-6 weeks for full implementation at 1-2 tests/day pace.

---

**Document Status:** Initial Analysis Complete  
**Next Review:** After Phase 1 completion  
**Owner:** Development Team  
**Last Updated:** 2026-02-11

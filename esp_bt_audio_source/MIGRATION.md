# Migration Guide: ESP32 Bluetooth Audio Source

This document tracks breaking changes, new features, and migration notes across versions.

---

## Version 0.3.0 (February 2026) - PLAY Command and WAV Playback Removal

### Overview

Major simplification release removing PLAY command, WAV file playback, and SPIFFS filesystem support. Focus on streamlining architecture to core I2S audio streaming and synthesizer functionality.

**Binary size:** 922,144 bytes (901 KB, reduced from 927 KB in v0.2.0)  
**Flash space reclaimed:** 1 MB (SPIFFS partition removed)  
**Partition count:** 3 (nvs, phy_init, factory) - reduced from 4

### Breaking Changes

**⚠️ This release removes WAV playback functionality.**

#### Removed Features
1. **PLAY Command**
   - `PLAY [path]` command removed from command interface
   - No replacement - use I2S audio streaming instead

2. **WAV File Playback**
   - `audio_processor_play_wav()` function removed
   - play_manager component removed
   - AUDIO_SOURCE_WAV enum removed
   - All WAV-related test cases removed

3. **SPIFFS Filesystem**
   - SPIFFS partition removed from partition table
   - SPIFFS mount code removed from main/main.c
   - spiffs/ directory and assets removed
   - SPIFFS tooling removed (make_spiffs.py, flash_and_verify_spiffs.py)
   - FILES command still available (for NVS debugging)

#### Simplified Audio Architecture
- **Before:** 4 audio sources (WAV, I2S, synth, silence)
- **After:** 3 audio sources (I2S, synth, silence)
- **Source priority:** beep > I2S > synth (was: beep > WAV > I2S > synth)

### Migration Steps

#### For Applications Using PLAY Command
1. **Replace WAV playback with I2S streaming**
   - Convert audio files to I2S stream format
   - Use I2S interface for audio playback
   - Configure I2S pins: BCLK=GPIO26, WCLK=GPIO25, DATA_IN=GPIO22

2. **Update Command Scripts**
   - Remove all `PLAY` command invocations
   - Use `START` command for I2S streaming
   - Use `BEEP` command for tone generation

3. **Partition Table Changes (if using custom partitions)**
   - SPIFFS partition at 0x1C0000 (256 KB) removed
   - Factory partition remains at 0x10000
   - No action needed if using default partitions.csv

#### For Test Suites
1. **Host Tests**
   - All 259 host test cases updated and passing
   - WAV-related mocks removed
   - SPIFFS test infrastructure removed

2. **Device Tests**
   - test_app: Command interface tests (PLAY tests removed)
   - test_app2: Bluetooth integration tests (unchanged)
   - All tests passing on hardware

### What's Unchanged

- All Bluetooth functionality (SCAN, CONNECT, PAIR, etc.)
- I2S audio streaming
- Synthesizer and beep functionality
- Command interface protocol
- NVS storage and pairing database
- UART communication

### Technical Details

#### Code Removed
- `components/audio_processor/play_manager.c` and `.h` (~800 lines)
- `components/audio_processor/audio_processor.c` PLAY command handling (~200 lines)
- `main/commands.c` PLAY command implementation (~120 lines)
- `main/main.c` SPIFFS mount code (~30 lines)
- `spiffs/` directory and all WAV assets
- All WAV-related test cases (~400 lines across test suites)

#### Documentation Updated
- main/README.md - Audio architecture simplified to 3 sources
- docs/FS.md - Functional specification updated, PLAY command removed
- Root README.md - Project status updated
- ARCH.md - Architecture diagrams simplified (see historical sections marked)

### Validation

**Hardware Testing:**
- ESP32-D0WD-V3 (revision v3.1)
- Boots cleanly without SPIFFS errors
- All subsystems operational (cmd=1, bt=1, audio=1)
- Partition table verified: 3 partitions only
- No SPIFFS mount attempts in boot log

**Test Results:**
- Host tests: 259/259 passing
- Device tests: Updated and passing
- Build: Clean with no errors

### Benefits

1. **Simplified Architecture**
   - Fewer audio pipeline states
   - Clearer source priority model
   - Reduced complexity in audio_processor

2. **Reduced Flash Usage**
   - 1 MB reclaimed (SPIFFS partition removed)
   - Binary size reduced by ~5 KB
   - More space for application code

3. **Maintainability**
   - ~1500 lines of code removed
   - Fewer subsystems to maintain
   - Clearer separation of concerns

### For More Information

- See `code_review/REMOVE_PLAY_TODO.md` for detailed removal tracking
- See `memory.md` for phase-by-phase completion log
- Git commits: Phases 1-6 (commits c0772235 through f37f44da)

---

## Version 0.2.0 (February 2026) - CODE_REVIEW2 Release

### Overview

Major architecture cleanup and stabilization release. Focus on code quality, layering, and configurability. All 385 automated tests passing (190 host + 195 device).

**Binary size:** 927,920 bytes (906 KB, +21KB from v0.1.0, 48% partition free)

### Breaking Changes

**None.** This release is **backward compatible** with existing deployments.

- Existing NVS data preserved (no schema changes to pairing database or pin overrides)
- All existing commands remain functional
- Default behavior unchanged (audio autostart still enabled by default)

### New Features

#### 1. Audio Autostart Control (Phase 3)

**New NVS Key:**
- **Namespace:** `bt_audio_cfg`
- **Key:** `audio_autostart`
- **Type:** `int32_t` (0 = disabled, 1 = enabled)
- **Default:** Determined by `CONFIG_AUDIO_AUTOSTART_DEFAULT` (defaults to enabled/1)

**New Command:**
```
AUDIO_AUTOSTART get    - Query current autostart setting
AUDIO_AUTOSTART on     - Enable autostart (persisted to NVS, requires reboot)
AUDIO_AUTOSTART off    - Disable autostart (persisted to NVS, requires reboot)
```

**Response:**
```
OK|AUDIO_AUTOSTART|<on|off>
```

**Use Cases:**
- **Battery-powered devices:** Disable autostart to save power at boot
- **Test harnesses:** Control initialization timing
- **Field customization:** Change boot behavior without recompiling

**Migration:** No action needed. If `audio_autostart` key doesn't exist in NVS, system uses Kconfig default (enabled). Users can optionally set preference via command.

#### 2. Kconfig Compile-Time Defaults (Phase 3)

**New Kconfig Options (menuconfig):**

```
Audio Configuration
├── CONFIG_AUDIO_DEFAULT_SAMPLE_RATE    (default: 44100 Hz)
│   Options: 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000
├── CONFIG_AUDIO_DEFAULT_VOLUME         (default: 80, range 0-100)
├── CONFIG_AUDIO_DEFAULT_BIT_DEPTH      (default: 16-bit)
│   Options: 16, 24, 32
└── CONFIG_AUDIO_AUTOSTART_DEFAULT      (default: yes)
    Determines initial autostart state if not set in NVS
```

**Configuration Hierarchy (highest precedence first):**
1. **NVS runtime overrides** (I2S pins via I2S_PINS command, autostart via AUDIO_AUTOSTART)
2. **Kconfig compile-time defaults** (set via `idf.py menuconfig`)
3. **Hard-coded fallbacks** (in audio_processor.c)

**Migration:** Existing projects will use Kconfig defaults. To customize:
```bash
cd esp_bt_audio_source
idf.py menuconfig
# Navigate to: Component config → Audio Configuration
# Set desired defaults
idf.py build flash
```

#### 3. Enhanced Documentation (Phase 5)

**New/Updated Documentation:**
- `ARCH.md`: Comprehensive architecture diagrams (component hierarchy, data flows, init sequences)
- `code_review/MANUAL_TEST_CHECKLIST.md`: On-device validation procedures (8 sections, ~405 lines)
- `MIGRATION.md`: This file (version tracking and upgrade notes)

**Command Documentation:** `README.md` now includes complete command reference table with all parameters and examples.

#### 4. CI Enforcement Tools (Phase 7)

**New Script:** `tools/ci_check_main_layering.sh`

Enforces architectural constraints on main.c:
- No direct ESP-IDF BT API calls (except `esp_bt_controller_mem_release`)
- No forbidden UART driver calls (only install/diagnostics allowed)
- No redundant NVS init (use `nvs_storage_init()` only)
- No obvious printf format errors

**Usage:**
```bash
./tools/ci_check_main_layering.sh
# Exit 0 = all checks passed
# Exit 1 = violations found
```

### Bug Fixes

#### Phase 1: Critical Fixes

1. **Invalid preprocessor guards (P0 CRITICAL)**
   - **Issue:** main.c used `#ifdef esp_rom_printf` (function name, not preprocessor token)
   - **Impact:** Undefined behavior on different ESP32 targets
   - **Fix:** Changed to `#ifdef CONFIG_IDF_TARGET_ESP32` (correct target detection)
   - **Commit:** ea7f0a40

2. **Init order contradiction (P0 CRITICAL)**
   - **Issue:** BT initialized before CMD layer, causing command unavailability
   - **Impact:** SCAN/PAIR commands not ready when BT becomes operational
   - **Fix:** Moved `cmd_init()` BEFORE `bt_manager_init()` (control plane before data plane)
   - **Commit:** 26b3eccd

#### Phase 2: Layering Stabilization

3. **Dangerous uart_driver_delete (P1 LAYERING)**
   - **Issue:** `cmd_init()` called `uart_driver_delete()`, breaking esp-console and logging
   - **Impact:** Logging and console unusable after CMD init
   - **Fix:** Removed delete call; main.c owns single UART driver install
   - **Commit:** 1e06d7ed

4. **Redundant NVS init (P1 LAYERING)**
   - **Issue:** Both main.c and bt_manager called `nvs_flash_init()`
   - **Impact:** Potential version mismatch errors, unclear ownership
   - **Fix:** main.c owns single `nvs_storage_init()` call (wraps nvs_flash_init)
   - **Commit:** 1e06d7ed

### Code Quality Improvements

#### Phase 4: Cleanup and Polish

- Removed unused `BT_APP_TASK_STACK_SIZE` define (dead code)
- Removed unnecessary `while(1)` loop in `app_main()` (FreeRTOS scheduler keeps system alive)
- Fixed all clang-tidy warnings in application code (0 warnings remaining)
- Added comprehensive WHY comments explaining architectural decisions
- Documented error handling policy (platform fail-fast, subsystems graceful degradation)

#### Phase 6: Testing and Validation

- **Test coverage:** 385/385 tests passing (100%)
  - 190 host tests (CTest, 1.87s runtime)
  - 195 device tests (Unity, 9 suites, ~4.5min runtime)
- **Performance validation:** Binary +2.4% growth justified by features
- **Manual test checklist:** Created comprehensive on-device validation procedures
- **Code quality:** Zero TODO/FIXME in production code, all error paths logged

### Upgrade Instructions

#### From v0.1.0 to v0.2.0

**No special steps required.** Flash new firmware:

```bash
cd esp_bt_audio_source
git pull origin master
idf.py build flash monitor
```

**NVS Data:** Existing NVS data (pairing database, I2S pin overrides) is preserved.

**Optional Configuration:**

1. **Customize Kconfig defaults:**
   ```bash
   idf.py menuconfig
   # Component config → Audio Configuration
   # Set sample rate, volume, bit depth, autostart default
   idf.py build flash
   ```

2. **Runtime autostart control:**
   ```
   AUDIO_AUTOSTART get     # Check current setting
   AUDIO_AUTOSTART off     # Disable autostart (persists to NVS)
   # Reboot required for change to take effect
   ```

3. **Verify configuration:**
   ```
   # After boot, check DIAG output for config confirmation:
   DIAG|AUDIO|STATUS|autostart=1|volume=80|rate=44100|bits=16
   ```

### Known Issues

**None affecting core functionality.**

**Informational:**
- Crystal frequency divergence: Some hardware shows 41.01MHz vs expected 40MHz (informational, no impact)
- ESP-IDF duplicate definition warning for `ESP_EVENT_ANY_ID` (harmless, planned cleanup)

### Deprecation Notices

**None.** All existing commands and APIs remain supported.

### Future Compatibility

**Dual-ESP32 Architecture (Planned Q2-Q4 2026):**

The v0.2.0 architecture is designed to support future migration to dual-ESP32 topology (Control ESP32 + Audio ESP32). Current layering ensures:
- Component boundaries are clean (minimal changes needed)
- Command interface can be relayed between ESP32s
- Configuration remains centralized
- Migration will be low-risk (weeks vs months of refactoring)

See `ARCH.md` section "Future Evolution: Single-ESP32 to Dual-ESP32 Architecture" for details.

---

## Version 0.1.0 (November 2025) - Initial Stable Release

### Overview

First stable release with comprehensive test coverage and audio pipeline hardening.

**Key Features:**
- A2DP audio source (Bluetooth streaming to speakers/headphones)
- I2S audio input
- Serial command interface (UART)
- Multiple device pairing support
- WAV file playback from SPIFFS
- Tone generation (beeps)

**Test Coverage:**
- 130 total tests (22 host + 108 device)
- All tests passing
- Full regression suite automation

### Known Issues (Resolved in v0.2.0)

1. **Invalid preprocessor guards** → Fixed in v0.2.0 Phase 1
2. **Init order contradiction** → Fixed in v0.2.0 Phase 1
3. **Dangerous uart_driver_delete** → Fixed in v0.2.0 Phase 2
4. **Redundant NVS init** → Fixed in v0.2.0 Phase 2

---

## Migration Support

For questions or issues during migration:
1. Check this document for known changes
2. Review `README.md` for current command reference
3. Check `ARCH.md` for architecture explanations
4. Run automated tests: `python tools/run_all_tests.py`
5. Use manual test checklist: `code_review/MANUAL_TEST_CHECKLIST.md`

**CI Enforcement:** Run `./tools/ci_check_main_layering.sh` to verify architectural compliance.

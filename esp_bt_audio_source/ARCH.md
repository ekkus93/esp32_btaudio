# ESP32 Bluetooth + WiFi Split Architecture

## Overview

This project uses two ESP32 devices to overcome the limitations of running WiFi and Bluetooth Classic simultaneously on a single ESP32:

1. **ESP32 #1: Bluetooth Audio Source**
   - Handles all Bluetooth A2DP audio streaming
   - Receives audio from ESP32 #2 via I2S
   - Streams audio to Bluetooth speakers/headphones

2. **ESP32 #2: WiFi and Web Interface**
   - Provides WiFi connectivity (Access Point or client)
   - Hosts web server for user interface
   - Sends audio data to Bluetooth ESP32 via I2S
   - Controls Bluetooth ESP32 via UART

This separation ensures better reliability and performance than trying to run both wireless stacks on a single ESP32.

## Detailed Architecture

### ESP32 #1 (Bluetooth-focused)

**Primary Responsibilities:**
- Connect to Bluetooth speakers/headphones using A2DP profile
- Receive and buffer audio data from ESP32 #2
- Stream audio data to connected Bluetooth devices
- Accept control commands from ESP32 #2 (via UART)
- Send status updates to ESP32 #2 (via UART)

**Key Components:**
- Bluetooth Classic A2DP source profile
- I2S slave receiver for audio input
- UART for command/control interface
- Optional: status LEDs

### ESP32 #2 (WiFi-focused)

**Primary Responsibilities:**
- Provide WiFi Access Point or client connection
- Host web server for user interface
- Generate or process audio data
- Send audio data to ESP32 #1
- Send control commands to ESP32 #1
- Receive status updates from ESP32 #1

**Key Components:**
- WiFi stack (AP or client mode)
- Web server with HTML/CSS/JS interface
- I2S master transmitter for audio output
- UART for command/control interface
- Optional: Additional audio processing (effects, volume control)

## Communication Interfaces

### 1. I2S Audio Interface

Used for high-quality digital audio transmission between the ESP32s:

**Connection Diagram:**
```
ESP32 #2 (WiFi)                     ESP32 #1 (Bluetooth)
----------------                    -------------------
I2S_BCK (GPIO26, Master) ---------> I2S_BCK (GPIO26, Slave)
I2S_WS (GPIO25, Master)  ---------> I2S_WS (GPIO25, Slave)
I2S_DO (GPIO22, Master)  ---------> I2S_DI (GPIO22, Slave)
GND                      ---------> GND
```

**Note:** For ESP32 WROOM32 modules, these are recommended GPIO pins for I2S. They avoid pins used for boot modes or connected to internal flash.

### 2. UART Control Interface

Used for commands and status updates between the ESP32s:

**Connection Diagram:**
```
ESP32 #2 (WiFi)                 ESP32 #1 (Bluetooth)
----------------                -------------------
TX (GPIO17)       ------------> RX (GPIO16)
RX (GPIO16)       <------------ TX (GPIO17)
GND               ------------> GND
```

**Note:** These UART pins (GPIO16/17) are chosen to avoid conflicts with other functions on ESP32 WROOM32 modules. For higher reliability, use a baud rate of 115200.

## Audio Pipeline

1. Audio is generated or processed on ESP32 #2 (WiFi)
2. Audio data is sent via I2S to ESP32 #1 (Bluetooth)
3. ESP32 #1 receives the audio via I2S and buffers it
4. ESP32 #1 streams the audio via A2DP to connected Bluetooth speakers/headphones

This separation allows each ESP32 to focus on its primary wireless protocol, ensuring better performance and reliability.

## Future Expansion Possibilities

- Add a microSD card to ESP32 #2 for audio file playback
- Implement streaming audio from web sources on ESP32 #2
- Add audio effects processing on ESP32 #2
- Use additional GPIO pins for hardware controls (buttons, rotary encoders)
- Add a display to ESP32 #2 for local user interface
- Implement battery level monitoring if devices are battery powered
- Add OTA (Over-The-Air) updates for the WiFi ESP32
- Create mobile app interface for remote control
- Add multi-room audio synchronization with multiple BT transmitters

## Recommended Hardware Configuration

### ESP32 #1 (Bluetooth)
- ESP32 DevKit or similar
- Connected to power source
- Optional: Power LED indicator
- Optional: Status LED for Bluetooth connection

### ESP32 #2 (WiFi)
- ESP32 DevKit or similar with more flash memory (for web interface)
- Connected to power source
- Optional: SSD1306 OLED display for status
- Optional: microSD card for audio storage
- Optional: Control buttons or rotary encoder for volume control

## Software Architecture on ESP32 #1 (Bluetooth)

### main.c Responsibilities

**Purpose:** Bootstrap policy layer - orchestrates subsystem initialization in correct order

**Core Responsibilities:**
1. **Early boot diagnostics:** DIAG markers for test harness synchronization (EARLY_BOOT_MARKER, UART_READY_FOR_CMD_LAYER)
2. **Platform services initialization:**
   - BLE memory release (A2DP-only optimization)
   - UART driver install (single install, never delete)
   - NVS initialization (single call to `nvs_storage_init()`)
3. **Subsystem composition:**
   - Command interface (control plane)
   - Bluetooth manager (data plane)
   - Audio processor (media plane)
4. **Configuration policy:**
   - Load audio boot config (sample rate, volume, pins)
   - Check autostart flags (NVS overrides Kconfig)
   - Apply runtime customization

**What main.c IS:**
- **Bootstrap orchestrator** - "when to initialize what"
- **Policy layer** - decisions about init order, defaults, autostart
- **Diagnostic gateway** - early boot markers for test automation
- **Configuration loader** - centralized config policy (Kconfig + NVS)

**What main.c IS NOT:**
- ❌ **Not** a Bluetooth implementation (no esp_a2d_*, esp_avrc_*, esp_bt_gap_*, esp_bluedroid_* calls)
- ❌ **Not** a command processor (delegates to cmd_handlers)
- ❌ **Not** an audio engine (delegates to audio_processor)
- ❌ **Not** a state machine (delegates to bt_manager)

**Line Budget:** ~320 lines (Feb 2026) - kept small by delegating all logic to components

**Architectural Principle:** main.c is **thin orchestration** - it knows WHEN to call what, but components know HOW to do it.

### main.c Bootstrap (current size: ~319 lines)
- **Purpose:** Clean entry point for ESP32 firmware
- **Responsibilities:**
  - Early boot diagnostics and UART initialization
  - **NVS (Non-Volatile Storage) initialization** - SINGLE call to `nvs_storage_init()` early in boot
  - Memory optimization (`esp_bt_controller_mem_release` for BLE)
  - Delegate ALL Bluetooth initialization to `bt_manager`
  - Initialize command interface and audio processor
  - Auto-configure I2S pins from NVS storage
  - Load audio boot config with Kconfig defaults + NVS overrides
  - Check autostart flag before initializing audio
- **What main.c MUST NOT contain:**
  - Direct Bluetooth API calls (esp_a2d_*, esp_avrc_*, esp_bt_gap_*, esp_bluedroid_*)
  - Bluetooth state machines or callbacks
  - Device-specific BT logic
  - Command processing logic (delegates to cmd_handlers)
  - Audio pipeline logic (delegates to audio_processor)
- **Policy:** ALL Bluetooth logic lives in the `bt_manager` component (enforced by CI)

### bt_manager Component (Single Source of Truth for BT)
- **Purpose:** Centralized Bluetooth lifecycle management
- **Responsibilities:**
  - Initialize Bluetooth controller and Bluedroid stack
  - Register ALL Bluetooth callbacks (A2DP, AVRCP, GAP)
  - Manage Bluetooth state machines (connection, pairing, streaming)
  - Handle device discovery and pairing
  - Coordinate with audio_processor for streaming
- **Sub-components:**
  - A2DP Source profile implementation
  - AVRCP profile for remote control
  - GAP for device discovery and pairing
  - Connection manager for device lifecycle
  - Streaming manager for audio data flow

### Audio Processor Component
- **Purpose:** Audio pipeline orchestration
- **Responsibilities:**
  - I2S slave configuration and management
  - Audio buffer management and ring buffer
  - Audio queue for multiple sources (I2S, WAV, beep, synth)
  - Coordinate with bt_manager for A2DP streaming
  - Generate keepalive tones and beeps
  - WAV file playback from SPIFFS

### Command Interface Component
- **Purpose:** UART-based control protocol
- **Responsibilities:**
  - Command parser and dispatcher
  - Status reporting to ESP32 #2
  - Error handling and validation
  - Command processing task (polls every 20ms)

## Initialization Ownership and Layering

### NVS (Non-Volatile Storage) Ownership
**Decision:** main.c owns NVS initialization (Option A)

**Rationale:**
- NVS is a **platform service** (like memory, filesystems) - it should be initialized once at boot
- Multiple components use NVS (bt_manager, audio_processor, nvs_storage abstraction)
- **Single ownership** prevents redundant init calls and race conditions
- Early initialization ensures NVS is ready before any component needs it
- Follows ESP-IDF best practice: platform services initialized in app_main()

**Implementation:**
- main.c calls `nvs_storage_init()` **once** early in app_main() (after BLE mem release, before BT init)
- `nvs_storage_init()` handles version mismatch and erase-on-error internally
- bt_manager, audio_processor, and other components **assume NVS is already initialized**
- Components call nvs_storage_get/set_* functions directly without re-initializing

**What NOT to do:**
- ❌ bt_manager must NOT call `nvs_flash_init()` or `nvs_storage_init()`
- ❌ No component should redundantly initialize NVS "just in case"
- ❌ No lazy initialization - NVS must be ready before subsystems start

**Benefits:**
- Clear ownership (main.c owns platform, components own logic)
- Prevents duplicate initialization
- Easier to reason about boot sequence
- Supports future two-ESP32 architecture (each ESP32's main.c initializes its own NVS)

### UART (Console) Ownership
**Decision:** Option C - Split ownership: main.c installs early for diagnostics, cmd_init assumes ready

**Rationale:**
- Early boot diagnostics are **critical** for programmatic test harness and host injectors
- Diagnostic markers (DIAG|BOOT|EARLY_BOOT_MARKER, DIAG|BOOT|UART_READY_FOR_CMD_LAYER) must appear before subsystem init
- printf() and esp_rom_printf() alone are **insufficient** - they are buffered/unreliable for host capture
- UART driver installation is required for unbuffered uart_write_bytes() diagnostic output
- cmd_init() and other components need UART already operational for synchronous I/O
- **Single install** at boot avoids driver reinstall complexity and state confusion

**Implementation:**
- main.c installs UART driver **once** early in app_main() (after early boot markers, before NVS)
- Installation is **best-effort** with error checking but continues on failure
- main.c uses uart_write_bytes() for critical diagnostic markers before subsystems init
- cmd_init() and command_interface **assume UART is already installed** - no reinstall
- command_interface checks `uart_is_driver_installed()` before writes (defensive but not required)

**What NOT to do:**
- ❌ main.c must NOT call `uart_driver_delete()` after install (breaks logging, esp-console, cmd layer)
- ❌ cmd_init() must NOT reinstall UART driver (causes state reset, double-init)
- ❌ No component should assume UART is uninstalled and try to install it

**Boot sequence:**
1. Very early: printf/esp_rom_printf for EARLY_BOOT_MARKER (before driver)
2. Early: main.c installs UART driver for unbuffered diagnostics
3. Early: main.c writes UART_READY_FOR_CMD_LAYER via uart_write_bytes()
4. Platform init: NVS, BT manager (all emit diagnostic markers)
5. cmd_init: command interface ready (assumes UART operational)

**Benefits:**
- Early diagnostics reliable and visible to test harness
- UART installed exactly once - no reinstall complexity
- cmd_init can immediately read/write without driver setup
- Clear contract: main.c owns platform UART, cmd_init owns command protocol
- Supports test injection and automated capture from first boot moment

### Initialization Order and Rationale

**Actual init sequence (as of Jan 2026, Phase 2 Task 2.6):**
```
1. Early diagnostics (UART install)
2. NVS init (platform service)
3. CMD init (control plane ready)
4. CMD task start (command interface processing)
5. BT manager init (data plane ready - NOW commands work!)
6. Audio init/start (if autostart enabled)
```

**Key Architectural Principle: Control Plane → Data Plane**

**Why CMD before BT?**
- **Control plane availability:** CMD interface must be ready BEFORE subsystems initialize
- **Command interface ready early:** Allows immediate SCAN/PAIR commands when BT becomes ready
- **Prevents confusion:** BT "ready" with no way to control it is misleading
- **Separation of concerns:** Communication infrastructure (CMD) separate from business logic (BT, audio)
- **Test harness friendly:** Commands available from earliest possible moment
- **Human debugging:** If BT init fails, commands still work for diagnostics

**Why NVS before everything?**
- **Platform dependency:** ALL subsystems need NVS (BT pairing data, audio config, command settings)
- **Fail-fast on critical errors:** NVS failure indicates corrupted flash/partition - nothing will work anyway
- **Single initialization:** Avoids race conditions, prevents redundant init calls
- **Configuration loading:** BT and audio need NVS data during their init

**Why Audio last?**
- **Optional subsystem:** Audio can be disabled via autostart flag (BT/CMD remain functional)
- **Resource intensive:** DMA buffers, GPIO pins, interrupts - only allocate if needed
- **Depends on BT:** Streaming audio requires BT connection (though audio can work standalone for test tones)
- **Deployment flexibility:** Headless mode (no audio), diagnostic mode (BT + CMD only), full mode

**Init Order Contradiction Fixed (Phase 2, Task 2.6):**
- **OLD (incorrect):** NVS → **BT** → CMD → Audio
  - Problem: Comment said "BT ready for SCAN/PAIR via commands" but CMD not ready yet - CONFUSING
  - Impact: Misleading state, potential race conditions if BT events trigger before CMD ready
- **NEW (correct):** NVS → **CMD** → BT → Audio
  - Benefit: Control plane available before data plane, clear layering, no race conditions

**Error Handling Philosophy:**
- **Platform services (NVS, UART):** Fail-fast with ESP_ERROR_CHECK
  - Rationale: System cannot operate without these - partial state is worse than clean abort
- **Subsystems (BT, Audio, CMD):** Graceful degradation with error logging
  - Rationale: Device still useful for diagnostics, testing, partial functionality
  - Example: BT fails → audio test tones still work, CMD interface available for diagnosis
  - Example: CMD fails → device continues boot, BT/Audio may still function for auto-connect scenarios

### Policy vs Platform Separation

**Platform Layer (owned by main.c):**
- **What:** Core ESP32 services that ALL components depend on
- **Responsibilities:**
  - Memory management (heap, PSRAM, BLE mem release)
  - Flash storage (NVS initialization)
  - Console I/O (UART driver install)
  - Early diagnostics (DIAG markers for test automation)
- **Characteristics:**
  - Initialized ONCE at boot
  - Fail-fast on errors (ESP_ERROR_CHECK)
  - No retry logic (hardware/partition issues don't self-heal)
  - Owned by main.c app_main()
- **Examples:**
  - `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` - platform memory optimization
  - `uart_driver_install()` - platform I/O service
  - `nvs_storage_init()` - platform persistence service

**Policy Layer (orchestrated by main.c):**
- **What:** Business decisions about WHEN and HOW to initialize subsystems
- **Responsibilities:**
  - Init order sequencing (control plane → data plane)
  - Configuration loading (Kconfig defaults + NVS overrides)
  - Autostart decisions (should audio start at boot?)
  - Resource allocation defaults (sample rate, volume, pins)
- **Characteristics:**
  - Configurable at compile-time (Kconfig) and runtime (NVS)
  - Documents architecture intent (WHY this order?)
  - Thin orchestration (delegates HOW to components)
  - Owned by main.c app_main()
- **Examples:**
  - `load_audio_boot_config()` - centralized audio policy
  - `nvs_storage_get_audio_autostart()` - runtime policy check
  - CMD before BT init - architectural policy decision

**Application Layer (implemented by components):**
- **What:** Actual business logic and subsystem implementations
- **Responsibilities:**
  - Bluetooth lifecycle (bt_manager)
  - Audio pipeline (audio_processor)
  - Command protocol (cmd_handlers)
  - Device-specific logic
- **Characteristics:**
  - Stateful (maintains internal state machines)
  - Complex (hundreds of lines per component)
  - Testable in isolation (unit tests, mocked dependencies)
  - Assumes platform services are ready
  - Graceful degradation on errors (log + continue)
- **Examples:**
  - `bt_manager_init()` - BT stack initialization, state machines, callbacks
  - `audio_processor_init()` - I2S config, DMA buffers, audio queues
  - `cmd_init()` - command parser, dispatcher, task spawning

**Why This Separation Matters:**
1. **Clarity:** Each layer has clear responsibilities - no "God objects"
2. **Testability:** Application layer can be tested with mocked platform
3. **Portability:** Platform layer is ESP32-specific, application layer could be ported
4. **Maintainability:** New developers understand what goes where
5. **Future architecture:** Two-ESP32 split will have separate platform layers, shared application logic
6. **Prevents drift:** Clear rules prevent mixing platform and application code in main.c

**Anti-pattern to Avoid:**
- ❌ **Mixing layers in main.c:**
  - DO NOT put Bluetooth state machines in main.c (application logic → bt_manager)
  - DO NOT put command parsing in main.c (application logic → cmd_handlers)
  - DO NOT put I2S management in main.c (application logic → audio_processor)
- ❌ **Platform calls in application components:**
  - DO NOT call `nvs_flash_init()` in bt_manager (platform → main.c owns)
  - DO NOT reinstall UART in cmd_init (platform → main.c owns)
- ❌ **Policy decisions in platform code:**
  - DO NOT hard-code audio defaults in audio_processor (policy → main.c config)
  - DO NOT decide init order in components (policy → main.c orchestration)

**Enforcement:**
- Code reviews check for layering violations
- CI checks prevent direct BT API calls in main.c
- Comments in main.c explain WHY each init step happens
- ARCH.md documents the separation for new contributors



## Software Architecture on ESP32 #2 (WiFi)

### WiFi Core
- Access Point or client mode
- Connection management
- Network security

### Web Server
- HTML/CSS/JS interface
- Control endpoints (REST API)
- WebSocket for real-time updates

### Audio Processing
- Audio generation or streaming
- I2S master configuration
- Audio effects (optional)

### UART Command Interface
- Command generation
- Status reception
- Error handling

### User Interface
- Display driver (if display is used)
- Button/encoder handling
- User feedback (LEDs, display)

---

## Future Evolution: Single-ESP32 to Dual-ESP32 Architecture

### Current State (Single ESP32)

The current codebase runs all functionality on a single ESP32:
- **Bluetooth Classic** (A2DP source for audio streaming)
- **Command interface** (UART-based control and diagnostics)
- **Audio processor** (I2S management, WAV playback, tone generation)
- **NVS storage** (configuration persistence)

This single-ESP32 architecture is functional but has limitations:
- **Cannot run WiFi + Bluetooth Classic simultaneously** (ESP32 hardware constraint)
- **Limited CPU/memory** for advanced audio processing + network features
- **Single point of failure** - any subsystem crash affects entire device

### Future Architecture: Dual-ESP32 Split

The architecture is designed to support future migration to two cooperating ESP32 devices for enhanced capabilities:

#### Control ESP32 (Primary)
**Responsibilities:**
- **Command interface** (UART control from host or user interface)
- **Minimal Bluetooth** (device discovery, pairing, connection management only)
- **NVS storage** (configuration, pairing database)
- **Inter-ESP32 communication** (command relay, status aggregation)
- **Optional: WiFi stack** (web UI, network streaming, OTA updates)

**Components migrated from current main.c:**
- `cmd_init()` + `cmd_process_task()` - Full command layer
- `bt_manager_init()` - Connection management only (no audio streaming)
- `nvs_storage_init()` - Configuration persistence
- UART driver installation - Early diagnostics and control

**Components NOT on Control ESP32:**
- Audio processing (delegated to Audio ESP32)
- A2DP audio streaming (delegated to Audio ESP32)

#### Audio ESP32 (Secondary)
**Responsibilities:**
- **A2DP audio streaming** (high-bandwidth Bluetooth audio to speakers/headphones)
- **Audio processing** (I2S, WAV playback, tone generation, effects)
- **Real-time audio** (low-latency DMA, interrupt-driven I2S)
- **Receives commands from Control ESP32** (play, stop, volume, etc.)
- **Sends status to Control ESP32** (playback state, errors)

**Components migrated from current main.c:**
- `audio_processor_init()` + `audio_processor_start()` - Full audio stack
- `bt_manager_start_audio()` - A2DP streaming only (no pairing/discovery)
- I2S driver - Audio I/O hardware

**Components NOT on Audio ESP32:**
- User command interface (handled by Control ESP32)
- WiFi stack (if needed, runs on Control ESP32)
- Configuration storage (Control ESP32 is source of truth)

### Communication Protocol (Inter-ESP32)

#### Physical Interface
**UART** (high-speed serial, 921600 baud recommended):
```
Control ESP32          Audio ESP32
-------------          -----------
TX (GPIO17)  -------> RX (GPIO16)
RX (GPIO16)  <------- TX (GPIO17)
GND          -------> GND
```

**Optional I2S** (for audio data from Control ESP32 → Audio ESP32):
```
Control ESP32 (I2S Master)    Audio ESP32 (I2S Slave)
--------------------------    -----------------------
I2S_BCK (GPIO26)  ---------> I2S_BCK (GPIO26)
I2S_WS (GPIO25)   ---------> I2S_WS (GPIO25)
I2S_DO (GPIO22)   ---------> I2S_DI (GPIO22)
GND               ---------> GND
```

#### Command Protocol (Control → Audio ESP32)

**Format:** Same as current command interface (newline-terminated text)

**Examples:**
```
AUDIO_START\n
AUDIO_STOP\n
VOLUME 80\n
PLAY /spiffs/beep.wav\n
BEEP 440 500\n
```

**Rationale:** Reuse existing command parser on Audio ESP32 for consistency. Control ESP32 becomes a command relay.

#### Status Protocol (Audio → Control ESP32)

**Format:** Structured status messages (same as current DIAG output)

**Examples:**
```
DIAG|AUDIO|STATUS|running=1|volume=80\n
DIAG|AUDIO|PLAYBACK|file=/spiffs/beep.wav|playing=1\n
DIAG|BT|AUDIO_STATE|STARTED\n
```

**Rationale:** Control ESP32 aggregates status from Audio ESP32 and exposes to host/UI.

### Migration Path from Current Single-ESP32

The current codebase is architected to support this split with **minimal changes**:

#### Phase 1: Current State (Single ESP32)
- ✅ **Already done:** Clean layering (main.c → bt_manager/audio_processor/cmd_interface)
- ✅ **Already done:** Clear component ownership (no cross-layer violations)
- ✅ **Already done:** Command-driven architecture (all operations via cmd interface)

#### Phase 2: Preparation (Still Single ESP32)
- Create `inter_esp_comm` component (UART protocol abstraction)
- Add command relay mode to `cmd_interface` (forward vs execute locally)
- Add status aggregation to `cmd_interface` (merge local + remote status)
- **No behavioral changes** - still single ESP32, but components are relay-ready

#### Phase 3: Physical Split (Dual ESP32)
- **Control ESP32 firmware:**
  - `main.c` calls: `cmd_init()`, `bt_manager_init()` (discovery only), `nvs_storage_init()`
  - Relay audio commands to Audio ESP32 via UART
  - Aggregate status from Audio ESP32 + local BT manager
  
- **Audio ESP32 firmware:**
  - `main.c` calls: `audio_processor_init()`, `cmd_init()` (receive mode)
  - Execute audio commands locally
  - Send status updates to Control ESP32 via UART

- **Shared code:** Same component libraries (bt_manager, audio_processor, cmd_interface)
  - Configuration at build time (e.g., `CONFIG_ESP_ROLE_CONTROL` vs `CONFIG_ESP_ROLE_AUDIO`)

#### Phase 4: Enhanced Features (Post-Split)
- Add WiFi to Control ESP32 (web UI, network streaming)
- Add advanced audio processing to Audio ESP32 (effects, EQ)
- Add OTA updates via Control ESP32 (update both ESP32s)

### Why Clean Layering Matters NOW

The **current single-ESP32 architecture** enforces clear ownership:

| Component | Owner | Rationale |
|-----------|-------|-----------|
| UART driver install | `main.c` | Platform service - needed for early diagnostics |
| UART usage (read/write) | `cmd_interface` | Control plane - all commands flow through here |
| BT controller init | `bt_manager` | Subsystem - encapsulates all BT operations |
| Audio I2S init | `audio_processor` | Subsystem - encapsulates all audio operations |
| NVS init | `main.c` via `nvs_storage` | Platform service - single source of truth |

**Without this layering**, the dual-ESP32 split would require:
- ❌ Untangling spaghetti code (BT calls in main.c, UART in audio, etc.)
- ❌ Rewriting command parsing (different on each ESP32)
- ❌ Debugging ownership conflicts (who owns what hardware?)

**With clean layering**, the dual-ESP32 split is:
- ✅ **Straightforward** - Each component already knows its boundaries
- ✅ **Low-risk** - No major refactoring, just configuration changes
- ✅ **Testable** - Same components, different deployment topology

### Main.c Component Migration Table

This table shows where each current `main.c` initialization call will move in the dual-ESP32 architecture:

| Current main.c Call | Control ESP32 | Audio ESP32 | Notes |
|---------------------|---------------|-------------|-------|
| `esp_bt_controller_mem_release(BLE)` | ✅ Yes | ✅ Yes | Both need Classic BT, not BLE |
| `uart_driver_install()` | ✅ Yes | ✅ Yes | Both need UART (host control / inter-ESP32 comm) |
| `nvs_storage_init()` | ✅ Yes | ❌ No | Control ESP32 is source of truth for config |
| `cmd_init()` | ✅ Yes (host mode) | ✅ Yes (relay mode) | Control receives host cmds; Audio receives relay cmds |
| `cmd_process_task()` | ✅ Yes | ✅ Yes | Both process commands (different sources) |
| `bt_manager_init()` | ✅ Yes (discovery/pair) | ⚠️ Partial (audio only) | Split BT manager into control + audio aspects |
| `audio_processor_init()` | ❌ No | ✅ Yes | Audio ESP32 owns all audio operations |
| `audio_processor_start()` | ❌ No | ✅ Yes | Audio ESP32 owns all audio operations |
| `load_audio_boot_config()` | ⚠️ Fetch from NVS | ⚠️ Receive from Control | Control ESP32 sends config to Audio ESP32 at boot |

**Legend:**
- ✅ Yes = Component runs on this ESP32
- ❌ No = Component does NOT run on this ESP32
- ⚠️ Partial = Component is split or adapted for this ESP32

### Benefits of This Evolution Plan

1. **Preserves Investment:** Current code remains usable (shared components)
2. **Incremental Migration:** Can develop/test dual-ESP32 without breaking single-ESP32
3. **Clear Boundaries:** Each ESP32 has well-defined responsibilities
4. **Future-Proof:** Architecture supports adding more features (WiFi, advanced audio)
5. **Testable:** Can test Control ESP32 and Audio ESP32 independently with mocks

### Timeline

- **Now (Phase 1):** ✅ Clean layering complete (CODE_REVIEW2 work)
- **Q2 2026 (Phase 2):** Create `inter_esp_comm` component, add relay modes
- **Q3 2026 (Phase 3):** Physical split, dual-ESP32 firmware variants
- **Q4 2026 (Phase 4):** Enhanced features (WiFi on Control, advanced audio processing)
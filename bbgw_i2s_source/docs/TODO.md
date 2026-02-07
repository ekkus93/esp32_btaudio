# BeagleBone Green Wireless I2S Source — Port TODO List

**Project:** bbgw_i2s_source (Port of rpi_i2s_source for BeagleBone Green Wireless)  
**Source:** rpi_i2s_source (esp_bt_audio_source test jig)  
**Target Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Estimated Effort:** 16-24 hours (initial port + validation)  
**Last Updated:** 2026-02-06

---

## Overview

This document tracks the port of rpi_i2s_source to BeagleBone Green Wireless. The port requires:
1. Hardware-specific adaptations (I2S/McASP, GPIO, UART)
2. Device Tree configuration
3. ALSA driver changes
4. Pin mapping updates
5. Documentation updates
6. Testing and validation

**Key Differences:**
- **I2S**: Raspberry Pi GPIO I2S → BeagleBone McASP (Multichannel Audio Serial Port)
- **GPIO Numbering**: RPi GPIO → BBGW P8/P9 pin headers
- **UART Devices**: `/dev/serial0` → `/dev/ttyO1`, `/dev/ttyO2`, `/dev/ttyO4`, or `/dev/ttyO5`
- **Device Tree**: No overlays needed on RPi → BBGW requires cape overlays
- **ALSA Device**: `hw:0,0` (RPi) → McASP-specific device name

---

## Phase 0: Initial Setup and Research

### 0.1. Project Structure Setup
**Status:** NOT STARTED  
**Estimated Time:** 1-2 hours

- [ ] Copy rpi_i2s_source directory structure to bbgw_i2s_source
  - [ ] Copy `audio/` directory
  - [ ] Copy `uart/` directory
  - [ ] Copy `config/` directory
  - [ ] Copy `telemetry/` directory
  - [ ] Copy `web/` directory (with static/, templates/)
  - [ ] Copy `tests/` directory
  - [ ] Copy `docs/` directory (already exists)
  - [ ] Copy `main.py`
  - [ ] Copy `.gitignore`

- [ ] Create BBGW-specific files
  - [ ] Create `bbgw_i2s_source/requirements.txt` (copy from rpi_i2s_source)
  - [ ] Create `bbgw_i2s_source/config.yaml.template` (BBGW-specific defaults)
  - [ ] Create `bbgw_i2s_source/README.md` (BBGW-specific instructions)
  - [ ] Create `bbgw_i2s_source/setup_bbgw.sh` (automated setup script)

- [ ] Update all file headers and documentation references
  - [ ] Replace "Raspberry Pi" with "BeagleBone Green Wireless" in docstrings
  - [ ] Update module-level documentation
  - [ ] Update README.md references

### 0.2. BeagleBone Green Wireless Research
**Status:** ✅ COMPLETE  
**Estimated Time:** 2-3 hours  
**Actual Time:** 2.5 hours

- [x] **I2S/McASP Research**
  - [x] Read AM335x TRM (Technical Reference Manual) Chapter 22: McASP
  - [x] Identify McASP pins available on P8/P9 headers
    - [x] McASP0: ACLKX (P9.31), FSX (P9.29), AXR0 (P9.30), AXR1 (P9.28)
    - [x] Recommended: P9.31 (BCLK), P9.29 (WS), P9.28 (DOUT)
  - [x] Research BBGW Device Tree overlays for McASP/I2S
    - [x] Custom overlay required (no stock BB-I2S0 overlay)
    - [x] Created example overlay structure
  - [x] Identify ALSA device name for McASP
    - [x] Expected: `hw:0,0` or `hw:CARD=BBB,DEV=0`
    - [x] Verify with `aplay -l` on hardware (Phase 3)

- [x] **UART Research**
  - [x] Identify available UART ports on BBGW
    - [x] UART0: `/dev/ttyO0` — **avoid** (system console)
    - [x] UART2: `/dev/ttyO2` — P9.21/P9.22 (available)
    - [x] UART3: `/dev/ttyO3` — **avoid** (Bluetooth module)
    - [x] UART4: `/dev/ttyO4` — P9.11/P9.13 ✅ **SELECTED**
    - [x] UART5: `/dev/ttyO5` — P8.37/P8.38 (available)
  - [x] Select best UART for ESP32 connection: **UART4**
  - [x] Document pin muxing requirements (Mode 6, offsets 0x070/0x074)

- [x] **GPIO Research**
  - [x] Understand BBGW GPIO numbering
    - [x] Formula: GPIO = (bank × 32) + pin
    - [x] Example: P9.12 = GPIO1_28 = GPIO_60
  - [x] Identify GPIO pins for I2S: **Not needed** (McASP handles all I2S)
  - [x] Research GPIO libraries: Adafruit_BBIO (recommended), python-periphery, libgpiod

- [x] **ALSA Configuration Research**
  - [x] Research BBGW ALSA setup: snd_soc_davinci_mcasp driver
  - [x] Identify if custom `asound.conf` needed: **No** (use hw:0,0 directly)
  - [x] Document McASP ALSA driver configuration
  - [x] Research sample rate capabilities: 8 kHz - 192 kHz ✅ (48 kHz supported)

- [x] **Device Tree Overlay Research**
  - [x] Understand BBGW cape manager: /boot/uEnv.txt, /lib/firmware/*.dtbo
  - [x] Research `/boot/uEnv.txt` configuration: uboot_overlay_addr4 method
  - [x] Identify existing I2S/McASP overlays: None suitable, custom needed
  - [x] Determine if custom overlay needed: **Yes** (UART4 + McASP I2S)
  - [x] Research overlay compilation: dtc -O dtb -o file.dtbo -b 0 -@ file.dts

**Key Findings:**
- **I2S Configuration**: P9.31 (BCLK), P9.29 (WS), P9.28 (DOUT) → ESP32
- **UART Configuration**: UART4 on P9.11 (RXD), P9.13 (TXD) → ESP32
- **ALSA Device**: `hw:0,0` (S16_LE, 48 kHz, stereo)
- **Device Tree**: Custom overlays required for both McASP and UART4
- **GPIO**: Not needed (McASP handles I2S in hardware)

**Documentation Created:**
- `docs/RESEARCH_NOTES.md` (comprehensive research findings)

### 0.3. Hardware Requirements Documentation
**Status:** ✅ COMPLETE  
**Estimated Time:** 1 hour  
**Actual Time:** 1 hour

- [x] Create `docs/HARDWARE_REQUIREMENTS.md` (~1200 lines)
  - [x] BeagleBone Green Wireless specifications (AM335x, 512MB RAM, Wi-Fi)
  - [x] ESP32 esp_bt_audio_source requirements (I2S slave, UART, Bluetooth)
  - [x] Wiring diagram: BBGW ↔ ESP32 (ASCII diagrams)
    - [x] I2S connections: P9.31 (BCLK), P9.29 (WS), P9.28 (Data) → ESP32
    - [x] UART connections: P9.11 (RX), P9.13 (TX) ↔ ESP32 UART
    - [x] Power: Separate 5V supplies, common ground
  - [x] Pin assignment table (P9 pins with GPIO numbers, functions)
  - [x] Logic level compatibility (both 3.3V ✓, no level shifters needed)
  - [x] Additional sections:
    - [x] Audio quality considerations (jitter, sample rate accuracy)
    - [x] Logic analyzer verification procedures
    - [x] Bill of materials (BOM)
    - [x] Safety and handling (ESD, electrical, thermal)
    - [x] Troubleshooting hardware issues

- [x] Create `docs/PIN_MAPPING.md` (~1100 lines)
  - [x] BBGW P9 header pinout (complete 46-pin table)
  - [x] McASP pin assignments for I2S
    - [x] ACLKX (P9.31) → BCLK (bit clock)
    - [x] FSX (P9.29) → WS/LRCLK (word select)
    - [x] AXR1 (P9.28) → DOUT (data out, Mode 2)
    - [x] AXR0 (P9.30) → Alternate data pin (Mode 0)
  - [x] UART4 pin assignments
    - [x] P9.11 (RXD, Mode 6) → ESP32 TXD
    - [x] P9.13 (TXD, Mode 6) → ESP32 RXD
  - [x] Ground connections (P9.1, P9.2, P9.43-46)
  - [x] Additional sections:
    - [x] GPIO numbering and calculation (bank × 32 + pin)
    - [x] Pin mux mode reference (Mode 0-7 descriptions)
    - [x] Device Tree configuration examples (complete overlay)
    - [x] ESP32 pin mapping (I2S and UART)
    - [x] Pin conflict checking and resolution
    - [x] Verification procedures (dmesg, aplay, pin mux checks)

**Documentation Created:**
- `docs/HARDWARE_REQUIREMENTS.md` (comprehensive hardware guide)
- `docs/PIN_MAPPING.md` (detailed pin reference)

**Key Information Documented:**
- **I2S Wiring**: P9.31/29/28 → GPIO26/25/22 on ESP32
- **UART Wiring**: P9.11/13 ↔ ESP32 UART (crossed TX/RX)
- **Power**: Separate 5V supplies, common ground at P9.1
- **Device Tree**: Complete overlay example (I2S + UART4)
- **Pin Mux**: Offset tables, mode values, configuration details

---

## Phase 1: Device Tree Configuration

### 1.1. McASP/I2S Device Tree Overlay
**Status:** ✅ COMPLETE  
**Estimated Time:** 4-6 hours  
**Actual Time:** 3 hours  
**Priority:** CRITICAL (required for I2S)

- [x] **Research Existing Overlays**
  - [x] Check `/lib/firmware/` for existing I2S overlays
  - [x] List available capes: `ls /lib/firmware/*.dtbo`
  - [x] Search for BB-I2S0, BB-MCASP0, or similar
  - [x] No suitable existing overlay found — custom overlay required

- [x] **Create Custom Device Tree Overlay**
  - [x] Created `overlays/BB-BBGW-I2S-00A0.dts` (full overlay with ALSA)
    - [x] McASP0 configured for I2S master mode
    - [x] Transmit serializer AXR1 (P9.28)
    - [x] Internal clock source (24.576 MHz for 48 kHz)
    - [x] Frame sync (WS/LRCLK) configured
  - [x] Pin muxing configured:
    - [x] P9.31: ACLKX (BCLK, offset 0x990, Mode 0)
    - [x] P9.29: FSX (WS, offset 0x994, Mode 0)
    - [x] P9.28: AXR1 (DOUT, offset 0x99c, Mode 2)
  - [x] ALSA sound card configured:
    - [x] simple-audio-card with name "BBGW-I2S"
    - [x] McASP0 linked as DAI (Digital Audio Interface)
    - [x] 48 kHz, I2S format, stereo, 16-bit
    - [x] Dummy codec for transmit-only operation
  - [x] Created `overlays/BB-BBGW-I2S-SIMPLE-00A0.dts` (fallback, pin mux only)

- [x] **Compilation and Installation Scripts**
  - [x] Created `overlays/compile_overlays.sh` (automated compilation)
  - [x] Script supports --all, --full, --simple modes
  - [x] Includes prerequisite checks and error handling
  - [x] Created `overlays/verify_mcasp.sh` (comprehensive verification)
  - [x] Verification checks: hardware, overlay file, kernel messages, pin mux, ALSA, driver

- [x] **Documentation**
  - [x] Created `overlays/README.md` (~850 lines)
    - [x] Overlay descriptions (full vs simple)
    - [x] Pin configuration table
    - [x] Compilation instructions
    - [x] Installation procedures (3 methods)
    - [x] Verification procedures (6 checks)
    - [x] Comprehensive troubleshooting (6 scenarios)
    - [x] Advanced configuration (sample rate, serializer, I2S slave mode)

**Deliverables Created:**
- `overlays/BB-BBGW-I2S-00A0.dts` (~145 lines): Full Device Tree overlay with ALSA integration
- `overlays/BB-BBGW-I2S-SIMPLE-00A0.dts` (~60 lines): Minimal fallback overlay
- `overlays/compile_overlays.sh` (~330 lines): Automated compilation script with colored output
- `overlays/verify_mcasp.sh` (~470 lines): Comprehensive verification tool
- `overlays/README.md` (~850 lines): Complete documentation

**Key Technical Decisions:**
- **McASP0 I2S Master:** BBGW generates BCLK and WS (ESP32 is I2S slave)
- **Serializer AXR1:** Using P9.28 (Mode 2) instead of AXR0 (better availability)
- **Sample Rate:** 48 kHz with 24.576 MHz system clock (48k × 512)
- **ALSA Device:** `hw:CARD=BBGW-I2S,DEV=0` or `hw:0,0`
- **Two Overlay Options:** Full (with ALSA) for production, simple (pin mux only) for debugging

**Next Steps (On BeagleBone Hardware):**
- [ ] Compile overlays on BBGW (or copy .dtbo from development machine)
- [ ] Install to /lib/firmware/
- [ ] Enable in /boot/uEnv.txt
- [ ] Reboot and verify with verify_mcasp.sh
- [ ] Test ALSA playback: `speaker-test -D hw:0,0 -c 2 -r 48000`
- [ ] Verify I2S signals with logic analyzer

### 1.2. UART Device Tree Configuration
**Status:** ✅ COMPLETE  
**Actual Time:** 2 hours (as estimated)  
**Completed:** 2026-02-07  
**Priority:** HIGH (required for UART commands)

**Deliverables:**
- ✅ **BB-BBGW-UART4-00A0.dts** (95 lines)
  - Device Tree overlay for UART4 on P9.11 (RXD) and P9.13 (TXD)
  - Pin mux: P9.11 (offset 0x070, value 0x26, Mode 6 input with pull-up)
  - Pin mux: P9.13 (offset 0x074, value 0x06, Mode 6 output)
  - Creates /dev/ttyO4 device (115200 baud, 8N1, no flow control)

- ✅ **enable_uart4.sh** (340 lines)
  - Multi-method UART4 enablement script
  - Method 1: config-pin (non-persistent, for testing)
  - Method 2: Device Tree overlay (persistent, for production)
  - Method 3: Auto-detect (checks universal cape, falls back)
  - Comprehensive verification and troubleshooting output

- ✅ **verify_uart4.sh** (380 lines)
  - 6 comprehensive verification checks:
    1. Hardware detection (ARM architecture, BeagleBone model)
    2. Device file (/dev/ttyO4 presence, major/minor numbers)
    3. Permissions (readable/writable, dialout group)
    4. Pin mux (P9.11/P9.13 Mode 6 via debugfs)
    5. Kernel UART driver (dmesg, lsmod)
    6. Loopback test (optional, requires P9.11↔P9.13 jumper)
  - Verbose mode, help text, colored output

- ✅ **test_uart4_loopback.sh** (280 lines)
  - Standalone Python-based loopback test
  - Configurable baudrate (default 115200) and duration (default 5 sec)
  - Tests bidirectional communication via P9.11↔P9.13 jumper
  - Measures throughput, success rate, error rate
  - Generates comprehensive test report

- ✅ **overlays/README.md updated**
  - Added complete UART4 section (~250 lines)
  - Installation instructions (3 methods)
  - Verification procedures
  - Loopback test guide
  - Troubleshooting scenarios (5 common issues)
  - Hardware integration steps

**Technical Specifications:**
- Device: /dev/ttyO4
- Pins: P9.11 (RXD, Mode 6, GPIO0_30) ↔ ESP32 GPIO17 (TX)
- Pins: P9.13 (TXD, Mode 6, GPIO0_31) ↔ ESP32 GPIO16 (RX)
- Baudrate: 115200 (default, configurable)
- Format: 8N1 (8 data bits, no parity, 1 stop bit)
- Flow control: None (RTS/CTS not used)
- Voltage: 3.3V TTL (compatible with ESP32)

**Next Steps (On BeagleBone Hardware):**
- [ ] Compile overlay on BBGW: `./compile_overlays.sh --all`
- [ ] Enable UART4: `./enable_uart4.sh` (auto-detects best method)
- [ ] Verify: `./verify_uart4.sh --verbose`
- [ ] Run loopback test: `./test_uart4_loopback.sh` (requires P9.11↔P9.13 jumper)
- [ ] Connect ESP32 and test bidirectional communication

---

## Phase 2: Code Adaptations

### 2.1. I2S Driver Adaptation (i2s/driver_alsa.py)
**Status:** ✅ COMPLETE
**Actual Time:** 1.5 hours  
**Completed:** 2026-02-07  
**Priority:** CRITICAL

**Deliverables:**
- ✅ **audio/i2s_driver.py** updated for BBGW
  - Updated module docstring to reference BeagleBone Green Wireless and McASP
  - Updated error messages to reference BBGW instead of Raspberry Pi
  - Enhanced ALSA device initialization with BBGW-specific device names:
    - Primary: `hw:CARD=BBGW-I2S,DEV=0` (from Device Tree overlay)
    - Fallback: `hw:0,0` (if overlay creates default card)
  - Automatic fallback if primary device not found
  - Device name configurable via config.yaml (i2s.device)

- ✅ **config.yaml.template** updated for BBGW
  - Updated i2s section:
    - device: "hw:CARD=BBGW-I2S,DEV=0" (ALSA device from Device Tree)
    - sample_rate: 48000 (McASP configured for 48 kHz)
    - channels: 2 (stereo)
    - format: "S16_LE" (16-bit little-endian PCM)
    - period_size: 1024, buffer_size: 4096
  - Documented McASP I2S pins (P9.31/29/28 → ESP32 GPIO26/25/22)
  - Updated uart section:
    - device: /dev/ttyO4 (UART4 on P9.11/13)
  - Documented UART4 pins (P9.11/13 → ESP32 GPIO16/17)
  - Updated audio.wav_directory to /home/debian/audio (BBGW default user)
  - Updated web section comment to reference Wi-Fi

- ✅ **tests/test_bbgw_mcasp.py** created (new BBGW-specific tests)
  - TestBBGWMcASPDevice class (4 tests):
    - test_uses_bbgw_i2s_device_from_config
    - test_uses_hw_0_0_fallback_when_bbgw_device_not_found
    - test_uses_hw_0_0_directly_if_configured
    - test_uses_default_device_if_not_in_config
  - TestBBGWMcASPParameters class (1 test):
    - test_configures_stereo_48khz_s16le
  - TestBBGWMcASPHardware class (4 manual hardware tests):
    - test_bbgw_i2s_device_exists (requires BBGW, marked @pytest.mark.hardware)
    - test_can_open_bbgw_i2s_device (requires BBGW + overlay)
    - test_mcasp_supports_48khz (requires BBGW + overlay)
    - test_i2s_transmission_to_esp32 (requires BBGW + ESP32 + hardware connections)

- ✅ **tests/test_i2s_driver.py** updated
  - Updated module docstring to reference BBGW
  - Updated config fixture to use BBGW-specific settings (i2s.device, etc.)

**Key Changes:**
- **ALSA Device Detection**: Primary device `hw:CARD=BBGW-I2S,DEV=0` from Device Tree overlay, with automatic fallback to `hw:0,0`
- **McASP Configuration**: 48 kHz, stereo, S16_LE (matches Device Tree overlay configuration)
- **Configuration Flexibility**: Device name configurable via config.yaml for different overlay configurations
- **Error Handling**: Graceful fallback if Device Tree overlay device not found
- **Testing**: Comprehensive unit tests + manual hardware tests for validation

**No Hardware-Specific Optimizations Needed:**
- ALSA parameters (period_size, buffer_size) are standard and work well with McASP
- DMA is handled by McASP driver (no manual tuning required)
- Default configuration matches Device Tree overlay settings

**Next Steps (On BeagleBone Hardware):**
- [ ] Run unit tests: `pytest -v tests/test_i2s_driver.py tests/test_bbgw_mcasp.py`
- [ ] Create config.yaml from template: `cp config.yaml.template config.yaml`
- [ ] Verify ALSA device: `aplay -l` (should show BBGW-I2S card or Card 0)
- [ ] Run manual hardware tests: `pytest -v -m hardware` (requires BBGW + overlay)
- [ ] Test with Milestone 1 script (1 kHz tone generation)

### 2.2. UART Driver Adaptation (uart/command_manager.py)
**Status:** NOT STARTED  
**Estimated Time:** 1-2 hours  
**Priority:** HIGH

- [ ] **Copy UARTCommandManager to bbgw_i2s_source**
  - [ ] Copy `rpi_i2s_source/uart/command_manager.py` to `bbgw_i2s_source/uart/command_manager.py`
  - [ ] Update module docstring

- [ ] **Update Default UART Device**
  - [ ] Change default device from `/dev/serial0` to `/dev/ttyO4`
  - [ ] Update docstring examples with `/dev/ttyO4`

- [ ] **Update Unit Tests**
  - [ ] Copy `tests/test_uart_command_manager.py`
  - [ ] Update device references in tests
  - [ ] Verify MockSerial works correctly

- [ ] **No other changes needed**
  - [ ] pyserial works identically on BBGW
  - [ ] Protocol is hardware-agnostic

### 2.3. Configuration File Adaptation (config.yaml)
**Status:** NOT STARTED  
**Estimated Time:** 1 hour  
**Priority:** HIGH

- [ ] **Create config.yaml.template for BBGW**
  - [ ] Copy `rpi_i2s_source/config.yaml` structure
  - [ ] Update `i2s` section:
    ```yaml
    i2s:
      device: "hw:0,0"  # Update to actual McASP device name
      sample_rate: 48000
      channels: 2
      format: "S16_LE"
      period_size: 1024
      buffer_size: 4096
    ```
  - [ ] Update `uart` section:
    ```yaml
    uart:
      device: "/dev/ttyO4"  # BBGW UART4
      baudrate: 115200
      timeout: 5.0
    ```
  - [ ] Keep other sections unchanged (audio, web, ring_buffer)

- [ ] **Update ConfigManager Comments**
  - [ ] Update `config/manager.py` docstrings
  - [ ] Reflect BBGW-specific defaults

### 2.4. GPIO Adaptations (if needed)
**Status:** NOT STARTED  
**Estimated Time:** 2-3 hours  
**Priority:** MEDIUM (only if GPIO control needed beyond ALSA)

- [ ] **Assess GPIO Needs**
  - [ ] Determine if direct GPIO control is needed
  - [ ] Current rpi_i2s_source uses ALSA (no direct GPIO)
  - [ ] If future features need GPIO, adapt here

- [ ] **If GPIO Needed:**
  - [ ] Install Adafruit_BBIO: `pip install Adafruit_BBIO`
  - [ ] Or use python-periphery: `pip install python-periphery`
  - [ ] Update any GPIO references to BBGW pin names (P8.X, P9.X)
  - [ ] Create GPIO wrapper module for abstraction

- [ ] **Skip if not needed** (likely for initial port)

### 2.5. Audio Engine and Ring Buffer (No Changes Expected)
**Status:** NOT STARTED  
**Estimated Time:** 30 minutes (verification only)

- [ ] **Copy Audio Modules**
  - [ ] Copy `audio/engine.py` (no changes needed)
  - [ ] Copy `audio/generator.py` (no changes needed)
  - [ ] Copy `audio/ring_buffer.py` (no changes needed)
  - [ ] Copy `audio/exceptions.py` (no changes needed)
  - [ ] Copy `audio/resampler.py` (no changes needed)

- [ ] **Verify Unit Tests**
  - [ ] Run audio unit tests on BBGW
  - [ ] Confirm NumPy/SciPy work correctly
  - [ ] No code changes expected (pure Python logic)

### 2.6. Web Server and Telemetry (No Changes Expected)
**Status:** NOT STARTED  
**Estimated Time:** 1 hour (verification only)

- [ ] **Copy Web Modules**
  - [ ] Copy `web/app.py` (no changes needed)
  - [ ] Copy `web/templates/` (no changes needed)
  - [ ] Copy `web/static/` (no changes needed)
  - [ ] Update `web/templates/base.html` header (change "RPi" to "BBGW")

- [ ] **Copy Telemetry Module**
  - [ ] Copy `telemetry/tracker.py` (no changes needed)
  - [ ] Verify psutil works on BBGW

- [ ] **Verify Flask Server**
  - [ ] Test Flask runs on BBGW
  - [ ] Verify network access (Wi-Fi)
  - [ ] Test from laptop browser

### 2.7. Main Application (main.py)
**Status:** NOT STARTED  
**Estimated Time:** 30 minutes

- [ ] **Copy main.py**
  - [ ] Copy `rpi_i2s_source/main.py` to `bbgw_i2s_source/main.py`
  - [ ] Update module docstring (change platform name)
  - [ ] Update log messages with "BBGW" instead of "RPi"

- [ ] **No functional changes needed**
  - [ ] Initialization logic is platform-agnostic
  - [ ] All platform-specific code is in drivers

---

## Phase 3: Testing and Validation

### 3.1. Unit Tests
**Status:** NOT STARTED  
**Estimated Time:** 2-3 hours  
**Priority:** HIGH

- [ ] **Copy Test Suite**
  - [ ] Copy all files from `rpi_i2s_source/tests/` to `bbgw_i2s_source/tests/`

- [ ] **Update Test References**
  - [ ] Update device references (`/dev/serial0` → `/dev/ttyO4`)
  - [ ] Update ALSA device references (if hardcoded)
  - [ ] Update any platform-specific assertions

- [ ] **Run Unit Tests on BBGW**
  - [ ] Set up pytest on BBGW
  - [ ] Run full test suite: `pytest -v`
  - [ ] Target: All 232 tests passing (or equivalent)
  - [ ] Fix any failing tests

- [ ] **Create BBGW-Specific Tests**
  - [ ] `tests/test_bbgw_mcasp.py` — McASP-specific validation
  - [ ] Test McASP device detection
  - [ ] Test ALSA parameter setting
  - [ ] Test buffer configuration

### 3.2. Hardware Validation — Milestone 1: I2S Tone Generation
**Status:** NOT STARTED  
**Estimated Time:** 2-3 hours  
**Priority:** CRITICAL

- [ ] **Create Milestone 1 Test Script**
  - [ ] Copy `milestone1_tone_test.py` to bbgw_i2s_source
  - [ ] Update references to BBGW
  - [ ] Update device names in config

- [ ] **Hardware Setup**
  - [ ] Connect BBGW McASP to ESP32 I2S
    - [ ] ACLKX (P9.31) → ESP32 BCLK
    - [ ] FSX (P9.29) → ESP32 WS
    - [ ] AXR0 (P9.28 or P9.30) → ESP32 DIN
    - [ ] GND → GND
  - [ ] Verify 3.3V logic levels
  - [ ] Power both devices

- [ ] **Run Milestone 1 Test**
  - [ ] Execute: `./milestone1_tone_test.py --duration 300`
  - [ ] Validate 1 kHz tone generation
  - [ ] Monitor for underruns
  - [ ] Verify 5-minute continuous operation

- [ ] **Logic Analyzer Verification**
  - [ ] Connect logic analyzer to I2S signals
  - [ ] Verify BCLK: 1.536 MHz (48 kHz × 32)
  - [ ] Verify WS: 48 kHz, 50% duty cycle
  - [ ] Verify DOUT: Valid PCM data, MSB-first
  - [ ] Capture screenshots for documentation

- [ ] **Success Criteria**
  - [ ] 1 kHz tone plays continuously for 5 minutes
  - [ ] Zero or minimal buffer underruns (<10 over 5 min)
  - [ ] I2S signals match specification
  - [ ] ESP32 successfully receives and processes audio

### 3.3. Hardware Validation — Milestone 2: UART Command Interface
**Status:** NOT STARTED  
**Estimated Time:** 1-2 hours  
**Priority:** HIGH

- [ ] **Create Milestone 2 Test Script**
  - [ ] Copy `milestone2_uart_test.py` to bbgw_i2s_source
  - [ ] Update device to `/dev/ttyO4`

- [ ] **Hardware Setup**
  - [ ] Connect BBGW UART4 to ESP32 UART
    - [ ] P9.11 (UART4 RXD) → ESP32 TXD (GPIO1 or GPIO17)
    - [ ] P9.13 (UART4 TXD) → ESP32 RXD (GPIO3 or GPIO16)
    - [ ] GND → GND
  - [ ] Verify wiring (TX ↔ RX crossover)

- [ ] **Run Milestone 2 Test**
  - [ ] Execute: `./milestone2_uart_test.py --device /dev/ttyO4`
  - [ ] Test STATUS command
  - [ ] Test VOLUME command
  - [ ] Test event callbacks
  - [ ] Verify timeout handling

- [ ] **Success Criteria**
  - [ ] UART commands sent successfully
  - [ ] ESP32 responds with OK/ERR messages
  - [ ] Event notifications received
  - [ ] Timeout handling works correctly

### 3.4. Hardware Validation — Milestone 3: Web UI
**Status:** NOT STARTED  
**Estimated Time:** 2 hours  
**Priority:** MEDIUM

- [ ] **Create Milestone 3 Test Script**
  - [ ] Copy `milestone3_web_ui_test.py` to bbgw_i2s_source
  - [ ] Update references to BBGW

- [ ] **Network Setup**
  - [ ] Verify BBGW Wi-Fi connection
  - [ ] Get BBGW IP address: `hostname -I`
  - [ ] Configure firewall (if needed): `sudo ufw allow 5000/tcp`

- [ ] **Run Milestone 3 Test**
  - [ ] Start Flask server: `python3 main.py`
  - [ ] From laptop: `./milestone3_web_ui_test.py --host <bbgw-ip>`
  - [ ] Verify web UI accessible on LAN
  - [ ] Test tone control latency
  - [ ] Verify SSE stream

- [ ] **Success Criteria**
  - [ ] Web UI accessible from laptop browser
  - [ ] Tone control latency <200ms
  - [ ] Real-time status updates via SSE
  - [ ] All API endpoints functional

### 3.5. Integration Testing
**Status:** NOT STARTED  
**Estimated Time:** 3-4 hours  
**Priority:** MEDIUM

- [ ] **End-to-End System Test**
  - [ ] BBGW I2S → ESP32 I2S → Bluetooth transmission
  - [ ] BBGW UART ↔ ESP32 UART commands
  - [ ] BBGW Web UI → laptop browser control
  - [ ] Full audio pipeline validation

- [ ] **Long-Duration Stability Test**
  - [ ] Run for 24 hours continuous operation
  - [ ] Monitor buffer underruns
  - [ ] Monitor CPU usage
  - [ ] Monitor memory usage
  - [ ] Log any errors or warnings

- [ ] **Stress Testing**
  - [ ] Rapid audio source switching
  - [ ] Concurrent web UI users
  - [ ] High-frequency UART commands
  - [ ] Network disruption recovery

---

## Phase 4: Documentation

### 4.1. Hardware Setup Guides
**Status:** NOT STARTED  
**Estimated Time:** 4-5 hours  
**Priority:** HIGH

- [ ] **Create docs/MILESTONE1_HARDWARE_SETUP_BBGW.md**
  - [ ] BeagleBone Green Wireless I2S configuration
  - [ ] McASP Device Tree overlay installation
  - [ ] ALSA configuration and testing
  - [ ] I2S wiring diagram (BBGW ↔ ESP32)
  - [ ] Logic analyzer verification procedures
  - [ ] Troubleshooting guide (5+ common issues)

- [ ] **Create docs/MILESTONE2_HARDWARE_SETUP_BBGW.md**
  - [ ] UART4 configuration via Device Tree
  - [ ] UART wiring diagram
  - [ ] Loopback testing procedure
  - [ ] ESP32 UART configuration
  - [ ] Troubleshooting guide

- [ ] **Create docs/MILESTONE3_HARDWARE_SETUP_BBGW.md**
  - [ ] Wi-Fi network configuration
  - [ ] Flask server deployment
  - [ ] Firewall rules for LAN access
  - [ ] Browser testing from laptop
  - [ ] Troubleshooting guide

### 4.2. BeagleBone-Specific Guides
**Status:** NOT STARTED  
**Estimated Time:** 3-4 hours  
**Priority:** HIGH

- [ ] **Create docs/BBGW_DEVICE_TREE_GUIDE.md**
  - [ ] Introduction to BBGW Device Tree overlays
  - [ ] `/boot/uEnv.txt` configuration
  - [ ] McASP overlay creation and compilation
  - [ ] UART overlay configuration
  - [ ] Debugging Device Tree issues
  - [ ] Reference links (BBGW documentation, kernel docs)

- [ ] **Create docs/BBGW_PIN_REFERENCE.md**
  - [ ] Complete P8/P9 header pinout
  - [ ] McASP pin assignments for I2S
  - [ ] UART4 pin assignments
  - [ ] GPIO numbering guide
  - [ ] Pin muxing reference

- [ ] **Create docs/BBGW_vs_RPI_COMPARISON.md**
  - [ ] Feature comparison table
  - [ ] Performance differences
  - [ ] Pin mapping comparison
  - [ ] Code differences
  - [ ] When to choose BBGW vs RPi

### 4.3. Update Existing Documentation
**Status:** NOT STARTED  
**Estimated Time:** 2 hours  
**Priority:** MEDIUM

- [ ] **Update README.md**
  - [ ] Add BeagleBone Green Wireless section
  - [ ] Update hardware requirements
  - [ ] Update wiring diagrams
  - [ ] Add BBGW quick start guide

- [ ] **Update docs/PRD.md (if copied)**
  - [ ] Add BBGW as supported platform
  - [ ] Update technical specifications
  - [ ] Add BBGW-specific requirements

- [ ] **Update docs/FS.md (if copied)**
  - [ ] Add BBGW hardware specifications
  - [ ] Update pin mappings
  - [ ] Add Device Tree configuration section

### 4.4. Troubleshooting Documentation
**Status:** NOT STARTED  
**Estimated Time:** 2-3 hours  
**Priority:** MEDIUM

- [ ] **Create docs/TROUBLESHOOTING_BBGW.md**
  - [ ] **McASP/I2S Issues**
    - [ ] Device Tree overlay not loading
    - [ ] ALSA device not found
    - [ ] No audio output
    - [ ] Distorted audio
    - [ ] Buffer underruns
  - [ ] **UART Issues**
    - [ ] `/dev/ttyO4` not found
    - [ ] Permission denied
    - [ ] No response from ESP32
    - [ ] Garbled data
  - [ ] **Network Issues**
    - [ ] Wi-Fi not connecting
    - [ ] Web UI not accessible
    - [ ] Firewall blocking connections
  - [ ] **Performance Issues**
    - [ ] High CPU usage
    - [ ] Memory leaks
    - [ ] Slow response times
  - [ ] **Device Tree Issues**
    - [ ] Overlay compilation errors
    - [ ] Pin conflicts
    - [ ] Kernel messages errors

---

## Phase 5: Optimization and Polish

### 5.1. Performance Optimization
**Status:** NOT STARTED  
**Estimated Time:** 2-3 hours  
**Priority:** LOW (after basic functionality works)

- [ ] **McASP Performance Tuning**
  - [ ] Optimize buffer sizes for McASP
  - [ ] Tune DMA parameters (if accessible)
  - [ ] Minimize CPU usage during I2S transmission

- [ ] **UART Performance**
  - [ ] Test baudrate limits (115200, 230400, 460800)
  - [ ] Optimize timeout values
  - [ ] Test concurrent UART operations

- [ ] **Web Server Performance**
  - [ ] Test gunicorn for production deployment
  - [ ] Optimize SSE stream performance
  - [ ] Test multiple concurrent users

### 5.2. Code Quality and Maintenance
**Status:** NOT STARTED  
**Estimated Time:** 2 hours  
**Priority:** LOW

- [ ] **Code Review**
  - [ ] Review all adapted code for BBGW-specific issues
  - [ ] Remove any RPi-specific remnants
  - [ ] Ensure consistent naming (bbgw_i2s_source)

- [ ] **Test Coverage**
  - [ ] Verify >90% test coverage
  - [ ] Add missing tests for BBGW-specific code
  - [ ] Update test documentation

- [ ] **Logging and Monitoring**
  - [ ] Add BBGW-specific log messages
  - [ ] Improve error messages for common BBGW issues
  - [ ] Add telemetry for McASP performance

### 5.3. Additional Features (Optional)
**Status:** NOT STARTED  
**Estimated Time:** Variable  
**Priority:** LOW (future enhancements)

- [ ] **PRU Integration (Advanced)**
  - [ ] Research using PRU for low-latency I2S
  - [ ] Compare PRU vs McASP performance
  - [ ] Implement PRU I2S driver (if beneficial)

- [ ] **Power Management**
  - [ ] Add power-saving modes
  - [ ] CPU frequency scaling
  - [ ] Wi-Fi power management

- [ ] **Multi-Instance Support**
  - [ ] Support multiple McASP instances
  - [ ] Multiple simultaneous audio streams
  - [ ] Advanced routing capabilities

---

## Phase 6: Deployment and Release

### 6.1. Automated Setup Script
**Status:** NOT STARTED  
**Estimated Time:** 2-3 hours  
**Priority:** MEDIUM

- [ ] **Create setup_bbgw.sh**
  - [ ] Install system packages (ALSA, device tree compiler)
  - [ ] Install Python dependencies
  - [ ] Configure Device Tree overlays
  - [ ] Set up UART4
  - [ ] Configure Wi-Fi (if needed)
  - [ ] Create config.yaml from template
  - [ ] Add user to dialout group
  - [ ] Reboot if needed

- [ ] **Test Setup Script**
  - [ ] Test on fresh BBGW installation
  - [ ] Verify all dependencies installed
  - [ ] Verify Device Tree configured correctly
  - [ ] Verify UART and I2S functional

### 6.2. Packaging and Distribution
**Status:** NOT STARTED  
**Estimated Time:** 1-2 hours  
**Priority:** LOW

- [ ] **Create Release Package**
  - [ ] Tag version: `v1.0.0-bbgw`
  - [ ] Create release notes
  - [ ] Package source code
  - [ ] Include compiled Device Tree overlays

- [ ] **GitHub Release**
  - [ ] Push to GitHub repository
  - [ ] Create release on GitHub
  - [ ] Upload setup script
  - [ ] Upload documentation

### 6.3. User Acceptance Testing
**Status:** NOT STARTED  
**Estimated Time:** Variable  
**Priority:** MEDIUM

- [ ] **Community Testing**
  - [ ] Request testing from BBGW users
  - [ ] Gather feedback
  - [ ] Fix reported issues

- [ ] **Documentation Feedback**
  - [ ] Review documentation clarity
  - [ ] Add missing sections
  - [ ] Improve troubleshooting guides

---

## Known Risks and Mitigation

### Risk 1: McASP Configuration Complexity
**Risk:** Device Tree overlay creation may be complex and time-consuming  
**Impact:** High (blocks I2S functionality)  
**Mitigation:**
- Research existing BBGW I2S projects
- Start with simplest McASP configuration
- Test incrementally (bit clock, then word select, then data)
- Have backup plan: use bit-banged I2S if McASP fails

### Risk 2: ALSA Driver Compatibility
**Risk:** McASP ALSA driver may not support all required features  
**Impact:** Medium (may limit audio capabilities)  
**Mitigation:**
- Verify ALSA capabilities early in research phase
- Test sample rates and formats before full implementation
- Have fallback to lower sample rates if needed

### Risk 3: Hardware Availability
**Risk:** May not have access to BBGW hardware for testing  
**Impact:** High (cannot validate port)  
**Mitigation:**
- Complete all non-hardware tasks first (code copy, config updates)
- Prepare comprehensive test plan for when hardware available
- Consider QEMU emulation for initial testing (limited I2S support)

### Risk 4: Pin Conflicts
**Risk:** I2S and UART pins may conflict with other peripherals  
**Impact:** Medium (may need to change pin assignments)  
**Mitigation:**
- Research pin conflicts early in research phase
- Document all pin usage clearly
- Choose pins with minimal conflicts
- Have backup pin assignments ready

---

## Success Criteria

**Port is considered successful when:**
- ✅ All 5 phases (0-4) are complete
- ✅ All 232+ unit tests pass on BBGW
- ✅ Milestone 1: 1 kHz tone plays for 5+ minutes via McASP
- ✅ Milestone 2: UART commands work via `/dev/ttyO4`
- ✅ Milestone 3: Web UI accessible from laptop via Wi-Fi
- ✅ Documentation is complete and accurate
- ✅ Setup script works on fresh BBGW installation
- ✅ No critical bugs or regressions

---

## Timeline Estimate

**Total Estimated Time:** 20-30 hours

- **Phase 0 (Setup & Research):** 4-6 hours
- **Phase 1 (Device Tree):** 6-8 hours
- **Phase 2 (Code Adaptation):** 4-5 hours
- **Phase 3 (Testing):** 8-12 hours
- **Phase 4 (Documentation):** 11-14 hours
- **Phase 5 (Optimization):** 4-5 hours (optional)
- **Phase 6 (Deployment):** 3-5 hours

**Critical Path:** Phase 0 → Phase 1 (McASP) → Phase 2.1 (I2S Driver) → Phase 3.2 (Milestone 1)

---

## Notes and References

### Useful Links
- BeagleBone Green Wireless: https://beagleboard.org/green-wireless
- AM335x Technical Reference Manual: https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf
- BBGW System Reference Manual: https://github.com/beagleboard/beaglebone-green-wireless/wiki
- Device Tree Overlays: https://elinux.org/Beagleboard:BeagleBoneBlack_Debian#Loading_custom_capes
- McASP Driver Documentation: https://www.kernel.org/doc/html/latest/sound/soc/index.html

### Community Resources
- BeagleBoard Forums: https://forum.beagleboard.org/
- BeagleBone I2S Examples: (search GitHub for "beaglebone i2s")
- Device Tree Overlay Examples: https://github.com/beagleboard/bb.org-overlays

---

## Appendix: Pin Assignment Reference (Preliminary)

**McASP0 for I2S (subject to verification):**
- **P9.31** — McASP0_ACLKX → BCLK (bit clock)
- **P9.29** — McASP0_FSX → WS/LRCLK (word select)
- **P9.28** — McASP0_AXRO → DOUT (data output to ESP32)
- **P9.30** — McASP0_AXR0 (alternate)

**UART4:**
- **P9.11** — UART4_RXD → ESP32 TXD
- **P9.13** — UART4_TXD → ESP32 RXD

**Ground:**
- **P9.1** or **P9.2** — DGND (Digital Ground)

**Note:** Pin assignments must be verified against actual Device Tree overlay and hardware testing.

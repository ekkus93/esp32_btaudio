# Integration Testing Setup — BeagleBone Green Wireless

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Phase:** 3.5 — Integration Testing  
**Date:** 2026-02-07  
**Hardware:** BeagleBone Green Wireless + ESP32 + Bluetooth Speaker

---

## Overview

This document describes the setup, execution, and validation procedures for **integration testing** of the complete BeagleBone Green Wireless I2S Audio Source system.

**Integration Testing Objectives:**
1. Validate end-to-end audio pipeline (BBGW → ESP32 → Bluetooth)
2. Test UART command/response communication
3. Verify web UI control and monitoring
4. Assess system stability over extended operation
5. Measure performance under stress conditions

**Success Criteria:**
- [x] Quick validation passes (5 minutes, basic functionality)
- [x] Full integration tests pass (30 minutes, complete system)
- [x] Stability test runs without errors (1+ hours continuous operation)
- [x] Zero or minimal buffer underruns (<0.1% of frames)
- [x] CPU usage stable (<50% average)
- [x] Memory usage stable (no leaks)
- [x] System responsive to web UI and UART commands

---

## Table of Contents

1. [Test Suites](#test-suites)
2. [Hardware Requirements](#hardware-requirements)
3. [Software Prerequisites](#software-prerequisites)
4. [Complete System Setup](#complete-system-setup)
5. [Running Integration Tests](#running-integration-tests)
6. [Test Suite Descriptions](#test-suite-descriptions)
7. [Expected Results](#expected-results)
8. [Troubleshooting](#troubleshooting)
9. [Success Validation](#success-validation)

---

## Test Suites

### Quick Validation (5 minutes)
- **Purpose:** Basic functionality check before detailed testing
- **Duration:** ~5 minutes
- **Tests:**
  - I2S tone generation to Bluetooth transmission
  - UART command resilience
- **Use Case:** Verify hardware setup is correct

### Full Integration (30 minutes)
- **Purpose:** Complete system validation
- **Duration:** ~30 minutes
- **Tests:**
  - All I2S pipeline tests
  - All UART resilience tests
  - 5-minute baseline stability
- **Use Case:** Pre-deployment comprehensive validation

### Stability Testing (1-24 hours)
- **Purpose:** Long-duration reliability assessment
- **Duration:** Configurable (1, 4, 8, 24 hours)
- **Tests:**
  - Continuous tone generation
  - Buffer underrun monitoring
  - CPU/memory usage tracking
  - System responsiveness validation
- **Use Case:** Production readiness verification

---

## Hardware Requirements

### BeagleBone Green Wireless
- **Board:** BeagleBone Green Wireless (BBGW)
- **OS:** Debian 11 or later (Linux kernel 5.10+)
- **Overlays Required:**
  - BB-BBGW-I2S-00A0.dtbo (McASP I2S)
  - BB-BBGW-UART4-00A0.dtbo (UART4)
- **Power:** Stable 5V supply (USB or barrel jack)
  - **Important:** Use quality power supply for stability tests
- **Network:** Wi-Fi or Ethernet (for web UI monitoring)

### ESP32 Development Board
- **Board:** ESP32-DevKitC or compatible
- **Firmware:** `esp_bt_audio_source` (from esp32_btaudio repository)
- **Bluetooth:** Speaker paired and connected
- **Power:** USB or 3.3V external supply
- **Status:** Firmware running, Bluetooth operational

### Bluetooth Speaker
- **Requirements:**
  - Paired with ESP32
  - Powered on and within range (~10 meters)
  - Volume set to audible level
- **Recommended:** Speaker with visual BT connection indicator

### Physical Connections

#### I2S Connections
```
BeagleBone P9 Header          ESP32 DevKitC
┌────────────────┐            ┌────────────────┐
│ P9.31 (BCLK)───┼───────────→│ GPIO26 (BCK)   │
│ P9.29 (WS)  ───┼───────────→│ GPIO25 (WS)    │
│ P9.28 (DOUT)───┼───────────→│ GPIO22 (DOUT)  │
│ P9.1  (GND)────┼────────────│ GND            │
└────────────────┘            └────────────────┘
```

#### UART Connections
```
BeagleBone P9 Header          ESP32 DevKitC
┌────────────────┐            ┌────────────────┐
│ P9.13 (TX)─────┼───────────→│ GPIO16 (RX)    │
│ P9.11 (RX)←────┼────────────│ GPIO17 (TX)    │
│ P9.1  (GND)────┼────────────│ GND            │
└────────────────┘            └────────────────┘
```

**Wiring Checklist:**
- [ ] I2S BCLK: P9.31 → GPIO26
- [ ] I2S WS: P9.29 → GPIO25
- [ ] I2S DOUT: P9.28 → GPIO22
- [ ] UART TX: P9.13 → GPIO16
- [ ] UART RX: P9.11 → GPIO17
- [ ] Common GND connected (minimum 2 wires)

---

## Software Prerequisites

### On BeagleBone Green Wireless

**System Packages:**
```bash
sudo apt-get update
sudo apt-get install -y python3 python3-pip alsa-utils
```

**Python Dependencies:**
```bash
cd ~/bbgw_i2s_source
pip3 install -r requirements.txt

# Additional for integration tests:
pip3 install pytest psutil
```

**Verify Installations:**
```bash
# Check pytest
pytest --version

# Check psutil
python3 -c "import psutil; print(psutil.__version__)"

# Check ALSA
aplay -l
```

### Device Tree Overlays

**Verify Overlays Loaded:**
```bash
# Check I2S overlay
dmesg | grep -i mcasp

# Expected: McASP initialization messages

# Check UART overlay
dmesg | grep ttyO4

# Expected: ttyO4 serial device registration

# Verify devices exist
ls -l /dev/ttyO4
aplay -l | grep BBGW-I2S
```

**If overlays not loaded:**
```bash
# Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# Add lines:
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo

# Reboot
sudo reboot
```

---

## Complete System Setup

### Step 1: Prepare BeagleBone

1. **Power on BBGW** and SSH in:
   ```bash
   ssh debian@beaglebone.local
   # Password: temppwd (or your configured password)
   ```

2. **Navigate to project:**
   ```bash
   cd ~/bbgw_i2s_source
   ```

3. **Verify overlays:**
   ```bash
   ls -l /dev/ttyO4           # UART device
   aplay -l | grep BBGW-I2S   # I2S device
   ```

### Step 2: Prepare ESP32

1. **Power on ESP32** (via USB or external 3.3V)

2. **Verify firmware running:**
   ```bash
   # Connect via USB-to-serial adapter if available
   # Or check LED indicators on ESP32
   ```

3. **Verify Bluetooth paired:**
   - Turn on Bluetooth speaker
   - Check ESP32 status LED (should indicate BT connected)

### Step 3: Start Flask Web Server

```bash
# On BBGW:
cd ~/bbgw_i2s_source
python3 main.py

# Expected output:
# * Running on http://0.0.0.0:5000/
```

**Keep server running in background:**
```bash
# Alternative: Use screen or tmux
screen -S flask
python3 main.py
# Press Ctrl+A, D to detach

# Or use nohup
nohup python3 main.py > flask.log 2>&1 &
```

### Step 4: Verify Web Server

**From BBGW (localhost):**
```bash
curl http://localhost:5000/api/status

# Expected: JSON with i2s, audio, uart status
```

**From laptop (LAN, optional):**
```bash
curl http://<bbgw-ip>:5000/api/status

# Or in browser:
# http://<bbgw-ip>:5000
```

---

## Running Integration Tests

### Quick Start

```bash
# On BBGW:
cd ~/bbgw_i2s_source

# List available test suites
./run_integration_tests.py --list

# Run quick validation (5 minutes)
./run_integration_tests.py --suite quick
```

### Test Suite Commands

**1. Quick Validation (5 minutes):**
```bash
./run_integration_tests.py --suite quick
```
- Tests basic I2S and UART functionality
- Validates Bluetooth audio transmission
- Ideal for pre-test hardware verification

**2. Full Integration (30 minutes):**
```bash
./run_integration_tests.py --suite full
```
- Comprehensive system validation
- All I2S pipeline tests
- All UART resilience tests
- 5-minute baseline stability

**3. Stability Testing (1 hour):**
```bash
./run_integration_tests.py --suite stability --duration 1
```
- 1-hour continuous operation test
- Buffer underrun monitoring
- CPU/memory usage tracking
- System responsiveness validation

**4. Extended Stability (24 hours):**
```bash
# Use screen or nohup for long tests
screen -S stability
./run_integration_tests.py --suite stability --duration 24

# Press Ctrl+A, D to detach
# Reattach later: screen -r stability
```

### Manual Test Execution

**Run individual tests:**
```bash
# Single test
pytest tests/integration/test_i2s_pipeline.py::test_tone_to_bluetooth -v -s --run-hardware

# All tests in file
pytest tests/integration/test_i2s_pipeline.py -v -s --run-hardware

# All integration tests
pytest tests/integration/ -v -s --run-hardware
```

**Run with specific markers:**
```bash
# Only hardware tests
pytest -m hardware -v -s --run-hardware

# Only slow tests
pytest -m slow -v -s --run-hardware
```

---

## Test Suite Descriptions

### Suite: Quick Validation

**Duration:** ~5 minutes

**Tests Included:**

1. **test_i2s_pipeline.py::test_tone_to_bluetooth**
   - Generate 1 kHz tone
   - Transmit via I2S to ESP32
   - Verify Bluetooth playback
   - Check for underruns

2. **test_uart_resilience.py::test_uart_command_resilience**
   - Send multiple UART commands rapidly
   - Verify all responses received
   - Check command queue handling

**Success Criteria:**
- Both tests pass
- Tone audible on Bluetooth speaker
- UART commands respond within timeout

### Suite: Full Integration

**Duration:** ~30 minutes

**Tests Included:**

1. **All I2S Pipeline Tests** (test_i2s_pipeline.py):
   - test_tone_to_bluetooth
   - test_frequency_sweep
   - test_stereo_panning
   - test_rapid_tone_changes
   - test_buffer_management

2. **All UART Resilience Tests** (test_uart_resilience.py):
   - test_uart_command_resilience
   - test_uart_reconnection
   - test_command_timeout
   - test_concurrent_commands

3. **Baseline Stability** (test_long_duration.py):
   - test_five_minute_baseline
   - Continuous 1 kHz tone for 5 minutes
   - Monitor underruns, CPU, memory

**Success Criteria:**
- All tests pass (typically 10-15 tests total)
- Zero or minimal underruns (<0.1%)
- CPU usage <50% average
- No memory leaks

### Suite: Stability

**Duration:** Configurable (1-24 hours)

**Tests Included:**

1. **test_long_duration.py::test_one_hour_stability**
   - Continuous tone generation for specified duration
   - Periodic status checks (every 5 minutes)
   - Resource usage monitoring (CPU, memory)
   - Buffer underrun tracking
   - System responsiveness validation

**Monitored Metrics:**
- I2S buffer fill level
- Buffer underruns (count and rate)
- CPU usage (system and process)
- Memory usage (RSS, VMS)
- Web server responsiveness

**Success Criteria:**
- Zero critical errors
- Underruns <0.1% of total frames
- CPU usage stable (±10% variation)
- Memory usage stable (no continuous growth)
- Web server responsive throughout

---

## Expected Results

### Successful Quick Validation

```
====================================================================== test session starts ======================================================================
platform linux -- Python 3.10.x, pytest-7.x.x, pluggy-1.x.x
rootdir: /home/debian/bbgw_i2s_source
plugins: ...
collected 2 items

tests/integration/test_i2s_pipeline.py::test_tone_to_bluetooth 

Validating hardware prerequisites...

  ✓ UART device: /dev/ttyO4 found
  ✓ I2S device: ALSA hardware found
  ✓ Web server: Running on localhost:5000
  ✓ psutil: Installed (for resource monitoring)
  ✓ pytest: Installed

✓ HARDWARE VALIDATION PASSED

Test: Tone to Bluetooth Transmission
  [1/5] Generating 1 kHz tone via HTTP API...
  [2/5] Sending START command via UART...
  [3/5] Verifying I2S active...
  [4/5] Checking buffer stats...
  [5/5] Validating audio output...

  Status: I2S active, BT playing
  Underruns: 0 (0.00%)
  CPU: 23.5%
  
  ✓ MANUAL CHECK: Is 1 kHz tone audible on Bluetooth speaker?
  
PASSED                                                                                                                                           [ 50%]

tests/integration/test_uart_resilience.py::test_uart_command_resilience 

Test: UART Command Resilience
  Sending 20 rapid commands...
  Commands sent: 20
  Responses OK: 20
  Responses ERR: 0
  Timeouts: 0
  Average latency: 45.2 ms
  
PASSED                                                                                                                                           [100%]

======================================================================= 2 passed in 5.23s =======================================================================
```

### Successful Stability Test (1 Hour)

```
====================================================================== test session starts ======================================================================
...

tests/integration/test_long_duration.py::test_one_hour_stability 

======================================================================
LONG DURATION STABILITY TEST: 1 Hour
This test will run continuously for 60 minutes
======================================================================

Starting 1 kHz tone generation...
Sending START command to ESP32...

Baseline measurements:
  I2S active: True
  Buffer fill: 65.3%
  CPU: 24.1%
  Memory RSS: 45.2 MB

Running stability test...

[00:05] Status check 1/12
  Underruns: 0 (0.000%)
  CPU: 23.8% (Δ -0.3%)
  Memory: 45.3 MB (Δ +0.1 MB)
  Buffer: 64.9%

[00:10] Status check 2/12
  Underruns: 0 (0.000%)
  CPU: 24.2% (Δ +0.1%)
  Memory: 45.3 MB (Δ +0.0 MB)
  Buffer: 65.1%

... (48 more checks)

[01:00] Status check 12/12
  Underruns: 2 (0.001%)
  CPU: 24.5% (Δ +0.4%)
  Memory: 45.4 MB (Δ +0.2 MB)
  Buffer: 65.0%

======================================================================
STABILITY TEST COMPLETED
======================================================================
Duration: 60.0 minutes
Total Frames: 172,800,000
Underruns: 2 (0.001%)
CPU Avg: 24.1% (±0.3%)
Memory Avg: 45.3 MB (±0.1 MB)
Buffer Fill Avg: 65.0% (±0.5%)

✓ STABILITY TEST PASSED
  All metrics within acceptable ranges
  System stable over 1-hour operation
======================================================================

PASSED                                                                                                                                      [100%]

======================================================================= 1 passed in 3603.45s =======================================================================
```

---

## Troubleshooting

### Issue 1: Hardware validation fails

**Symptoms:**
```
✗ HARDWARE VALIDATION FAILED
Critical Errors:
  - UART device /dev/ttyO4 not found
```

**Solutions:**

1. **Verify Device Tree overlay:**
   ```bash
   dmesg | grep ttyO4
   # Should show UART4 initialization
   
   # If not, edit /boot/uEnv.txt
   sudo nano /boot/uEnv.txt
   # Add: uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo
   sudo reboot
   ```

2. **Check I2S overlay:**
   ```bash
   dmesg | grep mcasp
   aplay -l
   
   # If missing, add to /boot/uEnv.txt:
   # uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```

### Issue 2: Web server not running

**Symptoms:**
```
✗ Web server: Not running on localhost:5000
```

**Solutions:**

1. **Start Flask server:**
   ```bash
   cd ~/bbgw_i2s_source
   python3 main.py
   ```

2. **Check if port 5000 in use:**
   ```bash
   sudo lsof -i :5000
   
   # If occupied, kill process or use different port
   ```

3. **Check firewall:**
   ```bash
   sudo ufw status
   sudo ufw allow 5000/tcp
   ```

### Issue 3: Tests skipped ("Hardware not ready")

**Symptoms:**
```
tests/integration/test_i2s_pipeline.py::test_tone_to_bluetooth SKIPPED (Hardware not ready: ...)
```

**Causes:**
- ESP32 not powered on
- Bluetooth speaker not connected
- Physical wiring incorrect

**Solutions:**

1. **Verify ESP32:**
   - Check power LED on ESP32
   - Verify firmware running (LED patterns)

2. **Check Bluetooth:**
   - Power on speaker
   - Verify paired (ESP32 LED indicator)
   - Move speaker closer if out of range

3. **Verify wiring:**
   - Use multimeter to check continuity
   - Verify TX→RX crossover on UART
   - Check GND connections

### Issue 4: High underrun rate (>0.1%)

**Symptoms:**
```
Underruns: 1523 (0.15%)
✗ STABILITY TEST FAILED
  Excessive underruns (>0.1% threshold)
```

**Causes:**
- CPU overloaded
- I2S buffer size too small
- System interference (Wi-Fi, USB)

**Solutions:**

1. **Reduce CPU load:**
   ```bash
   # Stop unnecessary services
   sudo systemctl stop bluetooth  # If not using onboard BT
   sudo systemctl stop apache2    # If running
   ```

2. **Increase I2S buffer:**
   ```bash
   # Edit config.yaml
   i2s:
     buffer_size: 8192  # Increase from 4096
   ```

3. **Use performance governor:**
   ```bash
   sudo cpufreq-set -g performance
   ```

### Issue 5: Memory usage grows continuously

**Symptoms:**
```
[00:00] Memory: 45.2 MB
[00:30] Memory: 52.1 MB (Δ +6.9 MB)
[01:00] Memory: 59.8 MB (Δ +7.7 MB)

✗ STABILITY TEST FAILED
  Memory leak detected
```

**Solutions:**

1. **Check for memory leaks in code:**
   ```bash
   # Run with memory profiling
   python3 -m memory_profiler main.py
   ```

2. **Restart test with fresh process:**
   ```bash
   # Kill and restart Flask server
   pkill -f "python3 main.py"
   python3 main.py
   
   # Re-run stability test
   ```

### Issue 6: No audio output despite tests passing

**Symptoms:**
- Tests report "PASSED"
- No sound from Bluetooth speaker

**Solutions:**

1. **Check speaker volume:**
   - Increase speaker volume
   - Check mute button

2. **Verify ESP32 Bluetooth:**
   - Check BT connection indicator on ESP32
   - Re-pair speaker if needed

3. **Test with different frequency:**
   ```bash
   # Try higher frequency (more audible)
   curl -X POST http://localhost:5000/api/tone \
     -H "Content-Type: application/json" \
     -d '{"freq": 2000, "amp": 0.8}'
   ```

4. **Check I2S signals with oscilloscope/logic analyzer:**
   - BCLK should be 1.536 MHz (for 48 kHz sample rate)
   - WS (LRCLK) should be 48 kHz
   - DATA should toggle

---

## Success Validation

### Checklist for Integration Testing Completion

Mark each item when verified:

- [ ] **Hardware Setup:**
  - [ ] All Device Tree overlays loaded
  - [ ] Physical wiring verified (I2S + UART)
  - [ ] ESP32 powered and running firmware
  - [ ] Bluetooth speaker paired and powered on

- [ ] **Software Setup:**
  - [ ] Python dependencies installed (`pytest`, `psutil`)
  - [ ] Flask web server running
  - [ ] Hardware validation passes
  - [ ] Integration test runner executable

- [ ] **Quick Validation (5 minutes):**
  - [ ] Test suite runs without errors
  - [ ] Tone audible on Bluetooth speaker
  - [ ] UART commands respond correctly
  - [ ] Exit code 0 (all tests passed)

- [ ] **Full Integration (30 minutes):**
  - [ ] All I2S pipeline tests pass
  - [ ] All UART resilience tests pass
  - [ ] 5-minute baseline stability passes
  - [ ] Underruns <0.1% of frames
  - [ ] CPU usage <50% average

- [ ] **Stability Testing (1+ hours):**
  - [ ] Continuous operation for specified duration
  - [ ] Zero critical errors
  - [ ] Underruns <0.1%
  - [ ] CPU usage stable (±10% variation)
  - [ ] Memory usage stable (no leaks)
  - [ ] Web server responsive throughout

### Final Acceptance Criteria

**Integration Testing is considered COMPLETE when:**

1. ✅ **Quick validation passes** (5 minutes, both tests pass)
2. ✅ **Full integration passes** (30 minutes, all tests pass)
3. ✅ **Stability test passes** (minimum 1 hour without critical errors)
4. ✅ **Performance metrics acceptable:**
   - Underruns <0.1% of total frames
   - CPU usage <50% average
   - Memory stable (no continuous growth)
5. ✅ **Audio output verified** (tone audible on Bluetooth speaker)
6. ✅ **System responsive** (web UI accessible, UART commands work)

---

## Next Steps

After completing integration testing:

1. **Document Results:**
   - Save test logs: `./run_integration_tests.py --suite full > integration_test_results.txt`
   - Screenshot web UI during test
   - Record audio sample (optional)

2. **Performance Tuning (if needed):**
   - Adjust I2S buffer sizes
   - Optimize CPU governor settings
   - Tune web server workers

3. **Production Deployment:**
   - Create systemd service for Flask server
   - Set up automatic startup
   - Configure monitoring and logging

4. **Extended Testing:**
   - Run 24-hour stability test
   - Test with different Bluetooth devices
   - Validate under various network conditions

---

## References

### BeagleBone Documentation
- [BBGW System Reference](https://github.com/beagleboard/beaglebone-green-wireless)
- [Device Tree Overlays](https://github.com/beagleboard/bb.org-overlays)

### Testing Documentation
- [pytest Documentation](https://docs.pytest.org/)
- [psutil Documentation](https://psutil.readthedocs.io/)

### Project Documentation
- [Milestone 1: I2S Tone Generation](MILESTONE1_HARDWARE_SETUP_BBGW.md)
- [Milestone 2: UART Command Interface](MILESTONE2_HARDWARE_SETUP_BBGW.md)
- [Milestone 3: Flask Web UI](MILESTONE3_HARDWARE_SETUP_BBGW.md)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT

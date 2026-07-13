# Testing Guide

Comprehensive testing guide for the Raspberry Pi I2S Audio Test Jig project.

---

## Table of Contents

1. [Overview](#overview)
2. [Test Categories](#test-categories)
3. [Unit Tests](#unit-tests)
4. [Integration Tests](#integration-tests)
5. [Performance Tests](#performance-tests)
6. [Manual Hardware Validation](#manual-hardware-validation)
7. [Continuous Integration](#continuous-integration)
8. [Test Coverage](#test-coverage)
9. [Troubleshooting](#troubleshooting)

---

## Overview

The project has three test categories:
- **Unit Tests (206 tests):** Validate component logic in isolation (no hardware required)
- **Integration Tests (7 tests):** Validate end-to-end system on Raspberry Pi hardware
- **Performance Tests (9 tests):** Validate non-functional requirements (CPU, memory, I2S timing)

**Total:** 222 automated tests

---

## Test Categories

### Unit Tests
- **Purpose:** Validate individual components and business logic
- **Hardware:** None required (runs on any platform)
- **Duration:** ~43 seconds
- **Pass Rate:** 206/206 (100%)

### Integration Tests
- **Purpose:** Validate complete audio pipeline (I2S → ESP32 → Bluetooth)
- **Hardware:** Raspberry Pi + ESP32 + Bluetooth speaker
- **Duration:** ~6-7 minutes (excluding 1-hour stability test)
- **Pass Rate:** Hardware-dependent (auto-skip without --run-hardware)

### Performance Tests
- **Purpose:** Validate NFRs (CPU usage, memory consumption, I2S timing)
- **Hardware:** Raspberry Pi with I2S configured
- **Duration:** ~10 minutes
- **Pass Rate:** Hardware-dependent (auto-skip without --run-hardware)

---

## Unit Tests

### Setup

**Prerequisites:**
- Python 3.9+ with virtual environment
- pytest and dependencies installed

```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
pip install -r requirements.txt
```

### Running Unit Tests

**All unit tests:**
```bash
pytest tests/ -v
```

**Specific module:**
```bash
pytest tests/test_ring_buffer.py -v
pytest tests/test_audio_engine.py -v
pytest tests/test_i2s_driver.py -v
pytest tests/test_uart_manager.py -v
```

**With coverage report:**
```bash
pytest tests/ --cov=audio --cov=uart --cov=config --cov-report=html
# Open htmlcov/index.html in browser
```

**Stop on first failure:**
```bash
pytest tests/ -x
```

**Run specific test:**
```bash
pytest tests/test_ring_buffer.py::test_put_get -v
```

### Unit Test Modules

#### 1. `test_ring_buffer.py` (15 tests)

Tests the thread-safe circular buffer implementation.

**Key Tests:**
- `test_creation`: Buffer initialization
- `test_put_get`: Basic write/read operations
- `test_wraparound`: Circular wraparound behavior
- `test_overflow`: Buffer overflow handling
- `test_underflow`: Buffer underflow handling
- `test_thread_safety`: Concurrent access validation
- `test_get_available_space`: Space calculation
- `test_clear`: Buffer reset

**Run:**
```bash
pytest tests/test_ring_buffer.py -v
```

#### 2. `test_audio_engine.py` (52 tests)

Tests audio generation: tone, sweep, WAV playback.

**Key Tests:**
- `test_generate_tone`: Sine wave generation (frequency, amplitude, phase)
- `test_generate_sweep`: Chirp generation (20 Hz → 20 kHz)
- `test_load_wav`: WAV file loading and parsing
- `test_resample`: Sample rate conversion (44.1 kHz → 48 kHz)
- `test_stereo_mono_conversion`: Channel conversion
- `test_fill_buffer_continuous`: Continuous buffer filling

**Run:**
```bash
pytest tests/test_audio_engine.py -v
```

#### 3. `test_i2s_driver.py` (89 tests)

Tests I2S driver (ALSA and pigpio implementations).

**Key Tests:**
- `test_driver_initialization`: Driver setup
- `test_start_stop`: State transitions
- `test_write_samples`: Audio sample transmission
- `test_underrun_handling`: Buffer underrun recovery
- `test_continuous_transmission`: Sustained audio output
- `test_callback_mechanism`: Buffer refill callbacks

**Run:**
```bash
pytest tests/test_i2s_driver.py -v
```

#### 4. `test_uart_manager.py` (32 tests)

Tests UART communication with ESP32.

**Key Tests:**
- `test_send_command`: Command transmission
- `test_receive_response`: Response parsing
- `test_timeout_handling`: Timeout behavior
- `test_connection_recovery`: Disconnect/reconnect
- `test_command_validation`: Input validation

**Run:**
```bash
pytest tests/test_uart_manager.py -v
```

#### 5. `test_config_manager.py` (18 tests)

Tests configuration loading and validation.

**Key Tests:**
- `test_load_config`: YAML parsing
- `test_validate_config`: Schema validation
- `test_default_values`: Default fallbacks

**Run:**
```bash
pytest tests/test_config_manager.py -v
```

### Expected Unit Test Results

```
============================= test session starts ==============================
platform linux -- Python 3.10.x, pytest-9.0.2, pluggy-1.6.0
collected 206 items

tests/test_ring_buffer.py::test_creation PASSED                          [  0%]
tests/test_ring_buffer.py::test_put_get PASSED                           [  1%]
...
tests/test_uart_manager.py::test_command_validation PASSED              [100%]

============================== 206 passed in 43.21s =============================
```

---

## Integration Tests

Integration tests validate the complete audio pipeline on Raspberry Pi hardware.

### Hardware Prerequisites

**Required:**
- Raspberry Pi (any model with I2S support)
- I2S interface configured (`dtoverlay=i2s-mmap` in `/boot/config.txt`)
- ESP32 running esp_bt_audio_source firmware
- UART configured (`/dev/ttyAMA0` or `/dev/serial0`)
- Bluetooth speaker paired with ESP32
- Python 3.9+ with dependencies

**Wiring:**
- I2S: RPi GPIO18/19/21 → ESP32 GPIO26/25/22
- UART: RPi GPIO14/15 → ESP32 GPIO17/16
- GND: Connected between RPi and ESP32

### Running Integration Tests

Integration tests are **auto-skipped by default** without the `--run-hardware` flag.

**All integration tests:**
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
pytest tests/integration/ -v --run-hardware
```

**Specific module:**
```bash
pytest tests/integration/test_i2s_pipeline.py -v --run-hardware
pytest tests/integration/test_uart_resilience.py -v --run-hardware
pytest tests/integration/test_long_duration.py -v --run-hardware
```

**Without hardware (auto-skip):**
```bash
pytest tests/integration/ -v
# All 7 tests skipped with message: "Integration tests require --run-hardware flag"
```

### Integration Test Modules

#### 1. `test_i2s_pipeline.py` (3 tests)

Tests end-to-end audio pipeline: tone/sweep/WAV → I2S → ESP32 → Bluetooth.

**Tests:**

**a) `test_tone_to_bluetooth` (~2 minutes)**
- Generate 1 kHz tone for 10 seconds
- Transmit via I2S to ESP32
- ESP32 sends to Bluetooth speaker
- **Manual verification:** Listen for 1 kHz tone from speaker
- **Automated checks:**
  - I2S status: Active
  - Bluetooth status: BT_PLAYING
  - Underruns: <100

**b) `test_frequency_sweep` (~2 minutes)**
- Generate 20 Hz → 20 kHz chirp (10 seconds)
- Transmit via I2S
- **Manual verification:** Listen for smooth rising pitch
- **Automated checks:**
  - No audio dropouts
  - Underruns: <100

**c) `test_wav_playback` (~2 minutes)**
- Load WAV file from `/home/pi/audio/`
- Resample to 48 kHz if needed
- Transmit via I2S
- **Manual verification:** Recognize WAV file content
- **Automated checks:**
  - WAV loaded successfully
  - Playback duration matches file

**Run:**
```bash
pytest tests/integration/test_i2s_pipeline.py -v --run-hardware
```

#### 2. `test_uart_resilience.py` (2 tests, interactive)

Tests UART connection resilience and error handling.

**Tests:**

**a) `test_disconnect_reconnect` (~2 minutes)**
- Interactive test with user prompts
- Validates auto-reconnect within 10 seconds
- **Steps:**
  1. Verify UART connected
  2. User disconnects ESP32 power
  3. Wait for disconnect detection
  4. User reconnects ESP32 power
  5. Wait for auto-reconnect (≤10 seconds)
- **Checks:**
  - Disconnect detected
  - Auto-reconnect successful
  - Commands work after reconnect

**b) `test_command_during_disconnect` (~1 minute)**
- Send command while UART disconnected
- Validate graceful error handling
- **Checks:**
  - Error response returned
  - No application crash
  - Clear error message

**Run:**
```bash
pytest tests/integration/test_uart_resilience.py -v --run-hardware
# Follow interactive prompts
```

#### 3. `test_long_duration.py` (2 tests, slow)

Tests long-duration stability and memory leak detection.

**Tests:**

**a) `test_one_hour_stability` (60 minutes, marked `@pytest.mark.slow`)**
- Generate continuous tone for 1 hour
- Monitor every 5 minutes
- **Checks:**
  - Underruns: <1000/hour
  - Memory: <150 MB RSS
  - No memory leaks
  - No crashes

**Run:**
```bash
pytest tests/integration/test_long_duration.py::TestLongDuration::test_one_hour_stability -v --run-hardware
# This takes 60 minutes
```

**b) `test_five_minute_baseline` (5 minutes)**
- Quick stability check (5 minutes instead of 60)
- **Checks:**
  - Underruns: <100
  - Memory: <150 MB RSS
  - Stable operation

**Run:**
```bash
pytest tests/integration/test_long_duration.py::TestLongDuration::test_five_minute_baseline -v --run-hardware
```

### Integration Test Duration

| Test | Duration | Manual Steps |
|------|----------|--------------|
| `test_tone_to_bluetooth` | ~2 min | Listen to tone |
| `test_frequency_sweep` | ~2 min | Listen to sweep |
| `test_wav_playback` | ~2 min | Recognize audio |
| `test_disconnect_reconnect` | ~2 min | Interactive prompts |
| `test_command_during_disconnect` | ~1 min | None |
| `test_five_minute_baseline` | ~5 min | None |
| `test_one_hour_stability` | ~60 min | None (optional) |
| **Total (excluding 1-hour)** | **~14 min** | |

---

## Performance Tests

Performance tests validate non-functional requirements (NFRs) from FS.md Section 10.3.

### Hardware Prerequisites

**Required:**
- Raspberry Pi with I2S configured
- Flask web server running (`python main.py`)
- I2S device: `dtoverlay=i2s-mmap` in `/boot/config.txt`

**Optional:**
- ESP32 via UART (for end-to-end tests)
- Logic analyzer (for I2S timing validation)

### Running Performance Tests

Performance tests are **auto-skipped by default** without the `--run-hardware` flag.

**All performance tests:**
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate

# Start web server in separate terminal
python main.py

# Run tests
pytest tests/performance/ -v --run-hardware
```

**Specific module:**
```bash
pytest tests/performance/test_cpu_usage.py -v --run-hardware
pytest tests/performance/test_memory_usage.py -v --run-hardware
```

### Performance Test Modules

#### 1. `test_cpu_usage.py` (5 tests, ~2 minutes)

Tests CPU usage during various audio operations.

**Tests:**

**a) `test_cpu_idle` (10 seconds)**
- Measure CPU when system idle
- **Target:** <10% average CPU
- **Checks:** 10 samples @ 1-second intervals

**b) `test_cpu_tone_generation` (12 seconds)**
- Measure CPU during 1 kHz tone generation
- **Target:** <25% average CPU (FS.md Section 10.3)
- **Checks:** 10 samples during active generation

**c) `test_cpu_wav_playback` (12 seconds)**
- Measure CPU during WAV playback with resampling
- **Target:** <30% average CPU
- **Checks:** 10 samples during playback

**d) `test_cpu_frequency_sweep` (12 seconds)**
- Measure CPU during chirp generation
- **Target:** <25% average CPU
- **Checks:** 10 samples during sweep

**e) `test_process_cpu_affinity` (<1 second)**
- Verify process can use all CPU cores
- **Check:** Process not pinned to single core

**Run:**
```bash
pytest tests/performance/test_cpu_usage.py -v --run-hardware
```

**Expected output:**
```
tests/performance/test_cpu_usage.py::TestCPUUsage::test_cpu_idle PASSED
Measuring idle CPU usage over 10 seconds...
  Sample 1/10: 2.3%
  Sample 2/10: 2.1%
  ...
CPU Usage (idle):
  Average: 2.2%
  Maximum: 3.5%
  Target: <10% average
✓ PASS: Idle CPU usage within target
```

#### 2. `test_memory_usage.py` (4 tests, ~8 minutes)

Tests memory usage and leak detection.

**Tests:**

**a) `test_memory_baseline` (<1 second)**
- Measure baseline memory when idle
- **Target:** <100 MB RSS
- **Check:** Single memory measurement

**b) `test_memory_during_tone_generation` (5 minutes)**
- Generate tone for 5 minutes, sample every 30 seconds
- **Targets:**
  - Average RSS: <100 MB
  - Memory growth: <1 MB/minute
- **Checks:** 11 samples, linear regression for leak detection

**c) `test_memory_after_multiple_operations` (~30 seconds)**
- Perform 10 tone generation cycles
- **Target:** Memory growth <10 MB
- **Check:** Memory released after operations

**d) `test_buffer_allocation` (~10 seconds)**
- Allocate 60-second audio buffer (~22 MB)
- **Checks:**
  - Allocation size reasonable
  - Memory released after clearing buffer

**Run:**
```bash
pytest tests/performance/test_memory_usage.py -v --run-hardware
```

**Expected output:**
```
tests/performance/test_memory_usage.py::TestMemoryUsage::test_memory_during_tone_generation PASSED
Measuring memory usage over 5 minutes (samples every 30s)...
  t=  0s: RSS = 45.2 MB
  t= 30s: RSS = 45.8 MB
  t= 60s: RSS = 46.1 MB
  ...
  t=300s: RSS = 47.3 MB
Memory Analysis (5-minute tone generation):
  Average: 46.2 MB
  Minimum: 45.2 MB
  Maximum: 47.3 MB
  Range: 2.1 MB
  Growth rate: 0.007 MB/minute
  Target: <100 MB average, <1 MB/min growth
✓ PASS: Memory usage and leak detection within targets
```

### Standalone Resource Monitoring

For extended monitoring or manual validation, use the standalone monitoring tool:

**Basic usage:**
```bash
python tests/performance/monitor_resources.py --duration=300
```

**With CSV export:**
```bash
python tests/performance/monitor_resources.py --duration=300 --interval=5 --output=perf.csv
```

**Monitor specific process:**
```bash
python tests/performance/monitor_resources.py --duration=60 --process=main.py
```

**Output example:**
```
Monitoring for 300 seconds (interval: 1s)
Started at: 2026-02-06 12:00:00

  Time       CPU%    RSS(MB)    VMS(MB) Threads
-------------------------------------------------------
    0s       15.2%     45.3 MB   120.5 MB       5
    1s       16.8%     45.4 MB   120.5 MB       5
   ...
  300s       14.1%     46.8 MB   121.2 MB       5

SUMMARY STATISTICS
CPU Usage:
  Average: 15.3%
  Minimum: 12.1%
  Maximum: 18.7%
Memory (RSS):
  Average: 46.1 MB
  Minimum: 45.3 MB
  Maximum: 46.8 MB
  Range: 1.5 MB
  Growth rate: 0.005 MB/minute
  ✓ Memory stable (growth < 0.1 MB/min)
```

### Performance Test Duration

| Test | Duration |
|------|----------|
| `test_cpu_idle` | ~10s |
| `test_cpu_tone_generation` | ~12s |
| `test_cpu_wav_playback` | ~12s |
| `test_cpu_frequency_sweep` | ~12s |
| `test_process_cpu_affinity` | <1s |
| `test_memory_baseline` | <1s |
| `test_memory_during_tone_generation` | ~5 min |
| `test_memory_after_multiple_operations` | ~30s |
| `test_buffer_allocation` | ~10s |
| **Total** | **~8 min** |

---

## Manual Hardware Validation

Some aspects require manual verification with physical hardware.

### I2S Timing Validation (Logic Analyzer)

**Purpose:** Verify I2S signal timing accuracy

**Required Equipment:**
- Logic analyzer (Saleae Logic 8, DSLogic, etc.)
- Probe connections to RPi GPIOs

**Setup:**

1. **Connect probes:**
   - CH0: GPIO18 (BCLK)
   - CH1: GPIO19 (WS / LRCLK)
   - CH2: GPIO21 (DOUT / DATA)
   - GND: RPi GND

2. **Configure logic analyzer:**
   - Sample rate: ≥20 MHz (to capture 1.536 MHz BCLK)
   - Channels: 3 (BCLK, WS, DATA)
   - Trigger: Rising edge on BCLK
   - Duration: 5 seconds

3. **Start audio playback:**
   ```bash
   curl -X POST http://localhost:5000/tone -H "Content-Type: application/json" \
        -d '{"frequency": 1000, "duration": 10, "amplitude": 0.5}'
   curl -X POST http://localhost:5000/i2s/start
   ```

4. **Capture and analyze:**
   - BCLK frequency: Should be **1.536 MHz** (64 × 48 kHz)
     - Acceptable: 1.536 MHz ±50 ppm = 1.53592 - 1.53608 MHz
   - WS frequency: Should be **48 kHz**
     - Acceptable: 48 kHz ±50 ppm = 47.998 - 48.002 kHz
   - BCLK/WS ratio: Exactly **64 BCLK cycles per WS period**
   - Phase alignment: WS transitions on BCLK falling edge
   - DATA transitions: On BCLK rising edge

**Expected waveforms:**
```
BCLK:  ┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌...  (1.536 MHz)
       └┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘
       
WS:    ┌────────────────┐                   (48 kHz, 32 BCLK per half)
       └                └────────────────┐
       
DATA:  ──────╱╲──╱─╱──╱╲╱─╱╲──╱─╱──╱╲─...  (PCM data, MSB-first)
```

### Audio Quality Validation

**Purpose:** Verify audio quality on Bluetooth speaker

**Procedure:**

1. **Generate test tones:**
   - 1 kHz sine wave (should sound pure, no distortion)
   - 100 Hz low frequency (should sound deep, no rattling)
   - 10 kHz high frequency (should sound clear, no artifacts)

2. **Frequency sweep:**
   - 20 Hz → 20 kHz chirp
   - Should sound smooth with no dropouts or glitches
   - Rising pitch should be continuous

3. **WAV playback:**
   - Play known audio file (music, speech)
   - Verify recognizable and clear
   - No clicks, pops, or distortion

4. **Long-duration stability:**
   - Generate 1 kHz tone for 10+ minutes
   - Verify no dropouts or underruns
   - Tone should remain constant

---

## Continuous Integration

### Local Pre-Commit Checks

Before committing code, run unit tests:

```bash
# Run all unit tests
pytest tests/ -v

# Check for test failures
echo $?  # Should be 0 (success)
```

### GitHub Actions (Future)

**Planned CI workflow:**
- Trigger: Push to `master` branch
- Jobs:
  1. **Unit tests:** Run on Ubuntu (no hardware)
  2. **Linting:** flake8, black, mypy
  3. **Coverage:** Generate coverage report, upload to codecov

**Self-hosted runner (optional):**
- Run integration/performance tests on Raspberry Pi hardware
- Scheduled nightly runs
- Performance regression detection

---

## Test Coverage

### Current Coverage

**As of Phase 3 completion:**

| Component | Files | Lines | Coverage |
|-----------|-------|-------|----------|
| `audio/` | 3 | ~450 | 95% |
| `uart/` | 1 | ~200 | 92% |
| `config/` | 1 | ~150 | 88% |
| `telemetry/` | 1 | ~100 | 90% |
| `web/` | 1 | ~300 | 75% (manual testing) |
| **Total** | **7** | **~1,200** | **~88%** |

**Coverage gaps:**
- Web UI JavaScript (requires browser testing)
- Edge cases in error handling
- Some platform-specific I2S code

### Generating Coverage Report

```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate

# Run tests with coverage
pytest tests/ --cov=audio --cov=uart --cov=config --cov=telemetry --cov-report=html

# Open report
firefox htmlcov/index.html  # Or chromium, etc.
```

---

## Troubleshooting

### Unit Tests

**Problem: Import errors**

**Solution:**
```bash
# Ensure virtual environment activated
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

**Problem: Mock-related failures**

**Solution:**
Unit tests use `pytest-mock`. Ensure it's installed:
```bash
pip install pytest-mock
```

### Integration Tests

**Problem: All tests skipped**

**Cause:** Missing `--run-hardware` flag

**Solution:**
```bash
pytest tests/integration/ -v --run-hardware
```

**Problem: "Flask web server not running"**

**Solution:**
Start web server in separate terminal:
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
python main.py
```

**Problem: "I2S ALSA device not found"**

**Solution:**
Enable I2S in `/boot/config.txt`:
```bash
sudo nano /boot/config.txt
# Add:
dtoverlay=i2s-mmap
# Save and reboot
sudo reboot
```

**Problem: "UART device not found"**

**Solution:**
See [SETUP.md](SETUP.md) UART Configuration section.

### Performance Tests

**Problem: CPU usage exceeds target**

**Possible causes:**
- Background processes consuming CPU
- Slow Raspberry Pi model
- Thermal throttling

**Solution:**
```bash
# Check system load
top
htop

# Stop unnecessary services
sudo systemctl stop <service>

# Check CPU temperature
vcgencmd measure_temp
# If >80°C, improve cooling
```

**Problem: Memory growth detected**

**Investigation:**
```bash
# Run standalone monitoring for detailed analysis
python tests/performance/monitor_resources.py --duration=600 --interval=10 --output=leak.csv

# Analyze CSV for memory growth trend
# Check for unreleased NumPy arrays, unclosed file handles
```

---

## Best Practices

### Writing New Tests

**Unit tests:**
1. Use pytest fixtures for setup/teardown
2. Mock external dependencies (I2S driver, UART, file I/O)
3. Test one behavior per test function
4. Use descriptive test names: `test_<component>_<behavior>_<expected_result>`

**Integration tests:**
5. Mark with `@pytest.mark.hardware`
6. Include manual verification steps in test docstring
7. Print clear instructions for user
8. Validate both automated checks and user confirmation

**Performance tests:**
9. Set clear NFR targets in test docstring
10. Print measurement details (samples, averages, targets)
11. Use statistical methods (linear regression for leak detection)
12. Allow for measurement variance (use averages, not single samples)

### Running Tests Efficiently

**During development:**
```bash
# Run only tests related to your changes
pytest tests/test_audio_engine.py -v

# Stop on first failure
pytest tests/ -x

# Re-run failed tests
pytest --lf
```

**Before committing:**
```bash
# Run all unit tests
pytest tests/ -v

# Check for warnings
pytest tests/ -v -W error
```

**On hardware:**
```bash
# Run integration tests (quick: ~14 min)
pytest tests/integration/ -v --run-hardware -k "not one_hour"

# Run performance tests
pytest tests/performance/ -v --run-hardware
```

---

## Summary

| Test Category | Tests | Duration | Hardware | Auto-Skip |
|---------------|-------|----------|----------|-----------|
| **Unit Tests** | 206 | ~43s | None | No |
| **Integration Tests** | 7 | ~14 min | RPi + ESP32 + BT | Yes |
| **Performance Tests** | 9 | ~10 min | RPi + I2S | Yes |
| **Total** | **222** | **~25 min** | | |

**Quick Commands:**
```bash
# Unit tests (no hardware, always run)
pytest tests/ -v

# Integration tests (requires hardware)
pytest tests/integration/ -v --run-hardware

# Performance tests (requires hardware)
pytest tests/performance/ -v --run-hardware

# Everything on hardware
pytest -v --run-hardware
```

---

## Next Steps

After completing testing:

1. **Validate on Hardware:** Run integration and performance tests on Raspberry Pi
2. **Document Results:** Update test reports with actual hardware results
3. **Deploy:** Configure systemd service for production (see [SETUP.md](SETUP.md))
4. **Monitor:** Use performance monitoring for long-term stability validation

---

## Resources

- **pytest Documentation:** https://docs.pytest.org/
- **pytest-mock Plugin:** https://pytest-mock.readthedocs.io/
- **Coverage.py:** https://coverage.readthedocs.io/
- **Test-Driven Development (TDD):** Kent Beck's principles (Red → Green → Refactor)

# Performance Tests

Performance tests validate **non-functional requirements (NFRs)** for the bbgw_i2s_source audio test jig, including CPU usage, memory consumption, and I2S timing accuracy.

## Overview

These tests ensure the system meets performance targets documented in `FS.md Section 10.3`:
- **CPU usage**: <25% during active audio generation, <10% idle
- **Memory usage**: <100 MB RSS, stable over time (no leaks)
- **I2S timing**: 48 kHz ±50 ppm accuracy

## Test Modules

### 1. `test_cpu_usage.py` - CPU Performance Tests

Validates CPU usage during various audio operations:

**Tests:**
- `test_cpu_idle`: Validates <10% CPU when system is idle
- `test_cpu_tone_generation`: Validates <25% CPU during 1 kHz tone generation (FS.md 10.3)
- `test_cpu_wav_playback`: Validates <30% CPU during WAV playback with resampling
- `test_cpu_frequency_sweep`: Validates <25% CPU during frequency sweep (20 Hz → 20 kHz)
- `test_process_cpu_affinity`: Verifies process can use all available CPU cores

**Run time**: ~2 minutes total

### 2. `test_memory_usage.py` - Memory Performance Tests

Validates memory usage and detects memory leaks:

**Tests:**
- `test_memory_baseline`: Validates <100 MB RSS baseline when idle
- `test_memory_during_tone_generation`: 5-minute stability test with leak detection
  - Target: <100 MB average RSS
  - Target: <1 MB/minute memory growth
- `test_memory_after_multiple_operations`: Validates memory release after 10 tone cycles
- `test_buffer_allocation`: Validates audio buffer allocation/deallocation

**Run time**: ~8 minutes total (includes 5-minute stability test)

### 3. `monitor_resources.py` - Standalone Monitoring Tool

Standalone utility for resource monitoring during manual testing or long-duration validation.

**Features:**
- Real-time CPU and memory monitoring
- Configurable sampling interval
- CSV export for analysis
- Summary statistics with leak detection

**Usage:**
```bash
# Monitor for 5 minutes (300 seconds)
python tests/performance/monitor_resources.py --duration=300

# Monitor with 5-second intervals, save to CSV
python tests/performance/monitor_resources.py --duration=300 --interval=5 --output=perf.csv

# Monitor specific process
python tests/performance/monitor_resources.py --duration=60 --process=main.py
```

## Hardware Requirements

**Required:**
- BeagleBone Green Wireless
- McASP I2S interface configured via Device Tree overlay
- Python 3.9+ with required packages

**Optional:**
- ESP32 via UART (for end-to-end tests)
- Logic analyzer (for I2S timing validation - not yet implemented)

**Software Dependencies:**
- `psutil`: CPU and memory monitoring
- `requests`: HTTP client for API testing
- `pytest`: Test framework

Install dependencies:
```bash
pip install psutil requests pytest
```

## Running Performance Tests

### Prerequisites

1. **Start the web server:**
   ```bash
   cd bbgw_i2s_source
   python main.py
   ```

2. **Verify I2S is configured:**
   ```bash
   aplay -l | grep -i mcasp
   # Should show McASP I2S device (BBGW-I2S)
   ```

### Run All Performance Tests

```bash
# Auto-skipped by default (no hardware)
pytest tests/performance/ -v

# Run on BeagleBone with hardware
pytest tests/performance/ -v --run-hardware
```

### Run Specific Test Modules

```bash
# CPU usage tests only (~2 minutes)
pytest tests/performance/test_cpu_usage.py -v --run-hardware

# Memory usage tests only (~8 minutes)
pytest tests/performance/test_memory_usage.py -v --run-hardware
```

### Run Individual Tests

```bash
# Run specific test
pytest tests/performance/test_cpu_usage.py::TestCPUUsage::test_cpu_tone_generation -v --run-hardware

# Run with verbose output
pytest tests/performance/test_memory_usage.py::TestMemoryUsage::test_memory_during_tone_generation -vv --run-hardware
```

## Expected Test Duration

| Test Module | Tests | Duration |
|-------------|-------|----------|
| `test_cpu_usage.py` | 5 | ~2 minutes |
| `test_memory_usage.py` | 4 | ~8 minutes |
| **Total** | **9** | **~10 minutes** |

## Success Criteria

### CPU Usage Targets
- ✅ Idle: <10% average CPU
- ✅ Tone generation: <25% average CPU
- ✅ WAV playback: <30% average CPU
- ✅ Frequency sweep: <25% average CPU

### Memory Usage Targets
- ✅ Baseline idle: <100 MB RSS
- ✅ During operation: <100 MB average RSS
- ✅ Memory growth: <1 MB/minute (leak detection)
- ✅ After operations: Memory properly released (<10 MB growth)

### I2S Timing Targets (Manual with Logic Analyzer)
- ⏸️ BCLK frequency: 1.536 MHz ±50 ppm
- ⏸️ WS (LRCLK) frequency: 48 kHz ±50 ppm
- ⏸️ BCLK/WS phase alignment: correct

## Troubleshooting

### Problem: All tests skipped

**Cause:** Missing `--run-hardware` flag

**Solution:**
```bash
pytest tests/performance/ -v --run-hardware
```

### Problem: "Flask web server not running"

**Cause:** Web server not started or wrong port

**Solution:**
```bash
# Start web server in separate terminal
cd bbgw_i2s_source
python main.py

# Verify it's running
curl http://localhost:5000/status
```

### Problem: "I2S ALSA device not found"

**Cause:** I2S not enabled in `/boot/config.txt`

**Solution:**
```bash
# Edit /boot/config.txt
sudo nano /boot/config.txt

# Add this line:
dtoverlay=i2s-mmap

# Reboot
sudo reboot

# Verify after reboot
aplay -l | grep -i i2s
```

### Problem: High CPU usage fails target

**Cause:** Background processes consuming CPU

**Solution:**
```bash
# Check system load
top
htop

# Stop unnecessary services
sudo systemctl stop <service-name>

# Run tests with minimal background load
```

### Problem: Memory leak detected

**Cause:** Potential memory leak in code or external library

**Investigation:**
```bash
# Use monitor_resources.py for detailed profiling
python tests/performance/monitor_resources.py --duration=600 --interval=10 --output=leak.csv

# Analyze CSV for memory growth trend
# Check for unreleased NumPy arrays, unclosed file handles, etc.
```

## Performance Test Design Notes

### CPU Monitoring Approach
- Uses `psutil.cpu_percent()` with 1-second sampling interval
- Measures over 10 seconds to average out variance
- Reports average, min, max for complete picture

### Memory Leak Detection
- Measures RSS (Resident Set Size) over 5 minutes
- Uses linear regression to calculate growth rate (MB/minute)
- Target: <1 MB/minute indicates stable memory

### Auto-Skip Mechanism
- Tests automatically skip without `--run-hardware` flag
- Prevents false failures on development machines
- Consistent with integration test approach

### Hardware Verification
- `verify_hardware` fixture checks prerequisites before running
- Validates: I2S device, web server, UART (optional)
- Fails fast with clear error messages if hardware missing

## I2S Timing Validation (Manual)

**Note:** Automated I2S timing tests are not yet implemented. Use logic analyzer for manual validation.

### Required Equipment
- Logic analyzer (Saleae Logic 8, etc.)
- Probe connections:
  - BCK (GPIO 18)
  - WS/LRCLK (GPIO 19)
  - DATA (GPIO 21)
  - GND

### Manual Validation Steps

1. **Connect logic analyzer probes**
   - BCK → GPIO 18
   - WS → GPIO 19
   - DATA → GPIO 21
   - GND → GND

2. **Start audio playback**
   ```bash
   curl -X POST http://localhost:5000/tone -H "Content-Type: application/json" \
        -d '{"frequency": 1000, "duration": 10, "amplitude": 0.5}'
   curl -X POST http://localhost:5000/i2s/start
   ```

3. **Capture signals with logic analyzer**
   - Sample rate: ≥20 MHz (for 1.536 MHz BCLK)
   - Duration: ≥5 seconds
   - Channels: BCK, WS, DATA

4. **Analyze timing**
   - **BCLK frequency**: Should be 1.536 MHz (64 × 48 kHz)
     - Acceptable range: 1.536 MHz ±50 ppm = 1.53592 - 1.53608 MHz
   - **WS frequency**: Should be 48 kHz
     - Acceptable range: 48 kHz ±50 ppm = 47.998 - 48.002 kHz
   - **BCLK/WS ratio**: Should be exactly 64 BCLK cycles per WS period (32 per channel)
   - **Phase alignment**: WS should transition on BCLK falling edge

5. **Verify DATA signal**
   - DATA should change on BCLK rising edge
   - MSB-first transmission (I2S standard)
   - 32-bit samples (16-bit data + 16-bit padding)

### Expected Waveforms

```
BCLK:  ┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌...  (1.536 MHz)
       └┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘
       
WS:    ┌────────────────┐                   (48 kHz, 32 BCLK per half)
       └                └────────────────┐
       
DATA:  ──────╱╲──╱─╱──╱╲╱─╱╲──╱─╱──╱╲─...  (PCM data, MSB-first)
```

## Future Enhancements

### Planned Additions
- [ ] Automated I2S timing tests (using GPIO sampling or external analyzer API)
- [ ] Jitter measurement for I2S clock
- [ ] Bluetooth latency measurement
- [ ] End-to-end latency (input → I2S → Bluetooth → speaker)
- [ ] Power consumption monitoring (for battery-powered scenarios)
- [ ] Temperature monitoring under sustained load

### Integration with CI/CD
- Performance tests currently require hardware (Raspberry Pi)
- Could run in CI/CD with:
  - Self-hosted GitHub Actions runner on Raspberry Pi
  - Scheduled nightly performance validation
  - Performance regression detection (compare against baseline)

## References

- **FS.md Section 10.3**: Non-functional requirements for CPU and memory
- **I2S Specification**: Philips I2S bus standard
- **psutil Documentation**: https://psutil.readthedocs.io/
- **pytest Documentation**: https://docs.pytest.org/

# Integration Tests

Integration tests for the RPi I2S Audio Test Jig. These tests validate the complete end-to-end system with actual hardware.

## Requirements

### Hardware
- **Raspberry Pi** (any model with I2S support)
  - I2S interface configured (BCK, WS, DATA on GPIOs 18, 19, 21)
  - UART enabled on /dev/ttyAMA0 (GPIOs 14, 15)
  
- **ESP32** running `esp_bt_audio_source` firmware
  - Connected via I2S (GPIOs 26, 25, 22)
  - Connected via UART (GPIOs 16, 17)
  - Bluetooth speaker paired

- **Bluetooth Speaker** 
  - Paired with ESP32
  - Within range and powered on

### Software
- Main application running: `python main.py`
- All dependencies installed (see main README.md)
- `psutil` package for resource monitoring: `pip install psutil`

## Hardware Setup

### I2S Connections
```
Raspberry Pi          ESP32
GPIO 18 (BCK)    →    GPIO 26 (I2S_BCK)
GPIO 19 (WS)     →    GPIO 25 (I2S_WS)
GPIO 21 (DATA)   →    GPIO 22 (I2S_DATA)
GND              →    GND
```

### UART Connections
```
Raspberry Pi          ESP32
GPIO 14 (TX)     →    GPIO 16 (RX)
GPIO 15 (RX)     →    GPIO 17 (TX)
GND              →    GND
```

## Running Tests

### Quick Validation (5 minutes)
Run the short stability test to validate basic functionality:
```bash
pytest tests/integration/test_long_duration.py::test_five_minute_baseline -v -s --run-hardware
```

### I2S Pipeline Tests
Test complete audio pipeline (tone, sweep, WAV playback):
```bash
pytest tests/integration/test_i2s_pipeline.py -v -s --run-hardware
```

Individual tests:
```bash
# Test tone generation → Bluetooth
pytest tests/integration/test_i2s_pipeline.py::test_tone_to_bluetooth -v -s --run-hardware

# Test frequency sweep
pytest tests/integration/test_i2s_pipeline.py::test_frequency_sweep -v -s --run-hardware

# Test WAV playback (requires /home/pi/audio/test.wav)
pytest tests/integration/test_i2s_pipeline.py::test_wav_playback -v -s --run-hardware
```

### UART Resilience Tests
Test UART disconnect/reconnect handling (requires manual intervention):
```bash
pytest tests/integration/test_uart_resilience.py -v -s --run-hardware
```

### Long Duration Stability
**1-hour test** (validates no memory leaks or performance degradation):
```bash
pytest tests/integration/test_long_duration.py::test_one_hour_stability -v -s --run-hardware
```

### Run All Integration Tests
```bash
pytest tests/integration/ -v -s --run-hardware
```

## Test Organization

### test_i2s_pipeline.py
- `test_tone_to_bluetooth`: End-to-end tone generation (FS.md Section 10.2)
- `test_frequency_sweep`: Validate smooth frequency sweep transmission
- `test_wav_playback`: Test WAV file loading and playback

### test_uart_resilience.py
- `test_disconnect_reconnect`: Validate auto-reconnect after ESP32 disconnect
- `test_command_during_disconnect`: Verify graceful error handling

### test_long_duration.py
- `test_one_hour_stability`: 1-hour continuous tone with resource monitoring
- `test_five_minute_baseline`: 5-minute quick stability check

## Manual Verification

Many integration tests require **manual verification** by listening to the Bluetooth speaker:

- **Tone tests**: Listen for clear, steady 1 kHz tone
- **Sweep tests**: Listen for smooth frequency transition (20 Hz → 20 kHz)
- **WAV tests**: Listen for clear audio without resampling artifacts

Tests will print clear instructions when manual verification is needed.

## Skipping Hardware Tests

By default, hardware tests are skipped when running on development machines:

```bash
# This will skip all hardware tests
pytest tests/integration/ -v
```

To force hardware tests to run, use the `--run-hardware` flag:

```bash
pytest tests/integration/ -v --run-hardware
```

## Troubleshooting

### Web Server Not Running
```bash
# Start the main application first
cd rpi_i2s_source
python main.py
```

### UART Device Not Found
```bash
# Check UART is enabled
ls -l /dev/ttyAMA0

# Enable UART in raspi-config if needed
sudo raspi-config
# → Interface Options → Serial Port → Enable
```

### I2S Device Not Found
```bash
# Verify I2S device exists
arecord -L | grep hw:

# Check device tree overlay in /boot/config.txt
# Should have: dtoverlay=i2s-mmap
```

### ESP32 Not Responding
```bash
# Check ESP32 is powered and programmed
# Check UART connections
# Check ESP32 serial output for errors

# Test UART manually
screen /dev/ttyAMA0 115200
```

### Bluetooth Speaker Not Connected
```bash
# On ESP32, ensure Bluetooth is paired
# Check Bluetooth speaker is powered and in range
# Check ESP32 serial output for Bluetooth status
```

## Expected Test Duration

- `test_tone_to_bluetooth`: ~15 seconds
- `test_frequency_sweep`: ~15 seconds
- `test_wav_playback`: ~10 seconds
- `test_disconnect_reconnect`: ~30 seconds (manual intervention)
- `test_command_during_disconnect`: ~20 seconds (manual intervention)
- `test_five_minute_baseline`: 5 minutes
- `test_one_hour_stability`: 60 minutes

**Total for all tests**: ~6-7 minutes (excluding 1-hour stability test)

## Success Criteria

### I2S Pipeline Tests
- ✅ Tone/sweep/WAV audio audible on Bluetooth speaker
- ✅ No clicks, pops, or dropouts in audio
- ✅ Underruns < 100 during test
- ✅ BT status shows "BT_PLAYING"

### UART Resilience Tests
- ✅ Disconnect detected within 2 seconds
- ✅ Reconnect successful within 10 seconds
- ✅ Commands fail gracefully during disconnect
- ✅ No server crashes or hangs

### Long Duration Tests
- ✅ Underrun rate < 1000 per hour
- ✅ Memory usage stable (< 150 MB)
- ✅ No performance degradation over time
- ✅ System remains responsive throughout

## Notes

- Integration tests complement unit tests (tests/ directory)
- Unit tests validate individual components in isolation
- Integration tests validate complete system on real hardware
- Some tests require manual listening verification
- Long-duration tests are marked with `@pytest.mark.slow`

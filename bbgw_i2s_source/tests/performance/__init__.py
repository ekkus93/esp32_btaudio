"""
Performance tests for rpi_i2s_source.

These tests validate non-functional requirements (NFRs) such as:
- CPU usage during tone generation, WAV playback, and frequency sweeps
- Memory usage and leak detection during sustained operation
- I2S timing accuracy (requires hardware logic analyzer)

**Hardware Requirements:**
- Raspberry Pi with I2S interface configured
- ESP32 via UART (optional, for end-to-end tests)
- Logic analyzer (for I2S timing tests only)

**Running Performance Tests:**

    # Auto-skipped by default (no hardware)
    pytest tests/performance/ -v

    # Run on Raspberry Pi with hardware
    pytest tests/performance/ -v --run-hardware

    # Run specific test modules
    pytest tests/performance/test_cpu_usage.py -v --run-hardware
    pytest tests/performance/test_memory_usage.py -v --run-hardware

**Standalone Resource Monitoring:**

    # Monitor resources for 5 minutes
    python tests/performance/monitor_resources.py --duration=300
"""

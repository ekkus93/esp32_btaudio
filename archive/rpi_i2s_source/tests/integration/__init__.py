"""
Integration tests for RPi I2S Audio Test Jig.

These tests require actual hardware:
- Raspberry Pi with I2S interface
- ESP32 running esp_bt_audio_source firmware
- Bluetooth speaker paired with ESP32
- I2S connections between RPi and ESP32

Tests validate end-to-end functionality of the complete audio pipeline
from tone generation through I2S output to Bluetooth transmission.

Run these tests on the Raspberry Pi:
    pytest tests/integration/ -v --tb=short

Individual test suites:
    pytest tests/integration/test_i2s_pipeline.py -v
    pytest tests/integration/test_uart_resilience.py -v
    pytest tests/integration/test_long_duration.py -v
"""

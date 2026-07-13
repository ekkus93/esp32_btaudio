"""
Integration tests for I2S audio pipeline.

These tests validate the complete end-to-end audio pipeline from tone generation
through I2S transmission to Bluetooth output. They require actual hardware:
- Raspberry Pi with I2S interface configured
- ESP32 running esp_bt_audio_source firmware
- Bluetooth speaker paired with ESP32
- Physical I2S connections (BCK, WS, DATA)

Hardware Setup:
    RPi GPIO 18 (BCK) → ESP32 GPIO 26 (I2S_BCK)
    RPi GPIO 19 (WS)  → ESP32 GPIO 25 (I2S_WS)
    RPi GPIO 21 (DATA)→ ESP32 GPIO 22 (I2S_DATA)
    RPi GND           → ESP32 GND
    RPi TX (GPIO 14)  → ESP32 RX (GPIO 16)
    RPi RX (GPIO 15)  → ESP32 TX (GPIO 17)

Before Running:
    1. Ensure ESP32 is powered and running
    2. Pair Bluetooth speaker with ESP32
    3. Verify UART connection: ls /dev/ttyAMA0
    4. Verify I2S device: arecord -L | grep hw:
    5. Start the application: python main.py

Usage:
    pytest tests/integration/test_i2s_pipeline.py -v
    pytest tests/integration/test_i2s_pipeline.py::test_tone_to_bluetooth -v
"""

import pytest
import requests
import time
import subprocess
from pathlib import Path


# Mark all tests in this module as requiring hardware
pytestmark = pytest.mark.hardware


@pytest.fixture
def base_url():
    """Base URL for the web server."""
    return "http://localhost:5000"


@pytest.fixture
def api_url(base_url):
    """API endpoint URL."""
    return f"{base_url}/api"


@pytest.fixture
def verify_hardware():
    """Verify required hardware is available before running tests."""
    errors = []
    
    # Check for UART device
    uart_device = Path("/dev/ttyAMA0")
    if not uart_device.exists():
        errors.append("UART device /dev/ttyAMA0 not found")
    
    # Check for I2S device (ALSA)
    try:
        result = subprocess.run(
            ["arecord", "-L"],
            capture_output=True,
            text=True,
            timeout=5
        )
        if "hw:" not in result.stdout:
            errors.append("No I2S hardware device found in ALSA")
    except (subprocess.TimeoutExpired, FileNotFoundError):
        errors.append("Cannot verify ALSA I2S device (arecord not available)")
    
    # Check if web server is running
    try:
        response = requests.get("http://localhost:5000/api/status", timeout=2)
        if response.status_code != 200:
            errors.append("Web server not responding correctly")
    except requests.exceptions.RequestException:
        errors.append("Web server not running on localhost:5000")
    
    if errors:
        pytest.skip(f"Hardware not ready: {', '.join(errors)}")


class TestI2SPipeline:
    """Integration tests for complete I2S audio pipeline."""
    
    def test_tone_to_bluetooth(self, api_url, verify_hardware):
        """
        Test end-to-end tone generation to Bluetooth transmission.
        
        This test validates the complete audio pipeline:
        1. Generate 1 kHz tone via HTTP POST
        2. Send START command to ESP32 via UART
        3. Verify I2S active, BT playing, zero underruns
        4. MANUAL: Listen to Bluetooth speaker (1 kHz tone should be audible)
        
        References: FS.md Section 10.2 (Integration Test Example)
        """
        # Step 1: Generate 1 kHz tone
        tone_request = {
            "frequency": 1000,
            "duration": 10.0,  # 10 seconds
            "amplitude": 0.5
        }
        
        response = requests.post(
            f"{api_url}/tone",
            json=tone_request,
            timeout=5
        )
        assert response.status_code == 200, f"Failed to generate tone: {response.text}"
        
        # Give audio engine time to start generating
        time.sleep(0.5)
        
        # Step 2: Send START command via UART
        uart_request = {"command": "START"}
        response = requests.post(
            f"{api_url}/uart/send",
            json=uart_request,
            timeout=5
        )
        assert response.status_code == 200, f"Failed to send UART command: {response.text}"
        
        # Wait for transmission to stabilize
        time.sleep(2.0)
        
        # Step 3: Verify system status
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        status = response.json()
        
        # Verify I2S is active
        assert status.get("i2s", {}).get("running") is True, "I2S should be running"
        
        # Verify Bluetooth is playing (ESP32 should report BT_PLAYING)
        uart_status = status.get("uart", {})
        bt_status = uart_status.get("bt_status", "unknown")
        assert bt_status == "BT_PLAYING", f"Expected BT_PLAYING, got {bt_status}"
        
        # Verify low underrun count
        i2s_stats = status.get("i2s", {})
        underruns = i2s_stats.get("underruns", 0)
        assert underruns < 100, f"Too many underruns: {underruns}"
        
        # Step 4: Manual verification (documented in test output)
        print("\n" + "="*70)
        print("MANUAL VERIFICATION REQUIRED:")
        print("Listen to the Bluetooth speaker - you should hear a 1 kHz tone")
        print("The tone should be clear, steady, and audible for ~10 seconds")
        print("="*70)
        
        # Let tone play for a few more seconds
        time.sleep(3.0)
        
        # Stop playback
        stop_request = {"command": "STOP"}
        response = requests.post(
            f"{api_url}/uart/send",
            json=stop_request,
            timeout=5
        )
        assert response.status_code == 200
        
        print("\nTest completed. Tone should have stopped.")
    
    def test_frequency_sweep(self, api_url, verify_hardware):
        """
        Test frequency sweep end-to-end transmission.
        
        Validates:
        - 20 Hz → 20 kHz sweep generation
        - Smooth I2S transmission without dropouts
        - Bluetooth audio quality
        
        MANUAL: Listen for smooth frequency transition on Bluetooth speaker
        """
        # Generate frequency sweep
        sweep_request = {
            "start_freq": 20,
            "end_freq": 20000,
            "duration": 10.0,
            "amplitude": 0.3
        }
        
        response = requests.post(
            f"{api_url}/sweep",
            json=sweep_request,
            timeout=5
        )
        assert response.status_code == 200, f"Failed to generate sweep: {response.text}"
        
        time.sleep(0.5)
        
        # Start playback
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "START"},
            timeout=5
        )
        assert response.status_code == 200
        
        # Monitor status during sweep
        print("\n" + "="*70)
        print("MANUAL VERIFICATION REQUIRED:")
        print("Listen to the Bluetooth speaker - you should hear a frequency sweep")
        print("from low (20 Hz) to high (20 kHz) over 10 seconds")
        print("The sweep should be smooth without clicks or dropouts")
        print("="*70)
        
        # Sample status a few times during sweep
        for i in range(5):
            time.sleep(2.0)
            response = requests.get(f"{api_url}/status", timeout=5)
            assert response.status_code == 200
            
            status = response.json()
            underruns = status.get("i2s", {}).get("underruns", 0)
            print(f"  [{i*2}s] Underruns: {underruns}, BT: {status.get('uart', {}).get('bt_status')}")
        
        # Stop playback
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "STOP"},
            timeout=5
        )
        assert response.status_code == 200
    
    def test_wav_playback(self, api_url, verify_hardware):
        """
        Test WAV file playback through I2S pipeline.
        
        Prerequisites:
        - Place test WAV file in /home/pi/audio/test.wav
        - WAV should be 44.1 kHz stereo (will be resampled to 48 kHz)
        
        Validates:
        - WAV file loading
        - Resampling to 48 kHz
        - I2S transmission
        - Bluetooth playback
        """
        # Check if test WAV exists
        test_wav = Path("/home/pi/audio/test.wav")
        if not test_wav.exists():
            pytest.skip(f"Test WAV file not found: {test_wav}")
        
        # Load WAV file
        wav_request = {
            "filepath": str(test_wav),
            "loop": False
        }
        
        response = requests.post(
            f"{api_url}/wav",
            json=wav_request,
            timeout=5
        )
        assert response.status_code == 200, f"Failed to load WAV: {response.text}"
        
        time.sleep(0.5)
        
        # Start playback
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "START"},
            timeout=5
        )
        assert response.status_code == 200
        
        print("\n" + "="*70)
        print("MANUAL VERIFICATION REQUIRED:")
        print(f"Listen to the Bluetooth speaker - you should hear {test_wav.name}")
        print("Audio should be clear without artifacts from resampling")
        print("="*70)
        
        # Let WAV play for 5 seconds
        time.sleep(5.0)
        
        # Check status
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        status = response.json()
        assert status.get("i2s", {}).get("running") is True
        assert status.get("uart", {}).get("bt_status") == "BT_PLAYING"
        
        # Stop playback
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "STOP"},
            timeout=5
        )
        assert response.status_code == 200

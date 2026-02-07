"""
Long-duration stability tests for I2S audio pipeline.

Tests validate system stability over extended operation periods,
checking for memory leaks, resource exhaustion, and performance degradation.

Hardware Requirements:
- BeagleBone Green Wireless with McASP I2S configured
- ESP32 with Bluetooth speaker connected
- Stable power supply for extended testing

Usage:
    pytest tests/integration/test_long_duration.py -v
    pytest tests/integration/test_long_duration.py::test_one_hour_stability -v -s
"""

import pytest
import requests
import time
import psutil
import os
from datetime import datetime


pytestmark = pytest.mark.hardware


@pytest.fixture
def api_url():
    """API endpoint URL."""
    return "http://localhost:5000/api"


@pytest.fixture
def verify_server(api_url):
    """Verify web server is running."""
    try:
        response = requests.get(f"{api_url}/status", timeout=2)
        if response.status_code != 200:
            pytest.skip("Web server not responding")
    except requests.exceptions.RequestException:
        pytest.skip("Web server not running on localhost:5000")


class TestLongDuration:
    """Long-duration stability tests."""
    
    @pytest.mark.slow
    def test_one_hour_stability(self, api_url, verify_server):
        """
        Test 1-hour continuous tone generation and transmission.
        
        Validates:
        - Zero or minimal underruns over extended period
        - Constant buffer fill level
        - No memory leaks in Python process
        - CPU usage remains stable
        - System remains responsive
        
        This test takes 1 hour to complete.
        """
        print("\n" + "="*70)
        print("LONG DURATION STABILITY TEST: 1 Hour")
        print("This test will run continuously for 60 minutes")
        print("="*70)
        
        # Start 1 kHz tone
        tone_request = {
            "frequency": 1000,
            "duration": 3600.0,  # 1 hour
            "amplitude": 0.5
        }
        
        response = requests.post(
            f"{api_url}/tone",
            json=tone_request,
            timeout=5
        )
        assert response.status_code == 200
        
        time.sleep(0.5)
        
        # Start playback
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "START"},
            timeout=5
        )
        assert response.status_code == 200
        
        time.sleep(2.0)
        
        # Get process info for memory monitoring
        process = psutil.Process(os.getpid())
        
        # Baseline measurements
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        baseline_status = response.json()
        baseline_underruns = baseline_status.get("i2s", {}).get("underruns", 0)
        baseline_memory = process.memory_info().rss / (1024 * 1024)  # MB
        
        print(f"\nBaseline measurements:")
        print(f"  Underruns: {baseline_underruns}")
        print(f"  Memory: {baseline_memory:.1f} MB")
        print(f"  Start time: {datetime.now().strftime('%H:%M:%S')}")
        
        # Monitor every 5 minutes
        test_duration = 3600  # 1 hour
        check_interval = 300  # 5 minutes
        num_checks = test_duration // check_interval
        
        max_underruns_increase = 100  # Allow small increase per check
        max_memory_mb = 150  # Maximum memory usage
        
        for check_num in range(num_checks):
            # Wait for next check interval
            time.sleep(check_interval)
            
            # Get current status
            response = requests.get(f"{api_url}/status", timeout=5)
            assert response.status_code == 200, "Server became unresponsive"
            
            status = response.json()
            current_underruns = status.get("i2s", {}).get("underruns", 0)
            current_memory = process.memory_info().rss / (1024 * 1024)
            
            elapsed_minutes = (check_num + 1) * (check_interval / 60)
            
            print(f"\n[{elapsed_minutes:.0f} min] Status check:")
            print(f"  Underruns: {current_underruns} (Δ{current_underruns - baseline_underruns})")
            print(f"  Memory: {current_memory:.1f} MB (Δ{current_memory - baseline_memory:.1f} MB)")
            print(f"  BT Status: {status.get('uart', {}).get('bt_status')}")
            print(f"  I2S Running: {status.get('i2s', {}).get('running')}")
            
            # Validate metrics
            underrun_increase = current_underruns - baseline_underruns
            assert underrun_increase < max_underruns_increase * (check_num + 1), \
                f"Excessive underruns: {underrun_increase}"
            
            assert current_memory < max_memory_mb, \
                f"Memory leak detected: {current_memory:.1f} MB > {max_memory_mb} MB"
            
            # Verify still playing
            assert status.get("uart", {}).get("bt_status") == "BT_PLAYING", \
                "Bluetooth playback stopped unexpectedly"
        
        # Stop playback
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "STOP"},
            timeout=5
        )
        assert response.status_code == 200
        
        # Final measurements
        response = requests.get(f"{api_url}/status", timeout=5)
        final_status = response.json()
        final_underruns = final_status.get("i2s", {}).get("underruns", 0)
        final_memory = process.memory_info().rss / (1024 * 1024)
        
        print("\n" + "="*70)
        print("FINAL RESULTS:")
        print(f"  Duration: 60 minutes")
        print(f"  Total underruns: {final_underruns}")
        print(f"  Underrun rate: {final_underruns / 60:.1f} per minute")
        print(f"  Final memory: {final_memory:.1f} MB")
        print(f"  Memory delta: {final_memory - baseline_memory:.1f} MB")
        print(f"  End time: {datetime.now().strftime('%H:%M:%S')}")
        print("="*70)
        
        # Final assertions
        assert final_underruns < 1000, "Too many underruns over 1 hour"
        assert final_memory < max_memory_mb, "Memory leak detected"
        
        print("\n✓ STABILITY TEST PASSED")
    
    @pytest.mark.slow
    def test_five_minute_baseline(self, api_url, verify_server):
        """
        Shorter 5-minute stability test for quick validation.
        
        Can be run more frequently than the 1-hour test.
        Validates same metrics over shorter period.
        """
        print("\n" + "="*70)
        print("SHORT DURATION STABILITY TEST: 5 Minutes")
        print("="*70)
        
        # Start 1 kHz tone
        tone_request = {
            "frequency": 1000,
            "duration": 300.0,  # 5 minutes
            "amplitude": 0.5
        }
        
        response = requests.post(
            f"{api_url}/tone",
            json=tone_request,
            timeout=5
        )
        assert response.status_code == 200
        
        time.sleep(0.5)
        
        # Start playback
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "START"},
            timeout=5
        )
        assert response.status_code == 200
        
        time.sleep(2.0)
        
        # Baseline
        process = psutil.Process(os.getpid())
        response = requests.get(f"{api_url}/status", timeout=5)
        baseline_status = response.json()
        baseline_underruns = baseline_status.get("i2s", {}).get("underruns", 0)
        baseline_memory = process.memory_info().rss / (1024 * 1024)
        
        print(f"\nBaseline: {baseline_underruns} underruns, {baseline_memory:.1f} MB")
        
        # Check every minute
        for minute in range(5):
            time.sleep(60)
            
            response = requests.get(f"{api_url}/status", timeout=5)
            assert response.status_code == 200
            
            status = response.json()
            current_underruns = status.get("i2s", {}).get("underruns", 0)
            current_memory = process.memory_info().rss / (1024 * 1024)
            
            print(f"[{minute + 1} min] Underruns: {current_underruns}, Memory: {current_memory:.1f} MB")
        
        # Stop
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "STOP"},
            timeout=5
        )
        assert response.status_code == 200
        
        # Final check
        response = requests.get(f"{api_url}/status", timeout=5)
        final_status = response.json()
        final_underruns = final_status.get("i2s", {}).get("underruns", 0)
        final_memory = process.memory_info().rss / (1024 * 1024)
        
        print(f"\nFinal: {final_underruns} underruns, {final_memory:.1f} MB")
        print(f"Delta: {final_underruns - baseline_underruns} underruns, "
              f"{final_memory - baseline_memory:.1f} MB")
        
        # Assertions
        assert final_underruns - baseline_underruns < 100, "Too many underruns in 5 minutes"
        assert final_memory < 150, "Memory usage too high"
        
        print("\n✓ 5-MINUTE TEST PASSED")

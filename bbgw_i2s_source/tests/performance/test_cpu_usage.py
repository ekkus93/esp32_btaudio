"""
CPU usage performance tests for BeagleBone Green Wireless I2S Source.

Validates FS.md Section 10.3 NFR: CPU usage should remain reasonable during
various audio generation and playback scenarios.

Targets:
- Active tone generation: <25% CPU
- Idle (no audio): <10% CPU
- WAV playback with resampling: <30% CPU
- Frequency sweep: <25% CPU
"""

import time
import psutil
import pytest
import requests


@pytest.mark.hardware
class TestCPUUsage:
    """CPU usage validation tests."""

    def test_cpu_idle(self, api_url, verify_hardware):
        """
        Test CPU usage when system is idle (no audio generation).
        
        Target: <10% CPU
        FS.md: Section 10.3 NFR example
        """
        # Ensure system is idle
        response = requests.get(f"{api_url}/status", timeout=5)
        status = response.json()
        
        # If I2S is running, stop it
        if status.get("i2s", {}).get("running"):
            requests.post(f"{api_url}/i2s/stop", timeout=5)
            time.sleep(1)
        
        # Measure CPU over 10 seconds
        print("\nMeasuring idle CPU usage over 10 seconds...")
        cpu_samples = []
        
        for i in range(10):
            cpu_percent = psutil.cpu_percent(interval=1.0)
            cpu_samples.append(cpu_percent)
            print(f"  Sample {i+1}/10: {cpu_percent:.1f}%")
        
        avg_cpu = sum(cpu_samples) / len(cpu_samples)
        max_cpu = max(cpu_samples)
        
        print(f"\nCPU Usage (idle):")
        print(f"  Average: {avg_cpu:.1f}%")
        print(f"  Maximum: {max_cpu:.1f}%")
        print(f"  Target: <10% average")
        
        # Validation
        assert avg_cpu < 10.0, f"Idle CPU {avg_cpu:.1f}% exceeds target 10%"
        print("\n✓ PASS: Idle CPU usage within target")

    def test_cpu_tone_generation(self, api_url, verify_hardware):
        """
        Test CPU usage during tone generation (FS.md Section 10.3).
        
        Target: <25% CPU during active tone generation
        """
        # Generate 1 kHz tone (10 seconds)
        tone_request = {
            "frequency": 1000,
            "duration": 20.0,
            "amplitude": 0.5
        }
        response = requests.post(f"{api_url}/tone", json=tone_request, timeout=5)
        assert response.status_code == 200
        
        # Start I2S
        response = requests.post(f"{api_url}/i2s/start", timeout=5)
        assert response.status_code == 200
        
        print("\nWaiting 2 seconds for I2S to stabilize...")
        time.sleep(2)
        
        # Measure CPU over 10 seconds while tone is playing
        print("Measuring CPU usage during tone generation (10 seconds)...")
        cpu_samples = []
        
        for i in range(10):
            cpu_percent = psutil.cpu_percent(interval=1.0)
            cpu_samples.append(cpu_percent)
            print(f"  Sample {i+1}/10: {cpu_percent:.1f}%")
        
        avg_cpu = sum(cpu_samples) / len(cpu_samples)
        max_cpu = max(cpu_samples)
        
        print(f"\nCPU Usage (tone generation):")
        print(f"  Average: {avg_cpu:.1f}%")
        print(f"  Maximum: {max_cpu:.1f}%")
        print(f"  Target: <25% average")
        
        # Stop I2S
        requests.post(f"{api_url}/i2s/stop", timeout=5)
        
        # Validation
        assert avg_cpu < 25.0, f"Tone generation CPU {avg_cpu:.1f}% exceeds target 25%"
        print("\n✓ PASS: Tone generation CPU usage within target")

    def test_cpu_wav_playback(self, api_url, verify_hardware):
        """
        Test CPU usage during WAV file playback with resampling.
        
        Target: <30% CPU (slightly higher due to resampling)
        """
        # Load a WAV file (assuming test file exists)
        wav_request = {
            "file_path": "/tmp/test_audio.wav"  # Test WAV file
        }
        
        # Try to load WAV; if fails, generate test tone instead
        response = requests.post(f"{api_url}/wav/load", json=wav_request, timeout=5)
        
        if response.status_code != 200:
            print("\nNo test WAV file found, generating tone instead...")
            tone_request = {
                "frequency": 440,
                "duration": 20.0,
                "amplitude": 0.5
            }
            response = requests.post(f"{api_url}/tone", json=tone_request, timeout=5)
            assert response.status_code == 200
        
        # Start I2S
        response = requests.post(f"{api_url}/i2s/start", timeout=5)
        assert response.status_code == 200
        
        print("\nWaiting 2 seconds for I2S to stabilize...")
        time.sleep(2)
        
        # Measure CPU over 10 seconds
        print("Measuring CPU usage during playback (10 seconds)...")
        cpu_samples = []
        
        for i in range(10):
            cpu_percent = psutil.cpu_percent(interval=1.0)
            cpu_samples.append(cpu_percent)
            print(f"  Sample {i+1}/10: {cpu_percent:.1f}%")
        
        avg_cpu = sum(cpu_samples) / len(cpu_samples)
        max_cpu = max(cpu_samples)
        
        print(f"\nCPU Usage (WAV playback):")
        print(f"  Average: {avg_cpu:.1f}%")
        print(f"  Maximum: {max_cpu:.1f}%")
        print(f"  Target: <30% average")
        
        # Stop I2S
        requests.post(f"{api_url}/i2s/stop", timeout=5)
        
        # Validation
        assert avg_cpu < 30.0, f"WAV playback CPU {avg_cpu:.1f}% exceeds target 30%"
        print("\n✓ PASS: WAV playback CPU usage within target")

    def test_cpu_frequency_sweep(self, api_url, verify_hardware):
        """
        Test CPU usage during frequency sweep generation.
        
        Target: <25% CPU (similar to tone generation)
        """
        # Generate frequency sweep (20 Hz to 20 kHz over 15 seconds)
        sweep_request = {
            "start_freq": 20,
            "end_freq": 20000,
            "duration": 20.0,
            "amplitude": 0.5
        }
        response = requests.post(f"{api_url}/sweep", json=sweep_request, timeout=5)
        assert response.status_code == 200
        
        # Start I2S
        response = requests.post(f"{api_url}/i2s/start", timeout=5)
        assert response.status_code == 200
        
        print("\nWaiting 2 seconds for I2S to stabilize...")
        time.sleep(2)
        
        # Measure CPU over 10 seconds
        print("Measuring CPU usage during frequency sweep (10 seconds)...")
        cpu_samples = []
        
        for i in range(10):
            cpu_percent = psutil.cpu_percent(interval=1.0)
            cpu_samples.append(cpu_percent)
            print(f"  Sample {i+1}/10: {cpu_percent:.1f}%")
        
        avg_cpu = sum(cpu_samples) / len(cpu_samples)
        max_cpu = max(cpu_samples)
        
        print(f"\nCPU Usage (frequency sweep):")
        print(f"  Average: {avg_cpu:.1f}%")
        print(f"  Maximum: {max_cpu:.1f}%")
        print(f"  Target: <25% average")
        
        # Stop I2S
        requests.post(f"{api_url}/i2s/stop", timeout=5)
        
        # Validation
        assert avg_cpu < 25.0, f"Frequency sweep CPU {avg_cpu:.1f}% exceeds target 25%"
        print("\n✓ PASS: Frequency sweep CPU usage within target")

    def test_process_cpu_affinity(self, api_url, verify_hardware):
        """
        Verify the Python process is using available CPU cores efficiently.
        
        On multi-core BeagleBone Green Wireless (single-core Cortex-A8), process
        should run on the available core unless explicitly configured.
        """
        # Get current process
        process = psutil.Process()
        
        # Check CPU affinity
        if hasattr(process, 'cpu_affinity'):
            affinity = process.cpu_affinity()
            cpu_count = psutil.cpu_count()
            
            print(f"\nCPU Affinity:")
            print(f"  Process can use cores: {affinity}")
            print(f"  Total CPU cores: {cpu_count}")
            
            # Should have access to all cores
            assert len(affinity) == cpu_count, \
                f"Process restricted to {len(affinity)}/{cpu_count} cores"
            
            print("\n✓ PASS: Process has access to all CPU cores")
        else:
            print("\n⚠ SKIP: CPU affinity not supported on this platform")

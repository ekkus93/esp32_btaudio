"""
Memory usage performance tests for BeagleBone Green Wireless I2S Source.

Validates FS.md Section 10.3 NFR: Memory usage should remain stable
during sustained operation with no memory leaks.

Targets:
- Total Python process RSS: <100 MB
- Memory stable over 5 minutes (no leaks)
- Memory growth rate: <1 MB/minute
"""

import time
import psutil
import pytest
import requests


@pytest.mark.hardware
class TestMemoryUsage:
    """Memory usage and leak detection tests."""

    def test_memory_baseline(self, api_url, verify_hardware):
        """
        Measure baseline memory usage when system is idle.
        
        Target: <100 MB RSS
        """
        # Ensure system is idle
        response = requests.get(f"{api_url}/status", timeout=5)
        status = response.json()
        
        # If I2S is running, stop it
        if status.get("i2s", {}).get("running"):
            requests.post(f"{api_url}/i2s/stop", timeout=5)
            time.sleep(1)
        
        # Get current process memory
        process = psutil.Process()
        mem_info = process.memory_info()
        
        rss_mb = mem_info.rss / (1024 * 1024)
        vms_mb = mem_info.vms / (1024 * 1024)
        
        print(f"\nBaseline Memory Usage:")
        print(f"  RSS (physical): {rss_mb:.1f} MB")
        print(f"  VMS (virtual): {vms_mb:.1f} MB")
        print(f"  Target: <100 MB RSS")
        
        # Validation
        assert rss_mb < 100.0, f"Baseline RSS {rss_mb:.1f} MB exceeds target 100 MB"
        print("\n✓ PASS: Baseline memory usage within target")

    def test_memory_during_tone_generation(self, api_url, verify_hardware):
        """
        Test memory usage during 5-minute tone generation.
        
        Validates:
        - Memory stays <100 MB RSS
        - No memory leak (stable over time)
        """
        # Generate long tone (6 minutes to allow for 5 minutes of measurement)
        tone_request = {
            "frequency": 1000,
            "duration": 360.0,  # 6 minutes
            "amplitude": 0.5
        }
        response = requests.post(f"{api_url}/tone", json=tone_request, timeout=5)
        assert response.status_code == 200
        
        # Start I2S
        response = requests.post(f"{api_url}/i2s/start", timeout=5)
        assert response.status_code == 200
        
        print("\nWaiting 10 seconds for system to stabilize...")
        time.sleep(10)
        
        # Measure memory over 5 minutes (sample every 30 seconds)
        print("Measuring memory usage over 5 minutes (samples every 30s)...")
        
        process = psutil.Process()
        memory_samples = []
        timestamps = []
        
        for i in range(11):  # 0, 30, 60, ..., 300 seconds = 11 samples
            mem_info = process.memory_info()
            rss_mb = mem_info.rss / (1024 * 1024)
            
            memory_samples.append(rss_mb)
            timestamps.append(i * 30)
            
            print(f"  t={timestamps[-1]:3d}s: RSS = {rss_mb:.1f} MB")
            
            if i < 10:  # Don't sleep after last sample
                time.sleep(30)
        
        # Stop I2S
        requests.post(f"{api_url}/i2s/stop", timeout=5)
        
        # Analysis
        avg_memory = sum(memory_samples) / len(memory_samples)
        max_memory = max(memory_samples)
        min_memory = min(memory_samples)
        
        # Calculate memory growth rate (linear regression slope)
        n = len(memory_samples)
        sum_x = sum(timestamps)
        sum_y = sum(memory_samples)
        sum_xy = sum(t * m for t, m in zip(timestamps, memory_samples))
        sum_x2 = sum(t * t for t in timestamps)
        
        slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x)
        growth_rate_mb_per_sec = slope
        growth_rate_mb_per_min = slope * 60
        
        print(f"\nMemory Analysis (5-minute tone generation):")
        print(f"  Average: {avg_memory:.1f} MB")
        print(f"  Minimum: {min_memory:.1f} MB")
        print(f"  Maximum: {max_memory:.1f} MB")
        print(f"  Range: {max_memory - min_memory:.1f} MB")
        print(f"  Growth rate: {growth_rate_mb_per_min:.3f} MB/minute")
        print(f"  Target: <100 MB average, <1 MB/min growth")
        
        # Validations
        assert avg_memory < 100.0, f"Average memory {avg_memory:.1f} MB exceeds target 100 MB"
        assert growth_rate_mb_per_min < 1.0, \
            f"Memory growth {growth_rate_mb_per_min:.3f} MB/min exceeds target 1 MB/min"
        
        print("\n✓ PASS: Memory usage and leak detection within targets")

    def test_memory_after_multiple_operations(self, api_url, verify_hardware):
        """
        Test memory after multiple tone generations and stops.
        
        Validates that memory is properly released after operations.
        """
        process = psutil.Process()
        
        # Get baseline
        mem_baseline = process.memory_info().rss / (1024 * 1024)
        print(f"\nBaseline memory: {mem_baseline:.1f} MB")
        
        # Perform 10 iterations of: generate tone → start I2S → wait → stop
        print("\nPerforming 10 tone generation cycles...")
        memory_after_each = []
        
        for i in range(10):
            # Generate short tone
            tone_request = {
                "frequency": 440 + (i * 100),  # Vary frequency
                "duration": 5.0,
                "amplitude": 0.5
            }
            response = requests.post(f"{api_url}/tone", json=tone_request, timeout=5)
            assert response.status_code == 200
            
            # Start I2S
            response = requests.post(f"{api_url}/i2s/start", timeout=5)
            assert response.status_code == 200
            
            # Wait briefly
            time.sleep(2)
            
            # Stop I2S
            response = requests.post(f"{api_url}/i2s/stop", timeout=5)
            assert response.status_code == 200
            
            # Measure memory
            mem_info = process.memory_info()
            rss_mb = mem_info.rss / (1024 * 1024)
            memory_after_each.append(rss_mb)
            
            print(f"  Iteration {i+1:2d}: {rss_mb:.1f} MB")
            
            time.sleep(1)
        
        # Final memory check
        mem_final = process.memory_info().rss / (1024 * 1024)
        mem_growth = mem_final - mem_baseline
        
        avg_after_ops = sum(memory_after_each) / len(memory_after_each)
        
        print(f"\nMemory Summary (10 operations):")
        print(f"  Baseline: {mem_baseline:.1f} MB")
        print(f"  Final: {mem_final:.1f} MB")
        print(f"  Growth: {mem_growth:.1f} MB")
        print(f"  Average during ops: {avg_after_ops:.1f} MB")
        print(f"  Target: <10 MB growth after operations")
        
        # Validation: Memory growth should be minimal
        assert mem_growth < 10.0, \
            f"Memory grew by {mem_growth:.1f} MB after 10 operations (target: <10 MB)"
        
        print("\n✓ PASS: Memory properly released after operations")

    def test_buffer_allocation(self, api_url, verify_hardware):
        """
        Test memory usage when allocating large audio buffers.
        
        Validates that buffer allocation is reasonable and doesn't
        cause excessive memory usage.
        """
        process = psutil.Process()
        
        # Get baseline
        mem_baseline = process.memory_info().rss / (1024 * 1024)
        print(f"\nBaseline memory: {mem_baseline:.1f} MB")
        
        # Generate long duration tone (this allocates large buffer)
        tone_request = {
            "frequency": 1000,
            "duration": 60.0,  # 60 seconds = ~11.5 MB buffer (48kHz, stereo, float32)
            "amplitude": 0.5
        }
        response = requests.post(f"{api_url}/tone", json=tone_request, timeout=5)
        assert response.status_code == 200
        
        # Measure memory after allocation
        time.sleep(1)  # Allow allocation to complete
        mem_after_alloc = process.memory_info().rss / (1024 * 1024)
        mem_increase = mem_after_alloc - mem_baseline
        
        print(f"\nMemory after allocating 60s buffer:")
        print(f"  Before: {mem_baseline:.1f} MB")
        print(f"  After: {mem_after_alloc:.1f} MB")
        print(f"  Increase: {mem_increase:.1f} MB")
        print(f"  Expected: ~11.5 MB (60s @ 48kHz stereo float32)")
        
        # Expected: 48000 samples/sec * 2 channels * 4 bytes * 60 sec = 23,040,000 bytes ≈ 22 MB
        # Allow for overhead
        assert mem_increase < 50.0, \
            f"Buffer allocation used {mem_increase:.1f} MB (expected ~22 MB + overhead)"
        
        # Clear buffer (generate shorter tone to replace)
        tone_request = {"frequency": 1000, "duration": 1.0, "amplitude": 0.5}
        requests.post(f"{api_url}/tone", json=tone_request, timeout=5)
        
        # Check if memory is released
        time.sleep(2)
        mem_after_clear = process.memory_info().rss / (1024 * 1024)
        
        print(f"  After clearing: {mem_after_clear:.1f} MB")
        print(f"  Released: {mem_after_alloc - mem_after_clear:.1f} MB")
        
        # Memory should be mostly released (some overhead acceptable)
        assert mem_after_clear < mem_baseline + 20.0, \
            f"Memory not properly released: {mem_after_clear:.1f} MB vs baseline {mem_baseline:.1f} MB"
        
        print("\n✓ PASS: Buffer allocation and deallocation working correctly")

"""
Unit tests for RingBuffer class

Tests FIFO behavior, overflow handling, underrun detection, and thread safety.

Author: bbgw_i2s_source
Date: 2026-02-07
"""

import pytest
import numpy as np
import threading
import time
from audio.ring_buffer import RingBuffer


class TestRingBufferBasic:
    """Basic functionality tests."""
    
    def test_init_valid_capacity(self):
        """Test initialization with valid capacity."""
        rb = RingBuffer(capacity=1024)
        assert rb.capacity == 1024
        assert rb.size == 0
        assert rb.get_fill_percentage() == 0.0
        
    def test_init_invalid_capacity(self):
        """Test initialization with invalid capacity raises ValueError."""
        with pytest.raises(ValueError, match="Capacity must be positive"):
            RingBuffer(capacity=0)
        with pytest.raises(ValueError, match="Capacity must be positive"):
            RingBuffer(capacity=-100)
            
    def test_write_read_roundtrip(self):
        """Test write/read roundtrip (FIFO order)."""
        rb = RingBuffer(capacity=100)
        samples_in = np.array([1, 2, 3, 4, 5], dtype=np.int16)
        
        written = rb.write(samples_in)
        assert written == 5
        assert rb.size == 5
        
        samples_out = rb.read(5)
        assert samples_out is not None
        np.testing.assert_array_equal(samples_out, samples_in)
        assert rb.size == 0
        
    def test_write_invalid_type(self):
        """Test write with non-numpy array raises TypeError."""
        rb = RingBuffer(capacity=100)
        with pytest.raises(TypeError, match="Expected numpy array"):
            rb.write([1, 2, 3])
            
    def test_write_invalid_dtype(self):
        """Test write with wrong dtype raises ValueError."""
        rb = RingBuffer(capacity=100)
        samples = np.array([1, 2, 3], dtype=np.float32)
        with pytest.raises(ValueError, match="Expected int16 dtype"):
            rb.write(samples)
            
    def test_read_invalid_count(self):
        """Test read with invalid num_samples raises ValueError."""
        rb = RingBuffer(capacity=100)
        with pytest.raises(ValueError, match="num_samples must be positive"):
            rb.read(0)
        with pytest.raises(ValueError, match="num_samples must be positive"):
            rb.read(-5)


class TestRingBufferOverflow:
    """Overflow handling tests (drop-oldest policy)."""
    
    def test_overflow_drop_oldest(self):
        """Test overflow drops oldest samples."""
        rb = RingBuffer(capacity=10)
        
        # Write 10 samples (fill buffer)
        samples1 = np.arange(10, dtype=np.int16)
        rb.write(samples1)
        assert rb.size == 10
        
        # Write 5 more samples (overflow by 5)
        samples2 = np.arange(10, 15, dtype=np.int16)
        rb.write(samples2)
        assert rb.size == 10  # Still at capacity
        
        # Read all samples - should get samples 5-14 (oldest 0-4 dropped)
        result = rb.read(10)
        expected = np.arange(5, 15, dtype=np.int16)
        np.testing.assert_array_equal(result, expected)
        
    def test_overflow_count(self):
        """Test overflow counter increments."""
        rb = RingBuffer(capacity=10)
        rb.write(np.arange(10, dtype=np.int16))  # Fill buffer
        
        stats = rb.get_stats()
        assert stats['overflows'] == 0
        
        rb.write(np.arange(5, dtype=np.int16))  # Cause overflow
        stats = rb.get_stats()
        assert stats['overflows'] == 1
        
    def test_overflow_exceeds_capacity(self):
        """Test writing more samples than capacity at once."""
        rb = RingBuffer(capacity=10)
        
        # Write 20 samples - should keep only last 10
        samples = np.arange(20, dtype=np.int16)
        rb.write(samples)
        
        result = rb.read(10)
        expected = np.arange(10, 20, dtype=np.int16)
        np.testing.assert_array_equal(result, expected)


class TestRingBufferUnderrun:
    """Underrun handling tests (return None)."""
    
    def test_underrun_empty_buffer(self):
        """Test read from empty buffer returns None."""
        rb = RingBuffer(capacity=100)
        result = rb.read(10)
        assert result is None
        
        stats = rb.get_stats()
        assert stats['underruns'] == 1
        
    def test_underrun_insufficient_samples(self):
        """Test read more samples than available returns None."""
        rb = RingBuffer(capacity=100)
        rb.write(np.arange(5, dtype=np.int16))
        
        result = rb.read(10)  # Try to read 10, only 5 available
        assert result is None
        assert rb.size == 5  # Buffer unchanged after underrun
        
        stats = rb.get_stats()
        assert stats['underruns'] == 1
        
    def test_partial_read_success(self):
        """Test reading exactly available samples succeeds."""
        rb = RingBuffer(capacity=100)
        rb.write(np.arange(5, dtype=np.int16))
        
        result = rb.read(5)  # Read exactly 5 available samples
        assert result is not None
        np.testing.assert_array_equal(result, np.arange(5, dtype=np.int16))


class TestRingBufferWrapAround:
    """Test buffer wrap-around at end of circular buffer."""
    
    def test_write_wrap_around(self):
        """Test write wraps around buffer end."""
        rb = RingBuffer(capacity=10)
        
        # Fill buffer
        rb.write(np.arange(10, dtype=np.int16))
        
        # Read 7 samples (read_ptr at 7, write_ptr at 10/0)
        rb.read(7)
        
        # Write 5 samples (should wrap: 3 at end, 2 at start)
        samples = np.array([100, 101, 102, 103, 104], dtype=np.int16)
        rb.write(samples)
        
        # Read all 8 samples (3 original + 5 new)
        result = rb.read(8)
        expected = np.array([7, 8, 9, 100, 101, 102, 103, 104], dtype=np.int16)
        np.testing.assert_array_equal(result, expected)
        
    def test_read_wrap_around(self):
        """Test read wraps around buffer end."""
        rb = RingBuffer(capacity=10)
        
        # Position pointers near end: write 7, read 5 (leaves 2 at indices 5-6)
        rb.write(np.arange(7, dtype=np.int16))
        rb.read(5)
        
        # Write more to wrap write_ptr: indices 7,8,9,0,1,2
        rb.write(np.arange(10, 16, dtype=np.int16))
        
        # Read all 8 samples (2 old + 6 new) - should wrap at read
        result = rb.read(8)
        expected = np.array([5, 6, 10, 11, 12, 13, 14, 15], dtype=np.int16)
        np.testing.assert_array_equal(result, expected)


class TestRingBufferClear:
    """Test buffer clear functionality."""
    
    def test_clear_resets_pointers(self):
        """Test clear resets read/write pointers."""
        rb = RingBuffer(capacity=100)
        rb.write(np.arange(50, dtype=np.int16))
        rb.read(20)
        
        assert rb.size > 0
        rb.clear()
        
        assert rb.size == 0
        assert rb.get_fill_percentage() == 0.0
        
    def test_clear_allows_fresh_start(self):
        """Test writing after clear works correctly."""
        rb = RingBuffer(capacity=100)
        rb.write(np.arange(50, dtype=np.int16))
        rb.clear()
        
        samples = np.array([100, 101, 102], dtype=np.int16)
        rb.write(samples)
        
        result = rb.read(3)
        np.testing.assert_array_equal(result, samples)


class TestRingBufferFillPercentage:
    """Test fill percentage calculation."""
    
    def test_fill_percentage_empty(self):
        """Test 0% fill on empty buffer."""
        rb = RingBuffer(capacity=100)
        assert rb.get_fill_percentage() == 0.0
        
    def test_fill_percentage_full(self):
        """Test 100% fill on full buffer."""
        rb = RingBuffer(capacity=100)
        rb.write(np.arange(100, dtype=np.int16))
        assert rb.get_fill_percentage() == 100.0
        
    def test_fill_percentage_half(self):
        """Test 50% fill."""
        rb = RingBuffer(capacity=100)
        rb.write(np.arange(50, dtype=np.int16))
        assert rb.get_fill_percentage() == 50.0


class TestRingBufferThreadSafety:
    """Test concurrent access (2 writers + 2 readers per FS.md Section 10.1)."""
    
    def test_concurrent_access(self):
        """Test 2 writers + 2 readers concurrently."""
        rb = RingBuffer(capacity=8192)
        iterations = 100
        samples_per_write = 64
        
        # Shared counters
        write_count = [0, 0]
        read_count = [0, 0]
        errors = []
        
        def writer(writer_id: int):
            """Writer thread: write samples repeatedly."""
            try:
                for i in range(iterations):
                    samples = np.full(samples_per_write, 
                                     writer_id * 1000 + i, 
                                     dtype=np.int16)
                    rb.write(samples)
                    write_count[writer_id] += 1
                    time.sleep(0.001)  # Small delay to encourage interleaving
            except Exception as e:
                errors.append(f"Writer {writer_id}: {e}")
                
        def reader(reader_id: int):
            """Reader thread: read samples repeatedly."""
            try:
                for i in range(iterations):
                    result = rb.read(samples_per_write)
                    if result is not None:
                        read_count[reader_id] += 1
                    time.sleep(0.001)
            except Exception as e:
                errors.append(f"Reader {reader_id}: {e}")
                
        # Start threads
        threads = []
        threads.append(threading.Thread(target=writer, args=(0,)))
        threads.append(threading.Thread(target=writer, args=(1,)))
        threads.append(threading.Thread(target=reader, args=(0,)))
        threads.append(threading.Thread(target=reader, args=(1,)))
        
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10.0)
            
        # Verify no errors occurred
        assert len(errors) == 0, f"Errors: {errors}"
        
        # Verify all writers completed
        assert write_count[0] == iterations
        assert write_count[1] == iterations
        
        # Verify readers made progress (may have underruns, but no crashes)
        assert read_count[0] + read_count[1] > 0
        
    def test_concurrent_read_write_no_corruption(self):
        """Test data integrity under concurrent access."""
        rb = RingBuffer(capacity=1024)
        iterations = 50
        corruption_detected = [False]
        
        def writer():
            """Write monotonically increasing samples."""
            value = 0
            for _ in range(iterations):
                samples = np.arange(value, value + 10, dtype=np.int16)
                rb.write(samples)
                value += 10
                time.sleep(0.002)
                
        def reader():
            """Read and verify monotonic sequence."""
            last_value = -1
            for _ in range(iterations):
                result = rb.read(10)
                if result is not None:
                    # Verify samples are in sequence
                    if not np.all(result[1:] == result[:-1] + 1):
                        corruption_detected[0] = True
                        break
                    # Verify monotonic across reads (allowing for drops)
                    if last_value >= 0 and result[0] < last_value:
                        corruption_detected[0] = True
                        break
                    last_value = result[-1]
                time.sleep(0.002)
                
        writer_thread = threading.Thread(target=writer)
        reader_thread = threading.Thread(target=reader)
        
        writer_thread.start()
        reader_thread.start()
        
        writer_thread.join(timeout=5.0)
        reader_thread.join(timeout=5.0)
        
        assert not corruption_detected[0], "Data corruption detected in concurrent access"


class TestRingBufferStats:
    """Test statistics reporting."""
    
    def test_get_stats(self):
        """Test get_stats returns correct values."""
        rb = RingBuffer(capacity=100)
        rb.write(np.arange(50, dtype=np.int16))
        
        stats = rb.get_stats()
        assert stats['size'] == 50
        assert stats['capacity'] == 100
        assert stats['fill_pct'] == 50.0
        assert stats['overflows'] >= 0
        assert stats['underruns'] >= 0


class TestRingBufferWaitForData:
    """Test wait_for_data blocking mechanism."""
    
    def test_wait_for_data_immediate(self):
        """Test wait_for_data returns immediately if data available."""
        rb = RingBuffer(capacity=100)
        rb.write(np.array([1, 2, 3], dtype=np.int16))
        
        result = rb.wait_for_data(timeout=0.1)
        assert result is True
        
    def test_wait_for_data_timeout(self):
        """Test wait_for_data times out on empty buffer."""
        rb = RingBuffer(capacity=100)
        
        start_time = time.time()
        result = rb.wait_for_data(timeout=0.1)
        elapsed = time.time() - start_time
        
        assert result is False
        assert elapsed >= 0.1
        assert elapsed < 0.2  # Reasonable timeout accuracy
        
    def test_wait_for_data_signaled(self):
        """Test wait_for_data is signaled when data written."""
        rb = RingBuffer(capacity=100)
        signaled = [False]
        
        def waiter():
            signaled[0] = rb.wait_for_data(timeout=1.0)
            
        thread = threading.Thread(target=waiter)
        thread.start()
        
        time.sleep(0.1)  # Ensure waiter is blocking
        rb.write(np.array([1, 2, 3], dtype=np.int16))  # Signal
        
        thread.join(timeout=0.5)
        assert signaled[0] is True

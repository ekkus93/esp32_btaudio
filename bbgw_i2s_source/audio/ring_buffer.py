"""
Ring Buffer for Audio Samples

Thread-safe circular buffer implementation for storing audio samples between
the AudioEngine (producer) and I2SDriver (consumer).

Author: bbgw_i2s_source
Date: 2026-02-07
"""

import threading
import numpy as np
from typing import Optional


class RingBuffer:
    """
    Thread-safe circular buffer for audio samples.
    
    Implements a fixed-size FIFO buffer with overflow protection (drop oldest)
    and underrun detection. Designed for high-performance audio streaming with
    minimal latency.
    
    Attributes:
        capacity (int): Maximum number of samples the buffer can hold
        
    Example:
        >>> rb = RingBuffer(capacity=8192)
        >>> samples = np.array([1, 2, 3, 4], dtype=np.int16)
        >>> rb.write(samples)
        >>> read_samples = rb.read(2)
        >>> print(read_samples)  # [1, 2]
    """
    
    def __init__(self, capacity: int):
        """
        Initialize ring buffer.
        
        Args:
            capacity: Maximum number of samples (int16 values) to store.
                     Must be positive integer.
                     
        Raises:
            ValueError: If capacity <= 0
        """
        if capacity <= 0:
            raise ValueError(f"Capacity must be positive, got {capacity}")
            
        self._capacity = capacity
        self._buffer = np.zeros(capacity, dtype=np.int16)
        self._read_ptr = 0   # Next position to read from
        self._write_ptr = 0  # Next position to write to
        self._size = 0       # Current number of samples in buffer
        
        # Thread synchronization
        self._lock = threading.Lock()
        self._refill_event = threading.Event()  # Signaled when buffer has data
        
        # Statistics
        self._overflow_count = 0
        self._underrun_count = 0
        
    def write(self, samples: np.ndarray) -> int:
        """
        Write samples to ring buffer with overflow handling.
        
        If buffer is full, oldest samples are dropped to make room (FIFO).
        This ensures the buffer always contains the most recent audio data.
        
        Args:
            samples: NumPy array of int16 audio samples to write
            
        Returns:
            Number of samples successfully written
            
        Raises:
            TypeError: If samples is not a NumPy array
            ValueError: If samples dtype is not int16
        """
        if not isinstance(samples, np.ndarray):
            raise TypeError(f"Expected numpy array, got {type(samples)}")
        if samples.dtype != np.int16:
            raise ValueError(f"Expected int16 dtype, got {samples.dtype}")
            
        if len(samples) == 0:
            return 0
            
        with self._lock:
            num_samples = len(samples)
            
            # Handle overflow: if incoming samples exceed capacity,
            # drop oldest samples to maintain most recent data
            if num_samples > self._capacity:
                # Keep only the last 'capacity' samples from input
                samples = samples[-self._capacity:]
                num_samples = self._capacity
                self._overflow_count += 1
                
            # Calculate how many samples will be overwritten
            overflow = max(0, self._size + num_samples - self._capacity)
            if overflow > 0:
                self._overflow_count += 1
                # Advance read pointer to drop oldest samples
                self._read_ptr = (self._read_ptr + overflow) % self._capacity
                self._size -= overflow
                
            # Write samples in two segments if wrapping around buffer end
            remaining = num_samples
            source_idx = 0
            
            while remaining > 0:
                # Calculate contiguous space from write_ptr to end of buffer
                space_to_end = self._capacity - self._write_ptr
                chunk_size = min(remaining, space_to_end)
                
                # Copy chunk
                self._buffer[self._write_ptr:self._write_ptr + chunk_size] = \
                    samples[source_idx:source_idx + chunk_size]
                    
                # Update pointers
                self._write_ptr = (self._write_ptr + chunk_size) % self._capacity
                source_idx += chunk_size
                remaining -= chunk_size
                self._size += chunk_size
                
            # Signal that buffer has data (for readers waiting on empty buffer)
            self._refill_event.set()
            
            return num_samples
            
    def read(self, num_samples: int) -> Optional[np.ndarray]:
        """
        Read samples from ring buffer.
        
        Args:
            num_samples: Number of samples to read
            
        Returns:
            NumPy array of int16 samples, or None if buffer underrun
            (insufficient samples available)
            
        Raises:
            ValueError: If num_samples <= 0
        """
        if num_samples <= 0:
            raise ValueError(f"num_samples must be positive, got {num_samples}")
            
        with self._lock:
            # Check for underrun
            if self._size < num_samples:
                self._underrun_count += 1
                return None
                
            # Allocate output array
            result = np.zeros(num_samples, dtype=np.int16)
            
            # Read samples in two segments if wrapping around buffer end
            remaining = num_samples
            dest_idx = 0
            
            while remaining > 0:
                # Calculate contiguous data from read_ptr to end of buffer
                data_to_end = self._capacity - self._read_ptr
                chunk_size = min(remaining, data_to_end)
                
                # Copy chunk
                result[dest_idx:dest_idx + chunk_size] = \
                    self._buffer[self._read_ptr:self._read_ptr + chunk_size]
                    
                # Update pointers
                self._read_ptr = (self._read_ptr + chunk_size) % self._capacity
                dest_idx += chunk_size
                remaining -= chunk_size
                self._size -= chunk_size
                
            # Clear refill event if buffer is now empty
            if self._size == 0:
                self._refill_event.clear()
                
            return result
            
    def get_fill_percentage(self) -> float:
        """
        Calculate buffer fill percentage.
        
        Returns:
            Fill percentage (0.0 to 100.0)
        """
        with self._lock:
            return (self._size / self._capacity) * 100.0
            
    def clear(self) -> None:
        """
        Clear buffer and reset pointers.
        
        Removes all samples and resets read/write pointers to zero.
        Useful for recovering from errors or changing audio sources.
        """
        with self._lock:
            self._read_ptr = 0
            self._write_ptr = 0
            self._size = 0
            self._buffer.fill(0)  # Optional: zero out buffer for security
            self._refill_event.clear()
            
    def wait_for_data(self, timeout: Optional[float] = None) -> bool:
        """
        Block until buffer has data (helper for consumers).
        
        Args:
            timeout: Maximum time to wait in seconds, or None for infinite wait
            
        Returns:
            True if data available, False if timeout occurred
        """
        return self._refill_event.wait(timeout)
        
    def get_stats(self) -> dict:
        """
        Get buffer statistics.
        
        Returns:
            Dictionary with keys: size, capacity, fill_pct, overflows, underruns
        """
        with self._lock:
            return {
                'size': self._size,
                'capacity': self._capacity,
                'fill_pct': (self._size / self._capacity) * 100.0,
                'overflows': self._overflow_count,
                'underruns': self._underrun_count,
            }
            
    @property
    def size(self) -> int:
        """Current number of samples in buffer (thread-safe)."""
        with self._lock:
            return self._size
            
    @property
    def capacity(self) -> int:
        """Maximum buffer capacity."""
        return self._capacity

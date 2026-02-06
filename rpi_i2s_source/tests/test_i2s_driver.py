"""
Unit tests for I2S Driver (ALSA implementation).

Tests the I2SDriverALSA class with mocked alsaaudio module (no hardware required).
"""

import pytest
import numpy as np
import threading
import time
from unittest.mock import MagicMock, patch, PropertyMock


# Mock alsaaudio module before importing I2SDriverALSA
@pytest.fixture(autouse=True)
def mock_alsaaudio():
    """Mock alsaaudio module for testing without hardware."""
    with patch('audio.i2s_driver.ALSAAUDIO_AVAILABLE', True):
        # Create mock alsaaudio module
        mock_alsa = MagicMock()
        mock_alsa.PCM_PLAYBACK = 0
        mock_alsa.PCM_NONBLOCK = 1
        mock_alsa.PCM_FORMAT_S16_LE = 2
        
        # Create mock PCM device
        mock_pcm = MagicMock()
        mock_pcm.write.return_value = 1024  # Return number of frames written
        mock_alsa.PCM.return_value = mock_pcm
        
        with patch('audio.i2s_driver.alsaaudio', mock_alsa):
            yield mock_alsa


@pytest.fixture
def config():
    """Create mock ConfigManager."""
    config = MagicMock()
    config.get.side_effect = lambda key: {
        'i2s.sample_rate': 48000,
        'i2s.gpio_bclk': 18,
        'i2s.gpio_ws': 19,
        'i2s.gpio_dout': 21,
        'i2s.buffer_size': 8192
    }.get(key, None)
    return config


@pytest.fixture
def ring_buffer():
    """Create real RingBuffer for integration testing."""
    from audio.ring_buffer import RingBuffer
    return RingBuffer(8192)


@pytest.fixture
def driver(config, ring_buffer):
    """Create I2SDriverALSA instance."""
    from audio.i2s_driver import I2SDriverALSA
    return I2SDriverALSA(config, ring_buffer)


class TestI2SDriverInit:
    """Test I2S driver initialization."""
    
    def test_init_stores_config_and_ring_buffer(self, config, ring_buffer):
        """Should store config and ring buffer references."""
        from audio.i2s_driver import I2SDriverALSA
        driver = I2SDriverALSA(config, ring_buffer)
        
        assert driver.config is config
        assert driver.ring_buffer is ring_buffer
    
    def test_init_sets_default_parameters(self, config, ring_buffer):
        """Should set default parameters from config."""
        from audio.i2s_driver import I2SDriverALSA
        driver = I2SDriverALSA(config, ring_buffer)
        
        assert driver.sample_rate == 48000
        assert driver.period_size == 1024
        assert driver.running == False
        assert driver.frames_sent == 0
        assert driver.underruns == 0
    
    def test_init_without_alsaaudio_raises_import_error(self, config, ring_buffer):
        """Should raise ImportError if alsaaudio not available."""
        from audio.i2s_driver import I2SDriverALSA
        
        with patch('audio.i2s_driver.ALSAAUDIO_AVAILABLE', False):
            with pytest.raises(ImportError, match="alsaaudio not available"):
                I2SDriverALSA(config, ring_buffer)


class TestI2SDriverALSAInit:
    """Test ALSA device initialization."""
    
    def test_init_alsa_device_opens_pcm(self, driver, mock_alsaaudio):
        """Should open ALSA PCM device with correct parameters."""
        driver._init_alsa_device()
        
        # Verify PCM opened with correct parameters
        mock_alsaaudio.PCM.assert_called_once_with(
            type=mock_alsaaudio.PCM_PLAYBACK,
            mode=mock_alsaaudio.PCM_NONBLOCK,
            device='hw:0,0'
        )
    
    def test_init_alsa_device_configures_pcm(self, driver, mock_alsaaudio):
        """Should configure PCM parameters (channels, rate, format, period size)."""
        driver._init_alsa_device()
        
        mock_pcm = mock_alsaaudio.PCM.return_value
        mock_pcm.setchannels.assert_called_once_with(2)  # Stereo
        mock_pcm.setrate.assert_called_once_with(48000)
        mock_pcm.setformat.assert_called_once_with(mock_alsaaudio.PCM_FORMAT_S16_LE)
        mock_pcm.setperiodsize.assert_called_once_with(1024)
    
    def test_init_alsa_device_sets_initialized_flag(self, driver):
        """Should set _device_initialized flag to True."""
        assert driver._device_initialized == False
        driver._init_alsa_device()
        assert driver._device_initialized == True


class TestI2SDriverStartStop:
    """Test I2S driver start/stop lifecycle."""
    
    def test_start_initializes_alsa_device(self, driver, mock_alsaaudio):
        """Should initialize ALSA device on first start."""
        driver.start()
        
        assert driver._device_initialized == True
        mock_alsaaudio.PCM.assert_called_once()
    
    def test_start_launches_dma_thread(self, driver):
        """Should launch background DMA thread."""
        driver.start()
        
        assert driver.running == True
        assert driver.thread is not None
        assert driver.thread.is_alive()
        
        driver.stop()
    
    def test_start_idempotent(self, driver, mock_alsaaudio):
        """Should be safe to call start() multiple times."""
        driver.start()
        driver.start()  # Second call should do nothing
        
        # Only one PCM device created
        assert mock_alsaaudio.PCM.call_count == 1
        
        driver.stop()
    
    def test_stop_gracefully_shuts_down(self, driver):
        """Should gracefully stop DMA thread."""
        driver.start()
        time.sleep(0.05)  # Let thread run briefly
        
        driver.stop()
        
        assert driver.running == False
        assert driver.thread is None
    
    def test_stop_closes_alsa_device(self, driver, mock_alsaaudio):
        """Should close ALSA device on stop."""
        driver.start()
        mock_pcm = mock_alsaaudio.PCM.return_value
        
        driver.stop()
        
        mock_pcm.close.assert_called_once()
        assert driver.device is None
    
    def test_stop_idempotent(self, driver):
        """Should be safe to call stop() multiple times."""
        driver.start()
        driver.stop()
        driver.stop()  # Second call should do nothing
        
        # No errors
        assert driver.running == False


class TestI2SDriverDMALoop:
    """Test I2S DMA loop behavior."""
    
    def test_dma_loop_reads_from_ring_buffer(self, driver, ring_buffer, mock_alsaaudio):
        """Should read samples from ring buffer."""
        # Pre-fill ring buffer with test data
        test_samples = np.arange(2048, dtype=np.int16)
        ring_buffer.write(test_samples)
        
        # Start driver and let it run briefly
        driver.start()
        time.sleep(0.05)
        driver.stop()
        
        # Verify samples were read (buffer should be less full)
        assert ring_buffer.get_fill_percentage() < 100.0
    
    def test_dma_loop_writes_to_alsa_device(self, driver, ring_buffer, mock_alsaaudio):
        """Should write samples to ALSA device."""
        # Pre-fill ring buffer
        test_samples = np.arange(2048, dtype=np.int16)
        ring_buffer.write(test_samples)
        
        mock_pcm = mock_alsaaudio.PCM.return_value
        
        # Start driver and let it run
        driver.start()
        time.sleep(0.05)
        driver.stop()
        
        # Verify ALSA write was called
        assert mock_pcm.write.call_count > 0
    
    def test_dma_loop_updates_frames_sent(self, driver, ring_buffer, mock_alsaaudio):
        """Should increment frames_sent counter."""
        # Pre-fill ring buffer
        test_samples = np.arange(4096, dtype=np.int16)
        ring_buffer.write(test_samples)
        
        # Mock ALSA write to return frames written
        mock_pcm = mock_alsaaudio.PCM.return_value
        mock_pcm.write.return_value = 1024
        
        # Start driver
        driver.start()
        time.sleep(0.05)
        driver.stop()
        
        # Verify frames_sent incremented
        assert driver.frames_sent > 0
    
    def test_dma_loop_handles_underrun(self, driver, ring_buffer, mock_alsaaudio):
        """Should handle ring buffer underrun by zero-filling."""
        # Start driver with empty ring buffer
        driver.start()
        time.sleep(0.05)
        driver.stop()
        
        # Verify underruns incremented
        assert driver.underruns > 0
    
    def test_dma_loop_updates_buffer_fill_stats(self, driver, ring_buffer):
        """Should update last_buffer_fill statistic."""
        # Pre-fill ring buffer to 50%
        test_samples = np.zeros(4096, dtype=np.int16)
        ring_buffer.write(test_samples)
        
        driver.start()
        time.sleep(0.05)
        driver.stop()
        
        # Verify buffer fill was updated (should be < 100%)
        assert driver.last_buffer_fill >= 0.0
        assert driver.last_buffer_fill <= 100.0


class TestI2SDriverGetStats:
    """Test I2S driver statistics retrieval."""
    
    def test_get_stats_returns_all_fields(self, driver):
        """Should return all statistics fields."""
        stats = driver.get_stats()
        
        assert 'frames_sent' in stats
        assert 'underruns' in stats
        assert 'buffer_fill_pct' in stats
        assert 'active' in stats
    
    def test_get_stats_reflects_current_state(self, driver, ring_buffer):
        """Should reflect current driver state."""
        # Before start
        stats_before = driver.get_stats()
        assert stats_before['active'] == False
        assert stats_before['frames_sent'] == 0
        
        # After start
        test_samples = np.zeros(2048, dtype=np.int16)
        ring_buffer.write(test_samples)
        driver.start()
        time.sleep(0.05)
        
        stats_running = driver.get_stats()
        assert stats_running['active'] == True
        
        driver.stop()
        
        # After stop
        stats_after = driver.get_stats()
        assert stats_after['active'] == False
    
    def test_get_stats_updates_counters(self, driver, ring_buffer, mock_alsaaudio):
        """Should show updated counters after transmission."""
        # Pre-fill ring buffer
        test_samples = np.zeros(4096, dtype=np.int16)
        ring_buffer.write(test_samples)
        
        mock_pcm = mock_alsaaudio.PCM.return_value
        mock_pcm.write.return_value = 1024
        
        driver.start()
        time.sleep(0.05)
        driver.stop()
        
        stats = driver.get_stats()
        assert stats['frames_sent'] > 0


class TestI2SDriverIntegration:
    """Integration tests with real RingBuffer."""
    
    def test_continuous_transmission(self, driver, ring_buffer, mock_alsaaudio):
        """Should continuously transmit audio from ring buffer."""
        # Pre-fill ring buffer with test data before starting driver
        for _ in range(4):  # Fill buffer to ~50%
            samples = np.sin(2 * np.pi * 1000 * np.arange(512) / 48000)
            samples = (samples * 16000).astype(np.int16)
            stereo_samples = np.empty(1024, dtype=np.int16)
            stereo_samples[0::2] = samples
            stereo_samples[1::2] = samples
            ring_buffer.write(stereo_samples)
        
        # Create continuous tone generator thread
        def generate_tone():
            while driver.running:
                samples = np.sin(2 * np.pi * 1000 * np.arange(512) / 48000)
                samples = (samples * 16000).astype(np.int16)
                stereo_samples = np.empty(1024, dtype=np.int16)
                stereo_samples[0::2] = samples
                stereo_samples[1::2] = samples
                ring_buffer.write(stereo_samples)
                time.sleep(0.005)  # Write more frequently
        
        # Start driver and tone generator
        driver.start()
        generator_thread = threading.Thread(target=generate_tone, daemon=True)
        generator_thread.start()
        
        time.sleep(0.1)  # Run for 100ms
        
        driver.stop()
        
        # Verify transmission occurred
        stats = driver.get_stats()
        assert stats['frames_sent'] > 0
        # Allow some underruns during startup (race condition in test environment)
        assert stats['underruns'] < 100  # Should be very low with continuous generation
    
    def test_underrun_recovery(self, driver, ring_buffer):
        """Should recover from underrun by zero-filling."""
        # Start with empty buffer (will underrun)
        driver.start()
        time.sleep(0.05)
        
        # Now fill buffer with real data
        test_samples = np.zeros(2048, dtype=np.int16)
        ring_buffer.write(test_samples)
        time.sleep(0.05)
        
        driver.stop()
        
        stats = driver.get_stats()
        # Should have underruns from initial empty period
        assert stats['underruns'] > 0
        # Should have transmitted frames after buffer filled
        assert stats['frames_sent'] > 0
    
    def test_thread_safety(self, driver, ring_buffer):
        """Should handle concurrent start/stop calls safely."""
        def start_stop_repeatedly():
            for _ in range(5):
                driver.start()
                time.sleep(0.01)
                driver.stop()
                time.sleep(0.01)
        
        # Run multiple threads calling start/stop
        threads = [threading.Thread(target=start_stop_repeatedly) for _ in range(3)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        
        # Should end in stopped state
        assert driver.running == False


class TestI2SDriverErrorHandling:
    """Test I2S driver error handling."""
    
    def test_alsa_init_failure(self, driver, mock_alsaaudio):
        """Should handle ALSA device initialization failure."""
        # Mock ALSA PCM to raise exception
        mock_alsaaudio.PCM.side_effect = Exception("ALSA init failed")
        
        with pytest.raises(Exception, match="ALSA init failed"):
            driver._init_alsa_device()
    
    def test_alsa_write_failure_continues_loop(self, driver, ring_buffer, mock_alsaaudio):
        """Should continue DMA loop after ALSA write failure."""
        # Pre-fill ring buffer
        test_samples = np.zeros(2048, dtype=np.int16)
        ring_buffer.write(test_samples)
        
        mock_pcm = mock_alsaaudio.PCM.return_value
        
        # Mock write to fail first time, then succeed
        mock_pcm.write.side_effect = [Exception("Write failed"), 1024, 1024, 1024]
        
        driver.start()
        time.sleep(0.1)  # Let it retry after error
        driver.stop()
        
        # Should have called write multiple times (recovered from error)
        assert mock_pcm.write.call_count > 1
    
    def test_device_close_failure_logged(self, driver, mock_alsaaudio):
        """Should log error if device close fails but continue shutdown."""
        driver.start()
        
        mock_pcm = mock_alsaaudio.PCM.return_value
        mock_pcm.close.side_effect = Exception("Close failed")
        
        # Should not raise, just log
        driver.stop()
        
        assert driver.device is None
        assert driver.running == False


if __name__ == '__main__':
    pytest.main([__file__, '-v'])

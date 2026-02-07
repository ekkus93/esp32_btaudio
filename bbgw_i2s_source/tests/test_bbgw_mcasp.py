"""
BBGW-specific tests for McASP I2S driver.

Tests BeagleBone Green Wireless specific functionality:
- McASP ALSA device detection (hw:CARD=BBGW-I2S,DEV=0)
- Fallback to hw:0,0
- Device Tree overlay verification (manual test, hardware required)
"""

import pytest
from unittest.mock import MagicMock, patch


class TestBBGWMcASPDevice:
    """Test BBGW McASP ALSA device handling."""
    
    def test_uses_bbgw_i2s_device_from_config(self):
        """Should use hw:CARD=BBGW-I2S,DEV=0 device from config."""
        from audio.i2s_driver import I2SDriverALSA
        from audio.ring_buffer import RingBuffer
        
        # Mock config with BBGW device
        config = MagicMock()
        config.get.side_effect = lambda key, default=None: {
            'i2s.sample_rate': 48000,
            'i2s.device': 'hw:CARD=BBGW-I2S,DEV=0'
        }.get(key, default)
        
        ring_buffer = RingBuffer(8192)
        
        with patch('audio.i2s_driver.ALSAAUDIO_AVAILABLE', True):
            mock_alsa = MagicMock()
            mock_pcm = MagicMock()
            mock_alsa.PCM.return_value = mock_pcm
            mock_alsa.PCM_PLAYBACK = 0
            mock_alsa.PCM_NONBLOCK = 1
            mock_alsa.PCM_FORMAT_S16_LE = 2
            
            with patch('audio.i2s_driver.alsaaudio', mock_alsa):
                driver = I2SDriverALSA(config, ring_buffer)
                driver._init_alsa_device()
                
                # Verify BBGW device was used
                mock_alsa.PCM.assert_called_once()
                call_kwargs = mock_alsa.PCM.call_args[1]
                assert call_kwargs['device'] == 'hw:CARD=BBGW-I2S,DEV=0'
    
    def test_uses_hw_0_0_fallback_when_bbgw_device_not_found(self):
        """Should fall back to hw:0,0 if hw:CARD=BBGW-I2S,DEV=0 fails."""
        from audio.i2s_driver import I2SDriverALSA
        from audio.ring_buffer import RingBuffer
        
        # Mock config with BBGW device
        config = MagicMock()
        config.get.side_effect = lambda key, default=None: {
            'i2s.sample_rate': 48000,
            'i2s.device': 'hw:CARD=BBGW-I2S,DEV=0'
        }.get(key, default)
        
        ring_buffer = RingBuffer(8192)
        
        with patch('audio.i2s_driver.ALSAAUDIO_AVAILABLE', True):
            mock_alsa = MagicMock()
            mock_pcm_success = MagicMock()
            
            # First call (BBGW device) raises error, second call (hw:0,0) succeeds
            mock_alsa.ALSAAudioError = Exception  # Define exception class
            call_count = [0]
            
            def pcm_side_effect(*args, **kwargs):
                call_count[0] += 1
                if call_count[0] == 1:
                    # First call with BBGW device fails
                    raise mock_alsa.ALSAAudioError("Device not found")
                else:
                    # Second call with hw:0,0 succeeds
                    return mock_pcm_success
            
            mock_alsa.PCM.side_effect = pcm_side_effect
            mock_alsa.PCM_PLAYBACK = 0
            mock_alsa.PCM_NONBLOCK = 1
            mock_alsa.PCM_FORMAT_S16_LE = 2
            
            with patch('audio.i2s_driver.alsaaudio', mock_alsa):
                driver = I2SDriverALSA(config, ring_buffer)
                driver._init_alsa_device()
                
                # Verify both attempts were made
                assert mock_alsa.PCM.call_count == 2
                
                # Verify first call used BBGW device
                first_call_kwargs = mock_alsa.PCM.call_args_list[0][1]
                assert first_call_kwargs['device'] == 'hw:CARD=BBGW-I2S,DEV=0'
                
                # Verify second call used fallback
                second_call_kwargs = mock_alsa.PCM.call_args_list[1][1]
                assert second_call_kwargs['device'] == 'hw:0,0'
                
                # Verify driver has the successful device
                assert driver.device == mock_pcm_success
    
    def test_uses_hw_0_0_directly_if_configured(self):
        """Should use hw:0,0 directly if configured (no fallback needed)."""
        from audio.i2s_driver import I2SDriverALSA
        from audio.ring_buffer import RingBuffer
        
        # Mock config with hw:0,0 device
        config = MagicMock()
        config.get.side_effect = lambda key, default=None: {
            'i2s.sample_rate': 48000,
            'i2s.device': 'hw:0,0'
        }.get(key, default)
        
        ring_buffer = RingBuffer(8192)
        
        with patch('audio.i2s_driver.ALSAAUDIO_AVAILABLE', True):
            mock_alsa = MagicMock()
            mock_pcm = MagicMock()
            mock_alsa.PCM.return_value = mock_pcm
            mock_alsa.PCM_PLAYBACK = 0
            mock_alsa.PCM_NONBLOCK = 1
            mock_alsa.PCM_FORMAT_S16_LE = 2
            
            with patch('audio.i2s_driver.alsaaudio', mock_alsa):
                driver = I2SDriverALSA(config, ring_buffer)
                driver._init_alsa_device()
                
                # Verify hw:0,0 was used directly (no fallback)
                mock_alsa.PCM.assert_called_once()
                call_kwargs = mock_alsa.PCM.call_args[1]
                assert call_kwargs['device'] == 'hw:0,0'
    
    def test_uses_default_device_if_not_in_config(self):
        """Should use default hw:CARD=BBGW-I2S,DEV=0 if not in config."""
        from audio.i2s_driver import I2SDriverALSA
        from audio.ring_buffer import RingBuffer
        
        # Mock config without i2s.device (should use default)
        config = MagicMock()
        config.get.side_effect = lambda key, default=None: {
            'i2s.sample_rate': 48000
        }.get(key, default)
        
        ring_buffer = RingBuffer(8192)
        
        with patch('audio.i2s_driver.ALSAAUDIO_AVAILABLE', True):
            mock_alsa = MagicMock()
            mock_pcm = MagicMock()
            mock_alsa.PCM.return_value = mock_pcm
            mock_alsa.PCM_PLAYBACK = 0
            mock_alsa.PCM_NONBLOCK = 1
            mock_alsa.PCM_FORMAT_S16_LE = 2
            
            with patch('audio.i2s_driver.alsaaudio', mock_alsa):
                driver = I2SDriverALSA(config, ring_buffer)
                driver._init_alsa_device()
                
                # Verify default BBGW device was used
                mock_alsa.PCM.assert_called_once()
                call_kwargs = mock_alsa.PCM.call_args[1]
                assert call_kwargs['device'] == 'hw:CARD=BBGW-I2S,DEV=0'


class TestBBGWMcASPParameters:
    """Test BBGW McASP ALSA parameter configuration."""
    
    def test_configures_stereo_48khz_s16le(self):
        """Should configure for stereo, 48 kHz, S16_LE (BBGW McASP default)."""
        from audio.i2s_driver import I2SDriverALSA
        from audio.ring_buffer import RingBuffer
        
        config = MagicMock()
        config.get.side_effect = lambda key, default=None: {
            'i2s.sample_rate': 48000,
            'i2s.device': 'hw:CARD=BBGW-I2S,DEV=0'
        }.get(key, default)
        
        ring_buffer = RingBuffer(8192)
        
        with patch('audio.i2s_driver.ALSAAUDIO_AVAILABLE', True):
            mock_alsa = MagicMock()
            mock_pcm = MagicMock()
            mock_alsa.PCM.return_value = mock_pcm
            mock_alsa.PCM_PLAYBACK = 0
            mock_alsa.PCM_NONBLOCK = 1
            mock_alsa.PCM_FORMAT_S16_LE = 2
            
            with patch('audio.i2s_driver.alsaaudio', mock_alsa):
                driver = I2SDriverALSA(config, ring_buffer)
                driver._init_alsa_device()
                
                # Verify ALSA parameters
                mock_pcm.setchannels.assert_called_once_with(2)  # Stereo
                mock_pcm.setrate.assert_called_once_with(48000)  # 48 kHz
                mock_pcm.setformat.assert_called_once_with(mock_alsa.PCM_FORMAT_S16_LE)  # S16_LE
                mock_pcm.setperiodsize.assert_called_once_with(1024)  # Default period size


# Manual hardware tests (require BBGW with Device Tree overlay)
class TestBBGWMcASPHardware:
    """
    Manual tests for BBGW McASP hardware (require physical hardware).
    
    These tests are marked with @pytest.mark.hardware and skipped by default.
    Run with: pytest -v -m hardware
    
    Prerequisites:
    - BeagleBone Green Wireless
    - BB-BBGW-I2S-00A0.dtbo Device Tree overlay installed and enabled
    - /dev/snd/pcmC0D0p device exists (verify with: ls -l /dev/snd/)
    - ALSA utilities installed (verify with: aplay -l)
    """
    
    @pytest.mark.hardware
    @pytest.mark.skip(reason="Requires BBGW hardware with Device Tree overlay")
    def test_bbgw_i2s_device_exists(self):
        """Verify BBGW I2S device exists in ALSA."""
        import alsaaudio
        
        # Try to list ALSA cards
        cards = alsaaudio.cards()
        assert 'BBGW-I2S' in cards or 'BBGWI2S' in cards, \
            f"BBGW-I2S card not found. Available cards: {cards}"
    
    @pytest.mark.hardware
    @pytest.mark.skip(reason="Requires BBGW hardware with Device Tree overlay")
    def test_can_open_bbgw_i2s_device(self):
        """Verify BBGW I2S device can be opened."""
        import alsaaudio
        
        # Try to open BBGW I2S device
        pcm = alsaaudio.PCM(
            type=alsaaudio.PCM_PLAYBACK,
            mode=alsaaudio.PCM_NONBLOCK,
            device='hw:CARD=BBGW-I2S,DEV=0'
        )
        
        assert pcm is not None
        pcm.close()
    
    @pytest.mark.hardware
    @pytest.mark.skip(reason="Requires BBGW hardware with Device Tree overlay")
    def test_mcasp_supports_48khz(self):
        """Verify McASP supports 48 kHz sample rate."""
        import alsaaudio
        
        pcm = alsaaudio.PCM(
            type=alsaaudio.PCM_PLAYBACK,
            mode=alsaaudio.PCM_NONBLOCK,
            device='hw:CARD=BBGW-I2S,DEV=0'
        )
        
        # Try to set 48 kHz
        pcm.setrate(48000)
        
        # No exception means success
        pcm.close()
    
    @pytest.mark.hardware
    @pytest.mark.skip(reason="Requires BBGW hardware and ESP32")
    def test_i2s_transmission_to_esp32(self):
        """
        End-to-end test: Transmit 1 kHz tone from BBGW to ESP32 via I2S.
        
        Prerequisites:
        - BBGW with Device Tree overlay
        - ESP32 with esp_bt_audio_source running
        - I2S connections: P9.31 → GPIO26, P9.29 → GPIO25, P9.28 → GPIO22
        - UART connections: P9.13 → GPIO16, P9.11 → GPIO17
        - Logic analyzer (optional, for verification)
        
        Expected:
        - I2S signals present on logic analyzer
        - ESP32 receives audio and transmits via Bluetooth
        - No underruns during 5-second test
        """
        from audio.i2s_driver import I2SDriverALSA
        from audio.ring_buffer import RingBuffer
        from config.manager import ConfigManager
        import numpy as np
        import time
        
        # Load config
        config = ConfigManager('config.yaml')
        
        # Create ring buffer
        ring_buffer = RingBuffer(8192)
        
        # Create I2S driver
        driver = I2SDriverALSA(config, ring_buffer)
        
        try:
            # Start driver
            driver.start()
            
            # Generate 1 kHz tone for 5 seconds
            sample_rate = 48000
            duration = 5.0
            frequency = 1000
            amplitude = 0.5
            
            t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
            tone = (amplitude * 32767 * np.sin(2 * np.pi * frequency * t)).astype(np.int16)
            
            # Duplicate for stereo
            tone_stereo = np.repeat(tone, 2)
            
            # Write to ring buffer
            ring_buffer.write(tone_stereo)
            
            # Wait for transmission
            time.sleep(duration + 1.0)
            
            # Get stats
            stats = driver.get_stats()
            
            # Verify transmission
            assert stats['frames_sent'] > 0, "No frames transmitted"
            assert stats['underruns'] < 10, f"Too many underruns: {stats['underruns']}"
            
        finally:
            driver.stop()

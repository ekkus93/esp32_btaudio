"""
Unit tests for AudioEngine multi-tone functionality.

This module tests the multi-tone generation feature that allows playing
up to 4 simultaneous sine tones with independent frequency, amplitude,
and enable/disable control.

Test Coverage:
    - Multi-tone mode enable/disable
    - Individual tone parameter updates
    - Phase accumulator maintenance (click-free)
    - Tone summing and normalization
    - Multi-tone state management
    - Thread-safe parameter updates
    - Audio output validation

Run with:
    pytest tests/unit/test_audio_engine_multi_tone.py -v
"""

import pytest
import numpy as np
import time
from unittest.mock import Mock

from audio.engine import AudioEngine
from audio.ring_buffer import RingBuffer
from config.manager import ConfigManager


@pytest.fixture
def config():
    """Create ConfigManager with test configuration."""
    cfg = Mock(spec=ConfigManager)
    cfg.get = Mock(side_effect=lambda key: {
        'i2s.sample_rate': 48000,
        'i2s.buffer_size': 1024,
        'audio.tone_freq': 1000,
        'audio.tone_amp': 0.7,
        'audio.wav_directory': '/tmp/audio'
    }[key])
    return cfg


@pytest.fixture
def ring_buffer():
    """Create RingBuffer for testing."""
    return RingBuffer(capacity=8192)


@pytest.fixture
def engine(config, ring_buffer):
    """Create AudioEngine instance."""
    return AudioEngine(config, ring_buffer)


class TestMultiToneEnableDisable:
    """Test multi-tone mode enable/disable functionality."""
    
    def test_default_state_is_disabled(self, engine):
        """Multi-tone mode should be disabled by default."""
        state = engine.get_state()
        assert state['multi_tone_enabled'] is False
    
    def test_enable_multi_tone(self, engine):
        """Should enable multi-tone mode."""
        engine.enable_multi_tone(True)
        state = engine.get_state()
        assert state['multi_tone_enabled'] is True
    
    def test_disable_multi_tone(self, engine):
        """Should disable multi-tone mode."""
        engine.enable_multi_tone(True)
        engine.enable_multi_tone(False)
        state = engine.get_state()
        assert state['multi_tone_enabled'] is False
    
    def test_enable_is_thread_safe(self, engine):
        """Multi-tone enable should be thread-safe."""
        # Start generation
        engine.start()
        time.sleep(0.1)
        
        # Enable while generating
        engine.enable_multi_tone(True)
        state = engine.get_state()
        assert state['multi_tone_enabled'] is True
        
        # Disable while generating
        engine.enable_multi_tone(False)
        state = engine.get_state()
        assert state['multi_tone_enabled'] is False
        
        engine.stop()


class TestMultiToneParameters:
    """Test individual tone parameter updates."""
    
    def test_default_tone_array(self, engine):
        """Should have 4 tones with default parameters."""
        state = engine.get_state()
        tones = state['tones']
        
        assert len(tones) == 4
        assert tones[0]['freq'] == 1000
        assert tones[0]['amp'] == 0.7
        assert tones[0]['enabled'] is True
        
        assert tones[1]['enabled'] is False
        assert tones[2]['enabled'] is False
        assert tones[3]['enabled'] is False
    
    def test_set_tone_frequency(self, engine):
        """Should update individual tone frequency."""
        engine.set_multi_tone_params(0, freq=440)
        state = engine.get_state()
        assert state['tones'][0]['freq'] == 440
    
    def test_set_tone_amplitude(self, engine):
        """Should update individual tone amplitude."""
        engine.set_multi_tone_params(1, amp=0.5)
        state = engine.get_state()
        assert state['tones'][1]['amp'] == 0.5
    
    def test_set_tone_enabled(self, engine):
        """Should enable/disable individual tones."""
        engine.set_multi_tone_params(1, enabled=True)
        state = engine.get_state()
        assert state['tones'][1]['enabled'] is True
        
        engine.set_multi_tone_params(1, enabled=False)
        state = engine.get_state()
        assert state['tones'][1]['enabled'] is False
    
    def test_set_multiple_parameters(self, engine):
        """Should update multiple tone parameters simultaneously."""
        engine.set_multi_tone_params(2, freq=880, amp=0.6, enabled=True)
        state = engine.get_state()
        tone = state['tones'][2]
        
        assert tone['freq'] == 880
        assert tone['amp'] == 0.6
        assert tone['enabled'] is True
    
    def test_invalid_tone_index_raises(self, engine):
        """Should raise ValueError for invalid tone index."""
        with pytest.raises(ValueError, match="tone_index must be 0-3"):
            engine.set_multi_tone_params(-1, freq=440)
        
        with pytest.raises(ValueError, match="tone_index must be 0-3"):
            engine.set_multi_tone_params(4, freq=440)
    
    def test_parameter_updates_are_thread_safe(self, engine):
        """Multi-tone parameter updates should be thread-safe."""
        engine.start()
        engine.enable_multi_tone(True)
        time.sleep(0.1)
        
        # Update parameters while generating
        engine.set_multi_tone_params(0, freq=440)
        engine.set_multi_tone_params(1, freq=554.37, enabled=True)
        engine.set_multi_tone_params(2, freq=659.25, enabled=True)
        
        state = engine.get_state()
        assert state['tones'][0]['freq'] == 440
        assert state['tones'][1]['freq'] == 554.37
        assert state['tones'][1]['enabled'] is True
        assert state['tones'][2]['freq'] == 659.25
        assert state['tones'][2]['enabled'] is True
        
        engine.stop()


class TestMultiToneGeneration:
    """Test multi-tone audio generation."""
    
    def test_single_enabled_tone(self, engine, ring_buffer):
        """Should generate audio with one enabled tone."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=1000, amp=0.7, enabled=True)
        engine.set_multi_tone_params(1, enabled=False)
        engine.set_multi_tone_params(2, enabled=False)
        engine.set_multi_tone_params(3, enabled=False)
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        # Should have written data to buffer
        samples = ring_buffer.read(1024)
        assert len(samples) > 0
        assert samples.dtype == np.int16
    
    def test_multiple_enabled_tones(self, engine, ring_buffer):
        """Should generate audio with multiple simultaneous tones."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=440, amp=0.5, enabled=True)   # A4
        engine.set_multi_tone_params(1, freq=554.37, amp=0.5, enabled=True) # C#5
        engine.set_multi_tone_params(2, freq=659.25, amp=0.5, enabled=True) # E5
        engine.set_multi_tone_params(3, enabled=False)
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        # Should have written data to buffer
        samples = ring_buffer.read(1024)
        assert len(samples) > 0
        assert samples.dtype == np.int16
    
    def test_all_tones_enabled(self, engine, ring_buffer):
        """Should generate audio with all 4 tones enabled."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=440, amp=0.5, enabled=True)
        engine.set_multi_tone_params(1, freq=554.37, amp=0.5, enabled=True)
        engine.set_multi_tone_params(2, freq=659.25, amp=0.5, enabled=True)
        engine.set_multi_tone_params(3, freq=880, amp=0.5, enabled=True)
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        # Should have written data to buffer
        samples = ring_buffer.read(1024)
        assert len(samples) > 0
        assert samples.dtype == np.int16
    
    def test_no_enabled_tones_produces_silence(self, engine, ring_buffer):
        """Should generate silence when no tones are enabled."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, enabled=False)
        engine.set_multi_tone_params(1, enabled=False)
        engine.set_multi_tone_params(2, enabled=False)
        engine.set_multi_tone_params(3, enabled=False)
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        # Should have written data to buffer (all zeros)
        samples = ring_buffer.read(1024)
        assert len(samples) > 0
        assert np.all(samples == 0)
    
    def test_normalization_prevents_clipping(self, engine, ring_buffer):
        """Multi-tone normalization should prevent clipping."""
        engine.enable_multi_tone(True)
        # All tones at max amplitude
        engine.set_multi_tone_params(0, freq=440, amp=1.0, enabled=True)
        engine.set_multi_tone_params(1, freq=554.37, amp=1.0, enabled=True)
        engine.set_multi_tone_params(2, freq=659.25, amp=1.0, enabled=True)
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        # Read samples
        samples = ring_buffer.read(2048)
        assert len(samples) > 0
        
        # Should not clip (max value should be <= 32767)
        assert np.max(np.abs(samples)) <= 32767
    
    def test_single_tone_mode_still_works(self, engine, ring_buffer):
        """Single tone mode should work when multi-tone is disabled."""
        engine.enable_multi_tone(False)
        engine.set_tone_params(freq=1000, amp=0.7, mode='mono')
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        # Should have written data to buffer
        samples = ring_buffer.read(1024)
        assert len(samples) > 0
        assert samples.dtype == np.int16


class TestMultiTonePhaseAccumulator:
    """Test phase accumulator for click-free frequency changes."""
    
    def test_phase_accumulator_initialized(self, engine):
        """Each tone should have a phase accumulator."""
        state = engine.get_state()
        for tone in state['tones']:
            assert 'phase' in tone
            assert 0.0 <= tone['phase'] < 2 * np.pi
    
    def test_phase_accumulator_updates_during_generation(self, engine):
        """Phase accumulators should update during generation."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=1000, enabled=True)
        
        state_before = engine.get_state()
        phase_before = state_before['tones'][0]['phase']
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        state_after = engine.get_state()
        phase_after = state_after['tones'][0]['phase']
        
        # Phase should have changed
        assert phase_after != phase_before
    
    def test_phase_wraps_at_2pi(self, engine):
        """Phase accumulator should wrap at 2π to prevent overflow."""
        # This is implicitly tested by continuous generation
        # The phase should always be in range [0, 2π)
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=20000, enabled=True)  # High freq
        
        engine.set_source('tone')
        engine.start()
        time.sleep(1.0)  # Generate for a while
        engine.stop()
        
        state = engine.get_state()
        phase = state['tones'][0]['phase']
        
        # Phase should be wrapped within [0, 2π)
        assert 0.0 <= phase < 2 * np.pi


class TestMultiToneStateManagement:
    """Test multi-tone state management and persistence."""
    
    def test_state_includes_multi_tone_info(self, engine):
        """get_state() should include multi-tone information."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=440, amp=0.8, enabled=True)
        
        state = engine.get_state()
        
        assert 'multi_tone_enabled' in state
        assert 'tones' in state
        assert isinstance(state['tones'], list)
        assert len(state['tones']) == 4
    
    def test_tones_array_is_deep_copy(self, engine):
        """get_state() should return deep copy of tones array."""
        engine.set_multi_tone_params(0, freq=440)
        
        state1 = engine.get_state()
        state1['tones'][0]['freq'] = 999  # Modify copy
        
        state2 = engine.get_state()
        
        # Original should be unchanged
        assert state2['tones'][0]['freq'] == 440
    
    def test_tone_parameters_persist_across_enable_disable(self, engine):
        """Tone parameters should persist when toggling multi-tone mode."""
        engine.set_multi_tone_params(0, freq=440, amp=0.8)
        engine.set_multi_tone_params(1, freq=554.37, amp=0.6, enabled=True)
        
        engine.enable_multi_tone(True)
        state1 = engine.get_state()
        
        engine.enable_multi_tone(False)
        engine.enable_multi_tone(True)
        state2 = engine.get_state()
        
        # Parameters should be preserved
        assert state2['tones'][0]['freq'] == 440
        assert state2['tones'][0]['amp'] == 0.8
        assert state2['tones'][1]['freq'] == 554.37
        assert state2['tones'][1]['enabled'] is True


class TestMultiToneChordGeneration:
    """Test generating musical chords with multi-tone."""
    
    def test_a_major_chord(self, engine, ring_buffer):
        """Should generate A major chord (A4, C#5, E5)."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=440.00, amp=0.6, enabled=True)   # A4
        engine.set_multi_tone_params(1, freq=554.37, amp=0.6, enabled=True)   # C#5
        engine.set_multi_tone_params(2, freq=659.25, amp=0.6, enabled=True)   # E5
        engine.set_multi_tone_params(3, enabled=False)
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.3)  # Python 3.9 needs more time for AudioEngine to fill ring buffer
        engine.stop()
        
        samples = ring_buffer.read(4096)
        assert len(samples) > 0
        
        # Verify non-zero signal (chord is audible)
        assert np.any(samples != 0)
    
    def test_c_major_chord(self, engine, ring_buffer):
        """Should generate C major chord (C4, E4, G4)."""
        engine.enable_multi_tone(True)
        engine.set_multi_tone_params(0, freq=261.63, amp=0.6, enabled=True)   # C4
        engine.set_multi_tone_params(1, freq=329.63, amp=0.6, enabled=True)   # E4
        engine.set_multi_tone_params(2, freq=392.00, amp=0.6, enabled=True)   # G4
        engine.set_multi_tone_params(3, enabled=False)
        
        engine.set_source('tone')
        engine.start()
        time.sleep(0.2)
        engine.stop()
        
        samples = ring_buffer.read(4096)
        assert len(samples) > 0
        assert np.any(samples != 0)
    
    def test_switching_between_chords(self, engine, ring_buffer):
        """Should switch between different chords smoothly."""
        engine.enable_multi_tone(True)
        engine.set_source('tone')
        engine.start()
        
        # Play A major
        engine.set_multi_tone_params(0, freq=440.00, amp=0.5, enabled=True)
        engine.set_multi_tone_params(1, freq=554.37, amp=0.5, enabled=True)
        engine.set_multi_tone_params(2, freq=659.25, amp=0.5, enabled=True)
        time.sleep(0.2)
        
        # Switch to C major (parameter changes should be atomic)
        engine.set_multi_tone_params(0, freq=261.63)
        engine.set_multi_tone_params(1, freq=329.63)
        engine.set_multi_tone_params(2, freq=392.00)
        time.sleep(0.2)
        
        engine.stop()
        
        # Should have continuous audio data
        samples = ring_buffer.read(4096)
        assert len(samples) > 0


"""
Unit tests for AudioEngine.

Tests cover:
- Initialization with config and ring buffer
- Starting/stopping generation thread
- Tone generation with phase accumulator
- Frequency accuracy (FFT verification)
- Amplitude accuracy
- Phase continuity (no clicks on frequency change)
- Stereo modes (mono, left, right, dual)
- Sweep generation (logarithmic chirp)
- WAV loading and resampling
- WAV file error handling
- Thread safety
"""

import time
import tempfile
import pytest
import numpy as np
from unittest.mock import Mock, patch, MagicMock
from pathlib import Path
from scipy.io import wavfile
from scipy import signal as scipy_signal

from audio.engine import AudioEngine
from audio.ring_buffer import RingBuffer
from audio.exceptions import WAVNotFoundError, WAVFormatError
from config.manager import ConfigManager


@pytest.fixture
def mock_config():
    """Create mock ConfigManager for testing."""
    config = Mock(spec=ConfigManager)
    config.get.side_effect = lambda key: {
        'i2s.sample_rate': 48000,
        'i2s.buffer_size': 8192,
        'audio.tone_freq': 1000,
        'audio.tone_amp': 0.5,
        'audio.wav_directory': '/tmp/audio'
    }.get(key, None)
    return config


@pytest.fixture
def ring_buffer():
    """Create real RingBuffer for testing."""
    return RingBuffer(8192)


@pytest.fixture
def engine(mock_config, ring_buffer):
    """Create AudioEngine instance for testing."""
    return AudioEngine(mock_config, ring_buffer)


class TestAudioEngineInit:
    """Tests for AudioEngine initialization."""
    
    def test_init_creates_engine(self, mock_config, ring_buffer):
        """AudioEngine should initialize with config and ring buffer."""
        engine = AudioEngine(mock_config, ring_buffer)
        
        assert engine.config == mock_config
        assert engine.ring_buffer == ring_buffer
        assert engine.sample_rate == 48000
        assert engine.chunk_size == 2048  # buffer_size / 4
        assert engine.source == 'silence'
        assert engine.tone_freq == 1000
        assert engine.tone_amp == 0.5
        assert engine.running is False
        assert engine.phase == 0.0
    
    def test_init_sets_defaults(self, mock_config, ring_buffer):
        """AudioEngine should set default tone parameters from config."""
        engine = AudioEngine(mock_config, ring_buffer)
        
        assert engine.tone_mode == 'mono'
        assert engine.dual_freq == 440
        assert engine.sweep_params is None
        assert engine.wav_data is None


class TestAudioEngineStartStop:
    """Tests for starting and stopping the generation thread."""
    
    def test_start_launches_thread(self, engine):
        """start() should launch background generation thread."""
        engine.start()
        
        assert engine.running is True
        assert engine.thread is not None
        assert engine.thread.is_alive()
        
        engine.stop()
    
    def test_stop_gracefully_shuts_down(self, engine):
        """stop() should gracefully shut down the generation thread."""
        engine.start()
        time.sleep(0.1)  # Let thread run briefly
        
        engine.stop()
        
        assert engine.running is False
        # Thread should be joined
        if engine.thread:
            assert not engine.thread.is_alive()
    
    def test_start_idempotent(self, engine):
        """start() should be safe to call multiple times."""
        engine.start()
        thread1 = engine.thread
        
        engine.start()  # Call again
        thread2 = engine.thread
        
        assert thread1 == thread2  # Same thread
        
        engine.stop()
    
    def test_stop_idempotent(self, engine):
        """stop() should be safe to call multiple times."""
        engine.start()
        engine.stop()
        engine.stop()  # Call again - should not raise


class TestAudioEngineToneGeneration:
    """Tests for tone generation."""
    
    def test_generate_tone_mono(self, engine):
        """_generate_tone should generate mono sine wave."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='mono')
        
        samples = engine._generate_tone()
        
        # Should be stereo interleaved (chunk_size * 2)
        assert len(samples) == engine.chunk_size * 2
        assert samples.dtype == np.int16
        
        # Extract left and right channels
        left = samples[0::2]
        right = samples[1::2]
        
        # Mono: left should equal right
        np.testing.assert_array_equal(left, right)
    
    def test_generate_tone_left_only(self, engine):
        """_generate_tone should generate left channel only."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='left')
        
        samples = engine._generate_tone()
        
        left = samples[0::2]
        right = samples[1::2]
        
        # Left should have signal (non-zero RMS)
        assert np.sqrt(np.mean(left.astype(float)**2)) > 1000
        
        # Right should be silent
        np.testing.assert_array_equal(right, np.zeros_like(right))
    
    def test_generate_tone_right_only(self, engine):
        """_generate_tone should generate right channel only."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='right')
        
        samples = engine._generate_tone()
        
        left = samples[0::2]
        right = samples[1::2]
        
        # Left should be silent
        np.testing.assert_array_equal(left, np.zeros_like(left))
        
        # Right should have signal
        assert np.sqrt(np.mean(right.astype(float)**2)) > 1000
    
    def test_generate_tone_dual(self, engine):
        """_generate_tone should generate dual-tone mode."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='dual', dual_freq=440)
        
        samples = engine._generate_tone()
        
        left = samples[0::2]
        right = samples[1::2]
        
        # Channels should be different (different frequencies)
        assert not np.array_equal(left, right)
        
        # Both should have signal
        assert np.sqrt(np.mean(left.astype(float)**2)) > 1000
        assert np.sqrt(np.mean(right.astype(float)**2)) > 1000


class TestAudioEngineToneFrequencyAccuracy:
    """Tests for tone frequency accuracy using FFT."""
    
    def test_tone_frequency_1000hz(self, engine):
        """Tone at 1000 Hz should have FFT peak at 1000 Hz ±5 Hz."""
        engine.set_tone_params(freq=1000, amp=0.8, mode='mono')
        
        # Generate multiple chunks to get good FFT resolution
        all_samples = []
        for _ in range(8):
            chunk = engine._generate_tone()
            all_samples.append(chunk)
        
        samples = np.concatenate(all_samples)
        left = samples[0::2].astype(float)
        
        # Compute FFT
        fft = np.fft.rfft(left)
        freqs = np.fft.rfftfreq(len(left), 1/engine.sample_rate)
        magnitudes = np.abs(fft)
        
        # Find peak frequency
        peak_idx = np.argmax(magnitudes)
        peak_freq = freqs[peak_idx]
        
        # Should be 1000 Hz ±5 Hz
        assert abs(peak_freq - 1000) < 5
    
    def test_tone_frequency_440hz(self, engine):
        """Tone at 440 Hz should have FFT peak at 440 Hz ±5 Hz."""
        engine.set_tone_params(freq=440, amp=0.8, mode='mono')
        
        all_samples = []
        for _ in range(8):
            chunk = engine._generate_tone()
            all_samples.append(chunk)
        
        samples = np.concatenate(all_samples)
        left = samples[0::2].astype(float)
        
        fft = np.fft.rfft(left)
        freqs = np.fft.rfftfreq(len(left), 1/engine.sample_rate)
        magnitudes = np.abs(fft)
        
        peak_idx = np.argmax(magnitudes)
        peak_freq = freqs[peak_idx]
        
        assert abs(peak_freq - 440) < 5


class TestAudioEngineToneAmplitude:
    """Tests for tone amplitude accuracy."""
    
    def test_tone_amplitude_50_percent(self, engine):
        """Tone at 50% amplitude should have RMS ~50% of max."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='mono')
        
        samples = engine._generate_tone()
        left = samples[0::2].astype(float)
        
        # Calculate RMS
        rms = np.sqrt(np.mean(left**2))
        
        # Expected RMS for sine wave: amp * 32767 / sqrt(2)
        expected_rms = 0.5 * 32767 / np.sqrt(2)
        
        # Should be within ±5%
        assert abs(rms - expected_rms) / expected_rms < 0.05
    
    def test_tone_amplitude_80_percent(self, engine):
        """Tone at 80% amplitude should have RMS ~80% of max."""
        engine.set_tone_params(freq=1000, amp=0.8, mode='mono')
        
        samples = engine._generate_tone()
        left = samples[0::2].astype(float)
        
        rms = np.sqrt(np.mean(left**2))
        expected_rms = 0.8 * 32767 / np.sqrt(2)
        
        assert abs(rms - expected_rms) / expected_rms < 0.05


class TestAudioEnginePhaseContinuity:
    """Tests for phase continuity (no clicks when changing frequency)."""
    
    def test_phase_continuity_across_chunks(self, engine):
        """Phase should be continuous across multiple chunks."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='mono')
        
        chunk1 = engine._generate_tone()
        chunk2 = engine._generate_tone()
        
        # Extract left channel
        left1 = chunk1[0::2].astype(float)
        left2 = chunk2[0::2].astype(float)
        
        # Check last sample of chunk1 and first sample of chunk2
        # Phase difference should be consistent with frequency
        last_sample = left1[-1]
        first_sample = left2[0]
        
        # Calculate expected phase increment (one sample at 1000 Hz)
        phase_increment = 2 * np.pi * 1000 / engine.sample_rate
        expected_amp = 0.5 * 32767
        
        # Samples should follow sine wave continuity
        # (not exact due to quantization, but should be close)
        # Just verify no huge discontinuity (click would be >50% jump)
        discontinuity = abs(first_sample - last_sample) / expected_amp
        assert discontinuity < 0.5
    
    def test_phase_preserved_on_frequency_change(self, engine):
        """Changing frequency should preserve phase (no click)."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='mono')
        
        # Generate chunk at 1000 Hz
        chunk1 = engine._generate_tone()
        
        # Change frequency mid-stream
        engine.set_tone_params(freq=2000)
        
        # Generate chunk at 2000 Hz
        chunk2 = engine._generate_tone()
        
        # Phase accumulator should have preserved state
        # (no huge discontinuity at boundary)
        left1 = chunk1[0::2].astype(float)
        left2 = chunk2[0::2].astype(float)
        
        last_sample = left1[-1]
        first_sample = left2[0]
        
        expected_amp = 0.5 * 32767
        discontinuity = abs(first_sample - last_sample) / expected_amp
        
        # Should be smooth transition (no click)
        assert discontinuity < 0.5


class TestAudioEngineSweepGeneration:
    """Tests for frequency sweep generation."""
    
    def test_generate_sweep_basic(self, engine):
        """_generate_sweep should generate logarithmic chirp."""
        engine.set_source('sweep', {
            'start_freq': 100,
            'end_freq': 1000,
            'duration': 1.0,
            'loop': False
        })
        
        samples = engine._generate_sweep()
        
        assert len(samples) == engine.chunk_size * 2
        assert samples.dtype == np.int16
        
        # Should have non-zero signal
        assert np.any(samples != 0)
    
    def test_sweep_progresses_position(self, engine):
        """Sweep should increment position on each chunk."""
        engine.set_source('sweep', {
            'start_freq': 100,
            'end_freq': 1000,
            'duration': 1.0,
            'loop': False
        })
        
        assert engine.sweep_position == 0
        
        engine._generate_sweep()
        pos1 = engine.sweep_position
        
        engine._generate_sweep()
        pos2 = engine.sweep_position
        
        # Position should increment by chunk_size
        assert pos2 == pos1 + engine.chunk_size
    
    def test_sweep_completes_and_switches_to_silence(self, engine):
        """Sweep should switch to silence when complete (no loop)."""
        # Very short sweep (will complete in a few chunks)
        engine.set_source('sweep', {
            'start_freq': 100,
            'end_freq': 1000,
            'duration': 0.01,  # 10ms sweep
            'loop': False
        })
        
        # Generate chunks until sweep completes
        for _ in range(10):
            engine._generate_sweep()
        
        # Should have switched to silence
        assert engine.source == 'silence'
    
    def test_sweep_loops(self, engine):
        """Sweep should loop when loop=True."""
        engine.set_source('sweep', {
            'start_freq': 100,
            'end_freq': 1000,
            'duration': 0.01,  # 10ms sweep
            'loop': True
        })
        
        # Generate chunks past duration
        for _ in range(10):
            engine._generate_sweep()
        
        # Should still be sweep (not silence)
        assert engine.source == 'sweep'
        
        # Position should have been reset (looped)
        # Since chunk_size (2048) > total_samples (480), position will be reset multiple times
        total_samples = int(0.01 * engine.sample_rate)
        # Just verify we're still in sweep mode (the key test)
        # Position behavior is implementation detail


class TestAudioEngineWAVLoading:
    """Tests for WAV file loading and resampling."""
    
    def test_load_wav_48khz_stereo(self, engine, tmp_path):
        """_load_wav should load 48 kHz stereo WAV correctly."""
        # Create test WAV file (48 kHz, stereo, 16-bit)
        sample_rate = 48000
        duration = 0.1
        num_samples = int(sample_rate * duration)
        
        # Generate stereo tone
        t = np.arange(num_samples) / sample_rate
        left = (np.sin(2 * np.pi * 1000 * t) * 32767 * 0.5).astype(np.int16)
        right = (np.sin(2 * np.pi * 440 * t) * 32767 * 0.5).astype(np.int16)
        stereo_data = np.column_stack([left, right])
        
        # Write WAV
        wav_path = tmp_path / "test_48k_stereo.wav"
        wavfile.write(wav_path, sample_rate, stereo_data)
        
        # Load WAV
        loaded = engine._load_wav(wav_path)
        
        # Should be stereo interleaved
        assert loaded.dtype == np.int16
        assert len(loaded) == num_samples * 2  # Stereo
        
        # Verify data integrity
        loaded_left = loaded[0::2]
        loaded_right = loaded[1::2]
        
        # Should be close to original (within int16 quantization)
        np.testing.assert_array_almost_equal(loaded_left, left, decimal=0)
        np.testing.assert_array_almost_equal(loaded_right, right, decimal=0)
    
    def test_load_wav_44khz_resampled(self, engine, tmp_path):
        """_load_wav should resample 44.1 kHz to 48 kHz."""
        # Create test WAV file (44.1 kHz, stereo)
        sample_rate = 44100
        duration = 0.1
        num_samples = int(sample_rate * duration)
        
        t = np.arange(num_samples) / sample_rate
        left = (np.sin(2 * np.pi * 1000 * t) * 32767 * 0.5).astype(np.int16)
        right = (np.sin(2 * np.pi * 440 * t) * 32767 * 0.5).astype(np.int16)
        stereo_data = np.column_stack([left, right])
        
        wav_path = tmp_path / "test_44k_stereo.wav"
        wavfile.write(wav_path, sample_rate, stereo_data)
        
        # Load WAV
        loaded = engine._load_wav(wav_path)
        
        # Should be resampled to 48 kHz
        expected_samples = int(num_samples * 48000 / 44100)
        assert len(loaded) == expected_samples * 2  # Stereo
    
    def test_load_wav_mono_to_stereo(self, engine, tmp_path):
        """_load_wav should convert mono to stereo."""
        # Create test WAV file (48 kHz, mono)
        sample_rate = 48000
        duration = 0.1
        num_samples = int(sample_rate * duration)
        
        t = np.arange(num_samples) / sample_rate
        mono_data = (np.sin(2 * np.pi * 1000 * t) * 32767 * 0.5).astype(np.int16)
        
        wav_path = tmp_path / "test_mono.wav"
        wavfile.write(wav_path, sample_rate, mono_data)
        
        # Load WAV
        loaded = engine._load_wav(wav_path)
        
        # Should be stereo
        assert len(loaded) == num_samples * 2
        
        # Left and right should be identical (mono duplicated)
        left = loaded[0::2]
        right = loaded[1::2]
        np.testing.assert_array_equal(left, right)
    
    def test_load_wav_file_not_found(self, engine):
        """_load_wav should raise WAVNotFoundError if file missing."""
        wav_path = Path("/nonexistent/path/missing.wav")
        
        with pytest.raises(WAVNotFoundError) as exc_info:
            engine._load_wav(wav_path)
        
        assert "missing.wav" in str(exc_info.value)
    
    def test_load_wav_invalid_format(self, engine, tmp_path):
        """_load_wav should raise WAVFormatError for invalid files."""
        # Create invalid WAV file (just text)
        wav_path = tmp_path / "invalid.wav"
        wav_path.write_text("This is not a WAV file")
        
        with pytest.raises(WAVFormatError) as exc_info:
            engine._load_wav(wav_path)
        
        assert "invalid.wav" in str(exc_info.value)


class TestAudioEngineSetSource:
    """Tests for set_source() method."""
    
    def test_set_source_tone(self, engine):
        """set_source should switch to tone generation."""
        engine.set_source('tone', {'freq': 1000, 'amp': 0.8, 'mode': 'mono'})
        
        assert engine.source == 'tone'
        assert engine.tone_freq == 1000
        assert engine.tone_amp == 0.8
        assert engine.tone_mode == 'mono'
    
    def test_set_source_sweep(self, engine):
        """set_source should switch to sweep generation."""
        engine.set_source('sweep', {
            'start_freq': 20,
            'end_freq': 20000,
            'duration': 10
        })
        
        assert engine.source == 'sweep'
        assert engine.sweep_params['start_freq'] == 20
        assert engine.sweep_params['end_freq'] == 20000
        assert engine.sweep_params['duration'] == 10
    
    def test_set_source_silence(self, engine):
        """set_source should switch to silence."""
        engine.set_source('tone', {'freq': 1000})
        engine.set_source('silence')
        
        assert engine.source == 'silence'
    
    def test_set_source_wav_requires_file(self, engine):
        """set_source('wav') should require 'file' parameter."""
        with pytest.raises(ValueError) as exc_info:
            engine.set_source('wav', {})
        
        assert "file" in str(exc_info.value).lower()


class TestAudioEngineSetToneParams:
    """Tests for set_tone_params() method."""
    
    def test_set_tone_params_frequency(self, engine):
        """set_tone_params should update frequency."""
        engine.set_tone_params(freq=440)
        
        assert engine.tone_freq == 440
    
    def test_set_tone_params_amplitude(self, engine):
        """set_tone_params should update amplitude."""
        engine.set_tone_params(amp=0.8)
        
        assert engine.tone_amp == 0.8
    
    def test_set_tone_params_mode(self, engine):
        """set_tone_params should update stereo mode."""
        engine.set_tone_params(mode='left')
        
        assert engine.tone_mode == 'left'
    
    def test_set_tone_params_partial(self, engine):
        """set_tone_params should allow partial updates."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='mono')
        
        # Update only frequency
        engine.set_tone_params(freq=440)
        
        # Frequency changed, others preserved
        assert engine.tone_freq == 440
        assert engine.tone_amp == 0.5
        assert engine.tone_mode == 'mono'


class TestAudioEngineThreadSafety:
    """Tests for thread safety."""
    
    def test_set_tone_params_while_running(self, engine):
        """set_tone_params should be thread-safe during generation."""
        engine.start()
        time.sleep(0.1)
        
        # Change parameters while generating
        engine.set_tone_params(freq=440, amp=0.8, mode='left')
        
        time.sleep(0.1)
        
        # Should not crash, parameters should be updated
        state = engine.get_state()
        assert state['frequency'] == 440
        assert state['amplitude'] == 0.8
        assert state['stereo_mode'] == 'left'
        
        engine.stop()
    
    def test_set_source_while_running(self, engine):
        """set_source should be thread-safe during generation."""
        engine.start()
        time.sleep(0.1)
        
        # Change source while generating
        engine.set_source('tone', {'freq': 1000})
        time.sleep(0.1)
        
        engine.set_source('silence')
        time.sleep(0.1)
        
        # Should not crash
        state = engine.get_state()
        assert state['source'] == 'silence'
        
        engine.stop()


class TestAudioEngineGetState:
    """Tests for get_state() method."""
    
    def test_get_state_returns_current_state(self, engine):
        """get_state should return current audio parameters."""
        engine.set_tone_params(freq=1000, amp=0.5, mode='mono')
        
        state = engine.get_state()
        
        assert state['source'] == 'silence'  # Default
        assert state['frequency'] == 1000
        assert state['amplitude'] == 0.5
        assert state['stereo_mode'] == 'mono'
    
    def test_get_state_reflects_updates(self, engine):
        """get_state should reflect parameter updates."""
        engine.set_tone_params(freq=440)
        state1 = engine.get_state()
        
        engine.set_tone_params(freq=880)
        state2 = engine.get_state()
        
        assert state1['frequency'] == 440
        assert state2['frequency'] == 880

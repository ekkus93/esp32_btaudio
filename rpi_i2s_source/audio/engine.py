"""
Audio Engine for RPi I2S Source.

This module provides the AudioEngine class for generating audio samples to feed
the I2S ring buffer. Supports tone generation, frequency sweeps, WAV playback,
and silence.

Classes:
    AudioEngine: Audio generation engine with background thread.

Example:
    >>> from audio.ring_buffer import RingBuffer
    >>> from config.manager import ConfigManager
    >>> 
    >>> config = ConfigManager('config.yaml')
    >>> ring_buffer = RingBuffer(8192)
    >>> engine = AudioEngine(config, ring_buffer)
    >>> engine.start()
    >>> engine.set_tone_params(freq=1000, amp=0.5, mode='mono')
    >>> # ... audio plays continuously in background thread
    >>> engine.stop()
"""

import threading
import time
import numpy as np
from pathlib import Path
from typing import Optional, Dict, Any
from scipy import signal
from scipy.io import wavfile

from audio.exceptions import WAVNotFoundError, WAVFormatError


class AudioEngine:
    """
    Audio generation engine with background thread.
    
    Generates audio samples (tone, sweep, WAV, silence) and writes them to the
    ring buffer for I2S transmission. Runs in background thread with thread-safe
    parameter updates for click-free transitions.
    
    Attributes:
        config: ConfigManager instance for audio parameters
        ring_buffer: RingBuffer for audio sample output
        sample_rate: Sample rate in Hz (from config, typically 48000)
        chunk_size: Samples per generation chunk (from config buffer size / 4)
        
        source: Current audio source ('silence', 'tone', 'sweep', 'wav')
        tone_freq: Tone frequency in Hz
        tone_amp: Tone amplitude (0.0-1.0)
        tone_mode: Stereo mode ('mono', 'left', 'right', 'dual')
        dual_freq: Second frequency for dual-tone mode
        
        sweep_params: Dictionary with sweep parameters (start_freq, end_freq, duration, loop)
        sweep_position: Current position in sweep (samples)
        
        wav_data: Loaded WAV file samples (16-bit stereo, 48 kHz)
        wav_position: Current position in WAV playback (samples)
        wav_loop: Whether to loop WAV playback
        
        running: Flag indicating generation thread is active
        thread: Background generation thread
        lock: Lock for thread-safe parameter updates
        phase: Phase accumulator for click-free frequency changes
    
    Thread Safety:
        All public methods are thread-safe. Parameter updates (set_tone_params,
        set_source) use locks to ensure atomic updates without audio glitches.
    """
    
    def __init__(self, config, ring_buffer):
        """
        Initialize audio engine.
        
        Args:
            config: ConfigManager instance with audio configuration
            ring_buffer: RingBuffer instance for audio output
        
        Example:
            >>> config = ConfigManager('config.yaml')
            >>> ring_buffer = RingBuffer(8192)
            >>> engine = AudioEngine(config, ring_buffer)
        """
        self.config = config
        self.ring_buffer = ring_buffer
        
        # Get parameters from config
        self.sample_rate = config.get('i2s.sample_rate')
        buffer_size = config.get('i2s.buffer_size')
        self.chunk_size = buffer_size // 4  # Generate in smaller chunks
        
        # Audio source state (protected by lock)
        self.source = 'silence'
        self.tone_freq = config.get('audio.tone_freq')
        self.tone_amp = config.get('audio.tone_amp')
        self.tone_mode = 'mono'
        self.dual_freq = 440  # Second frequency for dual-tone mode
        
        # Sweep state
        self.sweep_params = None
        self.sweep_position = 0
        
        # WAV state
        self.wav_data = None
        self.wav_position = 0
        self.wav_loop = False
        
        # Thread control
        self.running = False
        self.thread = None
        self.lock = threading.Lock()
        
        # Phase accumulator for click-free frequency changes
        self.phase = 0.0
    
    def start(self) -> None:
        """
        Start audio generation thread.
        
        Launches background thread that continuously generates audio chunks
        and writes them to the ring buffer.
        
        Example:
            >>> engine.start()
            >>> # Audio generation now running in background
        """
        if self.running:
            return
        
        self.running = True
        self.thread = threading.Thread(target=self._generation_loop, daemon=True)
        self.thread.start()
    
    def stop(self) -> None:
        """
        Stop audio generation thread (graceful shutdown).
        
        Signals the generation thread to stop and waits for it to finish.
        Safe to call multiple times.
        
        Example:
            >>> engine.stop()
            >>> # Audio generation stopped, thread joined
        """
        if not self.running:
            return
        
        self.running = False
        if self.thread:
            self.thread.join(timeout=1.0)
            self.thread = None
    
    def set_source(self, source: str, params: Optional[Dict[str, Any]] = None) -> None:
        """
        Switch audio source (tone/sweep/wav/silence).
        
        Thread-safe method to change the active audio source. Parameters are
        updated atomically to prevent audio glitches.
        
        Args:
            source: Audio source type ('silence', 'tone', 'sweep', 'wav')
            params: Optional source-specific parameters:
                - For 'tone': {'freq': Hz, 'amp': 0-1, 'mode': 'mono'/'left'/'right'/'dual', 'dual_freq': Hz}
                - For 'sweep': {'start_freq': Hz, 'end_freq': Hz, 'duration': seconds, 'loop': bool}
                - For 'wav': {'file': filename, 'loop': bool}
                - For 'silence': None
        
        Raises:
            WAVNotFoundError: If WAV file does not exist
            WAVFormatError: If WAV file format is unsupported
        
        Example:
            >>> engine.set_source('tone', {'freq': 1000, 'amp': 0.5, 'mode': 'mono'})
            >>> engine.set_source('sweep', {'start_freq': 20, 'end_freq': 20000, 'duration': 10})
            >>> engine.set_source('wav', {'file': 'test.wav', 'loop': True})
            >>> engine.set_source('silence')
        """
        with self.lock:
            if source == 'tone':
                self.source = 'tone'
                if params:
                    self.tone_freq = params.get('freq', self.tone_freq)
                    self.tone_amp = params.get('amp', self.tone_amp)
                    self.tone_mode = params.get('mode', 'mono')
                    self.dual_freq = params.get('dual_freq', 440)
            
            elif source == 'sweep':
                self.source = 'sweep'
                if params:
                    self.sweep_params = {
                        'start_freq': params.get('start_freq', 20),
                        'end_freq': params.get('end_freq', 20000),
                        'duration': params.get('duration', 10),
                        'loop': params.get('loop', False)
                    }
                    self.sweep_position = 0
            
            elif source == 'wav':
                if params and 'file' in params:
                    wav_dir = Path(self.config.get('audio.wav_directory'))
                    wav_path = wav_dir / params['file']
                    self.wav_data = self._load_wav(wav_path)
                    self.wav_position = 0
                    self.wav_loop = params.get('loop', False)
                    self.source = 'wav'
                else:
                    raise ValueError("WAV source requires 'file' parameter")
            
            elif source == 'silence':
                self.source = 'silence'
            
            else:
                raise ValueError(f"Unknown source: {source}")
    
    def set_tone_params(self, freq: Optional[float] = None, 
                       amp: Optional[float] = None,
                       mode: Optional[str] = None,
                       dual_freq: Optional[float] = None) -> None:
        """
        Update tone parameters (atomic, click-free).
        
        Thread-safe method to update tone generation parameters without stopping
        audio. Uses phase accumulator to maintain continuity during frequency
        changes, preventing clicks.
        
        Args:
            freq: Tone frequency in Hz (20-20000), or None to keep current
            amp: Tone amplitude (0.0-1.0), or None to keep current
            mode: Stereo mode ('mono'/'left'/'right'/'dual'), or None to keep current
            dual_freq: Second frequency for dual-tone mode, or None to keep current
        
        Example:
            >>> engine.set_tone_params(freq=1000)  # Change frequency only
            >>> engine.set_tone_params(freq=440, amp=0.8, mode='mono')  # Change multiple
        """
        with self.lock:
            if freq is not None:
                self.tone_freq = freq
            if amp is not None:
                self.tone_amp = amp
            if mode is not None:
                self.tone_mode = mode
            if dual_freq is not None:
                self.dual_freq = dual_freq
    
    def get_state(self) -> Dict[str, Any]:
        """
        Get current audio engine state.
        
        Returns:
            Dictionary with current state (source, frequency, amplitude, etc.)
        
        Example:
            >>> state = engine.get_state()
            >>> print(state['source'])  # 'tone'
            >>> print(state['frequency'])  # 1000
        """
        with self.lock:
            return {
                'source': self.source,
                'frequency': self.tone_freq,
                'amplitude': self.tone_amp,
                'stereo_mode': self.tone_mode,
                'dual_freq': self.dual_freq,
                'sweep_params': dict(self.sweep_params) if self.sweep_params else None,
                'wav_file': None,  # Could track filename if needed
                'wav_loop': self.wav_loop
            }
    
    def _generation_loop(self) -> None:
        """
        Main background thread for audio generation.
        
        Continuously generates audio chunks and writes them to the ring buffer.
        Checks buffer fill level and sleeps if buffer is nearly full to avoid
        busy-waiting.
        """
        while self.running:
            # Check if buffer has space
            fill_pct = self.ring_buffer.get_fill_percentage()
            if fill_pct > 80:
                # Buffer nearly full, sleep briefly
                time.sleep(0.01)
                continue
            
            # Generate next chunk
            chunk = self._generate_next_chunk()
            
            # Write to ring buffer
            self.ring_buffer.write(chunk)
    
    def _generate_next_chunk(self) -> np.ndarray:
        """
        Dispatch to appropriate generator based on current source.
        
        Returns:
            NumPy array of int16 samples (stereo interleaved LRLRLR...)
        """
        with self.lock:
            source = self.source
        
        if source == 'tone':
            return self._generate_tone()
        elif source == 'sweep':
            return self._generate_sweep()
        elif source == 'wav':
            return self._generate_wav()
        else:  # silence
            return np.zeros(self.chunk_size * 2, dtype=np.int16)
    
    def _generate_tone(self) -> np.ndarray:
        """
        Generate tone using NumPy sine wave with phase accumulator.
        
        Maintains phase continuity across chunks to prevent clicks when changing
        frequency. Supports multiple stereo modes:
        - mono: Same signal on both channels
        - left: Signal on left, silence on right
        - right: Silence on left, signal on right
        - dual: Different frequencies on left and right
        
        Returns:
            NumPy array of int16 samples (stereo interleaved LRLRLR...)
        
        Example output for mono mode:
            [L0, R0, L1, R1, L2, R2, ...]
            where L == R (same signal)
        """
        with self.lock:
            freq = self.tone_freq
            amp = self.tone_amp
            mode = self.tone_mode
            dual_freq = self.dual_freq
        
        # Generate time array for this chunk
        num_samples = self.chunk_size
        t = np.arange(num_samples) / self.sample_rate
        
        # Generate left channel tone with phase accumulator
        phase_increment = 2 * np.pi * freq / self.sample_rate
        phases = self.phase + phase_increment * np.arange(num_samples)
        left_signal = np.sin(phases)
        
        # Update phase accumulator (wrap to prevent overflow)
        self.phase = (self.phase + phase_increment * num_samples) % (2 * np.pi)
        
        # Generate right channel based on mode
        if mode == 'mono':
            right_signal = left_signal
        elif mode == 'left':
            right_signal = np.zeros(num_samples)
        elif mode == 'right':
            left_signal = np.zeros(num_samples)
            right_signal = np.sin(2 * np.pi * freq * t)
        elif mode == 'dual':
            # Left uses primary freq, right uses dual_freq
            right_signal = np.sin(2 * np.pi * dual_freq * t)
        else:
            right_signal = left_signal  # Default to mono
        
        # Apply amplitude scaling and convert to 16-bit
        scale = amp * 32767
        left_samples = (left_signal * scale).astype(np.int16)
        right_samples = (right_signal * scale).astype(np.int16)
        
        # Interleave stereo: LRLRLR...
        stereo = np.empty(num_samples * 2, dtype=np.int16)
        stereo[0::2] = left_samples
        stereo[1::2] = right_samples
        
        return stereo
    
    def _generate_sweep(self) -> np.ndarray:
        """
        Generate logarithmic chirp using scipy.signal.chirp.
        
        Creates a frequency sweep from start_freq to end_freq over the specified
        duration. Supports loop mode for continuous sweeps.
        
        Returns:
            NumPy array of int16 samples (stereo interleaved LRLRLR...)
        """
        with self.lock:
            if self.sweep_params is None:
                return np.zeros(self.chunk_size * 2, dtype=np.int16)
            
            params = self.sweep_params.copy()
            position = self.sweep_position
        
        start_freq = params['start_freq']
        end_freq = params['end_freq']
        duration = params['duration']
        loop = params['loop']
        
        total_samples = int(duration * self.sample_rate)
        num_samples = self.chunk_size
        
        # Check if sweep is complete
        if position >= total_samples:
            if loop:
                position = 0
                with self.lock:
                    self.sweep_position = 0
            else:
                # Sweep complete, switch to silence
                with self.lock:
                    self.source = 'silence'
                return np.zeros(num_samples * 2, dtype=np.int16)
        
        # Generate time array for this chunk
        t_start = position / self.sample_rate
        t = t_start + np.arange(num_samples) / self.sample_rate
        
        # Generate chirp using scipy
        mono_signal = signal.chirp(t, start_freq, duration, end_freq, method='logarithmic')
        
        # Update position
        with self.lock:
            self.sweep_position = position + num_samples
        
        # Convert to 16-bit stereo (mono duplicated to both channels)
        scale = 0.5 * 32767  # Use 0.5 amplitude for sweeps
        samples = (mono_signal * scale).astype(np.int16)
        
        stereo = np.empty(num_samples * 2, dtype=np.int16)
        stereo[0::2] = samples  # Left
        stereo[1::2] = samples  # Right
        
        return stereo
    
    def _generate_wav(self) -> np.ndarray:
        """
        Read chunk from loaded WAV buffer.
        
        Reads samples from the pre-loaded WAV data. Handles end-of-file by
        either stopping (if loop=False) or wrapping to beginning (if loop=True).
        
        Returns:
            NumPy array of int16 samples (stereo interleaved LRLRLR...)
        """
        with self.lock:
            if self.wav_data is None:
                return np.zeros(self.chunk_size * 2, dtype=np.int16)
            
            wav_data = self.wav_data
            position = self.wav_position
            loop = self.wav_loop
        
        num_samples = self.chunk_size
        total_samples = len(wav_data) // 2  # Stereo, so divide by 2
        
        # Check if we've reached end
        if position >= total_samples:
            if loop:
                position = 0
                with self.lock:
                    self.wav_position = 0
            else:
                # WAV complete, switch to silence
                with self.lock:
                    self.source = 'silence'
                    self.wav_data = None
                return np.zeros(num_samples * 2, dtype=np.int16)
        
        # Calculate samples to read (handle end of buffer)
        samples_remaining = total_samples - position
        samples_to_read = min(num_samples, samples_remaining)
        
        # Read from WAV data (already stereo interleaved)
        start_idx = position * 2
        end_idx = start_idx + samples_to_read * 2
        chunk = wav_data[start_idx:end_idx].copy()
        
        # Update position
        with self.lock:
            self.wav_position = position + samples_to_read
        
        # Pad with zeros if we didn't read enough
        if samples_to_read < num_samples:
            padding = np.zeros((num_samples - samples_to_read) * 2, dtype=np.int16)
            chunk = np.concatenate([chunk, padding])
        
        return chunk
    
    def _load_wav(self, filepath: Path) -> np.ndarray:
        """
        Load WAV file from disk.
        
        Loads WAV file and converts to 48 kHz stereo 16-bit PCM:
        - Resamples to 48 kHz if needed
        - Converts mono to stereo (duplicate channels)
        - Converts to 16-bit if needed
        
        Args:
            filepath: Path to WAV file
        
        Returns:
            NumPy array of int16 samples (stereo interleaved LRLRLR...)
        
        Raises:
            WAVNotFoundError: If file does not exist
            WAVFormatError: If file format is unsupported
        
        Example:
            >>> wav_data = engine._load_wav(Path('/home/pi/audio/test.wav'))
            >>> print(wav_data.shape)  # (N*2,) for stereo
        """
        if not filepath.exists():
            raise WAVNotFoundError(f"{filepath.name} not found in {filepath.parent}")
        
        try:
            # Load WAV file
            sample_rate, data = wavfile.read(filepath)
        except Exception as e:
            raise WAVFormatError(f"Failed to read {filepath.name}: {str(e)}")
        
        # Convert to float for processing
        if data.dtype == np.int16:
            data_float = data.astype(np.float32) / 32768.0
        elif data.dtype == np.int32:
            data_float = data.astype(np.float32) / 2147483648.0
        elif data.dtype == np.uint8:
            data_float = (data.astype(np.float32) - 128) / 128.0
        elif data.dtype == np.float32 or data.dtype == np.float64:
            data_float = data.astype(np.float32)
        else:
            raise WAVFormatError(f"Unsupported bit depth: {data.dtype}")
        
        # Convert mono to stereo
        if len(data_float.shape) == 1:
            # Mono: duplicate to both channels
            data_float = np.column_stack([data_float, data_float])
        elif data_float.shape[1] > 2:
            # More than 2 channels: take first two
            data_float = data_float[:, :2]
        
        # Resample to 48 kHz if needed
        if sample_rate != self.sample_rate:
            # Calculate new length
            num_samples = int(len(data_float) * self.sample_rate / sample_rate)
            
            # Resample each channel
            left_resampled = signal.resample(data_float[:, 0], num_samples)
            right_resampled = signal.resample(data_float[:, 1], num_samples)
            
            data_float = np.column_stack([left_resampled, right_resampled])
        
        # Convert to 16-bit integer
        data_int16 = (data_float * 32767).astype(np.int16)
        
        # Interleave stereo: LRLRLR...
        stereo = data_int16.flatten()
        
        return stereo

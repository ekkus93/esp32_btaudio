"""
I2S Driver for BBGW I2S Source using ALSA.

This module provides the I2SDriverALSA class for transmitting audio samples
via I2S interface using ALSA (Advanced Linux Sound Architecture). Designed
for BeagleBone Green Wireless hardware with McASP I2S output.

Classes:
    I2SDriverALSA: I2S driver using ALSA PCM interface.

Example:
    >>> from audio.ring_buffer import RingBuffer
    >>> from config.manager import ConfigManager
    >>> 
    >>> config = ConfigManager('config.yaml')
    >>> ring_buffer = RingBuffer(8192)
    >>> driver = I2SDriverALSA(config, ring_buffer)
    >>> driver.start()
    >>> # ... audio is transmitted via McASP I2S
    >>> driver.stop()

Note:
    This driver requires ALSA hardware support (BeagleBone Green Wireless with McASP configured).
    ALSA device is hw:CARD=BBGW-I2S,DEV=0 (or hw:0,0 depending on Device Tree configuration).
    For development/testing on non-BBGW systems, mock the alsaaudio module.
"""

import threading
import time
import logging
from typing import Dict, Any, Optional

try:
    import alsaaudio
    ALSAAUDIO_AVAILABLE = True
except ImportError:
    ALSAAUDIO_AVAILABLE = False
    # Create mock for development
    class alsaaudio:
        PCM_PLAYBACK = 0
        PCM_NONBLOCK = 1
        PCM_FORMAT_S16_LE = 2
        
        class PCM:
            def __init__(self, *args, **kwargs):
                raise ImportError("alsaaudio not available - requires BeagleBone Green Wireless with ALSA")
            
            def setchannels(self, channels):
                pass
            
            def setrate(self, rate):
                pass
            
            def setformat(self, fmt):
                pass
            
            def setperiodsize(self, size):
                pass
            
            def write(self, data):
                return 0


logger = logging.getLogger(__name__)


class I2SDriverALSA:
    """
    I2S driver using ALSA PCM interface.
    
    Continuously reads audio samples from ring buffer and transmits them via
    I2S using ALSA. Runs DMA loop in background thread with underrun handling.
    
    Attributes:
        config: ConfigManager instance for I2S parameters
        ring_buffer: RingBuffer containing audio samples to transmit
        sample_rate: Sample rate in Hz (typically 48000)
        period_size: ALSA period size in frames (typically 1024)
        
        device: ALSA PCM device handle
        running: Flag indicating DMA thread is active
        thread: Background DMA thread
        
        frames_sent: Total frames transmitted
        underruns: Count of underrun events
        last_buffer_fill: Last observed ring buffer fill percentage
    
    Thread Safety:
        start() and stop() are thread-safe. Statistics can be read from any thread.
    """
    
    def __init__(self, config, ring_buffer):
        """
        Initialize I2S driver with ALSA.
        
        Args:
            config: ConfigManager instance with I2S configuration
            ring_buffer: RingBuffer instance containing audio samples
        
        Raises:
            ImportError: If alsaaudio module not available (not on BeagleBone)
            alsaaudio.ALSAAudioError: If ALSA device cannot be opened
        
        Example:
            >>> config = ConfigManager('config.yaml')
            >>> ring_buffer = RingBuffer(8192)
            >>> driver = I2SDriverALSA(config, ring_buffer)
        """
        if not ALSAAUDIO_AVAILABLE:
            raise ImportError(
                "alsaaudio not available. This driver requires BeagleBone Green Wireless with ALSA support. "
                "Install with: sudo apt install python3-alsaaudio"
            )
        
        self.config = config
        self.ring_buffer = ring_buffer
        
        # Get parameters from config
        self.sample_rate = config.get('i2s.sample_rate')
        self.period_size = 1024  # ALSA period size (frames, not samples)
        
        # ALSA device
        self.device = None
        
        # Thread control
        self.running = False
        self.thread = None
        self.lock = threading.Lock()
        
        # Statistics
        self.frames_sent = 0
        self.underruns = 0
        self.last_buffer_fill = 0.0
        
        # Initialize ALSA device (deferred to start() to allow easier testing)
        self._device_initialized = False
    
    def _init_alsa_device(self) -> None:
        """
        Initialize ALSA PCM device.
        
        Configures ALSA for stereo 16-bit playback at 48 kHz with specified
        period size. Uses BeagleBone Green Wireless McASP I2S device.
        
        Raises:
            alsaaudio.ALSAAudioError: If device cannot be opened or configured
        """
        try:
            # Get ALSA device name from config, or use default
            # BBGW McASP I2S: hw:CARD=BBGW-I2S,DEV=0 (from Device Tree overlay)
            # Fallback: hw:0,0 (if overlay creates default card)
            alsa_device = self.config.get('i2s.device', 'hw:CARD=BBGW-I2S,DEV=0')
            
            # Try primary device first
            try:
                self.device = alsaaudio.PCM(
                    type=alsaaudio.PCM_PLAYBACK,
                    mode=alsaaudio.PCM_NONBLOCK,
                    device=alsa_device
                )
                logger.info(f"Opened ALSA device: {alsa_device}")
            except alsaaudio.ALSAAudioError as e:
                # Try fallback device (hw:0,0)
                if alsa_device != 'hw:0,0':
                    logger.warning(f"Failed to open {alsa_device}, trying fallback hw:0,0: {e}")
                    self.device = alsaaudio.PCM(
                        type=alsaaudio.PCM_PLAYBACK,
                        mode=alsaaudio.PCM_NONBLOCK,
                        device='hw:0,0'
                    )
                    logger.info("Opened fallback ALSA device: hw:0,0")
                else:
                    raise
            
            # Configure PCM parameters
            self.device.setchannels(2)  # Stereo
            self.device.setrate(self.sample_rate)  # 48000 Hz
            self.device.setformat(alsaaudio.PCM_FORMAT_S16_LE)  # 16-bit little-endian
            self.device.setperiodsize(self.period_size)  # Period size in frames
            
            self._device_initialized = True
            logger.info(f"ALSA I2S device initialized: {self.sample_rate} Hz, {self.period_size} frames/period")
            
        except Exception as e:
            error_msg = (
                f"Failed to initialize ALSA device: {e}\n"
                "\nBeagleBone Green Wireless Troubleshooting:\n"
                "1. Verify McASP Device Tree overlay is loaded:\n"
                "   dmesg | grep -i mcasp\n"
                "2. Check ALSA device exists:\n"
                "   aplay -l\n"
                "   (should show 'BBGW-I2S' or 'davinci-mcasp')\n"
                "3. Verify overlay in /boot/uEnv.txt:\n"
                "   cape_enable=bone_capemgr.enable_partno=BB-I2S0\n"
                "4. See docs/TROUBLESHOOTING_BBGW.md for detailed solutions"
            )
            logger.error(error_msg)
            raise RuntimeError(error_msg) from e
    
    def start(self) -> None:
        """
        Start I2S DMA thread.
        
        Initializes ALSA device (if not already done) and launches background
        thread that continuously transmits audio from ring buffer to I2S.
        
        Example:
            >>> driver.start()
            >>> # Audio transmission now active
        """
        with self.lock:
            if self.running:
                return
            
            # Initialize ALSA device if needed
            if not self._device_initialized:
                self._init_alsa_device()
            
            self.running = True
            self.thread = threading.Thread(target=self._dma_loop, daemon=True)
            self.thread.start()
            logger.info("I2S DMA thread started")
    
    def stop(self) -> None:
        """
        Stop I2S DMA thread (graceful shutdown).
        
        Signals the DMA thread to stop and waits for it to finish. Closes
        ALSA device. Safe to call multiple times.
        
        Example:
            >>> driver.stop()
            >>> # Audio transmission stopped, ALSA device closed
        """
        with self.lock:
            if not self.running:
                return
            
            self.running = False
        
        # Wait for thread to finish
        if self.thread:
            self.thread.join(timeout=1.0)
            self.thread = None
        
        # Close ALSA device
        if self.device:
            try:
                self.device.close()
            except Exception as e:
                logger.warning(f"Error closing ALSA device: {e}")
            finally:
                self.device = None
                self._device_initialized = False
        
        logger.info("I2S DMA thread stopped")
    
    def get_stats(self) -> Dict[str, Any]:
        """
        Get I2S driver statistics.
        
        Returns:
            Dictionary with statistics:
                - frames_sent: Total frames transmitted
                - underruns: Number of underrun events
                - buffer_fill_pct: Last observed ring buffer fill percentage
                - active: Whether DMA thread is running
        
        Example:
            >>> stats = driver.get_stats()
            >>> print(f"Frames sent: {stats['frames_sent']}")
            >>> print(f"Underruns: {stats['underruns']}")
        """
        return {
            'frames_sent': self.frames_sent,
            'underruns': self.underruns,
            'buffer_fill_pct': self.last_buffer_fill,
            'active': self.running
        }
    
    def _dma_loop(self) -> None:
        """
        Main DMA loop (background thread).
        
        Continuously reads audio samples from ring buffer and writes to ALSA
        device. Handles underruns by zero-filling and incrementing counter.
        
        Note:
            Runs in background thread. Do not call directly.
        """
        logger.info("DMA loop started")
        
        while self.running:
            try:
                # Update buffer fill stats
                self.last_buffer_fill = self.ring_buffer.get_fill_percentage()
                
                # Read samples from ring buffer (period_size frames = period_size * 2 samples)
                num_samples = self.period_size * 2  # Stereo: 2 samples per frame
                samples = self.ring_buffer.read(num_samples)
                
                # Handle underrun
                if samples is None:
                    # Ring buffer underrun - fill with zeros
                    import numpy as np
                    samples = np.zeros(num_samples, dtype=np.int16)
                    self.underruns += 1
                    logger.warning(f"Ring buffer underrun (total: {self.underruns})")
                
                # Convert to bytes
                data = samples.tobytes()
                
                # Write to ALSA device (blocking)
                # ALSA write returns number of frames written
                frames_written = self.device.write(data)
                
                if frames_written > 0:
                    self.frames_sent += frames_written
                else:
                    # ALSA buffer full, sleep briefly
                    time.sleep(0.001)
                
            except Exception as e:
                logger.error(f"Error in DMA loop: {e}", exc_info=True)
                # Continue running unless stop() was called
                if self.running:
                    time.sleep(0.01)  # Brief delay before retry
        
        logger.info("DMA loop stopped")

"""
Custom exception classes for Raspberry Pi I2S Audio Source.

This module defines all custom exceptions used throughout the application,
organized into hierarchies for different subsystems:
- I2S driver exceptions
- UART communication exceptions  
- Audio processing exceptions

All exceptions inherit from built-in Exception class and provide clear,
descriptive error messages for debugging and error handling.

Author: Raspberry Pi I2S Audio Source Project
License: MIT
"""


# ============================================================================
# I2S Driver Exceptions
# ============================================================================

class I2SError(Exception):
    """
    Base exception for I2S driver-related errors.
    
    All I2S-specific exceptions inherit from this class, allowing callers
    to catch I2S errors generically or handle specific error types.
    
    Example:
        >>> try:
        ...     i2s_driver.start()
        ... except I2SError as e:
        ...     logger.error(f"I2S error: {e}")
    """
    pass


class I2SUnderrunError(I2SError):
    """
    Raised when I2S driver experiences buffer underrun.
    
    Buffer underruns occur when the audio buffer is depleted faster than it
    can be filled, typically due to:
    - Audio engine not generating samples fast enough
    - CPU overload preventing timely buffer refills
    - Incorrect buffer sizing for the workload
    
    This is a recoverable error - the driver can continue operation but
    audio playback may have gaps or glitches.
    
    Example:
        >>> if buffer_empty and i2s_active:
        ...     raise I2SUnderrunError("Buffer underrun: filled 0 of 1024 frames")
    """
    pass


class I2SHardwareError(I2SError):
    """
    Raised when I2S hardware encounters a fatal error.
    
    Hardware errors indicate a problem with the I2S device itself that
    prevents normal operation:
    - ALSA device not found or unavailable
    - PCM stream open/setup failure
    - Device disconnected during operation
    - Hardware configuration mismatch
    
    This is typically a non-recoverable error requiring driver restart
    or system reconfiguration.
    
    Example:
        >>> raise I2SHardwareError("Failed to open ALSA device 'hw:0,0': No such device")
    """
    pass


# ============================================================================
# UART Communication Exceptions
# ============================================================================

class UARTError(Exception):
    """
    Base exception for UART communication errors.
    
    All UART-specific exceptions inherit from this class, allowing callers
    to catch UART errors generically or handle specific error types.
    
    Example:
        >>> try:
        ...     uart_mgr.send_command("SCAN")
        ... except UARTError as e:
        ...     logger.error(f"UART error: {e}")
    """
    pass


class UARTTimeoutError(UARTError):
    """
    Raised when UART operation times out.
    
    Timeout errors occur when:
    - Command sent but no response received within timeout period
    - Reading from serial port blocks longer than expected
    - ESP32 is unresponsive or processing is delayed
    
    This is usually a transient error that may succeed on retry.
    
    Example:
        >>> raise UARTTimeoutError("No response to SCAN command after 5.0s")
    """
    pass


class UARTDisconnectedError(UARTError):
    """
    Raised when UART device becomes disconnected.
    
    Disconnection errors indicate the serial device is no longer available:
    - USB-to-serial adapter unplugged
    - Serial port closed unexpectedly
    - Device driver error
    - ESP32 reset or power cycled
    
    This is a non-recoverable error requiring reconnection or restart.
    
    Example:
        >>> raise UARTDisconnectedError("Serial port /dev/ttyUSB0 closed unexpectedly")
    """
    pass


# ============================================================================
# Audio Processing Exceptions
# ============================================================================

class AudioError(Exception):
    """
    Base exception for audio-related errors.
    
    All audio processing exceptions inherit from this class, allowing callers
    to catch audio errors generically or handle specific error types.
    
    Example:
        >>> try:
        ...     audio_engine.play_wav("test.wav")
        ... except AudioError as e:
        ...     logger.error(f"Audio error: {e}")
    """
    pass


class WAVNotFoundError(AudioError):
    """
    Raised when a WAV file cannot be found.
    
    This exception is raised when attempting to load a WAV file that does not
    exist in the expected directory or the path is incorrect.
    
    Example:
        >>> raise WAVNotFoundError("WAV file not found: /home/pi/audio/test.wav")
    """
    pass


class WAVFormatError(AudioError):
    """
    Raised when a WAV file has an unsupported format.
    
    This exception is raised when a WAV file exists but cannot be loaded due to:
    - Non-PCM format (e.g., compressed, MP3 codec)
    - Unsupported bit depth (not 8, 16, 24, or 32-bit)
    - Corrupted file header or invalid RIFF structure
    - Unsupported sample rate that cannot be resampled
    - Unsupported channel count
    
    Example:
        >>> raise WAVFormatError("Unsupported WAV format: 24-bit float (expected 16-bit PCM)")
    """
    pass


# ============================================================================
# Exception Hierarchy Summary
# ============================================================================
#
# Exception (built-in)
#   │
#   ├── I2SError
#   │   ├── I2SUnderrunError
#   │   └── I2SHardwareError
#   │
#   ├── UARTError
#   │   ├── UARTTimeoutError
#   │   └── UARTDisconnectedError
#   │
#   └── AudioError
#       ├── WAVNotFoundError
#       └── WAVFormatError
#
# ============================================================================

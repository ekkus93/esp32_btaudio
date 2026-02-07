"""
Custom exceptions for audio module.

This module defines exceptions specific to audio generation and processing,
particularly for WAV file handling and audio engine errors.

Classes:
    AudioError: Base exception for audio-related errors.
    WAVNotFoundError: Raised when WAV file cannot be found.
    WAVFormatError: Raised when WAV file format is unsupported.
"""


class AudioError(Exception):
    """Base exception for audio-related errors."""
    pass


class WAVNotFoundError(AudioError):
    """
    Raised when a WAV file cannot be found.
    
    This exception is raised when attempting to load a WAV file that does not
    exist in the expected directory.
    
    Example:
        >>> raise WAVNotFoundError("test.wav not found in /home/pi/audio/")
    """
    pass


class WAVFormatError(AudioError):
    """
    Raised when a WAV file has an unsupported format.
    
    This exception is raised when a WAV file exists but cannot be loaded due to:
    - Non-PCM format
    - Unsupported bit depth (not 8, 16, 24, or 32-bit)
    - Corrupted file header
    - Unsupported sample rate that cannot be resampled
    
    Example:
        >>> raise WAVFormatError("test.wav is not PCM format (found MP3)")
    """
    pass

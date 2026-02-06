"""
Telemetry Tracker for RPi I2S Source.

This module provides the TelemetryTracker class for collecting and aggregating
statistics from all components (I2S, UART, Bluetooth, audio, system).

Classes:
    TelemetryTracker: Central statistics collector and aggregator.

Example:
    >>> tracker = TelemetryTracker()
    >>> tracker.update_i2s({'frames_sent': 1000, 'underruns': 0})
    >>> tracker.update_audio({'source': 'tone', 'frequency': 1000})
    >>> status = tracker.get_full_status()
    >>> print(status['i2s']['frames_sent'])
    1000
"""

import os
import time
from pathlib import Path
from typing import Dict, Any, Optional
import psutil


class TelemetryTracker:
    """
    Central telemetry tracker for collecting and aggregating statistics.
    
    Collects statistics from I2S driver, UART manager, Bluetooth, audio engine,
    and system resources (CPU temperature, memory usage). Provides unified status
    for web UI and logging.
    
    Attributes:
        i2s_stats: I2S driver statistics (frames_sent, underruns, buffer_fill, etc.)
        uart_stats: UART statistics (commands_sent, responses_ok, responses_err, etc.)
        bt_stats: Bluetooth statistics (connected, device_mac, playback_state, etc.)
        audio_stats: Audio engine statistics (source, frequency, amplitude, file, etc.)
        system_stats: System statistics (cpu_temp, memory_usage, uptime)
        start_time: Timestamp when tracker was initialized
    
    Thread Safety:
        Not thread-safe. Updates should be called from single thread or protected
        with external lock if needed.
    """
    
    def __init__(self):
        """
        Initialize telemetry tracker with empty statistics dictionaries.
        """
        # I2S statistics
        self.i2s_stats: Dict[str, Any] = {
            'active': False,
            'sample_rate': 48000,
            'buffer_fill_pct': 0.0,
            'frames_sent': 0,
            'underruns': 0,
            'last_update': None
        }
        
        # UART statistics
        self.uart_stats: Dict[str, Any] = {
            'connected': False,
            'commands_sent': 0,
            'responses_ok': 0,
            'responses_err': 0,
            'events_received': 0,
            'last_command': None,
            'last_response': None,
            'last_update': None
        }
        
        # Bluetooth statistics
        self.bt_stats: Dict[str, Any] = {
            'connected': False,
            'device_mac': None,
            'device_name': None,
            'playback_state': 'stopped',  # stopped, playing, paused
            'volume': 0,
            'last_update': None
        }
        
        # Audio statistics
        self.audio_stats: Dict[str, Any] = {
            'source': 'silence',  # silence, tone, sweep, wav
            'frequency': None,
            'amplitude': None,
            'stereo_mode': None,  # mono, left, right, dual
            'wav_file': None,
            'sweep_params': None,
            'last_update': None
        }
        
        # System statistics
        self.system_stats: Dict[str, Any] = {
            'cpu_temp': None,
            'memory_usage': None,
            'uptime': 0.0,
            'last_update': None
        }
        
        # Track start time for uptime calculation
        self.start_time = time.time()
    
    def update_i2s(self, stats: Dict[str, Any]) -> None:
        """
        Update I2S statistics.
        
        Args:
            stats: Dictionary with I2S statistics. Expected keys:
                - active (bool): Whether I2S is actively transmitting
                - sample_rate (int, optional): Sample rate in Hz
                - buffer_fill_pct (float, optional): Buffer fill percentage (0-100)
                - frames_sent (int, optional): Total frames transmitted
                - underruns (int, optional): Number of underrun events
        
        Example:
            >>> tracker.update_i2s({
            ...     'active': True,
            ...     'buffer_fill_pct': 65.5,
            ...     'frames_sent': 48000,
            ...     'underruns': 0
            ... })
        """
        self.i2s_stats.update(stats)
        self.i2s_stats['last_update'] = time.time()
    
    def update_uart(self, stats: Dict[str, Any]) -> None:
        """
        Update UART statistics.
        
        Args:
            stats: Dictionary with UART statistics. Expected keys:
                - connected (bool, optional): Whether UART is connected
                - commands_sent (int, optional): Total commands sent
                - responses_ok (int, optional): Successful responses count
                - responses_err (int, optional): Error responses count
                - events_received (int, optional): Event messages received
                - last_command (str, optional): Most recent command sent
                - last_response (str, optional): Most recent response received
        
        Example:
            >>> tracker.update_uart({
            ...     'connected': True,
            ...     'commands_sent': 5,
            ...     'responses_ok': 5,
            ...     'last_command': 'STATUS'
            ... })
        """
        self.uart_stats.update(stats)
        self.uart_stats['last_update'] = time.time()
    
    def update_bt(self, stats: Dict[str, Any]) -> None:
        """
        Update Bluetooth statistics.
        
        Args:
            stats: Dictionary with Bluetooth statistics. Expected keys:
                - connected (bool, optional): Whether Bluetooth device connected
                - device_mac (str, optional): MAC address of connected device
                - device_name (str, optional): Name of connected device
                - playback_state (str, optional): 'stopped', 'playing', or 'paused'
                - volume (int, optional): Volume level (0-100)
        
        Example:
            >>> tracker.update_bt({
            ...     'connected': True,
            ...     'device_mac': '00:11:22:33:44:55',
            ...     'playback_state': 'playing',
            ...     'volume': 75
            ... })
        """
        self.bt_stats.update(stats)
        self.bt_stats['last_update'] = time.time()
    
    def update_audio(self, state: Dict[str, Any]) -> None:
        """
        Update audio engine state.
        
        Args:
            state: Dictionary with audio state. Expected keys:
                - source (str, optional): 'silence', 'tone', 'sweep', 'wav'
                - frequency (float, optional): Tone frequency in Hz
                - amplitude (float, optional): Tone amplitude (0.0-1.0)
                - stereo_mode (str, optional): 'mono', 'left', 'right', 'dual'
                - wav_file (str, optional): WAV filename being played
                - sweep_params (dict, optional): Sweep parameters (start_freq, end_freq, duration)
        
        Example:
            >>> tracker.update_audio({
            ...     'source': 'tone',
            ...     'frequency': 1000,
            ...     'amplitude': 0.5,
            ...     'stereo_mode': 'mono'
            ... })
        """
        self.audio_stats.update(state)
        self.audio_stats['last_update'] = time.time()
    
    def get_full_status(self) -> Dict[str, Any]:
        """
        Get comprehensive status aggregating all statistics.
        
        This method collects all statistics from I2S, UART, Bluetooth, audio,
        and system components into a single JSON-serializable dictionary. System
        statistics (CPU temp, memory, uptime) are refreshed on each call.
        
        Returns:
            Dictionary containing all statistics with the following structure:
            {
                'i2s': {...},
                'uart': {...},
                'bluetooth': {...},
                'audio': {...},
                'system': {
                    'cpu_temp': float,  # Celsius
                    'memory_usage': int,  # bytes
                    'uptime': float,  # seconds
                    'last_update': float  # timestamp
                }
            }
        
        Example:
            >>> status = tracker.get_full_status()
            >>> print(f"I2S active: {status['i2s']['active']}")
            >>> print(f"CPU temp: {status['system']['cpu_temp']:.1f}°C")
        """
        # Refresh system stats
        self._refresh_system_stats()
        
        return {
            'i2s': dict(self.i2s_stats),
            'uart': dict(self.uart_stats),
            'bluetooth': dict(self.bt_stats),
            'audio': dict(self.audio_stats),
            'system': dict(self.system_stats)
        }
    
    def reset_stats(self) -> None:
        """
        Reset all statistics to initial values.
        
        Useful for testing or when restarting components. Does not reset start_time.
        
        Example:
            >>> tracker.reset_stats()
            >>> status = tracker.get_full_status()
            >>> assert status['i2s']['frames_sent'] == 0
        """
        self.i2s_stats['active'] = False
        self.i2s_stats['buffer_fill_pct'] = 0.0
        self.i2s_stats['frames_sent'] = 0
        self.i2s_stats['underruns'] = 0
        self.i2s_stats['last_update'] = None
        
        self.uart_stats['connected'] = False
        self.uart_stats['commands_sent'] = 0
        self.uart_stats['responses_ok'] = 0
        self.uart_stats['responses_err'] = 0
        self.uart_stats['events_received'] = 0
        self.uart_stats['last_command'] = None
        self.uart_stats['last_response'] = None
        self.uart_stats['last_update'] = None
        
        self.bt_stats['connected'] = False
        self.bt_stats['device_mac'] = None
        self.bt_stats['device_name'] = None
        self.bt_stats['playback_state'] = 'stopped'
        self.bt_stats['volume'] = 0
        self.bt_stats['last_update'] = None
        
        self.audio_stats['source'] = 'silence'
        self.audio_stats['frequency'] = None
        self.audio_stats['amplitude'] = None
        self.audio_stats['stereo_mode'] = None
        self.audio_stats['wav_file'] = None
        self.audio_stats['sweep_params'] = None
        self.audio_stats['last_update'] = None
        
        # System stats are refreshed on each get_full_status() call, so no reset needed
    
    def _refresh_system_stats(self) -> None:
        """
        Refresh system statistics (CPU temp, memory, uptime).
        
        Called internally by get_full_status() to update system metrics.
        """
        self.system_stats['cpu_temp'] = self._get_cpu_temp()
        self.system_stats['memory_usage'] = self._get_memory_usage()
        self.system_stats['uptime'] = time.time() - self.start_time
        self.system_stats['last_update'] = time.time()
    
    def _get_cpu_temp(self) -> Optional[float]:
        """
        Read CPU temperature from sysfs.
        
        Reads from /sys/class/thermal/thermal_zone0/temp (Raspberry Pi thermal zone).
        Temperature is in millidegrees Celsius, divided by 1000 to get degrees.
        
        Returns:
            CPU temperature in degrees Celsius, or None if unavailable.
        
        Example:
            >>> temp = tracker._get_cpu_temp()
            >>> if temp is not None:
            ...     print(f"CPU: {temp:.1f}°C")
        """
        temp_path = Path('/sys/class/thermal/thermal_zone0/temp')
        
        try:
            if temp_path.exists():
                temp_millidegrees = int(temp_path.read_text().strip())
                return temp_millidegrees / 1000.0
            else:
                return None
        except (OSError, ValueError):
            return None
    
    def _get_memory_usage(self) -> Optional[int]:
        """
        Get current process memory usage in bytes.
        
        Uses psutil to read RSS (Resident Set Size) of current process.
        
        Returns:
            Memory usage in bytes, or None if unavailable.
        
        Example:
            >>> mem = tracker._get_memory_usage()
            >>> if mem is not None:
            ...     print(f"Memory: {mem / 1024 / 1024:.1f} MB")
        """
        try:
            process = psutil.Process()
            return process.memory_info().rss
        except Exception:
            # Catch all exceptions (psutil.Error, AttributeError, etc.)
            return None

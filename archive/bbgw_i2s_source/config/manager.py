"""
Configuration Manager for BeagleBone Green Wireless I2S Source

Loads, validates, and manages YAML configuration with dot-notation access.
Merges user config with defaults and validates hardware constraints.

Author: bbgw_i2s_source
Date: 2026-02-07
"""

import os
import yaml
from typing import Any, Optional
from pathlib import Path


# Default configuration (all sections with sensible defaults for BBGW)
DEFAULT_CONFIG = {
    'i2s': {
        'device': 'hw:CARD=BBGW-I2S,DEV=0',  # ALSA device (from Device Tree overlay)
        'sample_rate': 48000,                 # 48 kHz sample rate (McASP configured)
        'channels': 2,                        # Stereo
        'format': 'S16_LE',                   # 16-bit little-endian PCM
        'period_size': 1024,                  # ALSA period size in frames
        'buffer_size': 4096,                  # ALSA buffer size in frames
    },
    'uart': {
        'device': '/dev/ttyO4',      # UART4 device path (P9.11/P9.13)
        'baudrate': 115200,          # Baud rate
        'timeout': 5.0,              # Read timeout (seconds)
    },
    'audio': {
        'default_source': 'tone',              # Default: tone generator
        'tone_freq': 1000,                     # Default tone frequency (Hz)
        'tone_amp': 0.5,                       # Default amplitude (0.0-1.0)
        'wav_directory': '/home/debian/audio', # WAV file directory (BBGW user)
    },
    'web': {
        'port': 5000,                   # Flask server port
        'bind_address': '0.0.0.0',      # Bind to all interfaces (Wi-Fi accessible)
        'log_level': 'INFO',            # Logging level
    },
    'bluetooth': {
        'last_device_mac': '',          # Last connected device MAC (auto-updated)
    },
}


class ConfigManager:
    """
    Configuration manager with YAML persistence and dot-notation access.
    
    Loads configuration from YAML file, merges with defaults, validates
    hardware constraints (GPIO pins, sample rate, etc.), and provides
    convenient dot-notation key access.
    
    Example:
        >>> cfg = ConfigManager('config.yaml')
        >>> bclk_pin = cfg.get('i2s.gpio_bclk')  # Returns 18
        >>> cfg.set('audio.tone_freq', 440)
        >>> cfg.save()
    """
    
    def __init__(self, config_path: str):
        """
        Initialize configuration manager.
        
        Args:
            config_path: Path to YAML configuration file. If file doesn't exist,
                        creates it with default values.
                        
        Raises:
            ValueError: If config file is invalid or validation fails
        """
        self._config_path = Path(config_path)
        self._config = self._load_or_create()
        
    def _load_or_create(self) -> dict:
        """
        Load existing config or create default.
        
        Returns:
            Configuration dictionary
            
        Raises:
            ValueError: If YAML is malformed or validation fails
        """
        if self._config_path.exists():
            # Load existing config
            try:
                with open(self._config_path, 'r') as f:
                    user_config = yaml.safe_load(f) or {}
            except yaml.YAMLError as e:
                raise ValueError(f"Invalid YAML in {self._config_path}: {e}")
                
            # Merge with defaults (fill missing keys)
            config = self._merge_with_defaults(user_config)
        else:
            # Create default config
            config = DEFAULT_CONFIG.copy()
            # Deep copy nested dicts
            config = {
                section: values.copy() if isinstance(values, dict) else values
                for section, values in config.items()
            }
            
            # Create directory if needed
            self._config_path.parent.mkdir(parents=True, exist_ok=True)
            
            # Save default config to file
            with open(self._config_path, 'w') as f:
                yaml.safe_dump(config, f, default_flow_style=False, sort_keys=False)
                
        # Validate configuration
        self._validate(config)
        
        return config
        
    def _merge_with_defaults(self, user_config: dict) -> dict:
        """
        Merge user config with defaults, filling missing keys.
        
        Args:
            user_config: User's configuration dictionary
            
        Returns:
            Merged configuration with all required keys
        """
        merged = DEFAULT_CONFIG.copy()
        
        # Deep merge: update each section
        for section, default_values in DEFAULT_CONFIG.items():
            if section in user_config:
                if isinstance(default_values, dict):
                    # Merge section keys
                    merged[section] = default_values.copy()
                    merged[section].update(user_config[section])
                else:
                    # Override scalar value
                    merged[section] = user_config[section]
            # else: keep default value
            
        return merged
        
    def _validate(self, config: dict) -> None:
        """
        Validate configuration values.
        
        Args:
            config: Configuration dictionary to validate
            
        Raises:
            ValueError: If any configuration value is invalid
        """
        # Validate I2S ALSA device (BBGW McASP)
        i2s = config.get('i2s', {})
        device = i2s.get('device')
        if device is not None:
            if not isinstance(device, str):
                raise ValueError(f"i2s.device must be string, got {type(device)}")
            # Common ALSA device patterns
            if not (device.startswith('hw:') or device.startswith('plughw:')):
                raise ValueError(f"i2s.device must start with 'hw:' or 'plughw:', got {device}")
                    
        # Validate sample rate (common audio rates)
        sample_rate = i2s.get('sample_rate')
        if sample_rate is not None:
            valid_rates = [8000, 16000, 22050, 32000, 44100, 48000, 96000]
            if sample_rate not in valid_rates:
                raise ValueError(f"i2s.sample_rate must be one of {valid_rates}, got {sample_rate}")
        
        # Validate channels
        channels = i2s.get('channels')
        if channels is not None:
            if not isinstance(channels, int) or channels < 1 or channels > 8:
                raise ValueError(f"i2s.channels must be 1-8, got {channels}")
        
        # Validate format (ALSA format string)
        fmt = i2s.get('format')
        if fmt is not None:
            valid_formats = ['S8', 'U8', 'S16_LE', 'S16_BE', 'S24_LE', 'S24_BE', 'S32_LE', 'S32_BE']
            if fmt not in valid_formats:
                raise ValueError(f"i2s.format must be one of {valid_formats}, got {fmt}")
        
        # Validate period_size
        period_size = i2s.get('period_size')
        if period_size is not None:
            if not isinstance(period_size, int) or period_size <= 0:
                raise ValueError(f"i2s.period_size must be positive integer, got {period_size}")
            if period_size < 64 or period_size > 8192:
                raise ValueError(f"i2s.period_size should be 64-8192, got {period_size}")
                
        # Validate buffer size (ALSA buffer size in frames)
        buffer_size = i2s.get('buffer_size')
        if buffer_size is not None:
            if not isinstance(buffer_size, int) or buffer_size <= 0:
                raise ValueError(f"i2s.buffer_size must be positive integer, got {buffer_size}")
            if buffer_size < 256 or buffer_size > 65536:
                raise ValueError(f"i2s.buffer_size should be 256-65536, got {buffer_size}")
                
        # Validate UART baudrate
        uart = config.get('uart', {})
        baudrate = uart.get('baudrate')
        if baudrate is not None:
            valid_bauds = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]
            if baudrate not in valid_bauds:
                raise ValueError(f"uart.baudrate must be one of {valid_bauds}, got {baudrate}")
                
        # Validate UART timeout
        timeout = uart.get('timeout')
        if timeout is not None:
            if not isinstance(timeout, (int, float)) or timeout <= 0:
                raise ValueError(f"uart.timeout must be positive number, got {timeout}")
                
        # Validate audio tone frequency
        audio = config.get('audio', {})
        tone_freq = audio.get('tone_freq')
        if tone_freq is not None:
            if not isinstance(tone_freq, (int, float)):
                raise ValueError(f"audio.tone_freq must be number, got {type(tone_freq)}")
            if tone_freq < 20 or tone_freq > 20000:
                raise ValueError(f"audio.tone_freq must be 20-20000 Hz, got {tone_freq}")
                
        # Validate audio amplitude
        tone_amp = audio.get('tone_amp')
        if tone_amp is not None:
            if not isinstance(tone_amp, (int, float)):
                raise ValueError(f"audio.tone_amp must be number, got {type(tone_amp)}")
            if tone_amp < 0.0 or tone_amp > 1.0:
                raise ValueError(f"audio.tone_amp must be 0.0-1.0, got {tone_amp}")
                
        # Validate web port
        web = config.get('web', {})
        port = web.get('port')
        if port is not None:
            if not isinstance(port, int) or port < 1 or port > 65535:
                raise ValueError(f"web.port must be 1-65535, got {port}")
                
        # Validate log level
        log_level = web.get('log_level')
        if log_level is not None:
            valid_levels = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']
            if log_level not in valid_levels:
                raise ValueError(f"web.log_level must be one of {valid_levels}, got {log_level}")
                
    def get(self, key_path: str, default: Any = None) -> Any:
        """
        Get configuration value by dot-separated path.
        
        Args:
            key_path: Dot-separated path (e.g., "i2s.gpio_bclk")
            default: Default value if key not found
            
        Returns:
            Configuration value, or default if not found
            
        Example:
            >>> cfg.get('i2s.sample_rate')  # Returns 48000
            >>> cfg.get('nonexistent.key', 42)  # Returns 42
        """
        keys = key_path.split('.')
        value = self._config
        
        for key in keys:
            if isinstance(value, dict) and key in value:
                value = value[key]
            else:
                return default
                
        return value
        
    def set(self, key_path: str, value: Any) -> None:
        """
        Set configuration value by dot-separated path.
        
        Creates intermediate dictionaries if needed. Does NOT auto-save.
        
        Args:
            key_path: Dot-separated path (e.g., "i2s.gpio_bclk")
            value: Value to set
            
        Raises:
            ValueError: If setting would make config invalid
            
        Example:
            >>> cfg.set('audio.tone_freq', 440)
            >>> cfg.save()  # Persist to disk
        """
        keys = key_path.split('.')
        
        # Navigate to parent dict, creating intermediate dicts as needed
        current = self._config
        for key in keys[:-1]:
            if key not in current:
                current[key] = {}
            elif not isinstance(current[key], dict):
                raise ValueError(f"Cannot set {key_path}: {key} is not a dict")
            current = current[key]
            
        # Set value
        current[keys[-1]] = value
        
        # Validate entire config
        self._validate(self._config)
        
    def save(self) -> None:
        """
        Save configuration to YAML file.
        
        Overwrites existing file with current in-memory config.
        
        Raises:
            IOError: If file cannot be written
        """
        # Ensure directory exists
        self._config_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Write YAML
        with open(self._config_path, 'w') as f:
            yaml.safe_dump(self._config, f, default_flow_style=False, sort_keys=False)
            
    def reload(self) -> None:
        """
        Reload configuration from file.
        
        Discards in-memory changes and reloads from disk.
        
        Raises:
            ValueError: If reloaded config is invalid
        """
        self._config = self._load_or_create()
        
    def get_all(self) -> dict:
        """
        Get entire configuration dictionary (deep copy).
        
        Returns:
            Deep copy of configuration dict
        """
        import copy
        return copy.deepcopy(self._config)
        
    @property
    def config_path(self) -> Path:
        """Path to configuration file."""
        return self._config_path

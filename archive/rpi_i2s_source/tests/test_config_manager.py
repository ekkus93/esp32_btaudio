"""
Unit tests for ConfigManager class

Tests YAML loading, validation, dot-notation access, and persistence.

Author: rpi_i2s_source
Date: 2026-02-06
"""

import pytest
import yaml
import tempfile
import os
from pathlib import Path
from config.manager import ConfigManager, DEFAULT_CONFIG


class TestConfigManagerInit:
    """Test initialization and file creation."""
    
    def test_create_default_config(self, tmp_path):
        """Test creating default config when file doesn't exist."""
        config_file = tmp_path / "config.yaml"
        
        cfg = ConfigManager(str(config_file))
        
        # Verify file was created
        assert config_file.exists()
        
        # Verify all default sections present
        assert cfg.get('i2s.gpio_bclk') == 18
        assert cfg.get('i2s.sample_rate') == 48000
        assert cfg.get('uart.baudrate') == 115200
        assert cfg.get('audio.tone_freq') == 1000
        assert cfg.get('web.port') == 5000
        
    def test_load_existing_config(self, tmp_path):
        """Test loading existing config file."""
        config_file = tmp_path / "config.yaml"
        
        # Create config file with custom values
        config_data = {
            'i2s': {'gpio_bclk': 20, 'gpio_ws': 21, 'gpio_dout': 22, 
                   'sample_rate': 48000, 'buffer_size': 4096},
            'uart': {'device': '/dev/ttyUSB0', 'baudrate': 115200, 'timeout': 3.0},
            'audio': {'default_source': 'wav', 'tone_freq': 440, 'tone_amp': 0.8,
                     'wav_directory': '/tmp/audio'},
            'web': {'port': 8080, 'bind_address': '127.0.0.1', 'log_level': 'DEBUG'},
            'bluetooth': {'last_device_mac': 'AA:BB:CC:DD:EE:FF'},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(config_data, f)
            
        cfg = ConfigManager(str(config_file))
        
        # Verify custom values loaded
        assert cfg.get('i2s.gpio_bclk') == 20
        assert cfg.get('uart.device') == '/dev/ttyUSB0'
        assert cfg.get('audio.tone_freq') == 440
        assert cfg.get('web.port') == 8080
        
    def test_merge_with_defaults(self, tmp_path):
        """Test partial config merges with defaults."""
        config_file = tmp_path / "config.yaml"
        
        # Create partial config (missing some keys)
        partial_config = {
            'i2s': {'gpio_bclk': 25},  # Only override one key
            'web': {'port': 3000},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(partial_config, f)
            
        cfg = ConfigManager(str(config_file))
        
        # Verify custom values
        assert cfg.get('i2s.gpio_bclk') == 25
        assert cfg.get('web.port') == 3000
        
        # Verify defaults filled in
        assert cfg.get('i2s.gpio_ws') == 19  # From defaults
        assert cfg.get('i2s.sample_rate') == 48000
        assert cfg.get('uart.baudrate') == 115200
        
    def test_invalid_yaml_raises_error(self, tmp_path):
        """Test malformed YAML raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        # Write invalid YAML
        with open(config_file, 'w') as f:
            f.write("invalid: yaml: content:\n  - broken\n  indentation")
            
        with pytest.raises(ValueError, match="Invalid YAML"):
            ConfigManager(str(config_file))


class TestConfigManagerValidation:
    """Test configuration validation."""
    
    def test_invalid_gpio_pin_raises_error(self, tmp_path):
        """Test invalid GPIO pin raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        # GPIO pin 50 is invalid (BCM only goes to 27)
        invalid_config = {
            'i2s': {'gpio_bclk': 50, 'gpio_ws': 19, 'gpio_dout': 21,
                   'sample_rate': 48000, 'buffer_size': 8192},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="i2s.gpio_bclk must be 0-27"):
            ConfigManager(str(config_file))
            
    def test_negative_gpio_pin_raises_error(self, tmp_path):
        """Test negative GPIO pin raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        invalid_config = {
            'i2s': {'gpio_bclk': -5, 'gpio_ws': 19, 'gpio_dout': 21,
                   'sample_rate': 48000, 'buffer_size': 8192},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="i2s.gpio_bclk must be 0-27"):
            ConfigManager(str(config_file))
            
    def test_duplicate_gpio_pins_raises_error(self, tmp_path):
        """Test duplicate GPIO pins raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        # BCLK and WS on same pin
        invalid_config = {
            'i2s': {'gpio_bclk': 18, 'gpio_ws': 18, 'gpio_dout': 21,
                   'sample_rate': 48000, 'buffer_size': 8192},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="GPIO pins must be unique"):
            ConfigManager(str(config_file))
            
    def test_invalid_sample_rate_raises_error(self, tmp_path):
        """Test invalid sample rate raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        invalid_config = {
            'i2s': {'gpio_bclk': 18, 'gpio_ws': 19, 'gpio_dout': 21,
                   'sample_rate': 12345, 'buffer_size': 8192},  # Not a standard rate
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="i2s.sample_rate must be one of"):
            ConfigManager(str(config_file))
            
    def test_invalid_buffer_size_raises_error(self, tmp_path):
        """Test invalid buffer size raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        # Buffer size too small
        invalid_config = {
            'i2s': {'gpio_bclk': 18, 'gpio_ws': 19, 'gpio_dout': 21,
                   'sample_rate': 48000, 'buffer_size': 100},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="i2s.buffer_size should be 1024-65536"):
            ConfigManager(str(config_file))
            
    def test_invalid_baudrate_raises_error(self, tmp_path):
        """Test invalid UART baudrate raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        invalid_config = {
            'uart': {'device': '/dev/serial0', 'baudrate': 12345, 'timeout': 5.0},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="uart.baudrate must be one of"):
            ConfigManager(str(config_file))
            
    def test_invalid_tone_freq_raises_error(self, tmp_path):
        """Test invalid tone frequency raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        # Frequency too high (ultrasonic)
        invalid_config = {
            'audio': {'default_source': 'tone', 'tone_freq': 30000,
                     'tone_amp': 0.5, 'wav_directory': '/home/pi/audio'},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="audio.tone_freq must be 20-20000"):
            ConfigManager(str(config_file))
            
    def test_invalid_amplitude_raises_error(self, tmp_path):
        """Test invalid amplitude raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        # Amplitude > 1.0
        invalid_config = {
            'audio': {'default_source': 'tone', 'tone_freq': 1000,
                     'tone_amp': 1.5, 'wav_directory': '/home/pi/audio'},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="audio.tone_amp must be 0.0-1.0"):
            ConfigManager(str(config_file))
            
    def test_invalid_port_raises_error(self, tmp_path):
        """Test invalid web port raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        invalid_config = {
            'web': {'port': 99999, 'bind_address': '0.0.0.0', 'log_level': 'INFO'},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="web.port must be 1-65535"):
            ConfigManager(str(config_file))
            
    def test_invalid_log_level_raises_error(self, tmp_path):
        """Test invalid log level raises ValueError."""
        config_file = tmp_path / "config.yaml"
        
        invalid_config = {
            'web': {'port': 5000, 'bind_address': '0.0.0.0', 'log_level': 'VERBOSE'},
        }
        with open(config_file, 'w') as f:
            yaml.safe_dump(invalid_config, f)
            
        with pytest.raises(ValueError, match="web.log_level must be one of"):
            ConfigManager(str(config_file))


class TestConfigManagerGetSet:
    """Test get/set with dot notation."""
    
    def test_get_nested_value(self, tmp_path):
        """Test get with dot notation."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        assert cfg.get('i2s.gpio_bclk') == 18
        assert cfg.get('uart.baudrate') == 115200
        assert cfg.get('audio.tone_freq') == 1000
        
    def test_get_nonexistent_key_returns_default(self, tmp_path):
        """Test get with nonexistent key returns default."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        assert cfg.get('nonexistent.key') is None
        assert cfg.get('nonexistent.key', 42) == 42
        assert cfg.get('i2s.nonexistent', 'fallback') == 'fallback'
        
    def test_set_existing_value(self, tmp_path):
        """Test set updates existing value."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        cfg.set('audio.tone_freq', 440)
        assert cfg.get('audio.tone_freq') == 440
        
    def test_set_validates_value(self, tmp_path):
        """Test set validates new value."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        # Try to set invalid GPIO pin
        with pytest.raises(ValueError, match="i2s.gpio_bclk must be 0-27"):
            cfg.set('i2s.gpio_bclk', 100)
            
    def test_set_creates_intermediate_dicts(self, tmp_path):
        """Test set creates intermediate dictionaries."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        cfg.set('new_section.new_key', 'value')
        assert cfg.get('new_section.new_key') == 'value'
        
    def test_get_all_returns_copy(self, tmp_path):
        """Test get_all returns deep copy."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        config_copy = cfg.get_all()
        
        # Modify copy
        config_copy['i2s']['gpio_bclk'] = 99
        
        # Original unchanged
        assert cfg.get('i2s.gpio_bclk') == 18


class TestConfigManagerPersistence:
    """Test save/reload functionality."""
    
    def test_save_persists_changes(self, tmp_path):
        """Test save writes changes to file."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        cfg.set('audio.tone_freq', 440)
        cfg.set('web.port', 8080)
        cfg.save()
        
        # Reload from file
        cfg2 = ConfigManager(str(config_file))
        assert cfg2.get('audio.tone_freq') == 440
        assert cfg2.get('web.port') == 8080
        
    def test_reload_discards_unsaved_changes(self, tmp_path):
        """Test reload discards in-memory changes."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        original_freq = cfg.get('audio.tone_freq')
        
        # Make unsaved change
        cfg.set('audio.tone_freq', 440)
        assert cfg.get('audio.tone_freq') == 440
        
        # Reload (without saving)
        cfg.reload()
        assert cfg.get('audio.tone_freq') == original_freq
        
    def test_save_reload_roundtrip(self, tmp_path):
        """Test save/reload roundtrip preserves all data."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        # Make multiple changes
        cfg.set('i2s.gpio_bclk', 20)
        cfg.set('uart.baudrate', 230400)
        cfg.set('audio.tone_freq', 440)
        cfg.set('web.port', 3000)
        cfg.set('bluetooth.last_device_mac', 'AA:BB:CC:DD:EE:FF')
        cfg.save()
        
        # Reload
        cfg.reload()
        
        # Verify all changes persisted
        assert cfg.get('i2s.gpio_bclk') == 20
        assert cfg.get('uart.baudrate') == 230400
        assert cfg.get('audio.tone_freq') == 440
        assert cfg.get('web.port') == 3000
        assert cfg.get('bluetooth.last_device_mac') == 'AA:BB:CC:DD:EE:FF'


class TestConfigManagerProperties:
    """Test properties and utility methods."""
    
    def test_config_path_property(self, tmp_path):
        """Test config_path property."""
        config_file = tmp_path / "config.yaml"
        cfg = ConfigManager(str(config_file))
        
        assert cfg.config_path == Path(config_file)
        
    def test_creates_directory_if_needed(self, tmp_path):
        """Test creates parent directories if needed."""
        config_file = tmp_path / "nested" / "dir" / "config.yaml"
        
        cfg = ConfigManager(str(config_file))
        
        assert config_file.exists()
        assert config_file.parent.exists()

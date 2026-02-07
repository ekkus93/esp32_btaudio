"""
Unit tests for TelemetryTracker.

Tests cover:
- Initialization with default empty stats
- Updating statistics from different components (I2S, UART, BT, audio)
- Statistics aggregation via get_full_status()
- System metrics collection (CPU temp, memory)
- Statistics reset functionality
- Thread safety considerations
"""

import time
import pytest
from unittest.mock import Mock, patch, mock_open
from pathlib import Path
from telemetry.tracker import TelemetryTracker


class TestTelemetryTrackerInit:
    """Tests for TelemetryTracker initialization."""
    
    def test_init_creates_empty_stats(self):
        """TelemetryTracker should initialize with default empty statistics."""
        tracker = TelemetryTracker()
        
        # Verify I2S stats defaults
        assert tracker.i2s_stats['active'] is False
        assert tracker.i2s_stats['sample_rate'] == 48000
        assert tracker.i2s_stats['buffer_fill_pct'] == 0.0
        assert tracker.i2s_stats['frames_sent'] == 0
        assert tracker.i2s_stats['underruns'] == 0
        assert tracker.i2s_stats['last_update'] is None
        
        # Verify UART stats defaults
        assert tracker.uart_stats['connected'] is False
        assert tracker.uart_stats['commands_sent'] == 0
        assert tracker.uart_stats['responses_ok'] == 0
        assert tracker.uart_stats['responses_err'] == 0
        assert tracker.uart_stats['events_received'] == 0
        
        # Verify BT stats defaults
        assert tracker.bt_stats['connected'] is False
        assert tracker.bt_stats['device_mac'] is None
        assert tracker.bt_stats['playback_state'] == 'stopped'
        assert tracker.bt_stats['volume'] == 0
        
        # Verify audio stats defaults
        assert tracker.audio_stats['source'] == 'silence'
        assert tracker.audio_stats['frequency'] is None
        assert tracker.audio_stats['amplitude'] is None
    
    def test_init_sets_start_time(self):
        """TelemetryTracker should record start time for uptime calculation."""
        before = time.time()
        tracker = TelemetryTracker()
        after = time.time()
        
        assert before <= tracker.start_time <= after


class TestTelemetryTrackerUpdateI2S:
    """Tests for updating I2S statistics."""
    
    def test_update_i2s_basic(self):
        """update_i2s should update I2S statistics and timestamp."""
        tracker = TelemetryTracker()
        
        before = time.time()
        tracker.update_i2s({
            'active': True,
            'buffer_fill_pct': 65.5,
            'frames_sent': 48000,
            'underruns': 0
        })
        after = time.time()
        
        assert tracker.i2s_stats['active'] is True
        assert tracker.i2s_stats['buffer_fill_pct'] == 65.5
        assert tracker.i2s_stats['frames_sent'] == 48000
        assert tracker.i2s_stats['underruns'] == 0
        assert before <= tracker.i2s_stats['last_update'] <= after
    
    def test_update_i2s_partial(self):
        """update_i2s should allow partial updates without overwriting other fields."""
        tracker = TelemetryTracker()
        
        tracker.update_i2s({'active': True, 'frames_sent': 1000})
        tracker.update_i2s({'buffer_fill_pct': 50.0})
        
        # active and frames_sent should persist
        assert tracker.i2s_stats['active'] is True
        assert tracker.i2s_stats['frames_sent'] == 1000
        assert tracker.i2s_stats['buffer_fill_pct'] == 50.0
    
    def test_update_i2s_incremental(self):
        """update_i2s should support incremental counter updates."""
        tracker = TelemetryTracker()
        
        tracker.update_i2s({'frames_sent': 1000})
        tracker.update_i2s({'frames_sent': 2000})
        tracker.update_i2s({'frames_sent': 3000})
        
        assert tracker.i2s_stats['frames_sent'] == 3000


class TestTelemetryTrackerUpdateUART:
    """Tests for updating UART statistics."""
    
    def test_update_uart_basic(self):
        """update_uart should update UART statistics and timestamp."""
        tracker = TelemetryTracker()
        
        before = time.time()
        tracker.update_uart({
            'connected': True,
            'commands_sent': 5,
            'responses_ok': 4,
            'responses_err': 1,
            'last_command': 'STATUS'
        })
        after = time.time()
        
        assert tracker.uart_stats['connected'] is True
        assert tracker.uart_stats['commands_sent'] == 5
        assert tracker.uart_stats['responses_ok'] == 4
        assert tracker.uart_stats['responses_err'] == 1
        assert tracker.uart_stats['last_command'] == 'STATUS'
        assert before <= tracker.uart_stats['last_update'] <= after
    
    def test_update_uart_last_response(self):
        """update_uart should track last_command and last_response."""
        tracker = TelemetryTracker()
        
        tracker.update_uart({'last_command': 'SCAN', 'last_response': 'OK|SCAN|2'})
        
        assert tracker.uart_stats['last_command'] == 'SCAN'
        assert tracker.uart_stats['last_response'] == 'OK|SCAN|2'


class TestTelemetryTrackerUpdateBT:
    """Tests for updating Bluetooth statistics."""
    
    def test_update_bt_connected(self):
        """update_bt should update Bluetooth connection statistics."""
        tracker = TelemetryTracker()
        
        before = time.time()
        tracker.update_bt({
            'connected': True,
            'device_mac': '00:11:22:33:44:55',
            'device_name': 'Speaker',
            'playback_state': 'playing',
            'volume': 75
        })
        after = time.time()
        
        assert tracker.bt_stats['connected'] is True
        assert tracker.bt_stats['device_mac'] == '00:11:22:33:44:55'
        assert tracker.bt_stats['device_name'] == 'Speaker'
        assert tracker.bt_stats['playback_state'] == 'playing'
        assert tracker.bt_stats['volume'] == 75
        assert before <= tracker.bt_stats['last_update'] <= after
    
    def test_update_bt_disconnected(self):
        """update_bt should handle disconnection state."""
        tracker = TelemetryTracker()
        
        # Connect first
        tracker.update_bt({'connected': True, 'device_mac': '00:11:22:33:44:55'})
        
        # Disconnect
        tracker.update_bt({
            'connected': False,
            'device_mac': None,
            'playback_state': 'stopped'
        })
        
        assert tracker.bt_stats['connected'] is False
        assert tracker.bt_stats['device_mac'] is None
        assert tracker.bt_stats['playback_state'] == 'stopped'


class TestTelemetryTrackerUpdateAudio:
    """Tests for updating audio engine state."""
    
    def test_update_audio_tone(self):
        """update_audio should update tone generation state."""
        tracker = TelemetryTracker()
        
        before = time.time()
        tracker.update_audio({
            'source': 'tone',
            'frequency': 1000,
            'amplitude': 0.5,
            'stereo_mode': 'mono'
        })
        after = time.time()
        
        assert tracker.audio_stats['source'] == 'tone'
        assert tracker.audio_stats['frequency'] == 1000
        assert tracker.audio_stats['amplitude'] == 0.5
        assert tracker.audio_stats['stereo_mode'] == 'mono'
        assert before <= tracker.audio_stats['last_update'] <= after
    
    def test_update_audio_sweep(self):
        """update_audio should handle sweep parameters."""
        tracker = TelemetryTracker()
        
        tracker.update_audio({
            'source': 'sweep',
            'sweep_params': {
                'start_freq': 20,
                'end_freq': 20000,
                'duration': 10
            }
        })
        
        assert tracker.audio_stats['source'] == 'sweep'
        assert tracker.audio_stats['sweep_params']['start_freq'] == 20
        assert tracker.audio_stats['sweep_params']['end_freq'] == 20000
    
    def test_update_audio_wav(self):
        """update_audio should track WAV file playback."""
        tracker = TelemetryTracker()
        
        tracker.update_audio({
            'source': 'wav',
            'wav_file': 'test_tone.wav'
        })
        
        assert tracker.audio_stats['source'] == 'wav'
        assert tracker.audio_stats['wav_file'] == 'test_tone.wav'
    
    def test_update_audio_silence(self):
        """update_audio should handle silence state."""
        tracker = TelemetryTracker()
        
        # Play tone first
        tracker.update_audio({'source': 'tone', 'frequency': 1000})
        
        # Switch to silence
        tracker.update_audio({'source': 'silence'})
        
        assert tracker.audio_stats['source'] == 'silence'


class TestTelemetryTrackerFullStatus:
    """Tests for get_full_status aggregation."""
    
    @patch('telemetry.tracker.TelemetryTracker._get_cpu_temp')
    @patch('telemetry.tracker.TelemetryTracker._get_memory_usage')
    def test_get_full_status_structure(self, mock_memory, mock_cpu):
        """get_full_status should return all stats in correct structure."""
        mock_cpu.return_value = 45.5
        mock_memory.return_value = 100 * 1024 * 1024  # 100 MB
        
        tracker = TelemetryTracker()
        tracker.update_i2s({'active': True, 'frames_sent': 1000})
        tracker.update_uart({'connected': True, 'commands_sent': 5})
        tracker.update_bt({'connected': True, 'volume': 75})
        tracker.update_audio({'source': 'tone', 'frequency': 1000})
        
        status = tracker.get_full_status()
        
        # Verify structure
        assert 'i2s' in status
        assert 'uart' in status
        assert 'bluetooth' in status
        assert 'audio' in status
        assert 'system' in status
        
        # Verify content
        assert status['i2s']['active'] is True
        assert status['i2s']['frames_sent'] == 1000
        assert status['uart']['connected'] is True
        assert status['uart']['commands_sent'] == 5
        assert status['bluetooth']['connected'] is True
        assert status['bluetooth']['volume'] == 75
        assert status['audio']['source'] == 'tone'
        assert status['audio']['frequency'] == 1000
        assert status['system']['cpu_temp'] == 45.5
        assert status['system']['memory_usage'] == 100 * 1024 * 1024
    
    @patch('telemetry.tracker.TelemetryTracker._get_cpu_temp')
    @patch('telemetry.tracker.TelemetryTracker._get_memory_usage')
    def test_get_full_status_calculates_uptime(self, mock_memory, mock_cpu):
        """get_full_status should include uptime since initialization."""
        mock_cpu.return_value = 45.0
        mock_memory.return_value = 50 * 1024 * 1024
        
        tracker = TelemetryTracker()
        time.sleep(0.1)  # Sleep briefly to get measurable uptime
        
        status = tracker.get_full_status()
        
        assert status['system']['uptime'] >= 0.1
        assert status['system']['uptime'] < 1.0  # Should be small
    
    @patch('telemetry.tracker.TelemetryTracker._get_cpu_temp')
    @patch('telemetry.tracker.TelemetryTracker._get_memory_usage')
    def test_get_full_status_returns_copy(self, mock_memory, mock_cpu):
        """get_full_status should return deep copy to prevent external mutation."""
        mock_cpu.return_value = 45.0
        mock_memory.return_value = 50 * 1024 * 1024
        
        tracker = TelemetryTracker()
        tracker.update_i2s({'active': True})
        
        status1 = tracker.get_full_status()
        status1['i2s']['active'] = False  # Mutate returned dict
        
        status2 = tracker.get_full_status()
        
        # Internal state should not be affected
        assert status2['i2s']['active'] is True


class TestTelemetryTrackerSystemMetrics:
    """Tests for system metrics collection."""
    
    def test_get_cpu_temp_reads_sysfs(self, tmp_path):
        """_get_cpu_temp should read from thermal_zone0 sysfs file."""
        # Create mock sysfs file
        temp_file = tmp_path / "temp"
        temp_file.write_text("45678\n")  # 45.678°C in millidegrees
        
        tracker = TelemetryTracker()
        
        with patch('telemetry.tracker.Path') as mock_path:
            mock_path.return_value.exists.return_value = True
            mock_path.return_value.read_text.return_value = "45678\n"
            
            temp = tracker._get_cpu_temp()
        
        assert temp == 45.678
    
    def test_get_cpu_temp_handles_missing_file(self):
        """_get_cpu_temp should return None if thermal zone file missing."""
        tracker = TelemetryTracker()
        
        with patch('telemetry.tracker.Path') as mock_path:
            mock_path.return_value.exists.return_value = False
            
            temp = tracker._get_cpu_temp()
        
        assert temp is None
    
    def test_get_cpu_temp_handles_read_error(self):
        """_get_cpu_temp should return None on read error."""
        tracker = TelemetryTracker()
        
        with patch('telemetry.tracker.Path') as mock_path:
            mock_path.return_value.exists.return_value = True
            mock_path.return_value.read_text.side_effect = OSError("Permission denied")
            
            temp = tracker._get_cpu_temp()
        
        assert temp is None
    
    def test_get_cpu_temp_handles_invalid_format(self):
        """_get_cpu_temp should return None if file contains non-numeric data."""
        tracker = TelemetryTracker()
        
        with patch('telemetry.tracker.Path') as mock_path:
            mock_path.return_value.exists.return_value = True
            mock_path.return_value.read_text.return_value = "invalid\n"
            
            temp = tracker._get_cpu_temp()
        
        assert temp is None
    
    @patch('telemetry.tracker.psutil.Process')
    def test_get_memory_usage_reads_rss(self, mock_process_class):
        """_get_memory_usage should read RSS from psutil."""
        mock_process = Mock()
        mock_process.memory_info.return_value.rss = 100 * 1024 * 1024  # 100 MB
        mock_process_class.return_value = mock_process
        
        tracker = TelemetryTracker()
        memory = tracker._get_memory_usage()
        
        assert memory == 100 * 1024 * 1024
        mock_process_class.assert_called_once()
        mock_process.memory_info.assert_called_once()
    
    @patch('telemetry.tracker.psutil.Process')
    def test_get_memory_usage_handles_psutil_error(self, mock_process_class):
        """_get_memory_usage should return None on psutil error."""
        mock_process_class.side_effect = Exception("psutil error")
        
        tracker = TelemetryTracker()
        memory = tracker._get_memory_usage()
        
        assert memory is None


class TestTelemetryTrackerReset:
    """Tests for statistics reset functionality."""
    
    def test_reset_stats_clears_all_counters(self):
        """reset_stats should reset all statistics to initial values."""
        tracker = TelemetryTracker()
        
        # Populate with data
        tracker.update_i2s({'active': True, 'frames_sent': 1000, 'underruns': 5})
        tracker.update_uart({'connected': True, 'commands_sent': 10})
        tracker.update_bt({'connected': True, 'volume': 75})
        tracker.update_audio({'source': 'tone', 'frequency': 1000})
        
        # Reset
        tracker.reset_stats()
        
        # Verify all cleared
        assert tracker.i2s_stats['active'] is False
        assert tracker.i2s_stats['frames_sent'] == 0
        assert tracker.i2s_stats['underruns'] == 0
        assert tracker.uart_stats['connected'] is False
        assert tracker.uart_stats['commands_sent'] == 0
        assert tracker.bt_stats['connected'] is False
        assert tracker.bt_stats['volume'] == 0
        assert tracker.audio_stats['source'] == 'silence'
        assert tracker.audio_stats['frequency'] is None
    
    def test_reset_stats_preserves_start_time(self):
        """reset_stats should not reset start_time (for uptime calculation)."""
        tracker = TelemetryTracker()
        original_start = tracker.start_time
        
        time.sleep(0.1)
        tracker.reset_stats()
        
        assert tracker.start_time == original_start

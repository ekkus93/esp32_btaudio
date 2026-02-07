"""
Unit tests for Flask Web Server.

Tests all API endpoints with mocked dependencies.
"""

import pytest
import json
import time
from unittest.mock import Mock, MagicMock, patch

from web.app import WebServer
from audio.exceptions import WAVNotFoundError, WAVFormatError


@pytest.fixture
def mock_config():
    """Mock ConfigManager."""
    config = Mock()
    config.get.side_effect = lambda key: {
        'web.bind_address': '0.0.0.0',
        'web.port': 5000
    }.get(key)
    return config


@pytest.fixture
def mock_audio_engine():
    """Mock AudioEngine."""
    engine = Mock()
    engine.get_state.return_value = {
        'source': 'tone',
        'freq': 1000,
        'amp': 0.5,
        'mode': 'mono'
    }
    return engine


@pytest.fixture
def mock_uart_manager():
    """Mock UARTCommandManager."""
    uart = Mock()
    uart.send_command.return_value = {"status": "ok", "result": "SCAN|2"}
    uart.get_last_status.return_value = {"connected": True, "mac": "AA:BB:CC:DD:EE:FF"}
    return uart


@pytest.fixture
def mock_telemetry():
    """Mock TelemetryTracker."""
    telemetry = Mock()
    telemetry.get_full_status.return_value = {
        "i2s": {"active": True, "underruns": 0},
        "uart": {"connected": True},
        "bt": {"connected": False},
        "audio": {"source": "tone", "freq": 1000},
        "system": {"uptime": 3600, "cpu_temp": 45.0}
    }
    return telemetry


@pytest.fixture
def web_server(mock_config, mock_audio_engine, mock_uart_manager, mock_telemetry):
    """Create WebServer instance with mocked dependencies."""
    server = WebServer(mock_config, mock_audio_engine, mock_uart_manager, mock_telemetry)
    # Get Flask test client
    server.app.config['TESTING'] = True
    return server


@pytest.fixture
def client(web_server):
    """Flask test client."""
    return web_server.app.test_client()


# =============================================================================
# Test Initialization
# =============================================================================

class TestWebServerInit:
    """Test WebServer initialization."""
    
    def test_init_stores_dependencies(self, mock_config, mock_audio_engine, mock_uart_manager, mock_telemetry):
        """Should store all component references."""
        server = WebServer(mock_config, mock_audio_engine, mock_uart_manager, mock_telemetry)
        
        assert server.config is mock_config
        assert server.audio_engine is mock_audio_engine
        assert server.uart_manager is mock_uart_manager
        assert server.telemetry is mock_telemetry
    
    def test_init_loads_config(self, mock_config, mock_audio_engine, mock_uart_manager, mock_telemetry):
        """Should load host and port from config."""
        server = WebServer(mock_config, mock_audio_engine, mock_uart_manager, mock_telemetry)
        
        assert server.host == '0.0.0.0'
        assert server.port == 5000
        assert mock_config.get.call_count >= 2
    
    def test_init_creates_flask_app(self, web_server):
        """Should create Flask app instance."""
        assert web_server.app is not None
        assert web_server.app.name == 'web.app'
    
    def test_init_without_uart_manager(self, mock_config, mock_audio_engine, mock_telemetry):
        """Should handle None uart_manager."""
        server = WebServer(mock_config, mock_audio_engine, None, mock_telemetry)
        
        assert server.uart_manager is None
        assert server.app is not None


# =============================================================================
# Test Status Endpoints
# =============================================================================

class TestStatusEndpoints:
    """Test status and monitoring endpoints."""
    
    def test_get_status_success(self, client, mock_telemetry):
        """Should return full system status."""
        response = client.get('/api/status')
        
        assert response.status_code == 200
        data = json.loads(response.data)
        assert 'i2s' in data
        assert 'uart' in data
        assert 'bt' in data
        assert 'audio' in data
        assert 'system' in data
        mock_telemetry.get_full_status.assert_called_once()
    
    def test_get_status_error_handling(self, client, mock_telemetry):
        """Should handle telemetry errors gracefully."""
        mock_telemetry.get_full_status.side_effect = Exception("Telemetry error")
        
        response = client.get('/api/status')
        
        assert response.status_code == 500
        data = json.loads(response.data)
        assert 'error' in data
    
    def test_stream_status_sse_format(self, client, mock_telemetry):
        """Should return SSE-formatted stream."""
        response = client.get('/api/stream')
        
        assert response.status_code == 200
        assert response.mimetype == 'text/event-stream'
        
        # Read first chunk (generator produces infinite stream)
        # We'll just verify response headers
        mock_telemetry.get_full_status.assert_called()


# =============================================================================
# Test Audio Control Endpoints
# =============================================================================

class TestAudioControlEndpoints:
    """Test audio generation control endpoints."""
    
    def test_set_tone_valid_params(self, client, mock_audio_engine):
        """Should set tone parameters with valid input."""
        response = client.post('/api/tone',
            data=json.dumps({'freq': 1000, 'amp': 0.5, 'mode': 'mono'}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        data = json.loads(response.data)
        assert data['status'] == 'ok'
        mock_audio_engine.set_tone_params.assert_called_once_with(
            freq=1000, amp=0.5, mode='mono', dual_freq=None
        )
    
    def test_set_tone_partial_params(self, client, mock_audio_engine):
        """Should accept partial parameter updates."""
        response = client.post('/api/tone',
            data=json.dumps({'freq': 2000}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        mock_audio_engine.set_tone_params.assert_called_once_with(
            freq=2000, amp=None, mode=None, dual_freq=None
        )
    
    def test_set_tone_dual_mode(self, client, mock_audio_engine):
        """Should handle dual-tone mode with two frequencies."""
        response = client.post('/api/tone',
            data=json.dumps({'freq': 1000, 'amp': 0.5, 'mode': 'dual', 'dual_freq': 440}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        mock_audio_engine.set_tone_params.assert_called_once_with(
            freq=1000, amp=0.5, mode='dual', dual_freq=440
        )
    
    def test_set_tone_invalid_freq(self, client, mock_audio_engine):
        """Should reject frequency out of range."""
        response = client.post('/api/tone',
            data=json.dumps({'freq': 25000}),  # > 20000 Hz
            content_type='application/json'
        )
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
        assert 'freq must be 20-20000 Hz' in data['error']
        mock_audio_engine.set_tone_params.assert_not_called()
    
    def test_set_tone_invalid_amp(self, client, mock_audio_engine):
        """Should reject amplitude out of range."""
        response = client.post('/api/tone',
            data=json.dumps({'amp': 1.5}),  # > 1.0
            content_type='application/json'
        )
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
        assert 'amp must be 0.0-1.0' in data['error']
    
    def test_set_tone_invalid_mode(self, client, mock_audio_engine):
        """Should reject invalid stereo mode."""
        response = client.post('/api/tone',
            data=json.dumps({'mode': 'invalid'}),
            content_type='application/json'
        )
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
        assert 'mode must be one of' in data['error']
    
    def test_set_tone_no_json(self, client):
        """Should reject request without JSON body."""
        response = client.post('/api/tone')
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
    
    def test_set_tone_switches_source(self, client, mock_audio_engine):
        """Should switch to tone source if not already."""
        mock_audio_engine.get_state.return_value = {'source': 'sweep'}
        
        response = client.post('/api/tone',
            data=json.dumps({'freq': 1000}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        mock_audio_engine.set_source.assert_called_once_with('tone')
    
    def test_set_sweep_valid_params(self, client, mock_audio_engine):
        """Should set sweep parameters."""
        response = client.post('/api/sweep',
            data=json.dumps({'duration': 10, 'loop': False}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        data = json.loads(response.data)
        assert data['status'] == 'ok'
        mock_audio_engine.set_source.assert_called_once_with('sweep', {
            'duration': 10,
            'loop': False
        })
    
    def test_set_sweep_defaults(self, client, mock_audio_engine):
        """Should use default duration and loop if not specified."""
        response = client.post('/api/sweep',
            data=json.dumps({}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        mock_audio_engine.set_source.assert_called_once_with('sweep', {
            'duration': 10,  # Default
            'loop': False    # Default
        })
    
    def test_set_sweep_invalid_duration(self, client, mock_audio_engine):
        """Should reject duration out of range."""
        response = client.post('/api/sweep',
            data=json.dumps({'duration': 100}),  # > 60 seconds
            content_type='application/json'
        )
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
        assert 'duration must be 1-60 seconds' in data['error']
    
    def test_set_wav_valid_params(self, client, mock_audio_engine):
        """Should set WAV playback."""
        response = client.post('/api/wav',
            data=json.dumps({'file': 'test.wav', 'loop': False}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        data = json.loads(response.data)
        assert data['status'] == 'ok'
        mock_audio_engine.set_source.assert_called_once_with('wav', {
            'file': 'test.wav',
            'loop': False
        })
    
    def test_set_wav_missing_file(self, client):
        """Should reject request without file parameter."""
        response = client.post('/api/wav',
            data=json.dumps({'loop': False}),
            content_type='application/json'
        )
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
        assert 'file parameter required' in data['error']
    
    def test_set_wav_file_not_found(self, client, mock_audio_engine):
        """Should return 404 for missing WAV file."""
        mock_audio_engine.set_source.side_effect = WAVNotFoundError("File not found: test.wav")
        
        response = client.post('/api/wav',
            data=json.dumps({'file': 'test.wav'}),
            content_type='application/json'
        )
        
        assert response.status_code == 404
        data = json.loads(response.data)
        assert 'error' in data
    
    def test_set_wav_format_error(self, client, mock_audio_engine):
        """Should return 400 for invalid WAV format."""
        mock_audio_engine.set_source.side_effect = WAVFormatError("Unsupported format")
        
        response = client.post('/api/wav',
            data=json.dumps({'file': 'test.wav'}),
            content_type='application/json'
        )
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
    
    def test_set_silence(self, client, mock_audio_engine):
        """Should set silence mode."""
        response = client.post('/api/silence')
        
        assert response.status_code == 200
        data = json.loads(response.data)
        assert data['status'] == 'ok'
        mock_audio_engine.set_source.assert_called_once_with('silence')


# =============================================================================
# Test Bluetooth Control Endpoints
# =============================================================================

class TestBluetoothEndpoints:
    """Test Bluetooth control endpoints (UART-dependent)."""
    
    def test_bt_command_success(self, client, mock_uart_manager):
        """Should send Bluetooth command via UART."""
        response = client.post('/api/bt/command',
            data=json.dumps({'command': 'SCAN', 'args': ''}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        data = json.loads(response.data)
        assert data['status'] == 'ok'
        mock_uart_manager.send_command.assert_called_once_with('SCAN', '')
    
    def test_bt_command_with_args(self, client, mock_uart_manager):
        """Should send command with arguments."""
        response = client.post('/api/bt/command',
            data=json.dumps({'command': 'CONNECT', 'args': 'AA:BB:CC:DD:EE:FF'}),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        mock_uart_manager.send_command.assert_called_once_with('CONNECT', 'AA:BB:CC:DD:EE:FF')
    
    def test_bt_command_missing_command(self, client):
        """Should reject request without command parameter."""
        response = client.post('/api/bt/command',
            data=json.dumps({'args': ''}),
            content_type='application/json'
        )
        
        assert response.status_code == 400
        data = json.loads(response.data)
        assert 'error' in data
        assert 'command parameter required' in data['error']
    
    def test_bt_command_timeout(self, client, mock_uart_manager):
        """Should return 504 on command timeout."""
        mock_uart_manager.send_command.side_effect = TimeoutError("Command timeout")
        
        response = client.post('/api/bt/command',
            data=json.dumps({'command': 'SCAN'}),
            content_type='application/json'
        )
        
        assert response.status_code == 504
        data = json.loads(response.data)
        assert 'error' in data
    
    def test_bt_command_no_uart_manager(self, mock_config, mock_audio_engine, mock_telemetry):
        """Should return 503 when UART manager not available."""
        server = WebServer(mock_config, mock_audio_engine, None, mock_telemetry)
        client = server.app.test_client()
        
        response = client.post('/api/bt/command',
            data=json.dumps({'command': 'SCAN'}),
            content_type='application/json'
        )
        
        assert response.status_code == 503
        data = json.loads(response.data)
        assert 'error' in data
        assert 'UART manager not available' in data['error']
    
    def test_bt_status_success(self, client, mock_uart_manager):
        """Should return Bluetooth status."""
        response = client.get('/api/bt/status')
        
        assert response.status_code == 200
        data = json.loads(response.data)
        assert 'connected' in data
        assert 'mac' in data
        mock_uart_manager.get_last_status.assert_called_once()
    
    def test_bt_status_no_uart_manager(self, mock_config, mock_audio_engine, mock_telemetry):
        """Should return 503 when UART manager not available."""
        server = WebServer(mock_config, mock_audio_engine, None, mock_telemetry)
        client = server.app.test_client()
        
        response = client.get('/api/bt/status')
        
        assert response.status_code == 503
        data = json.loads(response.data)
        assert 'error' in data


# =============================================================================
# Test Error Handling
# =============================================================================

class TestErrorHandling:
    """Test error handling across all endpoints."""
    
    def test_tone_engine_exception(self, client, mock_audio_engine):
        """Should handle audio engine exceptions."""
        mock_audio_engine.set_tone_params.side_effect = Exception("Engine error")
        
        response = client.post('/api/tone',
            data=json.dumps({'freq': 1000}),
            content_type='application/json'
        )
        
        assert response.status_code == 500
        data = json.loads(response.data)
        assert 'error' in data
    
    def test_sweep_engine_exception(self, client, mock_audio_engine):
        """Should handle sweep exceptions."""
        mock_audio_engine.set_source.side_effect = Exception("Sweep error")
        
        response = client.post('/api/sweep',
            data=json.dumps({'duration': 10}),
            content_type='application/json'
        )
        
        assert response.status_code == 500
        data = json.loads(response.data)
        assert 'error' in data
    
    def test_bt_command_exception(self, client, mock_uart_manager):
        """Should handle UART exceptions."""
        mock_uart_manager.send_command.side_effect = Exception("UART error")
        
        response = client.post('/api/bt/command',
            data=json.dumps({'command': 'SCAN'}),
            content_type='application/json'
        )
        
        assert response.status_code == 500
        data = json.loads(response.data)
        assert 'error' in data


# =============================================================================
# Test Integration
# =============================================================================

class TestIntegration:
    """Integration tests with real Flask test client."""
    
    def test_multiple_tone_changes(self, client, mock_audio_engine):
        """Should handle rapid tone parameter changes."""
        for freq in [1000, 2000, 3000]:
            response = client.post('/api/tone',
                data=json.dumps({'freq': freq}),
                content_type='application/json'
            )
            assert response.status_code == 200
        
        assert mock_audio_engine.set_tone_params.call_count == 3
    
    def test_source_switching(self, client, mock_audio_engine):
        """Should handle switching between audio sources."""
        # Set tone
        response = client.post('/api/tone',
            data=json.dumps({'freq': 1000}),
            content_type='application/json'
        )
        assert response.status_code == 200
        
        # Set sweep
        response = client.post('/api/sweep',
            data=json.dumps({'duration': 10}),
            content_type='application/json'
        )
        assert response.status_code == 200
        
        # Set silence
        response = client.post('/api/silence')
        assert response.status_code == 200
        
        assert mock_audio_engine.set_source.call_count >= 2
    
    def test_concurrent_status_requests(self, client, mock_telemetry):
        """Should handle concurrent status requests."""
        responses = []
        for _ in range(10):
            response = client.get('/api/status')
            responses.append(response)
        
        assert all(r.status_code == 200 for r in responses)
        assert mock_telemetry.get_full_status.call_count == 10

"""
Unit tests for UART Command Manager.

Tests all UART communication with mocked pyserial.
"""

import pytest
import time
import threading
from unittest.mock import Mock, MagicMock, patch, call
from concurrent.futures import Future, TimeoutError as FuturesTimeoutError

from uart.command_manager import UARTCommandManager


@pytest.fixture
def mock_config():
    """Mock ConfigManager."""
    config = Mock()
    config.get.side_effect = lambda key: {
        'uart.device': '/dev/ttyO4',
        'uart.baudrate': 115200,
        'uart.timeout': 1.0
    }.get(key)
    return config


@pytest.fixture
def mock_serial():
    """Mock serial.Serial."""
    serial_mock = Mock()
    serial_mock.is_open = True
    serial_mock.write.return_value = 10
    serial_mock.readline.return_value = b""
    return serial_mock


@pytest.fixture(autouse=True)
def patch_serial(mock_serial):
    """Patch serial module globally."""
    with patch('uart.command_manager.serial') as serial_module:
        serial_module.Serial.return_value = mock_serial
        serial_module.SerialException = Exception
        yield serial_module


# =============================================================================
# Test Initialization
# =============================================================================

class TestUARTCommandManagerInit:
    """Test UARTCommandManager initialization."""
    
    def test_init_stores_config(self, mock_config):
        """Should store configuration."""
        uart = UARTCommandManager(mock_config)
        
        assert uart.config is mock_config
        assert uart.device == '/dev/ttyO4'
        assert uart.baudrate == 115200
        assert uart.timeout == 1.0
    
    def test_init_not_running(self, mock_config):
        """Should not be running initially."""
        uart = UARTCommandManager(mock_config)
        
        assert uart.running is False
        assert uart.rx_thread is None
        assert uart.serial_port is None
    
    def test_init_empty_structures(self, mock_config):
        """Should initialize empty data structures."""
        uart = UARTCommandManager(mock_config)
        
        assert uart.pending_responses == {}
        assert uart.event_callbacks == []
        assert uart.last_status is None
    
    def test_init_stats(self, mock_config):
        """Should initialize statistics."""
        uart = UARTCommandManager(mock_config)
        
        assert uart.stats['sent'] == 0
        assert uart.stats['ok'] == 0
        assert uart.stats['err'] == 0
        assert uart.stats['events'] == 0
        assert uart.stats['reconnects'] == 0


# =============================================================================
# Test Serial Port Initialization
# =============================================================================

class TestSerialPortInit:
    """Test serial port initialization."""
    
    def test_init_serial_port_opens_port(self, mock_config, patch_serial, mock_serial):
        """Should open serial port with correct parameters."""
        uart = UARTCommandManager(mock_config)
        uart._init_serial_port()
        
        patch_serial.Serial.assert_called_once_with(
            port='/dev/ttyO4',
            baudrate=115200,
            timeout=1.0
        )
        assert uart.serial_port is mock_serial
    
    def test_init_serial_port_idempotent(self, mock_config, patch_serial):
        """Should not reopen if already open."""
        uart = UARTCommandManager(mock_config)
        uart._init_serial_port()
        uart._init_serial_port()
        
        # Should only be called once
        assert patch_serial.Serial.call_count == 1
    
    def test_init_serial_port_raises_on_error(self, mock_config, patch_serial):
        """Should raise SerialException on error."""
        patch_serial.Serial.side_effect = patch_serial.SerialException("Port error")
        
        uart = UARTCommandManager(mock_config)
        
        with pytest.raises(Exception):  # SerialException
            uart._init_serial_port()


# =============================================================================
# Test Start/Stop
# =============================================================================

class TestStartStop:
    """Test start and stop methods."""
    
    def test_start_initializes_serial(self, mock_config, patch_serial, mock_serial):
        """Should initialize serial port on start."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        assert uart.serial_port is mock_serial
        patch_serial.Serial.assert_called_once()
    
    def test_start_launches_thread(self, mock_config):
        """Should launch RX thread."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        assert uart.running is True
        assert uart.rx_thread is not None
        assert uart.rx_thread.is_alive()
        
        uart.stop()
    
    def test_start_idempotent(self, mock_config, patch_serial):
        """Should be idempotent (safe to call multiple times)."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        uart.start()
        
        # Should only initialize once
        assert patch_serial.Serial.call_count == 1
        
        uart.stop()
    
    def test_stop_stops_thread(self, mock_config):
        """Should stop RX thread."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        assert uart.running is True
        
        uart.stop()
        
        assert uart.running is False
        # Thread should finish
        time.sleep(0.3)
        assert not uart.rx_thread.is_alive()
    
    def test_stop_closes_serial(self, mock_config, mock_serial):
        """Should close serial port."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        uart.stop()
        
        mock_serial.close.assert_called_once()
    
    def test_stop_idempotent(self, mock_config):
        """Should be safe to call stop multiple times."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        uart.stop()
        uart.stop()  # Should not raise
        
        assert uart.running is False


# =============================================================================
# Test Command Sending
# =============================================================================

class TestCommandSending:
    """Test command sending."""
    
    def test_send_command_writes_to_serial(self, mock_config, mock_serial):
        """Should write command to serial port."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Mock response in separate thread
        def mock_response():
            time.sleep(0.3)
            uart._process_line("OK|STATUS|IDLE")
        
        threading.Thread(target=mock_response, daemon=True).start()
        
        response = uart.send_command('STATUS', '')
        
        # Should have written command
        mock_serial.write.assert_called()
        call_args = mock_serial.write.call_args[0][0]
        assert b'STATUS' in call_args
        
        uart.stop()
    
    def test_send_command_increments_sent_stat(self, mock_config, mock_serial):
        """Should increment sent counter."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Mock response
        def mock_response():
            time.sleep(0.3)
            uart._process_line("OK|STATUS|IDLE")
        
        threading.Thread(target=mock_response, daemon=True).start()
        
        uart.send_command('STATUS', '')
        
        assert uart.stats['sent'] == 1
        
        uart.stop()
    
    def test_send_command_with_args(self, mock_config, mock_serial):
        """Should send command with arguments."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Mock response
        def mock_response():
            time.sleep(0.3)
            uart._process_line("OK|CONNECT|CONNECTED")
        
        threading.Thread(target=mock_response, daemon=True).start()
        
        uart.send_command('CONNECT', 'AA:BB:CC:DD:EE:FF')
        
        # Should have written command with args
        call_args = mock_serial.write.call_args[0][0]
        assert b'CONNECT AA:BB:CC:DD:EE:FF' in call_args
        
        uart.stop()
    
    def test_send_command_timeout(self, mock_config):
        """Should raise TimeoutError on timeout."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Don't send response - let it timeout
        with pytest.raises(FuturesTimeoutError):
            uart.send_command('STATUS', '', timeout=0.5)
        
        uart.stop()
    
    def test_send_command_not_running(self, mock_config):
        """Should raise RuntimeError if not running."""
        uart = UARTCommandManager(mock_config)
        
        with pytest.raises(RuntimeError):
            uart.send_command('STATUS', '')


# =============================================================================
# Test Response Parsing
# =============================================================================

class TestResponseParsing:
    """Test response parsing."""
    
    def test_parse_ok_response(self, mock_config):
        """Should parse OK response correctly."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Mock response
        def mock_response():
            time.sleep(0.3)
            uart._process_line("OK|SCAN|2")
        
        threading.Thread(target=mock_response, daemon=True).start()
        
        response = uart.send_command('SCAN', '')
        
        assert response['status'] == 'ok'
        assert response['command'] == 'SCAN'
        assert response['result'] == '2'
        assert uart.stats['ok'] == 1
        
        uart.stop()
    
    def test_parse_err_response(self, mock_config):
        """Should parse ERR response correctly."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Mock response
        def mock_response():
            time.sleep(0.3)
            uart._process_line("ERR|CONNECT|NOT_FOUND|Device not found")
        
        threading.Thread(target=mock_response, daemon=True).start()
        
        response = uart.send_command('CONNECT', 'AA:BB:CC:DD:EE:FF')
        
        assert response['status'] == 'error'
        assert response['command'] == 'CONNECT'
        assert response['error_code'] == 'NOT_FOUND'
        assert response['message'] == 'Device not found'
        assert uart.stats['err'] == 1
        
        uart.stop()
    
    def test_cache_status_response(self, mock_config):
        """Should cache STATUS response."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Mock response
        def mock_response():
            time.sleep(0.3)  # Python 3.9 needs slightly more time for thread scheduling
            uart._process_line("OK|STATUS|CONNECTED|AA:BB:CC:DD:EE:FF")
        
        threading.Thread(target=mock_response, daemon=True).start()
        
        response = uart.send_command('STATUS', '')
        
        # Should be cached
        assert uart.last_status is not None
        assert uart.last_status['status'] == 'ok'
        assert uart.last_status['command'] == 'STATUS'
        
        uart.stop()


# =============================================================================
# Test Event Handling
# =============================================================================

class TestEventHandling:
    """Test event handling."""
    
    def test_parse_event(self, mock_config):
        """Should parse EVENT correctly."""
        uart = UARTCommandManager(mock_config)
        
        event_received = []
        
        def on_event(event):
            event_received.append(event)
        
        uart.register_event_callback(on_event)
        uart.start()
        
        # Simulate event
        uart._process_line("EVENT|BT|CONNECTED|AA:BB:CC:DD:EE:FF")
        
        time.sleep(0.1)  # Wait for callback
        
        assert len(event_received) == 1
        assert event_received[0]['type'] == 'BT'
        assert event_received[0]['subtype'] == 'CONNECTED'
        assert event_received[0]['data'] == 'AA:BB:CC:DD:EE:FF'
        assert uart.stats['events'] == 1
        
        uart.stop()
    
    def test_multiple_event_callbacks(self, mock_config):
        """Should call all registered callbacks."""
        uart = UARTCommandManager(mock_config)
        
        events1 = []
        events2 = []
        
        uart.register_event_callback(lambda e: events1.append(e))
        uart.register_event_callback(lambda e: events2.append(e))
        uart.start()
        
        uart._process_line("EVENT|BT|DISCONNECTED|")
        
        time.sleep(0.1)
        
        assert len(events1) == 1
        assert len(events2) == 1
        
        uart.stop()


# =============================================================================
# Test Async Commands
# =============================================================================

class TestAsyncCommands:
    """Test asynchronous command sending."""
    
    def test_send_command_async(self, mock_config):
        """Should send command asynchronously."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        response_received = []
        
        def on_response(response):
            response_received.append(response)
        
        # Start async command first
        uart.send_command_async('SCAN', '', on_response)
        
        # Then mock response after a short delay
        time.sleep(0.05)
        uart._process_line("OK|SCAN|3")
        
        # Wait for async completion
        time.sleep(0.3)
        
        assert len(response_received) == 1
        assert response_received[0]['status'] == 'ok'
        
        uart.stop()


# =============================================================================
# Test Statistics and Status
# =============================================================================

class TestStatisticsAndStatus:
    """Test statistics and status methods."""
    
    def test_get_stats(self, mock_config):
        """Should return statistics."""
        uart = UARTCommandManager(mock_config)
        
        stats = uart.get_stats()
        
        assert 'sent' in stats
        assert 'ok' in stats
        assert 'err' in stats
        assert 'events' in stats
        assert 'reconnects' in stats
    
    def test_get_last_status_none_initially(self, mock_config):
        """Should return None if never queried."""
        uart = UARTCommandManager(mock_config)
        
        assert uart.get_last_status() is None
    
    def test_get_last_status_after_query(self, mock_config):
        """Should return cached status after STATUS command."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Mock response
        def mock_response():
            time.sleep(0.3)
            uart._process_line("OK|STATUS|IDLE")
        
        threading.Thread(target=mock_response, daemon=True).start()
        
        uart.send_command('STATUS', '')
        
        status = uart.get_last_status()
        assert status is not None
        assert status['command'] == 'STATUS'
        
        uart.stop()


# =============================================================================
# Test Error Handling
# =============================================================================

class TestErrorHandling:
    """Test error handling."""
    
    def test_handle_malformed_ok(self, mock_config):
        """Should handle malformed OK response."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Process malformed response (should not crash)
        uart._process_line("OK|SCAN")  # Missing result
        
        time.sleep(0.1)
        
        # Should not have incremented OK counter
        assert uart.stats['ok'] == 0
        
        uart.stop()
    
    def test_handle_malformed_err(self, mock_config):
        """Should handle malformed ERR response."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Process malformed response
        uart._process_line("ERR|CONNECT")  # Missing error_code and message
        
        time.sleep(0.1)
        
        # Should not have incremented ERR counter
        assert uart.stats['err'] == 0
        
        uart.stop()
    
    def test_handle_unknown_message_type(self, mock_config):
        """Should handle unknown message type."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        # Process unknown message (should not crash)
        uart._process_line("UNKNOWN|DATA")
        
        time.sleep(0.1)
        
        uart.stop()
    
    def test_event_callback_exception(self, mock_config):
        """Should handle exception in event callback."""
        uart = UARTCommandManager(mock_config)
        
        def bad_callback(event):
            raise Exception("Callback error")
        
        uart.register_event_callback(bad_callback)
        uart.start()
        
        # Should not crash despite callback exception
        uart._process_line("EVENT|BT|CONNECTED|MAC")
        
        time.sleep(0.1)
        
        # Event should still be counted
        assert uart.stats['events'] == 1
        
        uart.stop()


# =============================================================================
# Test Integration
# =============================================================================

class TestIntegration:
    """Integration tests."""
    
    def test_multiple_commands(self, mock_config, mock_serial):
        """Should handle multiple sequential commands."""
        uart = UARTCommandManager(mock_config)
        uart.start()
        
        commands = ['STATUS', 'SCAN', 'VOLUME']
        
        for i, cmd in enumerate(commands):
            # Mock response for each command
            def mock_response(command=cmd, idx=i):
                time.sleep(0.3)
                uart._process_line(f"OK|{command}|result_{idx}")
            
            threading.Thread(target=mock_response, daemon=True).start()
            
            response = uart.send_command(cmd, '')
            assert response['status'] == 'ok'
        
        assert uart.stats['sent'] == 3
        assert uart.stats['ok'] == 3
        
        uart.stop()
    
    def test_mixed_responses_and_events(self, mock_config):
        """Should handle mix of responses and events."""
        uart = UARTCommandManager(mock_config)
        
        events = []
        uart.register_event_callback(lambda e: events.append(e))
        uart.start()
        
        # Send command
        def mock_responses():
            time.sleep(0.3)
            uart._process_line("EVENT|BT|SCANNING|")
            time.sleep(0.05)
            uart._process_line("OK|SCAN|2")
            time.sleep(0.05)
            uart._process_line("EVENT|BT|SCAN_COMPLETE|")
        
        threading.Thread(target=mock_responses, daemon=True).start()
        
        response = uart.send_command('SCAN', '')
        
        time.sleep(0.5)  # Wait longer to ensure both events are processed (CI can be slow)
        
        assert response['status'] == 'ok'
        assert len(events) == 2
        assert events[0]['subtype'] == 'SCANNING'
        assert events[1]['subtype'] == 'SCAN_COMPLETE'
        
        uart.stop()

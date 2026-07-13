"""
UART Command Manager for RPi I2S Source.

This module provides the UARTCommandManager class for serial communication
with the esp_bt_audio_source over UART. Handles command/response protocol
and asynchronous event notifications.

Classes:
    UARTCommandManager: Serial communication manager with command queue.

Example:
    >>> from config.manager import ConfigManager
    >>> config = ConfigManager('config.yaml')
    >>> 
    >>> uart = UARTCommandManager(config)
    >>> uart.start()
    >>> 
    >>> # Send blocking command
    >>> response = uart.send_command('STATUS', '')
    >>> print(response)  # {"status": "ok", "result": "..."}
    >>> 
    >>> # Register event callback
    >>> def on_bt_event(event):
    ...     print(f"Bluetooth event: {event}")
    >>> uart.register_event_callback(on_bt_event)
    >>> 
    >>> uart.stop()
"""

import logging
import serial
import threading
import time
from typing import Optional, Dict, Any, Callable, List
from concurrent.futures import Future
import uuid


logger = logging.getLogger(__name__)


# Mock serial module for development/testing
try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    logger.warning("pyserial not available - using mock serial")
    SERIAL_AVAILABLE = False
    
    class MockSerial:
        """Mock serial.Serial for development without hardware."""
        def __init__(self, port, baudrate, timeout):
            self.port = port
            self.baudrate = baudrate
            self.timeout = timeout
            self.is_open = True
            logger.info(f"Mock serial opened: {port} @ {baudrate}")
        
        def write(self, data):
            logger.debug(f"Mock serial write: {data}")
            return len(data)
        
        def readline(self):
            time.sleep(0.1)  # Simulate blocking read
            return b""  # No data available
        
        def close(self):
            self.is_open = False
            logger.info("Mock serial closed")
    
    class MockSerialModule:
        """Mock serial module."""
        Serial = MockSerial
        SerialException = Exception
    
    serial = MockSerialModule()


class UARTCommandManager:
    """
    UART command manager for esp_bt_audio_source communication.
    
    Manages serial communication with command/response protocol and
    asynchronous event notifications.
    
    Protocol:
        Commands: COMMAND args\\n
        Responses: OK|COMMAND|result OR ERR|COMMAND|error_code|message
        Events: EVENT|TYPE|SUBTYPE|data
    
    Attributes:
        config: ConfigManager instance
        device: Serial port device path (e.g., '/dev/serial0')
        baudrate: Serial baudrate (e.g., 115200)
        timeout: Read timeout in seconds
        
        serial_port: pyserial Serial instance
        running: Thread running flag
        rx_thread: Background receive thread
        
        pending_responses: Dict of command_id -> Future
        event_callbacks: List of event callback functions
        last_status: Cached STATUS response
        
        stats: Command statistics (sent, ok, err)
    
    Thread Safety:
        All public methods are thread-safe.
    """
    
    def __init__(self, config):
        """
        Initialize UART command manager.
        
        Args:
            config: ConfigManager instance
        
        Example:
            >>> uart = UARTCommandManager(config)
        """
        self.config = config
        
        # Get UART configuration
        self.device = config.get('uart.device')
        self.baudrate = config.get('uart.baudrate')
        self.timeout = config.get('uart.timeout')
        
        # Serial port (deferred initialization)
        self.serial_port = None
        
        # Threading
        self.running = False
        self.rx_thread = None
        self.lock = threading.Lock()
        
        # Command/response tracking
        self.pending_responses: Dict[str, Future] = {}
        self.event_callbacks: List[Callable] = []
        self.last_status = None
        
        # Statistics
        self.stats = {
            'sent': 0,
            'ok': 0,
            'err': 0,
            'events': 0,
            'reconnects': 0
        }
        
        logger.info(f"UARTCommandManager initialized (device={self.device}, baudrate={self.baudrate})")
    
    def _init_serial_port(self) -> None:
        """
        Initialize serial port.
        
        Opens serial port with configured parameters.
        
        Raises:
            serial.SerialException: If port cannot be opened
        """
        if self.serial_port and self.serial_port.is_open:
            return  # Already open
        
        try:
            self.serial_port = serial.Serial(
                port=self.device,
                baudrate=self.baudrate,
                timeout=self.timeout
            )
            logger.info(f"Serial port opened: {self.device} @ {self.baudrate}")
        except serial.SerialException as e:
            logger.error(f"Failed to open serial port: {e}")
            raise
    
    def start(self) -> None:
        """
        Start UART command manager.
        
        Opens serial port and starts background receive thread.
        
        Example:
            >>> uart.start()
        """
        with self.lock:
            if self.running:
                logger.warning("UART manager already running")
                return
            
            # Initialize serial port
            self._init_serial_port()
            
            # Start RX thread
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()
            
            logger.info("UART manager started")
    
    def stop(self) -> None:
        """
        Stop UART command manager.
        
        Stops receive thread and closes serial port.
        
        Example:
            >>> uart.stop()
        """
        with self.lock:
            if not self.running:
                logger.warning("UART manager not running")
                return
            
            # Stop RX thread
            self.running = False
            
        # Wait for thread to finish (outside lock)
        if self.rx_thread:
            self.rx_thread.join(timeout=1.0)
            if self.rx_thread.is_alive():
                logger.warning("RX thread did not stop cleanly")
        
        # Close serial port
        if self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.close()
                logger.info("Serial port closed")
            except Exception as e:
                logger.error(f"Error closing serial port: {e}")
        
        logger.info("UART manager stopped")
    
    def send_command(self, command: str, args: str = '', timeout: float = 5.0) -> Dict[str, Any]:
        """
        Send command and wait for response (blocking).
        
        Args:
            command: Command name (e.g., 'STATUS', 'SCAN', 'CONNECT')
            args: Command arguments (optional)
            timeout: Response timeout in seconds (default 5.0)
        
        Returns:
            Response dict:
                {"status": "ok", "result": "..."} or
                {"status": "error", "error_code": "...", "message": "..."}
        
        Raises:
            TimeoutError: If no response received within timeout
            RuntimeError: If UART manager not running
        
        Example:
            >>> response = uart.send_command('STATUS', '')
            >>> if response['status'] == 'ok':
            ...     print(f"Status: {response['result']}")
        """
        if not self.running:
            raise RuntimeError("UART manager not running")
        
        # Create unique command ID
        command_id = str(uuid.uuid4())
        
        # Create future for response
        future = Future()
        self.pending_responses[command_id] = future
        
        # Send command
        try:
            cmd_line = f"{command} {args}\n".strip() + "\n"
            self.serial_port.write(cmd_line.encode('utf-8'))
            self.stats['sent'] += 1
            logger.debug(f"Sent command: {cmd_line.strip()} (id={command_id})")
        except Exception as e:
            # Remove pending future on write error
            self.pending_responses.pop(command_id, None)
            logger.error(f"Error sending command: {e}")
            raise
        
        # Wait for response
        try:
            response = future.result(timeout=timeout)
            return response
        except TimeoutError:
            # Remove pending future on timeout
            self.pending_responses.pop(command_id, None)
            logger.warning(f"Command timeout: {command}")
            raise TimeoutError(f"Command timeout: {command}")
    
    def send_command_async(self, command: str, args: str = '', callback: Optional[Callable] = None) -> None:
        """
        Send command asynchronously (non-blocking).
        
        Args:
            command: Command name
            args: Command arguments (optional)
            callback: Callback function(response) (optional)
        
        Example:
            >>> def on_response(response):
            ...     print(f"Response: {response}")
            >>> uart.send_command_async('SCAN', '', on_response)
        """
        def _async_send():
            try:
                response = self.send_command(command, args)
                if callback:
                    callback(response)
            except Exception as e:
                logger.error(f"Async command error: {e}")
                if callback:
                    callback({"status": "error", "message": str(e)})
        
        thread = threading.Thread(target=_async_send, daemon=True)
        thread.start()
    
    def register_event_callback(self, callback: Callable) -> None:
        """
        Register event callback function.
        
        Args:
            callback: Function(event_dict) to call on events
        
        Example:
            >>> def on_event(event):
            ...     print(f"Event: {event}")
            >>> uart.register_event_callback(on_event)
        """
        self.event_callbacks.append(callback)
        logger.debug(f"Registered event callback (total={len(self.event_callbacks)})")
    
    def get_last_status(self) -> Optional[Dict[str, Any]]:
        """
        Get last cached STATUS response.
        
        Returns:
            Last STATUS response or None if never queried
        
        Example:
            >>> status = uart.get_last_status()
            >>> if status:
            ...     print(f"Cached status: {status}")
        """
        return self.last_status
    
    def get_stats(self) -> Dict[str, int]:
        """
        Get command statistics.
        
        Returns:
            Stats dict with keys: sent, ok, err, events, reconnects
        
        Example:
            >>> stats = uart.get_stats()
            >>> print(f"Commands sent: {stats['sent']}")
        """
        return self.stats.copy()
    
    # =========================================================================
    # Internal Methods
    # =========================================================================
    
    def _rx_loop(self) -> None:
        """
        Background receive thread loop.
        
        Reads lines from serial port and processes them.
        Handles serial exceptions with reconnect logic.
        """
        logger.info("RX thread started")
        
        while self.running:
            try:
                # Read line from serial port
                line = self.serial_port.readline()
                
                if not line:
                    continue  # Timeout, no data
                
                # Decode and strip
                line = line.decode('utf-8', errors='ignore').strip()
                
                if not line:
                    continue  # Empty line
                
                logger.debug(f"RX: {line}")
                
                # Process line
                self._process_line(line)
                
            except serial.SerialException as e:
                logger.error(f"Serial exception in RX loop: {e}")
                if self.running:
                    self._reconnect()
            
            except Exception as e:
                logger.error(f"Error in RX loop: {e}", exc_info=True)
        
        logger.info("RX thread stopped")
    
    def _process_line(self, line: str) -> None:
        """
        Process received line.
        
        Parses line and dispatches to response or event handler.
        
        Args:
            line: Received line (stripped)
        """
        # Tokenize on pipe
        parts = line.split('|')
        
        if not parts:
            return
        
        msg_type = parts[0]
        
        if msg_type == 'OK':
            self._handle_response(line, 'ok', parts)
        elif msg_type == 'ERR':
            self._handle_response(line, 'error', parts)
        elif msg_type == 'EVENT':
            self._handle_event(parts)
        else:
            logger.warning(f"Unknown message type: {msg_type}")
    
    def _handle_response(self, line: str, status: str, parts: List[str]) -> None:
        """
        Handle command response.
        
        Matches response to pending future and resolves it.
        
        Args:
            line: Full response line
            status: Response status ('ok' or 'error')
            parts: Tokenized parts
        """
        # Parse response
        if status == 'ok':
            # OK|COMMAND|result
            if len(parts) >= 3:
                command = parts[1]
                result = '|'.join(parts[2:])
                response = {"status": "ok", "result": result, "command": command}
                
                # Cache STATUS response
                if command == 'STATUS':
                    self.last_status = response
                
                self.stats['ok'] += 1
            else:
                logger.warning(f"Malformed OK response: {line}")
                return
        
        else:  # error
            # ERR|COMMAND|error_code|message
            if len(parts) >= 4:
                command = parts[1]
                error_code = parts[2]
                message = '|'.join(parts[3:])
                response = {
                    "status": "error",
                    "command": command,
                    "error_code": error_code,
                    "message": message
                }
                self.stats['err'] += 1
            else:
                logger.warning(f"Malformed ERR response: {line}")
                return
        
        # Resolve pending future (match by command for now, no command ID in protocol)
        # In real implementation, would need command ID tracking
        # For simplicity, resolve oldest pending future
        if self.pending_responses:
            command_id = next(iter(self.pending_responses))
            future = self.pending_responses.pop(command_id)
            future.set_result(response)
            logger.debug(f"Resolved response for {command_id}")
    
    def _handle_event(self, parts: List[str]) -> None:
        """
        Handle asynchronous event.
        
        Calls all registered event callbacks.
        
        Args:
            parts: Tokenized event parts (EVENT|TYPE|SUBTYPE|data...)
        """
        # EVENT|TYPE|SUBTYPE|data
        if len(parts) < 3:
            logger.warning(f"Malformed EVENT: {parts}")
            return
        
        event = {
            "type": parts[1] if len(parts) > 1 else "",
            "subtype": parts[2] if len(parts) > 2 else "",
            "data": '|'.join(parts[3:]) if len(parts) > 3 else ""
        }
        
        self.stats['events'] += 1
        logger.info(f"Event: {event}")
        
        # Call all event callbacks
        for callback in self.event_callbacks:
            try:
                callback(event)
            except Exception as e:
                logger.error(f"Error in event callback: {e}", exc_info=True)
    
    def _reconnect(self) -> None:
        """
        Attempt to reconnect serial port.
        
        Tries up to 10 times with 5 second delay.
        """
        logger.warning("Attempting serial reconnect...")
        
        for attempt in range(10):
            if not self.running:
                break  # Stop requested
            
            try:
                # Close old port
                if self.serial_port and self.serial_port.is_open:
                    self.serial_port.close()
                
                # Reopen
                time.sleep(5.0)  # Wait before retry
                self._init_serial_port()
                
                self.stats['reconnects'] += 1
                logger.info(f"Serial reconnected (attempt {attempt + 1})")
                return
                
            except Exception as e:
                logger.error(f"Reconnect attempt {attempt + 1} failed: {e}")
        
        logger.error("Serial reconnect failed after 10 attempts")

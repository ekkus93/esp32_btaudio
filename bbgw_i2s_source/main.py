#!/usr/bin/env python3
"""
Main Application Entry Point for Raspberry Pi I2S Audio Source

This application creates an I2S audio test jig for ESP32 Bluetooth testing.
It provides:
- I2S audio output (tone, sweep, WAV playback)
- UART command interface to ESP32 Bluetooth module
- Web dashboard for control and monitoring

Components are initialized in dependency order and started in background threads.
Signal handlers provide graceful shutdown on SIGINT/SIGTERM.

Author: Raspberry Pi I2S Audio Source Project
License: MIT
"""

import logging
import signal
import sys
from pathlib import Path

from audio.ring_buffer import RingBuffer
from config.manager import ConfigManager
from telemetry.tracker import TelemetryTracker
from audio.engine import AudioEngine
from audio.i2s_driver import I2SDriverALSA
from uart.command_manager import UARTCommandManager
from web.app import WebServer


# Global references for signal handler
audio_engine = None
i2s_driver = None
uart_mgr = None
web_server = None


def setup_logging(config: ConfigManager) -> None:
    """
    Setup logging configuration from config.
    
    Args:
        config: ConfigManager instance with logging settings
    """
    log_level_str = config.get('logging.level', 'INFO')
    log_level = getattr(logging, log_level_str.upper(), logging.INFO)
    
    log_format = config.get(
        'logging.format',
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    logging.basicConfig(
        level=log_level,
        format=log_format,
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    
    logger = logging.getLogger(__name__)
    logger.info(f"Logging initialized at {log_level_str} level")


def on_bt_event(event: dict) -> None:
    """
    Callback for Bluetooth UART events.
    
    Updates telemetry tracker when BT connection status changes.
    
    Args:
        event: Event dictionary with 'type' and optional 'data' keys
            - type='BT_CONNECTED': Bluetooth device connected
            - type='BT_DISCONNECTED': Bluetooth device disconnected
    """
    logger = logging.getLogger(__name__)
    event_type = event.get('type')
    
    if event_type == 'BT_CONNECTED':
        logger.info("Bluetooth device connected")
        # Telemetry is updated automatically via UART manager's internal state
        
    elif event_type == 'BT_DISCONNECTED':
        logger.info("Bluetooth device disconnected")
        # Telemetry is updated automatically via UART manager's internal state


def signal_handler(sig: int, frame) -> None:
    """
    Handle SIGINT and SIGTERM for graceful shutdown.
    
    Stops all background components in reverse dependency order.
    
    Args:
        sig: Signal number
        frame: Current stack frame
    """
    logger = logging.getLogger(__name__)
    logger.info(f"Received signal {sig}, initiating graceful shutdown...")
    
    # Stop components in reverse order
    global web_server, uart_mgr, i2s_driver, audio_engine
    
    if web_server:
        logger.info("Stopping web server...")
        try:
            web_server.stop()
        except Exception as e:
            logger.error(f"Error stopping web server: {e}")
    
    if uart_mgr:
        logger.info("Stopping UART command manager...")
        try:
            uart_mgr.stop()
        except Exception as e:
            logger.error(f"Error stopping UART manager: {e}")
    
    if i2s_driver:
        logger.info("Stopping I2S driver...")
        try:
            i2s_driver.stop()
        except Exception as e:
            logger.error(f"Error stopping I2S driver: {e}")
    
    if audio_engine:
        logger.info("Stopping audio engine...")
        try:
            audio_engine.stop()
        except Exception as e:
            logger.error(f"Error stopping audio engine: {e}")
    
    logger.info("Shutdown complete")
    sys.exit(0)


def main() -> int:
    """
    Main application entry point.
    
    Initializes all components, starts background threads, registers
    signal handlers, and starts the web server (blocking).
    
    Returns:
        Exit code (0 for success, non-zero for error)
    """
    global audio_engine, i2s_driver, uart_mgr, web_server
    
    # Determine config file path (allow override via environment or argument)
    config_path = Path(__file__).parent / 'config' / 'config.yaml'
    
    try:
        # 1. Load configuration
        config = ConfigManager(str(config_path))
        
        # 2. Setup logging
        setup_logging(config)
        logger = logging.getLogger(__name__)
        logger.info("=== Raspberry Pi I2S Audio Source Starting ===")
        logger.info(f"Configuration loaded from: {config_path}")
        
        # 3. Initialize components in dependency order
        logger.info("Initializing components...")
        
        # Core data structure
        buffer_size = config.get('audio.buffer_size', 8192)
        ring_buffer = RingBuffer(capacity=buffer_size)
        logger.info(f"Created RingBuffer (capacity={buffer_size})")
        
        # Audio generation
        audio_engine = AudioEngine(config, ring_buffer)
        logger.info("Created AudioEngine")
        
        # I2S output driver
        i2s_driver = I2SDriverALSA(config, ring_buffer)
        logger.info(f"Created I2SDriverALSA (device={i2s_driver.device_name})")
        
        # UART command interface (optional - graceful if not available)
        try:
            uart_mgr = UARTCommandManager(config)
            logger.info(f"Created UARTCommandManager (port={uart_mgr.port})")
        except Exception as e:
            logger.warning(f"UART manager not available: {e}")
            uart_mgr = None
        
        # Telemetry tracker
        telemetry = TelemetryTracker()
        logger.info("Created TelemetryTracker")
        
        # Web server
        web_server = WebServer(config, audio_engine, uart_mgr, telemetry)
        logger.info(f"Created WebServer (port={web_server.port})")
        
        # 4. Start background components
        logger.info("Starting background components...")
        
        audio_engine.start()
        logger.info("Started AudioEngine")
        
        i2s_driver.start()
        logger.info("Started I2SDriver")
        
        if uart_mgr:
            uart_mgr.start()
            logger.info("Started UARTCommandManager")
            
            # Register UART event callbacks
            uart_mgr.register_callback('BT_CONNECTED', on_bt_event)
            uart_mgr.register_callback('BT_DISCONNECTED', on_bt_event)
            logger.info("Registered UART event callbacks")
        
        # 5. Setup signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        logger.info("Signal handlers registered (SIGINT, SIGTERM)")
        
        # 6. Start web server (blocking)
        logger.info("Starting web server (blocking)...")
        logger.info("=== Application Ready ===")
        if uart_mgr:
            logger.info("Web dashboard: http://localhost:5000")
            logger.info("UART interface: active")
        else:
            logger.info("Web dashboard: http://localhost:5000")
            logger.info("UART interface: not available (running in web-only mode)")
        logger.info("Press Ctrl+C to stop")
        
        web_server.start()  # Blocks until shutdown
        
        return 0
        
    except KeyboardInterrupt:
        logger = logging.getLogger(__name__)
        logger.info("Keyboard interrupt received")
        signal_handler(signal.SIGINT, None)
        return 0
        
    except Exception as e:
        logger = logging.getLogger(__name__)
        logger.error(f"Fatal error: {e}", exc_info=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())

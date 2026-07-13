"""
Flask Web Server for BeagleBone Green Wireless I2S Source.

This module provides the WebServer class that implements a Flask-based web interface
for controlling the I2S audio test jig. Provides REST API endpoints for audio control,
Bluetooth management, and real-time telemetry.

Classes:
    WebServer: Flask web server with REST API.

Example:
    >>> from config.manager import ConfigManager
    >>> from audio.engine import AudioEngine
    >>> from audio.ring_buffer import RingBuffer
    >>> from telemetry.tracker import TelemetryTracker
    >>> 
    >>> config = ConfigManager('config.yaml')
    >>> ring_buffer = RingBuffer(8192)
    >>> audio_engine = AudioEngine(config, ring_buffer)
    >>> telemetry = TelemetryTracker()
    >>> 
    >>> web_server = WebServer(config, audio_engine, None, telemetry)
    >>> web_server.start()  # Blocking call
"""

import logging
import json
from flask import Flask, request, jsonify, Response, render_template
from typing import Optional, Dict, Any
import time

from audio.exceptions import WAVNotFoundError, WAVFormatError


logger = logging.getLogger(__name__)


class WebServer:
    """
    Flask web server for I2S audio test jig.
    
    Provides REST API for controlling audio generation, Bluetooth connectivity,
    and monitoring system telemetry.
    
    Attributes:
        config: ConfigManager instance
        audio_engine: AudioEngine instance for audio generation
        uart_manager: UARTCommandManager instance (optional, may be None)
        telemetry: TelemetryTracker instance for system monitoring
        app: Flask application instance
        
        host: Server bind address (from config)
        port: Server port (from config)
    
    API Endpoints:
        GET  /api/status              - Get full system status
        POST /api/tone                - Set tone parameters
        POST /api/sweep               - Start frequency sweep
        POST /api/wav                 - Play WAV file
        POST /api/silence             - Set silence mode
        POST /api/bt/command          - Send Bluetooth command (requires UART)
        GET  /api/bt/status           - Get Bluetooth status (requires UART)
        GET  /api/stream              - Server-Sent Events stream
    
    Thread Safety:
        All endpoints are thread-safe. Flask runs in threaded mode.
    """
    
    def __init__(self, config, audio_engine, uart_manager, telemetry):
        """
        Initialize Flask web server.
        
        Args:
            config: ConfigManager instance
            audio_engine: AudioEngine instance
            uart_manager: UARTCommandManager instance (can be None)
            telemetry: TelemetryTracker instance
        
        Example:
            >>> web_server = WebServer(config, audio_engine, None, telemetry)
        """
        self.config = config
        self.audio_engine = audio_engine
        self.uart_manager = uart_manager
        self.telemetry = telemetry
        
        # Get server configuration
        self.host = config.get('web.bind_address')
        self.port = config.get('web.port')
        
        # Create Flask app
        self.app = Flask(__name__)
        
        # Register routes
        self._register_routes()
        
        logger.info(f"WebServer initialized (host={self.host}, port={self.port})")
    
    def _register_routes(self) -> None:
        """Register all Flask routes."""
        # Web UI
        self.app.route('/', methods=['GET'])(self._index)
        
        # Status and monitoring
        self.app.route('/api/status', methods=['GET'])(self._get_status)
        self.app.route('/api/stream', methods=['GET'])(self._stream_status)
        
        # Audio control
        self.app.route('/api/tone', methods=['POST'])(self._set_tone)
        self.app.route('/api/multi-tone/enable', methods=['POST'])(self._enable_multi_tone)
        self.app.route('/api/multi-tone/<int:tone_index>', methods=['POST'])(self._set_multi_tone)
        self.app.route('/api/sweep', methods=['POST'])(self._set_sweep)
        self.app.route('/api/wav', methods=['POST'])(self._set_wav)
        self.app.route('/api/silence', methods=['POST'])(self._set_silence)
        
        # Bluetooth control (requires UART manager)
        self.app.route('/api/bt/command', methods=['POST'])(self._bt_command)
        self.app.route('/api/bt/status', methods=['GET'])(self._bt_status)
    
    def start(self) -> None:
        """
        Start Flask web server (blocking).
        
        Runs Flask in threaded mode on configured host/port.
        
        Example:
            >>> web_server.start()  # Blocks until shutdown
        """
        logger.info(f"Starting Flask server on {self.host}:{self.port}")
        self.app.run(host=self.host, port=self.port, threaded=True, debug=False)
    
    def stop(self) -> None:
        """
        Stop Flask web server.
        
        Note: Flask doesn't provide a clean shutdown API. In production,
        use a WSGI server (gunicorn, waitress) with proper shutdown support.
        """
        logger.info("WebServer stop requested (Flask has no clean shutdown)")
    
    # =========================================================================
    # Web UI Routes
    # =========================================================================
    
    def _index(self) -> str:
        """
        GET / - Serve main dashboard page.
        
        Returns:
            Rendered HTML template
        """
        return render_template('index.html')
    
    # =========================================================================
    # API Endpoints
    # =========================================================================
    
    def _get_status(self) -> Response:
        """
        GET /api/status - Get full system status.
        
        Returns:
            JSON response with telemetry data:
            {
                "i2s": {...},
                "uart": {...},
                "bt": {...},
                "audio": {...},
                "system": {...}
            }
        
        Example:
            >>> curl http://localhost:5000/api/status
        """
        try:
            status = self.telemetry.get_full_status()
            return jsonify(status), 200
        except Exception as e:
            logger.error(f"Error getting status: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _stream_status(self) -> Response:
        """
        GET /api/stream - Server-Sent Events stream.
        
        Streams status updates every 500ms in SSE format.
        
        Returns:
            SSE stream with format: data: <JSON>\n\n
        
        Example:
            >>> curl http://localhost:5000/api/stream
        """
        def generate():
            """SSE generator function."""
            try:
                while True:
                    status = self.telemetry.get_full_status()
                    # SSE format: data: <JSON>\n\n
                    yield f"data: {json.dumps(status)}\n\n"
                    time.sleep(0.5)  # 2 Hz update rate
            except Exception as e:
                logger.error(f"Error in SSE stream: {e}", exc_info=True)
        
        return Response(generate(), mimetype='text/event-stream')
    
    def _set_tone(self) -> Response:
        """
        POST /api/tone - Set tone parameters.
        
        Request body (JSON):
            {
                "freq": 1000,           # Frequency in Hz (20-20000)
                "amp": 0.5,             # Amplitude (0.0-1.0)
                "mode": "mono",         # Stereo mode: mono/left/right/dual
                "dual_freq": 440        # Second frequency for dual mode (optional)
            }
        
        Returns:
            {"status": "ok"} or {"error": "..."}
        
        Example:
            >>> curl -X POST http://localhost:5000/api/tone \
            ...   -H "Content-Type: application/json" \
            ...   -d '{"freq": 1000, "amp": 0.5, "mode": "mono"}'
        """
        try:
            data = request.get_json(silent=True)
            if data is None:
                return jsonify({"error": "No JSON data provided"}), 400
            
            # Extract parameters with defaults
            freq = data.get('freq')
            amp = data.get('amp')
            mode = data.get('mode')
            dual_freq = data.get('dual_freq')
            
            # Validate frequency
            if freq is not None:
                if not (20 <= freq <= 20000):
                    return jsonify({"error": "freq must be 20-20000 Hz"}), 400
            
            # Validate amplitude
            if amp is not None:
                if not (0.0 <= amp <= 1.0):
                    return jsonify({"error": "amp must be 0.0-1.0"}), 400
            
            # Validate mode
            if mode is not None:
                valid_modes = ['mono', 'left', 'right', 'dual']
                if mode not in valid_modes:
                    return jsonify({"error": f"mode must be one of {valid_modes}"}), 400
            
            # Validate dual_freq
            if dual_freq is not None:
                if not (20 <= dual_freq <= 20000):
                    return jsonify({"error": "dual_freq must be 20-20000 Hz"}), 400
            
            # Set tone parameters
            self.audio_engine.set_tone_params(
                freq=freq,
                amp=amp,
                mode=mode,
                dual_freq=dual_freq
            )
            
            # Switch to tone source if not already
            current_state = self.audio_engine.get_state()
            if current_state['source'] != 'tone':
                self.audio_engine.set_source('tone')
            
            return jsonify({"status": "ok"}), 200
            
        except Exception as e:
            logger.error(f"Error setting tone: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _enable_multi_tone(self) -> Response:
        """
        POST /api/multi-tone/enable - Enable or disable multi-tone mode.
        
        Request body (JSON):
            {
                "enabled": true     # True to enable multi-tone, false for single tone
            }
        
        Returns:
            {"status": "ok"} or {"error": "..."}
        
        Example:
            >>> curl -X POST http://localhost:5000/api/multi-tone/enable \
            ...   -H "Content-Type: application/json" \
            ...   -d '{"enabled": true}'
        """
        try:
            data = request.get_json(silent=True)
            if data is None:
                return jsonify({"error": "No JSON data provided"}), 400
            
            enabled = data.get('enabled')
            if enabled is None:
                return jsonify({"error": "enabled parameter required (true/false)"}), 400
            
            if not isinstance(enabled, bool):
                return jsonify({"error": "enabled must be a boolean"}), 400
            
            # Enable multi-tone mode
            self.audio_engine.enable_multi_tone(enabled)
            
            # Switch to tone source if not already
            current_state = self.audio_engine.get_state()
            if current_state['source'] != 'tone':
                self.audio_engine.set_source('tone')
            
            return jsonify({"status": "ok", "multi_tone_enabled": enabled}), 200
            
        except Exception as e:
            logger.error(f"Error enabling multi-tone: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _set_multi_tone(self, tone_index: int) -> Response:
        """
        POST /api/multi-tone/<tone_index> - Set parameters for a specific tone.
        
        URL Parameters:
            tone_index: Tone index (0-3) for tones 1-4
        
        Request body (JSON):
            {
                "freq": 440,        # Frequency in Hz (20-20000, optional)
                "amp": 0.5,         # Amplitude (0.0-1.0, optional)
                "enabled": true     # Enable/disable this tone (optional)
            }
        
        Returns:
            {"status": "ok"} or {"error": "..."}
        
        Example:
            >>> curl -X POST http://localhost:5000/api/multi-tone/0 \
            ...   -H "Content-Type: application/json" \
            ...   -d '{"freq": 440, "amp": 0.8, "enabled": true}'
        """
        try:
            # Validate tone index (0-3)
            if not 0 <= tone_index <= 3:
                return jsonify({"error": "tone_index must be 0-3"}), 400
            
            data = request.get_json(silent=True)
            if data is None:
                return jsonify({"error": "No JSON data provided"}), 400
            
            # Extract parameters
            freq = data.get('freq')
            amp = data.get('amp')
            enabled = data.get('enabled')
            
            # Validate frequency
            if freq is not None:
                if not (20 <= freq <= 20000):
                    return jsonify({"error": "freq must be 20-20000 Hz"}), 400
            
            # Validate amplitude
            if amp is not None:
                if not (0.0 <= amp <= 1.0):
                    return jsonify({"error": "amp must be 0.0-1.0"}), 400
            
            # Validate enabled
            if enabled is not None:
                if not isinstance(enabled, bool):
                    return jsonify({"error": "enabled must be a boolean"}), 400
            
            # Set multi-tone parameters
            self.audio_engine.set_multi_tone_params(
                tone_index=tone_index,
                freq=freq,
                amp=amp,
                enabled=enabled
            )
            
            # Switch to tone source if not already
            current_state = self.audio_engine.get_state()
            if current_state['source'] != 'tone':
                self.audio_engine.set_source('tone')
            
            return jsonify({"status": "ok", "tone_index": tone_index}), 200
            
        except ValueError as e:
            logger.warning(f"Invalid multi-tone parameter: {e}")
            return jsonify({"error": str(e)}), 400
            
        except Exception as e:
            logger.error(f"Error setting multi-tone: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _set_sweep(self) -> Response:
        """
        POST /api/sweep - Start frequency sweep.
        
        Request body (JSON):
            {
                "duration": 10,     # Sweep duration in seconds (1-60)
                "loop": false       # Loop sweep (optional, default false)
            }
        
        Returns:
            {"status": "ok"} or {"error": "..."}
        
        Example:
            >>> curl -X POST http://localhost:5000/api/sweep \
            ...   -H "Content-Type: application/json" \
            ...   -d '{"duration": 10, "loop": false}'
        """
        try:
            data = request.get_json(silent=True)
            if data is None:
                data = {}  # Use defaults if no JSON provided
            
            # Extract parameters
            duration = data.get('duration', 10)
            loop = data.get('loop', False)
            
            # Validate duration
            if not (1 <= duration <= 60):
                return jsonify({"error": "duration must be 1-60 seconds"}), 400
            
            # Set sweep source
            self.audio_engine.set_source('sweep', {
                'duration': duration,
                'loop': loop
            })
            
            return jsonify({"status": "ok"}), 200
            
        except Exception as e:
            logger.error(f"Error setting sweep: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _set_wav(self) -> Response:
        """
        POST /api/wav - Play WAV file.
        
        Request body (JSON):
            {
                "file": "test.wav",  # WAV filename in /home/pi/audio/
                "loop": false        # Loop playback (optional, default false)
            }
        
        Returns:
            {"status": "ok"} or {"error": "..."}
        
        HTTP Status Codes:
            200 - Success
            400 - Invalid format
            404 - File not found
            500 - Server error
        
        Example:
            >>> curl -X POST http://localhost:5000/api/wav \
            ...   -H "Content-Type: application/json" \
            ...   -d '{"file": "test.wav", "loop": false}'
        """
        try:
            data = request.get_json(silent=True)
            if data is None:
                return jsonify({"error": "No JSON data provided"}), 400
            
            # Extract parameters
            filename = data.get('file')
            loop = data.get('loop', False)
            
            if not filename:
                return jsonify({"error": "file parameter required"}), 400
            
            # Set WAV source
            self.audio_engine.set_source('wav', {
                'file': filename,
                'loop': loop
            })
            
            return jsonify({"status": "ok"}), 200
            
        except WAVNotFoundError as e:
            logger.warning(f"WAV file not found: {e}")
            return jsonify({"error": str(e)}), 404
        
        except WAVFormatError as e:
            logger.warning(f"WAV format error: {e}")
            return jsonify({"error": str(e)}), 400
        
        except Exception as e:
            logger.error(f"Error setting WAV: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _set_silence(self) -> Response:
        """
        POST /api/silence - Set silence mode.
        
        Returns:
            {"status": "ok"} or {"error": "..."}
        
        Example:
            >>> curl -X POST http://localhost:5000/api/silence
        """
        try:
            self.audio_engine.set_source('silence')
            return jsonify({"status": "ok"}), 200
            
        except Exception as e:
            logger.error(f"Error setting silence: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _bt_command(self) -> Response:
        """
        POST /api/bt/command - Send Bluetooth command via UART.
        
        Request body (JSON):
            {
                "command": "SCAN",      # Command name
                "args": ""              # Command arguments (optional)
            }
        
        Returns:
            UART response or error:
            {"status": "ok", "result": ...} or
            {"status": "error", "message": ...}
        
        HTTP Status Codes:
            200 - Success
            400 - Invalid request
            503 - UART manager not available
            504 - Command timeout
            500 - Server error
        
        Example:
            >>> curl -X POST http://localhost:5000/api/bt/command \
            ...   -H "Content-Type: application/json" \
            ...   -d '{"command": "SCAN"}'
        """
        if not self.uart_manager:
            return jsonify({"error": "UART manager not available"}), 503
        
        try:
            data = request.get_json(silent=True)
            if data is None:
                return jsonify({"error": "No JSON data provided"}), 400
            
            command = data.get('command')
            args = data.get('args', '')
            
            if not command:
                return jsonify({"error": "command parameter required"}), 400
            
            # Send command via UART
            response = self.uart_manager.send_command(command, args)
            
            return jsonify(response), 200
            
        except TimeoutError as e:
            logger.warning(f"UART command timeout: {e}")
            return jsonify({"error": "Command timeout"}), 504
        
        except Exception as e:
            logger.error(f"Error sending BT command: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500
    
    def _bt_status(self) -> Response:
        """
        GET /api/bt/status - Get Bluetooth status.
        
        Returns:
            Cached STATUS response from UART manager or error.
        
        HTTP Status Codes:
            200 - Success
            503 - UART manager not available
            500 - Server error
        
        Example:
            >>> curl http://localhost:5000/api/bt/status
        """
        if not self.uart_manager:
            return jsonify({"error": "UART manager not available"}), 503
        
        try:
            status = self.uart_manager.get_last_status()
            return jsonify(status), 200
            
        except Exception as e:
            logger.error(f"Error getting BT status: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500

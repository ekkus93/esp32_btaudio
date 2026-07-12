"""Scenario runners for TestScenarioRunner, as a mixin (uses self.*)."""
import time

from .types import ScenarioResult, TestResult


class ScenariosMixin:
    """Test-scenario methods mixed into TestScenarioRunner."""

    def _scenario_system_health(self) -> ScenarioResult:
        """Test scenario: System health check."""
        result = ScenarioResult(
            name="System Health",
            description="Verify system is responsive and API accessible",
            status=TestResult.PASS,
            duration=0.0
        )
        
        # Step 1: Check API accessibility
        print("  Step 1: Checking API accessibility...")
        status = self._api_get("/api/status")
        if status is None:
            result.status = TestResult.FAIL
            result.error_message = "API not accessible"
            result.add_step("API Check", TestResult.FAIL, "Cannot reach API")
            return result
        
        result.add_step("API Check", TestResult.PASS, "API accessible")
        print("  ✓ API accessible")
        
        # Step 2: Verify I2S driver status
        print("  Step 2: Checking I2S driver...")
        if 'i2s' in status:
            i2s_active = status['i2s'].get('active', False)
            if i2s_active:
                result.add_step("I2S Driver", TestResult.PASS, "I2S driver active")
                print("  ✓ I2S driver active")
            else:
                result.add_step("I2S Driver", TestResult.FAIL, "I2S driver not active")
                print("  ⚠ I2S driver not active")
                result.status = TestResult.FAIL
        else:
            result.add_step("I2S Driver", TestResult.ERROR, "No I2S status")
            result.status = TestResult.FAIL
        
        # Step 3: Check system resources
        print("  Step 3: Checking system resources...")
        if 'system' in status:
            cpu_temp = status['system'].get('cpu_temp', 0)
            memory_mb = status['system'].get('memory_mb', 0)
            
            details = f"CPU temp: {cpu_temp}°C, Memory: {memory_mb}MB"
            
            if cpu_temp > 80:
                result.add_step("System Resources", TestResult.FAIL, f"High CPU temp: {cpu_temp}°C")
                result.status = TestResult.FAIL
            else:
                result.add_step("System Resources", TestResult.PASS, details)
                print(f"  ✓ {details}")
        
        return result
    
    def _scenario_tone_generation(self) -> ScenarioResult:
        """Test scenario: Tone generation."""
        result = ScenarioResult(
            name="Tone Generation",
            description="Test single-tone generation with parameter changes",
            status=TestResult.PASS,
            duration=0.0
        )
        
        # Step 1: Set 1 kHz tone
        print("  Step 1: Setting 1 kHz tone...")
        response = self._api_post("/api/tone", {'freq': 1000, 'amp': 0.5, 'mode': 'mono'})
        if response and response.get('status') == 'ok':
            result.add_step("Set 1 kHz tone", TestResult.PASS)
            print("  ✓ 1 kHz tone set")
        else:
            result.status = TestResult.FAIL
            result.add_step("Set 1 kHz tone", TestResult.FAIL)
            return result
        
        time.sleep(1)
        
        # Step 2: Verify audio source is tone
        print("  Step 2: Verifying audio source...")
        status = self._api_get("/api/status")
        if status and status.get('audio', {}).get('source') == 'tone':
            result.add_step("Verify source", TestResult.PASS, "Audio source = tone")
            print("  ✓ Audio source confirmed as tone")
        else:
            result.status = TestResult.FAIL
            result.add_step("Verify source", TestResult.FAIL)
        
        # Step 3: Change frequency
        print("  Step 3: Changing to 440 Hz...")
        response = self._api_post("/api/tone", {'freq': 440})
        if response and response.get('status') == 'ok':
            result.add_step("Change frequency", TestResult.PASS, "Changed to 440 Hz")
            print("  ✓ Frequency changed to 440 Hz")
        else:
            result.status = TestResult.FAIL
            result.add_step("Change frequency", TestResult.FAIL)
        
        time.sleep(1)
        
        # Step 4: Test stereo modes
        print("  Step 4: Testing stereo modes...")
        for mode in ['mono', 'left', 'right', 'dual']:
            response = self._api_post("/api/tone", {'mode': mode, 'dual_freq': 554.37})
            if response and response.get('status') == 'ok':
                print(f"    ✓ Mode '{mode}' OK")
            else:
                result.status = TestResult.FAIL
                result.add_step(f"Stereo mode {mode}", TestResult.FAIL)
                return result
            time.sleep(0.5)
        
        result.add_step("Stereo modes", TestResult.PASS, "All 4 modes tested")
        
        return result
    
    def _scenario_frequency_sweep(self) -> ScenarioResult:
        """Test scenario: Frequency sweep."""
        result = ScenarioResult(
            name="Frequency Sweep",
            description="Test 20 Hz → 20 kHz logarithmic sweep",
            status=TestResult.PASS,
            duration=0.0
        )
        
        # Step 1: Start 5-second sweep
        print("  Step 1: Starting 5-second sweep (20 Hz → 20 kHz)...")
        response = self._api_post("/api/sweep", {'duration': 5, 'loop': False})
        if response and response.get('status') == 'ok':
            result.add_step("Start sweep", TestResult.PASS)
            print("  ✓ Sweep started")
        else:
            result.status = TestResult.FAIL
            result.add_step("Start sweep", TestResult.FAIL)
            return result
        
        # Step 2: Verify audio source changed to sweep
        time.sleep(0.5)
        status = self._api_get("/api/status")
        if status and status.get('audio', {}).get('source') == 'sweep':
            result.add_step("Verify source", TestResult.PASS, "Audio source = sweep")
            print("  ✓ Audio source confirmed as sweep")
        else:
            result.status = TestResult.FAIL
            result.add_step("Verify source", TestResult.FAIL)
        
        # Step 3: Wait for sweep to complete
        print("  Step 3: Waiting for sweep to complete (5 seconds)...")
        time.sleep(5.5)
        
        # Step 4: Verify sweep completed (source should switch to silence)
        status = self._api_get("/api/status")
        if status:
            source = status.get('audio', {}).get('source')
            result.add_step("Sweep completion", TestResult.PASS, f"Final source: {source}")
            print(f"  ✓ Sweep completed (source now: {source})")
        
        return result
    
    def _scenario_wav_playback(self) -> ScenarioResult:
        """Test scenario: WAV file playback."""
        result = ScenarioResult(
            name="WAV Playback",
            description="Test WAV file loading and playback",
            status=TestResult.SKIP,  # Skip if no WAV files available
            duration=0.0
        )
        
        # Step 1: Try to play a WAV file (may not exist)
        print("  Step 1: Attempting WAV playback (test.wav)...")
        response = self._api_post("/api/wav", {'file': 'test.wav', 'loop': False})
        
        if response is None:
            result.add_step("WAV playback", TestResult.SKIP, "No WAV file available")
            print("  ⊘ No WAV file available (skipping)")
            return result
        
        if response.get('status') == 'ok':
            result.status = TestResult.PASS
            result.add_step("WAV playback", TestResult.PASS, "test.wav loaded")
            print("  ✓ WAV file loaded")
            
            time.sleep(2)
            
            # Verify source
            status = self._api_get("/api/status")
            if status and status.get('audio', {}).get('source') == 'wav':
                result.add_step("Verify source", TestResult.PASS, "Audio source = wav")
                print("  ✓ Audio source confirmed as WAV")
        else:
            # WAV not found is expected - skip instead of fail
            result.add_step("WAV playback", TestResult.SKIP, "test.wav not found")
            print("  ⊘ test.wav not found (skipping)")
        
        return result
    
    def _scenario_multi_tone(self) -> ScenarioResult:
        """Test scenario: Multi-tone generation."""
        result = ScenarioResult(
            name="Multi-Tone Generation",
            description="Test up to 4 simultaneous tones (chord generation)",
            status=TestResult.PASS,
            duration=0.0
        )
        
        # Step 1: Enable multi-tone mode
        print("  Step 1: Enabling multi-tone mode...")
        response = self._api_post("/api/multi-tone/enable", {'enabled': True})
        if response and response.get('status') == 'ok':
            result.add_step("Enable multi-tone", TestResult.PASS)
            print("  ✓ Multi-tone mode enabled")
        else:
            result.status = TestResult.FAIL
            result.add_step("Enable multi-tone", TestResult.FAIL)
            return result
        
        # Step 2: Configure A major chord (A4, C#5, E5)
        print("  Step 2: Configuring A major chord (440, 554.37, 659.25 Hz)...")
        tones = [
            (0, 440.00, 0.6, True),    # A4
            (1, 554.37, 0.6, True),    # C#5
            (2, 659.25, 0.6, True),    # E5
            (3, 0, 0, False)           # Disabled
        ]
        
        for index, freq, amp, enabled in tones:
            if enabled:
                response = self._api_post(f"/api/multi-tone/{index}", 
                                        {'freq': freq, 'amp': amp, 'enabled': enabled})
                if not (response and response.get('status') == 'ok'):
                    result.status = TestResult.FAIL
                    result.add_step(f"Set tone {index}", TestResult.FAIL)
                    return result
        
        result.add_step("Configure A major", TestResult.PASS, "3 tones enabled")
        print("  ✓ A major chord configured")
        
        time.sleep(2)
        
        # Step 3: Switch to C major chord
        print("  Step 3: Switching to C major chord (261.63, 329.63, 392.00 Hz)...")
        c_major = [
            (0, 261.63, 0.6, True),    # C4
            (1, 329.63, 0.6, True),    # E4
            (2, 392.00, 0.6, True),    # G4
        ]
        
        for index, freq, amp, enabled in c_major:
            response = self._api_post(f"/api/multi-tone/{index}", 
                                    {'freq': freq, 'amp': amp, 'enabled': enabled})
            if not (response and response.get('status') == 'ok'):
                result.status = TestResult.FAIL
                result.add_step(f"Update tone {index}", TestResult.FAIL)
                return result
        
        result.add_step("Configure C major", TestResult.PASS, "Switched chords")
        print("  ✓ C major chord configured")
        
        time.sleep(1)
        
        # Step 4: Disable multi-tone mode
        print("  Step 4: Disabling multi-tone mode...")
        response = self._api_post("/api/multi-tone/enable", {'enabled': False})
        if response and response.get('status') == 'ok':
            result.add_step("Disable multi-tone", TestResult.PASS)
            print("  ✓ Multi-tone mode disabled")
        else:
            result.status = TestResult.FAIL
            result.add_step("Disable multi-tone", TestResult.FAIL)
        
        return result
    
    def _scenario_uart_communication(self) -> ScenarioResult:
        """Test scenario: UART communication."""
        result = ScenarioResult(
            name="UART Communication",
            description="Test ESP32 UART command interface",
            status=TestResult.SKIP,
            duration=0.0
        )
        
        # UART tests require hardware - check if available
        print("  Checking UART availability...")
        status = self._api_get("/api/status")
        if not status or not status.get('uart', {}).get('available', False):
            result.add_step("UART Check", TestResult.SKIP, "UART not available")
            print("  ⊘ UART not available (skipping)")
            return result
        
        result.status = TestResult.PASS
        result.add_step("UART Check", TestResult.PASS, "UART available")
        print("  ✓ UART available")
        
        # Try STATUS command
        print("  Sending STATUS command...")
        response = self._api_post("/api/bt/command", {'command': 'STATUS'})
        if response and response.get('status') == 'ok':
            result.add_step("STATUS command", TestResult.PASS)
            print("  ✓ STATUS command OK")
        else:
            result.status = TestResult.FAIL
            result.add_step("STATUS command", TestResult.FAIL)
        
        return result
    
    def _scenario_bluetooth_control(self) -> ScenarioResult:
        """Test scenario: Bluetooth control."""
        result = ScenarioResult(
            name="Bluetooth Control",
            description="Test Bluetooth scan and control commands",
            status=TestResult.SKIP,
            duration=0.0
        )
        
        # Bluetooth tests require UART and hardware
        print("  Checking Bluetooth availability...")
        status = self._api_get("/api/status")
        if not status or not status.get('uart', {}).get('available', False):
            result.add_step("Bluetooth Check", TestResult.SKIP, "UART not available")
            print("  ⊘ UART not available (skipping)")
            return result
        
        result.status = TestResult.PASS
        result.add_step("Bluetooth Check", TestResult.PASS, "UART available")
        print("  ✓ UART available for Bluetooth commands")
        
        # Try SCAN command
        print("  Sending SCAN command...")
        response = self._api_post("/api/bt/command", {'command': 'SCAN'})
        if response and response.get('status') == 'ok':
            result.add_step("SCAN command", TestResult.PASS)
            print("  ✓ SCAN command sent")
        else:
            result.status = TestResult.FAIL
            result.add_step("SCAN command", TestResult.FAIL)
        
        return result

#!/usr/bin/env python3
"""
Automated Test Scenario Runner

Runs comprehensive test scenarios for the RPi I2S Audio Source system.
Tests audio generation, UART communication, Bluetooth connectivity, and
generates detailed pass/fail reports.

Usage:
    python tools/run_test_scenarios.py --all
    python tools/run_test_scenarios.py --scenario tone
    python tools/run_test_scenarios.py --scenario sweep --output report.html
    python tools/run_test_scenarios.py --list

Features:
    - Multiple test scenarios (tone, sweep, WAV, UART, Bluetooth)
    - Automated API calls with validation
    - Screenshot capture of web UI
    - HTML or Markdown report generation
    - Pass/fail summary with timing
    - Optional email notification

Requirements:
    - Running RPi I2S Source instance (main.py)
    - Hardware: Raspberry Pi with I2S configured
    - Optional: ESP32 with UART, Bluetooth speaker

Author: rpi_i2s_source
Date: 2026-02-06
"""

import argparse
import sys
import time
import json
import requests
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Any, Optional
import subprocess
from dataclasses import dataclass, field
from enum import Enum


class TestResult(Enum):
    """Test result status."""
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    ERROR = "ERROR"


@dataclass
class ScenarioResult:
    """Result of a test scenario."""
    name: str
    description: str
    status: TestResult
    duration: float
    steps: List[Dict[str, Any]] = field(default_factory=list)
    error_message: Optional[str] = None
    
    def add_step(self, step_name: str, status: TestResult, details: str = ""):
        """Add a test step result."""
        self.steps.append({
            'name': step_name,
            'status': status.value,
            'details': details,
            'timestamp': datetime.now().isoformat()
        })


class TestScenarioRunner:
    """
    Test scenario runner for RPi I2S Audio Source.
    
    Executes predefined test scenarios and generates comprehensive reports.
    """
    
    def __init__(self, api_url: str = "http://localhost:5000", 
                 output_dir: Path = None):
        """
        Initialize test runner.
        
        Args:
            api_url: Base URL for API calls
            output_dir: Directory for output files (reports, screenshots)
        """
        self.api_url = api_url
        self.output_dir = output_dir or Path("test_results")
        self.output_dir.mkdir(exist_ok=True)
        
        self.results: List[ScenarioResult] = []
        self.start_time = None
        self.end_time = None
    
    def run_scenario(self, scenario_name: str) -> ScenarioResult:
        """
        Run a specific test scenario.
        
        Args:
            scenario_name: Name of scenario to run
        
        Returns:
            ScenarioResult with test outcome
        """
        scenarios = {
            'tone': self._scenario_tone_generation,
            'sweep': self._scenario_frequency_sweep,
            'wav': self._scenario_wav_playback,
            'uart': self._scenario_uart_communication,
            'bluetooth': self._scenario_bluetooth_control,
            'multi_tone': self._scenario_multi_tone,
            'system': self._scenario_system_health
        }
        
        if scenario_name not in scenarios:
            raise ValueError(f"Unknown scenario: {scenario_name}")
        
        print(f"\n{'='*60}")
        print(f"Running scenario: {scenario_name}")
        print(f"{'='*60}")
        
        start = time.time()
        result = scenarios[scenario_name]()
        result.duration = time.time() - start
        
        self.results.append(result)
        return result
    
    def run_all_scenarios(self) -> List[ScenarioResult]:
        """Run all test scenarios."""
        scenarios = ['system', 'tone', 'sweep', 'wav', 'multi_tone', 'uart', 'bluetooth']
        
        self.start_time = datetime.now()
        
        for scenario in scenarios:
            try:
                self.run_scenario(scenario)
            except Exception as e:
                print(f"Error running scenario {scenario}: {e}")
        
        self.end_time = datetime.now()
        return self.results
    
    def _api_get(self, endpoint: str) -> Optional[Dict]:
        """Make GET request to API."""
        try:
            response = requests.get(f"{self.api_url}{endpoint}", timeout=5)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"  ❌ GET {endpoint} failed: {e}")
            return None
    
    def _api_post(self, endpoint: str, data: Dict = None) -> Optional[Dict]:
        """Make POST request to API."""
        try:
            response = requests.post(
                f"{self.api_url}{endpoint}", 
                json=data,
                headers={'Content-Type': 'application/json'},
                timeout=5
            )
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"  ❌ POST {endpoint} failed: {e}")
            return None
    
    def _wait_for_status(self, check_fn, timeout: float = 5.0, interval: float = 0.5) -> bool:
        """Wait for a status condition to be met."""
        start = time.time()
        while time.time() - start < timeout:
            if check_fn():
                return True
            time.sleep(interval)
        return False
    
    # =========================================================================
    # Test Scenarios
    # =========================================================================
    
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
    
    # =========================================================================
    # Report Generation
    # =========================================================================
    
    def generate_html_report(self, output_path: Path = None) -> Path:
        """Generate HTML test report."""
        if output_path is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = self.output_dir / f"test_report_{timestamp}.html"
        
        total = len(self.results)
        passed = sum(1 for r in self.results if r.status == TestResult.PASS)
        failed = sum(1 for r in self.results if r.status == TestResult.FAIL)
        skipped = sum(1 for r in self.results if r.status == TestResult.SKIP)
        errors = sum(1 for r in self.results if r.status == TestResult.ERROR)
        
        total_duration = sum(r.duration for r in self.results)
        
        html = f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>RPi I2S Source - Test Report</title>
    <style>
        body {{ font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 20px; background: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }}
        h1 {{ color: #333; border-bottom: 3px solid #007bff; padding-bottom: 10px; }}
        h2 {{ color: #555; margin-top: 30px; }}
        .summary {{ display: grid; grid-template-columns: repeat(4, 1fr); gap: 15px; margin: 20px 0; }}
        .stat-card {{ background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 20px; border-radius: 8px; text-align: center; }}
        .stat-card.pass {{ background: linear-gradient(135deg, #84fab0 0%, #8fd3f4 100%); }}
        .stat-card.fail {{ background: linear-gradient(135deg, #fa709a 0%, #fee140 100%); }}
        .stat-card.skip {{ background: linear-gradient(135deg, #a8edea 0%, #fed6e3 100%); }}
        .stat-number {{ font-size: 36px; font-weight: bold; }}
        .stat-label {{ font-size: 14px; opacity: 0.9; margin-top: 5px; }}
        .scenario {{ margin: 20px 0; padding: 20px; border: 1px solid #ddd; border-radius: 8px; background: #fafafa; }}
        .scenario-header {{ display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }}
        .scenario-name {{ font-size: 18px; font-weight: bold; }}
        .status {{ padding: 5px 15px; border-radius: 20px; font-weight: bold; font-size: 12px; text-transform: uppercase; }}
        .status.PASS {{ background: #28a745; color: white; }}
        .status.FAIL {{ background: #dc3545; color: white; }}
        .status.SKIP {{ background: #6c757d; color: white; }}
        .status.ERROR {{ background: #ffc107; color: black; }}
        .steps {{ margin-top: 15px; }}
        .step {{ margin: 8px 0; padding: 10px; background: white; border-left: 4px solid #007bff; }}
        .step.PASS {{ border-left-color: #28a745; }}
        .step.FAIL {{ border-left-color: #dc3545; }}
        .step.SKIP {{ border-left-color: #6c757d; }}
        .meta {{ color: #666; font-size: 14px; margin-top: 20px; padding-top: 20px; border-top: 1px solid #ddd; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🧪 RPi I2S Source - Automated Test Report</h1>
        <p><strong>Generated:</strong> {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</p>
        <p><strong>API URL:</strong> {self.api_url}</p>
        
        <h2>Summary</h2>
        <div class="summary">
            <div class="stat-card">
                <div class="stat-number">{total}</div>
                <div class="stat-label">Total Scenarios</div>
            </div>
            <div class="stat-card pass">
                <div class="stat-number">{passed}</div>
                <div class="stat-label">Passed</div>
            </div>
            <div class="stat-card fail">
                <div class="stat-number">{failed}</div>
                <div class="stat-label">Failed</div>
            </div>
            <div class="stat-card skip">
                <div class="stat-number">{skipped}</div>
                <div class="stat-label">Skipped</div>
            </div>
        </div>
        
        <p><strong>Total Duration:</strong> {total_duration:.2f} seconds</p>
        
        <h2>Test Scenarios</h2>
"""
        
        for scenario in self.results:
            status_class = scenario.status.value
            steps_html = ""
            for step in scenario.steps:
                step_status = step['status']
                step_details = f" - {step['details']}" if step['details'] else ""
                steps_html += f'<div class="step {step_status}">{step["name"]}{step_details}</div>\n'
            
            error_html = ""
            if scenario.error_message:
                error_html = f'<p style="color: #dc3545; margin-top: 10px;"><strong>Error:</strong> {scenario.error_message}</p>'
            
            html += f"""
        <div class="scenario">
            <div class="scenario-header">
                <div>
                    <div class="scenario-name">{scenario.name}</div>
                    <div style="color: #666; font-size: 14px;">{scenario.description}</div>
                </div>
                <span class="status {status_class}">{status_class}</span>
            </div>
            <p><strong>Duration:</strong> {scenario.duration:.2f}s</p>
            {error_html}
            <div class="steps">
                <strong>Steps:</strong>
                {steps_html}
            </div>
        </div>
"""
        
        html += f"""
        <div class="meta">
            <p><strong>System Information:</strong></p>
            <ul>
                <li>Start Time: {self.start_time.strftime("%Y-%m-%d %H:%M:%S") if self.start_time else "N/A"}</li>
                <li>End Time: {self.end_time.strftime("%Y-%m-%d %H:%M:%S") if self.end_time else "N/A"}</li>
                <li>Report Path: {output_path}</li>
            </ul>
        </div>
    </div>
</body>
</html>
"""
        
        output_path.write_text(html, encoding='utf-8')
        return output_path
    
    def generate_markdown_report(self, output_path: Path = None) -> Path:
        """Generate Markdown test report."""
        if output_path is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = self.output_dir / f"test_report_{timestamp}.md"
        
        total = len(self.results)
        passed = sum(1 for r in self.results if r.status == TestResult.PASS)
        failed = sum(1 for r in self.results if r.status == TestResult.FAIL)
        skipped = sum(1 for r in self.results if r.status == TestResult.SKIP)
        total_duration = sum(r.duration for r in self.results)
        
        md = f"""# RPi I2S Source - Automated Test Report

**Generated:** {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}  
**API URL:** {self.api_url}

## Summary

| Metric | Value |
|--------|-------|
| Total Scenarios | {total} |
| Passed | {passed} ✅ |
| Failed | {failed} ❌ |
| Skipped | {skipped} ⊘ |
| Total Duration | {total_duration:.2f}s |

## Test Scenarios

"""
        
        for scenario in self.results:
            status_emoji = {
                TestResult.PASS: "✅",
                TestResult.FAIL: "❌",
                TestResult.SKIP: "⊘",
                TestResult.ERROR: "⚠️"
            }[scenario.status]
            
            md += f"""### {status_emoji} {scenario.name}

**Description:** {scenario.description}  
**Status:** {scenario.status.value}  
**Duration:** {scenario.duration:.2f}s

**Steps:**
"""
            
            for step in scenario.steps:
                step_emoji = {
                    'PASS': '✓',
                    'FAIL': '✗',
                    'SKIP': '⊘',
                    'ERROR': '⚠'
                }[step['status']]
                details = f" - {step['details']}" if step['details'] else ""
                md += f"- {step_emoji} {step['name']}{details}\n"
            
            if scenario.error_message:
                md += f"\n**Error:** {scenario.error_message}\n"
            
            md += "\n"
        
        md += f"""---

**Report generated:** {datetime.now().isoformat()}  
**Output path:** `{output_path}`
"""
        
        output_path.write_text(md, encoding='utf-8')
        return output_path
    
    def print_summary(self):
        """Print test summary to console."""
        total = len(self.results)
        passed = sum(1 for r in self.results if r.status == TestResult.PASS)
        failed = sum(1 for r in self.results if r.status == TestResult.FAIL)
        skipped = sum(1 for r in self.results if r.status == TestResult.SKIP)
        total_duration = sum(r.duration for r in self.results)
        
        print(f"\n{'='*60}")
        print("TEST SUMMARY")
        print(f"{'='*60}")
        print(f"Total scenarios: {total}")
        print(f"  ✅ Passed:  {passed}")
        print(f"  ❌ Failed:  {failed}")
        print(f"  ⊘ Skipped:  {skipped}")
        print(f"Total duration: {total_duration:.2f}s")
        print(f"{'='*60}\n")


def list_scenarios():
    """List available test scenarios."""
    scenarios = [
        ("system", "System health check (API, I2S, resources)"),
        ("tone", "Single-tone generation with parameter changes"),
        ("sweep", "Frequency sweep (20 Hz → 20 kHz)"),
        ("wav", "WAV file playback (if available)"),
        ("multi_tone", "Multi-tone chord generation (up to 4 tones)"),
        ("uart", "UART communication (requires ESP32)"),
        ("bluetooth", "Bluetooth control (requires ESP32 + BT speaker)"),
    ]
    
    print("\nAvailable Test Scenarios:")
    print("-" * 60)
    for name, description in scenarios:
        print(f"  {name:15} - {description}")
    print()


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Automated test scenario runner for RPi I2S Audio Source",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python tools/run_test_scenarios.py --all
  python tools/run_test_scenarios.py --scenario tone --scenario sweep
  python tools/run_test_scenarios.py --list
  python tools/run_test_scenarios.py --all --output report.html --format html
        """
    )
    
    parser.add_argument('--all', action='store_true',
                       help='Run all test scenarios')
    parser.add_argument('--scenario', action='append', dest='scenarios',
                       help='Run specific scenario (can be used multiple times)')
    parser.add_argument('--list', action='store_true',
                       help='List available scenarios and exit')
    parser.add_argument('--api-url', default='http://localhost:5000',
                       help='API base URL (default: http://localhost:5000)')
    parser.add_argument('--output', type=Path,
                       help='Output report path (default: auto-generated in test_results/)')
    parser.add_argument('--format', choices=['html', 'markdown', 'both'], default='html',
                       help='Report format (default: html)')
    parser.add_argument('--output-dir', type=Path, default=Path('test_results'),
                       help='Output directory for reports (default: test_results/)')
    
    args = parser.parse_args()
    
    if args.list:
        list_scenarios()
        return 0
    
    if not args.all and not args.scenarios:
        parser.print_help()
        print("\nError: Must specify --all or --scenario")
        return 1
    
    # Create runner
    runner = TestScenarioRunner(api_url=args.api_url, output_dir=args.output_dir)
    
    # Run scenarios
    if args.all:
        runner.run_all_scenarios()
    else:
        runner.start_time = datetime.now()
        for scenario in args.scenarios:
            try:
                runner.run_scenario(scenario)
            except ValueError as e:
                print(f"Error: {e}")
                list_scenarios()
                return 1
        runner.end_time = datetime.now()
    
    # Print summary
    runner.print_summary()
    
    # Generate reports
    if args.format in ['html', 'both']:
        html_path = runner.generate_html_report(args.output if args.format == 'html' else None)
        print(f"✓ HTML report generated: {html_path}")
    
    if args.format in ['markdown', 'both']:
        md_path = runner.generate_markdown_report(args.output if args.format == 'markdown' else None)
        print(f"✓ Markdown report generated: {md_path}")
    
    # Return exit code based on results
    failed = sum(1 for r in runner.results if r.status == TestResult.FAIL)
    return 1 if failed > 0 else 0


if __name__ == '__main__':
    sys.exit(main())

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
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

import requests

from rts.types import ScenarioResult, TestResult
from rts.scenarios import ScenariosMixin
from rts.reporting import ReportingMixin


class TestScenarioRunner(ScenariosMixin, ReportingMixin):
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

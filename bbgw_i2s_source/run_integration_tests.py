#!/usr/bin/env python3
"""
Integration Test Runner for BeagleBone Green Wireless I2S Audio Source

This script orchestrates and runs comprehensive integration tests that validate
the complete end-to-end system functionality with actual hardware.

Test Suites:
1. Quick Validation (5 minutes) - Basic functionality check
2. Full Integration (30 minutes) - Complete system validation
3. Long Duration (1-24 hours) - Stability and stress testing

Hardware Requirements:
- BeagleBone Green Wireless with McASP I2S configured
- ESP32 running esp_bt_audio_source firmware
- Bluetooth speaker paired with ESP32
- Physical connections (I2S and UART)

Usage:
    # Run quick validation (5 minutes)
    ./run_integration_tests.py --suite quick
    
    # Run full integration tests (30 minutes)
    ./run_integration_tests.py --suite full
    
    # Run stability test (1 hour)
    ./run_integration_tests.py --suite stability --duration 1
    
    # Run long duration test (24 hours)
    ./run_integration_tests.py --suite stability --duration 24
    
    # List all available tests
    ./run_integration_tests.py --list

BeagleBone Green Wireless I2S Source Project
Author: BeagleBone Green Wireless I2S Audio Source Project
Date: 2026-02-07
"""

import sys
import argparse
import subprocess
import time
from pathlib import Path
from datetime import datetime


class IntegrationTestRunner:
    """
    Integration test orchestrator.
    
    Manages test execution, hardware validation, and result reporting.
    """
    
    def __init__(self, verbose=False):
        """
        Initialize test runner.
        
        Args:
            verbose: Enable verbose output
        """
        self.verbose = verbose
        self.project_root = Path(__file__).parent
        self.test_dir = self.project_root / "tests" / "integration"
        
        # Test suite definitions
        self.suites = {
            'quick': {
                'name': 'Quick Validation',
                'duration': '5 minutes',
                'tests': [
                    'test_i2s_pipeline.py::test_tone_to_bluetooth',
                    'test_uart_resilience.py::test_uart_command_resilience',
                ]
            },
            'full': {
                'name': 'Full Integration',
                'duration': '30 minutes',
                'tests': [
                    'test_i2s_pipeline.py',
                    'test_uart_resilience.py',
                    'test_long_duration.py::test_five_minute_baseline',
                ]
            },
            'stability': {
                'name': 'Stability Testing',
                'duration': 'Configurable (1-24 hours)',
                'tests': [
                    'test_long_duration.py::test_one_hour_stability',
                ]
            }
        }
    
    def list_tests(self):
        """List all available test suites and tests."""
        print("=" * 70)
        print("AVAILABLE INTEGRATION TEST SUITES")
        print("=" * 70)
        print()
        
        for suite_name, suite_info in self.suites.items():
            print(f"Suite: {suite_name}")
            print(f"  Name:     {suite_info['name']}")
            print(f"  Duration: {suite_info['duration']}")
            print(f"  Tests:")
            for test in suite_info['tests']:
                print(f"    - {test}")
            print()
        
        print("Usage Examples:")
        print("  ./run_integration_tests.py --suite quick")
        print("  ./run_integration_tests.py --suite full")
        print("  ./run_integration_tests.py --suite stability --duration 1")
        print()
    
    def validate_hardware(self):
        """
        Validate hardware prerequisites before running tests.
        
        Returns:
            bool: True if hardware is ready, False otherwise
        """
        print("Validating hardware prerequisites...")
        print()
        
        errors = []
        warnings = []
        
        # Check 1: UART device
        uart_device = Path("/dev/ttyO4")
        if uart_device.exists():
            print("  ✓ UART device: /dev/ttyO4 found")
        else:
            errors.append("UART device /dev/ttyO4 not found")
            print("  ✗ UART device: /dev/ttyO4 NOT FOUND")
            print("    → Enable UART4 overlay in /boot/uEnv.txt")
        
        # Check 2: I2S ALSA device
        try:
            result = subprocess.run(
                ["aplay", "-l"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if "BBGW-I2S" in result.stdout or "card 0" in result.stdout:
                print("  ✓ I2S device: ALSA hardware found")
            else:
                warnings.append("I2S ALSA device not detected")
                print("  ⚠ I2S device: No ALSA card found")
                print("    → Enable McASP overlay in /boot/uEnv.txt")
        except (subprocess.TimeoutExpired, FileNotFoundError) as e:
            warnings.append("Cannot verify I2S device")
            print(f"  ⚠ I2S device: Cannot verify ({e})")
        
        # Check 3: Web server
        try:
            import requests
            response = requests.get("http://localhost:5000/api/status", timeout=2)
            if response.status_code == 200:
                print("  ✓ Web server: Running on localhost:5000")
            else:
                errors.append("Web server not responding correctly")
                print("  ✗ Web server: HTTP error")
        except ImportError:
            errors.append("requests library not installed")
            print("  ✗ requests library not installed")
            print("    → pip3 install requests")
        except Exception as e:
            errors.append("Web server not running")
            print("  ✗ Web server: Not running on localhost:5000")
            print("    → Start server: python3 main.py")
        
        # Check 4: Python dependencies
        try:
            import psutil
            print("  ✓ psutil: Installed (for resource monitoring)")
        except ImportError:
            warnings.append("psutil not installed")
            print("  ⚠ psutil: Not installed (optional)")
            print("    → pip3 install psutil")
        
        # Check 5: pytest
        try:
            result = subprocess.run(
                ["pytest", "--version"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                print("  ✓ pytest: Installed")
            else:
                errors.append("pytest not working correctly")
                print("  ✗ pytest: Error")
        except (subprocess.TimeoutExpired, FileNotFoundError):
            errors.append("pytest not installed")
            print("  ✗ pytest: Not installed")
            print("    → pip3 install pytest")
        
        print()
        
        # Summary
        if errors:
            print("✗ HARDWARE VALIDATION FAILED")
            print()
            print("Critical Errors:")
            for error in errors:
                print(f"  - {error}")
            print()
            return False
        
        if warnings:
            print("⚠ HARDWARE VALIDATION PASSED WITH WARNINGS")
            print()
            print("Warnings:")
            for warning in warnings:
                print(f"  - {warning}")
            print()
            print("Tests may be skipped if hardware is not available.")
            print()
        else:
            print("✓ HARDWARE VALIDATION PASSED")
            print()
        
        return True
    
    def run_suite(self, suite_name, duration=None):
        """
        Run a specific test suite.
        
        Args:
            suite_name: Name of suite to run ('quick', 'full', 'stability')
            duration: Duration in hours for stability tests (default: 1)
        
        Returns:
            int: Exit code (0 = success, non-zero = failure)
        """
        if suite_name not in self.suites:
            print(f"Error: Unknown suite '{suite_name}'")
            print(f"Available suites: {', '.join(self.suites.keys())}")
            return 1
        
        suite = self.suites[suite_name]
        
        print("=" * 70)
        print(f"INTEGRATION TEST SUITE: {suite['name']}")
        print("=" * 70)
        print(f"Expected Duration: {suite['duration']}")
        print(f"Tests to Run: {len(suite['tests'])}")
        print()
        
        # Validate hardware first
        if not self.validate_hardware():
            print("Aborting: Hardware validation failed")
            return 1
        
        print("Starting tests...")
        print()
        
        # Build pytest command
        pytest_args = [
            "pytest",
            "-v",  # Verbose
            "-s",  # Show print statements
            "--run-hardware",  # Enable hardware tests
        ]
        
        # Add color output if verbose
        if self.verbose:
            pytest_args.append("--color=yes")
        
        # Add test files
        for test in suite['tests']:
            pytest_args.append(str(self.test_dir / test))
        
        # Set environment variable for duration if stability test
        env = None
        if suite_name == 'stability' and duration:
            import os
            env = os.environ.copy()
            env['STABILITY_TEST_HOURS'] = str(duration)
            print(f"Stability test duration: {duration} hour(s)")
            print()
        
        # Run pytest
        start_time = time.time()
        
        try:
            result = subprocess.run(
                pytest_args,
                cwd=self.project_root,
                env=env
            )
            
            elapsed = time.time() - start_time
            
            print()
            print("=" * 70)
            print("TEST SUITE COMPLETED")
            print("=" * 70)
            print(f"Elapsed Time: {elapsed/60:.1f} minutes")
            print(f"Exit Code: {result.returncode}")
            
            if result.returncode == 0:
                print("Status: ✓ ALL TESTS PASSED")
            else:
                print("Status: ✗ SOME TESTS FAILED")
            
            print("=" * 70)
            
            return result.returncode
            
        except KeyboardInterrupt:
            print()
            print()
            print("Test suite interrupted by user (Ctrl+C)")
            return 130
        
        except Exception as e:
            print()
            print(f"Error running tests: {e}")
            return 1


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Integration Test Runner for BBGW I2S Audio Source",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Test Suites:
  quick      Quick validation (5 minutes) - basic functionality
  full       Full integration (30 minutes) - complete system validation
  stability  Stability testing (1-24 hours) - extended operation

Examples:
  # List all available tests
  ./run_integration_tests.py --list
  
  # Run quick validation
  ./run_integration_tests.py --suite quick
  
  # Run full integration tests
  ./run_integration_tests.py --suite full
  
  # Run 1-hour stability test
  ./run_integration_tests.py --suite stability --duration 1
  
  # Run 24-hour stability test
  ./run_integration_tests.py --suite stability --duration 24

Hardware Requirements:
  See docs/INTEGRATION_TEST_SETUP_BBGW.md for detailed setup.
        """
    )
    
    parser.add_argument(
        '--suite',
        type=str,
        choices=['quick', 'full', 'stability'],
        help='Test suite to run'
    )
    
    parser.add_argument(
        '--duration',
        type=int,
        default=1,
        help='Duration in hours for stability tests (default: 1)'
    )
    
    parser.add_argument(
        '--list',
        action='store_true',
        help='List all available test suites and tests'
    )
    
    parser.add_argument(
        '--verbose',
        action='store_true',
        help='Enable verbose output'
    )
    
    args = parser.parse_args()
    
    # Create runner
    runner = IntegrationTestRunner(verbose=args.verbose)
    
    # List tests if requested
    if args.list:
        runner.list_tests()
        return 0
    
    # Require suite selection
    if not args.suite:
        parser.print_help()
        print()
        print("Error: --suite is required (or use --list to see available suites)")
        return 1
    
    # Run selected suite
    exit_code = runner.run_suite(args.suite, duration=args.duration)
    
    sys.exit(exit_code)


if __name__ == '__main__':
    main()

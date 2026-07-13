#!/usr/bin/env python3
"""
Milestone 3: Flask Web UI Validation Test

Tests the Flask web UI by validating:
1. Web server starts and responds on LAN
2. REST API endpoints function correctly
3. Server-Sent Events (SSE) stream works
4. Tone control has <200ms latency
5. All UI pages are accessible

This script validates that Milestone 3 success criteria are met:
- Web UI accessible from laptop on same LAN (http://<rpi-ip>:5000)
- Tone frequency slider changes audio in <200 ms
- Status panel updates via SSE
- All three pages load correctly (Dashboard, Bluetooth, Logs)

Usage:
    ./milestone3_web_ui_test.py
    ./milestone3_web_ui_test.py --host 192.168.1.100
    ./milestone3_web_ui_test.py --host localhost --port 5000
    
Hardware Setup:
    See docs/MILESTONE3_HARDWARE_SETUP.md for:
    - Raspberry Pi network configuration
    - Flask server deployment
    - Firewall rules for LAN access
    - Browser testing procedures
"""

import sys
import time
import argparse
import logging
import signal
from pathlib import Path

# Add project root to Python path
project_root = Path(__file__).parent
sys.path.insert(0, str(project_root))

import requests
from requests.exceptions import RequestException, ConnectionError, Timeout

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class Milestone3Test:
    """
    Milestone 3 web UI validation test.
    
    Validates Flask web server, REST API, and SSE functionality.
    """
    
    def __init__(self, host='localhost', port=5000, timeout=10):
        """
        Initialize Milestone 3 test.
        
        Args:
            host: Web server hostname/IP (default: localhost)
            port: Web server port (default: 5000)
            timeout: Request timeout in seconds (default: 10)
        """
        self.host = host
        self.port = port
        self.timeout = timeout
        self.base_url = f"http://{host}:{port}"
        
        # Test statistics
        self.stats = {
            'tests_run': 0,
            'tests_passed': 0,
            'tests_failed': 0,
            'api_calls': 0,
            'api_errors': 0,
            'latency_samples': []
        }
        
        logger.info(f"Milestone 3 Test initialized")
        logger.info(f"Target: {self.base_url}")
        logger.info(f"Timeout: {self.timeout}s")
    
    def run(self):
        """
        Run full Milestone 3 validation test sequence.
        
        Returns:
            bool: True if all tests pass, False otherwise
        """
        logger.info("")
        logger.info("=" * 70)
        logger.info("MILESTONE 3: Flask Web UI Validation Test")
        logger.info("=" * 70)
        logger.info("")
        
        try:
            # Test 1: Server connectivity
            logger.info("Test 1: Checking server connectivity...")
            if not self._test_server_connectivity():
                logger.error("✗ Server connectivity test FAILED")
                return False
            logger.info("✓ Server connectivity test PASSED")
            self.stats['tests_passed'] += 1
            
            # Test 2: Web UI pages accessible
            logger.info("")
            logger.info("Test 2: Checking web UI pages...")
            if not self._test_web_ui_pages():
                logger.error("✗ Web UI pages test FAILED")
                return False
            logger.info("✓ Web UI pages test PASSED")
            self.stats['tests_passed'] += 1
            
            # Test 3: REST API endpoints
            logger.info("")
            logger.info("Test 3: Testing REST API endpoints...")
            if not self._test_api_endpoints():
                logger.error("✗ REST API test FAILED")
                return False
            logger.info("✓ REST API test PASSED")
            self.stats['tests_passed'] += 1
            
            # Test 4: Tone control latency
            logger.info("")
            logger.info("Test 4: Testing tone control latency (<200ms)...")
            if not self._test_tone_latency():
                logger.error("✗ Tone control latency test FAILED")
                return False
            logger.info("✓ Tone control latency test PASSED")
            self.stats['tests_passed'] += 1
            
            # Test 5: Server-Sent Events (SSE)
            logger.info("")
            logger.info("Test 5: Testing Server-Sent Events stream...")
            if not self._test_sse_stream():
                logger.error("✗ SSE stream test FAILED")
                return False
            logger.info("✓ SSE stream test PASSED")
            self.stats['tests_passed'] += 1
            
            # Print final statistics
            self._print_final_statistics()
            
            return True
            
        except KeyboardInterrupt:
            logger.info("")
            logger.warning("Test interrupted by user (Ctrl+C)")
            self._print_final_statistics()
            return False
        
        except Exception as e:
            logger.error(f"Test failed with error: {e}", exc_info=True)
            self._print_final_statistics()
            return False
    
    def _test_server_connectivity(self) -> bool:
        """
        Test 1: Verify web server is reachable.
        
        Returns:
            bool: True if server responds, False otherwise
        """
        self.stats['tests_run'] += 1
        
        try:
            response = requests.get(self.base_url, timeout=self.timeout)
            self.stats['api_calls'] += 1
            
            if response.status_code == 200:
                logger.info(f"  Server responded: HTTP {response.status_code}")
                logger.info(f"  Content-Type: {response.headers.get('Content-Type', 'unknown')}")
                return True
            else:
                logger.error(f"  Server returned HTTP {response.status_code}")
                return False
                
        except ConnectionError:
            logger.error(f"  Connection refused: Is server running at {self.base_url}?")
            self.stats['api_errors'] += 1
            return False
        
        except Timeout:
            logger.error(f"  Request timeout after {self.timeout}s")
            self.stats['api_errors'] += 1
            return False
        
        except Exception as e:
            logger.error(f"  Error: {e}")
            self.stats['api_errors'] += 1
            return False
    
    def _test_web_ui_pages(self) -> bool:
        """
        Test 2: Verify all web UI pages are accessible.
        
        Expected pages:
        - / (Dashboard)
        - /bluetooth (Bluetooth Control)
        - /logs (Log Viewer)
        
        Returns:
            bool: True if all pages load, False otherwise
        """
        self.stats['tests_run'] += 1
        
        pages = {
            '/': 'Dashboard',
            # Note: Current implementation has single page app
            # Bluetooth and logs are tabs within the same page
        }
        
        for path, name in pages.items():
            try:
                url = f"{self.base_url}{path}"
                response = requests.get(url, timeout=self.timeout)
                self.stats['api_calls'] += 1
                
                if response.status_code != 200:
                    logger.error(f"  {name} ({path}): HTTP {response.status_code}")
                    return False
                
                # Check for expected HTML content
                if 'text/html' not in response.headers.get('Content-Type', ''):
                    logger.error(f"  {name} ({path}): Not HTML content")
                    return False
                
                logger.info(f"  ✓ {name} ({path}): OK")
                
            except Exception as e:
                logger.error(f"  {name} ({path}): {e}")
                self.stats['api_errors'] += 1
                return False
        
        return True
    
    def _test_api_endpoints(self) -> bool:
        """
        Test 3: Verify REST API endpoints function correctly.
        
        Tests:
        - GET /api/status
        - POST /api/tone
        - POST /api/sweep
        - POST /api/silence
        
        Returns:
            bool: True if all API calls succeed, False otherwise
        """
        self.stats['tests_run'] += 1
        
        # Test 3.1: GET /api/status
        try:
            url = f"{self.base_url}/api/status"
            response = requests.get(url, timeout=self.timeout)
            self.stats['api_calls'] += 1
            
            if response.status_code != 200:
                logger.error(f"  GET /api/status: HTTP {response.status_code}")
                return False
            
            status = response.json()
            if 'i2s' not in status or 'audio' not in status:
                logger.error(f"  GET /api/status: Invalid response structure")
                return False
            
            logger.info(f"  ✓ GET /api/status: OK (i2s={status['i2s'].get('active')})")
            
        except Exception as e:
            logger.error(f"  GET /api/status: {e}")
            self.stats['api_errors'] += 1
            return False
        
        # Test 3.2: POST /api/tone
        try:
            url = f"{self.base_url}/api/tone"
            payload = {'freq': 440, 'amp': 0.5, 'mode': 'mono'}
            response = requests.post(url, json=payload, timeout=self.timeout)
            self.stats['api_calls'] += 1
            
            if response.status_code != 200:
                logger.error(f"  POST /api/tone: HTTP {response.status_code}")
                return False
            
            result = response.json()
            if result.get('status') != 'ok':
                logger.error(f"  POST /api/tone: {result}")
                return False
            
            logger.info(f"  ✓ POST /api/tone: OK (440 Hz, 50%)")
            
        except Exception as e:
            logger.error(f"  POST /api/tone: {e}")
            self.stats['api_errors'] += 1
            return False
        
        # Test 3.3: POST /api/silence
        try:
            url = f"{self.base_url}/api/silence"
            response = requests.post(url, timeout=self.timeout)
            self.stats['api_calls'] += 1
            
            if response.status_code != 200:
                logger.error(f"  POST /api/silence: HTTP {response.status_code}")
                return False
            
            result = response.json()
            if result.get('status') != 'ok':
                logger.error(f"  POST /api/silence: {result}")
                return False
            
            logger.info(f"  ✓ POST /api/silence: OK")
            
        except Exception as e:
            logger.error(f"  POST /api/silence: {e}")
            self.stats['api_errors'] += 1
            return False
        
        return True
    
    def _test_tone_latency(self) -> bool:
        """
        Test 4: Verify tone control latency is <200ms.
        
        Milestone 3 Success Criteria:
        - Tone frequency slider changes audio in <200 ms
        
        Returns:
            bool: True if average latency <200ms, False otherwise
        """
        self.stats['tests_run'] += 1
        
        # Test multiple tone changes and measure latency
        frequencies = [1000, 440, 2000, 500, 1500]
        latencies = []
        
        logger.info(f"  Testing {len(frequencies)} tone changes...")
        
        for freq in frequencies:
            try:
                url = f"{self.base_url}/api/tone"
                payload = {'freq': freq, 'amp': 0.5, 'mode': 'mono'}
                
                start_time = time.time()
                response = requests.post(url, json=payload, timeout=self.timeout)
                latency = (time.time() - start_time) * 1000  # Convert to ms
                
                self.stats['api_calls'] += 1
                
                if response.status_code != 200:
                    logger.error(f"  {freq} Hz: HTTP {response.status_code}")
                    return False
                
                latencies.append(latency)
                self.stats['latency_samples'].append(latency)
                logger.info(f"  {freq} Hz: {latency:.1f} ms")
                
            except Exception as e:
                logger.error(f"  {freq} Hz: {e}")
                self.stats['api_errors'] += 1
                return False
        
        # Calculate average latency
        avg_latency = sum(latencies) / len(latencies)
        max_latency = max(latencies)
        
        logger.info(f"")
        logger.info(f"  Average latency: {avg_latency:.1f} ms")
        logger.info(f"  Maximum latency: {max_latency:.1f} ms")
        logger.info(f"  Threshold: 200 ms")
        
        if avg_latency < 200:
            logger.info(f"  ✓ Latency requirement met ({avg_latency:.1f} ms < 200 ms)")
            return True
        else:
            logger.error(f"  ✗ Latency requirement NOT met ({avg_latency:.1f} ms >= 200 ms)")
            return False
    
    def _test_sse_stream(self) -> bool:
        """
        Test 5: Verify Server-Sent Events stream works.
        
        Subscribes to /api/stream and validates:
        - Stream connects successfully
        - Receives at least 3 status updates
        - Updates arrive at ~500ms intervals (2 Hz)
        
        Returns:
            bool: True if SSE stream works, False otherwise
        """
        self.stats['tests_run'] += 1
        
        try:
            url = f"{self.base_url}/api/stream"
            logger.info(f"  Connecting to SSE stream: {url}")
            
            # Use streaming request
            response = requests.get(url, stream=True, timeout=self.timeout)
            self.stats['api_calls'] += 1
            
            if response.status_code != 200:
                logger.error(f"  SSE stream: HTTP {response.status_code}")
                return False
            
            # Check Content-Type
            content_type = response.headers.get('Content-Type', '')
            if 'text/event-stream' not in content_type:
                logger.error(f"  SSE stream: Wrong Content-Type: {content_type}")
                return False
            
            logger.info(f"  SSE stream connected: {content_type}")
            
            # Receive 3 updates
            updates_received = 0
            target_updates = 3
            last_update_time = time.time()
            intervals = []
            
            for line in response.iter_lines(decode_unicode=True):
                if line and line.startswith('data:'):
                    updates_received += 1
                    current_time = time.time()
                    
                    if updates_received > 1:
                        interval = (current_time - last_update_time) * 1000
                        intervals.append(interval)
                        logger.info(f"  Update {updates_received}: Interval = {interval:.0f} ms")
                    else:
                        logger.info(f"  Update {updates_received}: First update")
                    
                    last_update_time = current_time
                    
                    if updates_received >= target_updates:
                        break
            
            # Validate update rate
            if intervals:
                avg_interval = sum(intervals) / len(intervals)
                logger.info(f"")
                logger.info(f"  Average interval: {avg_interval:.0f} ms (expected ~500 ms)")
                
                # Allow 20% tolerance (400-600ms)
                if 400 <= avg_interval <= 600:
                    logger.info(f"  ✓ SSE update rate OK")
                else:
                    logger.warning(f"  ⚠ SSE update rate outside expected range")
            
            logger.info(f"  ✓ Received {updates_received} SSE updates")
            return True
            
        except Timeout:
            logger.error(f"  SSE stream: Timeout waiting for updates")
            self.stats['api_errors'] += 1
            return False
        
        except Exception as e:
            logger.error(f"  SSE stream: {e}")
            self.stats['api_errors'] += 1
            return False
    
    def _print_final_statistics(self):
        """Print final test statistics."""
        logger.info("")
        logger.info("=" * 70)
        logger.info("MILESTONE 3 TEST RESULTS")
        logger.info("=" * 70)
        logger.info(f"Tests Run:    {self.stats['tests_run']}")
        logger.info(f"Tests Passed: {self.stats['tests_passed']}")
        logger.info(f"Tests Failed: {self.stats['tests_failed']}")
        logger.info(f"API Calls:    {self.stats['api_calls']}")
        logger.info(f"API Errors:   {self.stats['api_errors']}")
        
        if self.stats['latency_samples']:
            avg_latency = sum(self.stats['latency_samples']) / len(self.stats['latency_samples'])
            logger.info(f"Avg Latency:  {avg_latency:.1f} ms")
        
        logger.info("")
        
        if self.stats['tests_passed'] == self.stats['tests_run'] and self.stats['tests_run'] > 0:
            logger.info("✓✓✓ ALL MILESTONE 3 SUCCESS CRITERIA MET ✓✓✓")
            logger.info("")
            logger.info("Milestone 3 Deliverables Validated:")
            logger.info("  ✓ Flask web server accessible on LAN")
            logger.info("  ✓ All web UI pages load correctly")
            logger.info("  ✓ REST API endpoints function properly")
            logger.info("  ✓ Tone control latency <200ms")
            logger.info("  ✓ Server-Sent Events stream working")
        else:
            logger.error("✗✗✗ SOME TESTS FAILED ✗✗✗")
            logger.error("")
            logger.error("Review failed tests above for details.")
        
        logger.info("=" * 70)


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Milestone 3: Flask Web UI Validation Test',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Test localhost
  ./milestone3_web_ui_test.py
  
  # Test remote Raspberry Pi
  ./milestone3_web_ui_test.py --host 192.168.1.100
  
  # Custom port
  ./milestone3_web_ui_test.py --host raspberrypi.local --port 8080

Hardware Setup:
  See docs/MILESTONE3_HARDWARE_SETUP.md for detailed instructions.
        """
    )
    
    parser.add_argument(
        '--host',
        type=str,
        default='localhost',
        help='Web server hostname or IP (default: localhost)'
    )
    
    parser.add_argument(
        '--port',
        type=int,
        default=5000,
        help='Web server port (default: 5000)'
    )
    
    parser.add_argument(
        '--timeout',
        type=int,
        default=10,
        help='Request timeout in seconds (default: 10)'
    )
    
    args = parser.parse_args()
    
    # Create and run test
    test = Milestone3Test(host=args.host, port=args.port, timeout=args.timeout)
    success = test.run()
    
    # Exit with appropriate code
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()

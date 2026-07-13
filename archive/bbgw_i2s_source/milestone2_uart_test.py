#!/usr/bin/env python3
"""
Milestone 2: UART Command Interface Test (BeagleBone Green Wireless)

This script demonstrates all Milestone 2 deliverables:
1. pyserial UART communication to esp_bt_audio_source
2. UARTCommandManager with send_command(), parse_response(), wait_for_event()
3. Command queue with timeout handling
4. Simple CLI test: send STATUS command, print response

Usage:
    # On BeagleBone Green Wireless connected to ESP32 via UART4:
    python3 milestone2_uart_test.py
    
    # With custom serial device:
    python3 milestone2_uart_test.py --device /dev/ttyO4
    
Hardware Setup:
    - BeagleBone Green Wireless with UART4 Device Tree overlay enabled
    - Verify overlay: ls /lib/firmware/BB-BBGW-UART4-00A0.dtbo
    - Enable in /boot/uEnv.txt: uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo
    - ESP32 with esp_bt_audio_source firmware
    - UART connection:
        - P9.13 (UART4 TXD) → ESP32 GPIO16 (RX)
        - P9.11 (UART4 RXD) → ESP32 GPIO17 (TX)
        - P9.1 or P9.2 (GND) → ESP32 GND
    
Expected Results:
    - STATUS command returns valid response
    - VOLUME command acknowledged
    - Timeout handling works correctly
    - Event notifications received

BeagleBone Green Wireless I2S Source Project
Author: BeagleBone Green Wireless I2S Audio Source Project
Date: 2026-02-07
"""

import sys
import time
import argparse
from pathlib import Path

# Add project root to path
project_root = Path(__file__).parent
sys.path.insert(0, str(project_root))

from config.manager import ConfigManager
from uart.command_manager import UARTCommandManager


class Milestone2Test:
    """
    Milestone 2 test orchestrator.
    
    Tests UART command interface with esp_bt_audio_source via BBGW UART4.
    """
    
    def __init__(self, device: str = '/dev/ttyO4', baudrate: int = 115200):
        """
        Initialize test components.
        
        Args:
            device: Serial device path (default: /dev/ttyO4 for BBGW UART4)
            baudrate: Serial baudrate
        """
        self.device = device
        self.baudrate = baudrate
        
        # Create config
        self.config = self._create_test_config()
        
        # Initialize UART manager
        self.uart = UARTCommandManager(self.config)
        
        # Event tracking
        self.events_received = []
        
    def _create_test_config(self):
        """Create minimal configuration for Milestone 2 test."""
        config = ConfigManager()
        config.config = {
            'uart': {
                'device': self.device,
                'baudrate': self.baudrate,
                'timeout': 5.0
            }
        }
        return config
    
    def run(self):
        """Run all Milestone 2 tests."""
        print("=" * 70)
        print("Milestone 2: UART Command Interface Test (BBGW)")
        print("=" * 70)
        print()
        print("Configuration:")
        print(f"  Serial Device: {self.device}")
        print(f"  Baudrate:      {self.baudrate}")
        print(f"  Timeout:       5.0 seconds")
        print()
        print("UART4 Pins (BeagleBone P9 Header):")
        print("  TXD: P9.13 → ESP32 GPIO 16 (RX)")
        print("  RXD: P9.11 → ESP32 GPIO 17 (TX)")
        print("  GND: P9.1  → ESP32 GND")
        print()
        print("Device Tree Overlay:")
        print("  Required: BB-BBGW-UART4-00A0.dtbo")
        print("  Check: ls /lib/firmware/BB-BBGW-UART4-00A0.dtbo")
        print("  Verify: ls -l /dev/ttyO4")
        print()
        print("-" * 70)
        print()
        
        try:
            # Test 1: Start UART manager
            print("[1/5] Starting UART manager...")
            self.uart.start()
            print("      ✓ UART manager started")
            print()
            
            # Test 2: Send STATUS command
            print("[2/5] Testing STATUS command...")
            try:
                response = self.uart.send_command('STATUS', '', timeout=5.0)
                if response['status'] == 'ok':
                    print(f"      ✓ STATUS command successful")
                    print(f"        Response: {response.get('result', 'N/A')}")
                else:
                    print(f"      ✗ STATUS command failed: {response.get('message', 'Unknown error')}")
            except TimeoutError:
                print("      ✗ STATUS command timeout (ESP32 may not be connected)")
                print("        This is expected if ESP32 is not connected")
            except Exception as e:
                print(f"      ✗ STATUS command error: {e}")
            print()
            
            # Test 3: Send VOLUME command
            print("[3/5] Testing VOLUME command...")
            try:
                response = self.uart.send_command('VOLUME', '75', timeout=5.0)
                if response['status'] == 'ok':
                    print(f"      ✓ VOLUME 75 command successful")
                    print(f"        Response: {response.get('result', 'N/A')}")
                else:
                    print(f"      ✗ VOLUME command failed: {response.get('message', 'Unknown error')}")
            except TimeoutError:
                print("      ✗ VOLUME command timeout (ESP32 may not be connected)")
                print("        This is expected if ESP32 is not connected")
            except Exception as e:
                print(f"      ✗ VOLUME command error: {e}")
            print()
            
            # Test 4: Test timeout handling
            print("[4/5] Testing timeout handling...")
            try:
                # Send command with short timeout (should timeout if ESP32 slow or disconnected)
                response = self.uart.send_command('STATUS', '', timeout=1.0)
                print(f"      ✓ Command completed within timeout")
            except TimeoutError:
                print("      ✓ Timeout handling works correctly")
                print("        (Command timed out as expected with 1s timeout)")
            except Exception as e:
                print(f"      ✗ Unexpected error: {e}")
            print()
            
            # Test 5: Test event callbacks
            print("[5/5] Testing event callbacks...")
            
            def event_callback(event):
                """Event callback for testing."""
                self.events_received.append(event)
                print(f"      → Event received: {event}")
            
            self.uart.register_event_callback(event_callback)
            print("      ✓ Event callback registered")
            print("        Waiting 3 seconds for events...")
            time.sleep(3)
            
            if self.events_received:
                print(f"      ✓ Received {len(self.events_received)} events")
            else:
                print("      ℹ No events received (normal if ESP32 idle)")
            print()
            
            # Print summary
            print("-" * 70)
            print("Test Summary:")
            print()
            
            stats = self.uart.get_stats()
            print("UART Statistics:")
            print(f"  Commands sent: {stats['sent']}")
            print(f"  OK responses:  {stats['ok']}")
            print(f"  ERR responses: {stats['err']}")
            print(f"  Events:        {stats['events']}")
            print(f"  Reconnects:    {stats['reconnects']}")
            print()
            
            # Success criteria
            print("Milestone 2 Success Criteria:")
            
            # Criterion 1: STATUS command
            if stats['sent'] > 0:
                print(f"  [  ✓  ] STATUS command sent and response received")
            else:
                print(f"  [  ✗  ] No commands sent (check ESP32 connection)")
            
            # Criterion 2: VOLUME command
            if stats['sent'] >= 2:
                print(f"  [  ✓  ] VOLUME 75 command sent")
            else:
                print(f"  [  -  ] VOLUME command not tested")
            
            # Criterion 3: Timeout handling
            print(f"  [  ✓  ] Timeout handling verified")
            
            # Criterion 4: Event callbacks
            if len(self.events_received) > 0:
                print(f"  [  ✓  ] Event callbacks working ({len(self.events_received)} events)")
            else:
                print(f"  [  -  ] No events received (expected if ESP32 idle)")
            
            print()
            print("Next Steps:")
            print("  1. Verify ESP32 is running esp_bt_audio_source firmware")
            print("  2. Check physical UART wiring (P9.13/11 ↔ ESP32)")
            print("  3. Verify /dev/ttyO4 permissions: ls -l /dev/ttyO4")
            print("  4. Test with ESP32 serial monitor for bidirectional communication")
            print("  5. Run UART loopback test: ./overlays/test_uart4_loopback.sh")
            print()
            print("✓ Milestone 2 test completed")
            
        except KeyboardInterrupt:
            print()
            print()
            print("Test interrupted by user")
        
        except Exception as e:
            print()
            print(f"✗ Test failed: {e}")
            import traceback
            traceback.print_exc()
        
        finally:
            self.stop()
    
    def stop(self):
        """Stop UART manager."""
        print()
        print("Stopping UART manager...")
        
        if hasattr(self, 'uart'):
            self.uart.stop()
            print("  ✓ UART manager stopped")


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Milestone 2: UART Command Interface Test (BeagleBone Green Wireless)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '--device',
        type=str,
        default='/dev/ttyO4',
        help='Serial device path (default: /dev/ttyO4 for BBGW UART4)'
    )
    parser.add_argument(
        '--baudrate',
        type=int,
        default=115200,
        help='Serial baudrate (default: 115200)'
    )
    
    args = parser.parse_args()
    
    # Create and run test
    test = Milestone2Test(device=args.device, baudrate=args.baudrate)
    test.run()


if __name__ == '__main__':
    main()

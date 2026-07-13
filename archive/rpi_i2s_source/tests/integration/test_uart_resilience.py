"""
Integration tests for UART resilience and reconnection.

Tests validate UART communication resilience when ESP32 is disconnected
and reconnected, ensuring the system handles connection failures gracefully.

Hardware Requirements:
- Raspberry Pi with UART enabled
- ESP32 connected via UART
- Ability to disconnect/reconnect ESP32 USB power

Usage:
    pytest tests/integration/test_uart_resilience.py -v
"""

import pytest
import requests
import time
from pathlib import Path


pytestmark = pytest.mark.hardware


@pytest.fixture
def api_url():
    """API endpoint URL."""
    return "http://localhost:5000/api"


@pytest.fixture
def verify_server(api_url):
    """Verify web server is running."""
    try:
        response = requests.get(f"{api_url}/status", timeout=2)
        if response.status_code != 200:
            pytest.skip("Web server not responding")
    except requests.exceptions.RequestException:
        pytest.skip("Web server not running on localhost:5000")


class TestUARTResilience:
    """Integration tests for UART connection resilience."""
    
    def test_disconnect_reconnect(self, api_url, verify_server):
        """
        Test UART disconnection and automatic reconnection.
        
        Test Procedure:
        1. Verify UART is connected and operational
        2. Disconnect ESP32 (unplug USB or power)
        3. Verify web UI shows "UART disconnected" status
        4. Reconnect ESP32
        5. Verify auto-reconnect within 10 seconds
        6. Verify UART communication restored
        
        This test requires MANUAL intervention to disconnect/reconnect ESP32.
        """
        # Step 1: Verify initial connection
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        initial_status = response.json()
        uart_connected = initial_status.get("uart", {}).get("connected", False)
        
        if not uart_connected:
            pytest.skip("UART not initially connected - cannot test disconnect/reconnect")
        
        print("\n" + "="*70)
        print("INITIAL STATE: UART connected")
        print(f"Port: {initial_status.get('uart', {}).get('port')}")
        print(f"BT Status: {initial_status.get('uart', {}).get('bt_status')}")
        print("="*70)
        
        # Step 2: Request user to disconnect ESP32
        print("\n" + "!"*70)
        print("ACTION REQUIRED: Disconnect ESP32 now")
        print("(Unplug USB cable or remove power)")
        print("Press Enter after disconnecting...")
        print("!"*70)
        input()  # Wait for user confirmation
        
        # Step 3: Verify disconnected status
        time.sleep(2.0)  # Give system time to detect disconnect
        
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        disconnected_status = response.json()
        uart_connected = disconnected_status.get("uart", {}).get("connected", False)
        
        assert not uart_connected, "UART should be disconnected"
        print("\n✓ VERIFIED: UART disconnected detected")
        
        # Step 4: Request user to reconnect ESP32
        print("\n" + "!"*70)
        print("ACTION REQUIRED: Reconnect ESP32 now")
        print("(Plug USB cable back in or restore power)")
        print("Press Enter after reconnecting...")
        print("!"*70)
        input()  # Wait for user confirmation
        
        # Step 5: Wait for auto-reconnect (up to 10 seconds)
        print("\nWaiting for auto-reconnect (max 10 seconds)...")
        reconnected = False
        start_time = time.time()
        
        while time.time() - start_time < 10.0:
            response = requests.get(f"{api_url}/status", timeout=5)
            assert response.status_code == 200
            
            status = response.json()
            if status.get("uart", {}).get("connected", False):
                reconnected = True
                reconnect_time = time.time() - start_time
                print(f"\n✓ VERIFIED: UART reconnected after {reconnect_time:.1f} seconds")
                break
            
            time.sleep(0.5)
        
        assert reconnected, "UART did not reconnect within 10 seconds"
        
        # Step 6: Verify communication restored
        time.sleep(1.0)  # Let connection stabilize
        
        # Send test command
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "STATUS"},
            timeout=5
        )
        assert response.status_code == 200
        
        # Check status reflects command was processed
        time.sleep(0.5)
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        final_status = response.json()
        assert final_status.get("uart", {}).get("connected") is True
        
        print("\n✓ VERIFIED: UART communication restored")
        print(f"BT Status: {final_status.get('uart', {}).get('bt_status')}")
        print("\nTest PASSED: Disconnect/reconnect cycle successful")
    
    def test_command_during_disconnect(self, api_url, verify_server):
        """
        Test that commands sent during UART disconnect are handled gracefully.
        
        Validates:
        - Commands fail gracefully when UART disconnected
        - Error messages are clear and informative
        - System remains stable (no crashes)
        """
        # Get initial status
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        initial_status = response.json()
        uart_connected = initial_status.get("uart", {}).get("connected", False)
        
        if not uart_connected:
            pytest.skip("UART not connected - cannot test command during disconnect")
        
        # Request user to disconnect
        print("\n" + "!"*70)
        print("ACTION REQUIRED: Disconnect ESP32 now")
        print("Press Enter after disconnecting...")
        print("!"*70)
        input()
        
        time.sleep(2.0)
        
        # Verify disconnected
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        disconnected_status = response.json()
        assert not disconnected_status.get("uart", {}).get("connected", False)
        
        # Try to send command while disconnected
        response = requests.post(
            f"{api_url}/uart/send",
            json={"command": "START"},
            timeout=5
        )
        
        # Should return error but not crash
        assert response.status_code in [400, 503], "Expected error status for disconnected UART"
        
        error_data = response.json()
        assert "error" in error_data or "message" in error_data, "Should return error message"
        
        print(f"\n✓ VERIFIED: Graceful error handling: {response.status_code}")
        print(f"Error message: {error_data}")
        
        # Verify server is still responsive
        response = requests.get(f"{api_url}/status", timeout=5)
        assert response.status_code == 200
        
        print("✓ VERIFIED: Server remained stable after command to disconnected UART")
        
        # Reconnect for cleanup
        print("\n" + "!"*70)
        print("You can now reconnect the ESP32 for cleanup")
        print("Press Enter to continue...")
        print("!"*70)
        input()

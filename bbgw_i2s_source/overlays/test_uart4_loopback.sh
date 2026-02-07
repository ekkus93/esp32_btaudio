#!/bin/bash
#
# test_uart4_loopback.sh - Test UART4 loopback on BeagleBone Green Wireless
#
# Description:
#   Interactive loopback test for UART4 (/dev/ttyO4).
#   Requires physical connection between P9.11 (RXD) and P9.13 (TXD).
#   Tests bidirectional communication at 115200 baud.
#
# Usage:
#   ./test_uart4_loopback.sh [--baudrate=<rate>] [--duration=<seconds>]
#
# Options:
#   --baudrate=<rate>     Baudrate to test (default: 115200)
#   --duration=<seconds>  Test duration (default: 5 seconds)
#   --help                Show this help message
#
# Requirements:
#   - UART4 must be enabled (run enable_uart4.sh first)
#   - P9.11 must be connected to P9.13 (loopback jumper)
#   - python3 and pyserial must be installed
#
# Author: bbgw_i2s_source project
# Date: 2026-02-07
#

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Print functions
error() {
    echo -e "${RED}✗ $1${NC}" >&2
}

success() {
    echo -e "${GREEN}✓ $1${NC}"
}

warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

heading() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# Show help
show_help() {
    cat << EOF
test_uart4_loopback.sh - UART4 loopback test

Usage:
  ./test_uart4_loopback.sh [options]

Options:
  --baudrate=<rate>     Baudrate to test (default: 115200)
                        Common: 9600, 19200, 38400, 57600, 115200, 230400
  --duration=<seconds>  Test duration in seconds (default: 5)
  --help                Show this help message

Description:
  Tests UART4 loopback communication by sending data from TXD to RXD.
  Requires physical jumper wire connecting P9.11 to P9.13.

Hardware Setup:
  1. Locate P9.11 (UART4 RXD) and P9.13 (UART4 TXD) on BeagleBone
  2. Connect P9.11 to P9.13 with a jumper wire
  3. Run this script
  4. Disconnect jumper after test

Test Details:
  - Sends random data packets at specified baudrate
  - Verifies all received data matches sent data
  - Reports success/failure statistics
  - Measures throughput and error rate

Examples:
  ./test_uart4_loopback.sh                    # Default (115200 baud, 5 sec)
  ./test_uart4_loopback.sh --baudrate=230400  # Test at 230400 baud
  ./test_uart4_loopback.sh --duration=10      # Test for 10 seconds

EOF
}

# Default parameters
BAUDRATE=115200
DURATION=5

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --baudrate=*)
            BAUDRATE="${1#*=}"
            shift
            ;;
        --duration=*)
            DURATION="${1#*=}"
            shift
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
done

heading "UART4 Loopback Test"
info "Device: /dev/ttyO4"
info "Baudrate: $BAUDRATE"
info "Duration: $DURATION seconds"
echo ""

# Check if device exists
if [ ! -c /dev/ttyO4 ]; then
    error "Device not found: /dev/ttyO4"
    echo ""
    echo "UART4 may not be enabled. Run:"
    echo "  ./enable_uart4.sh"
    exit 1
fi
success "Device exists: /dev/ttyO4"

# Check if readable/writable
if [ ! -r /dev/ttyO4 ] || [ ! -w /dev/ttyO4 ]; then
    error "Cannot read/write /dev/ttyO4"
    echo ""
    echo "Add user to dialout group:"
    echo "  sudo usermod -a -G dialout \$USER"
    echo "  # Log out and back in"
    exit 1
fi
success "Device is readable and writable"

# Check if python3 is available
if ! command -v python3 &> /dev/null; then
    error "python3 not found (required for loopback test)"
    echo "Install: sudo apt install python3"
    exit 1
fi
success "python3 found"

# Check if pyserial is available
if ! python3 -c "import serial" 2>/dev/null; then
    error "pyserial not found (required for loopback test)"
    echo "Install: pip3 install pyserial"
    exit 1
fi
success "pyserial found"

# Prompt for hardware setup
echo ""
warning "Hardware setup required:"
echo "  1. Connect P9.11 (UART4 RXD) to P9.13 (UART4 TXD) with jumper wire"
echo "  2. Do NOT connect anything else to these pins during test"
echo ""
echo "Press Enter when ready (or Ctrl+C to cancel)..."
read -r

# Create Python loopback test script
info "Creating loopback test script..."

TEST_SCRIPT=$(mktemp /tmp/uart4_loopback_XXXXXX.py)

cat > "$TEST_SCRIPT" << PYTHON_EOF
#!/usr/bin/env python3
"""
UART4 Loopback Test Script
Tests bidirectional communication via loopback (P9.11 ↔ P9.13)
"""

import serial
import time
import random
import sys

# Configuration
DEVICE = '/dev/ttyO4'
BAUDRATE = $BAUDRATE
DURATION = $DURATION
TEST_PACKET_SIZE = 64  # bytes per packet
MIN_PACKETS = 10

def generate_test_data(size):
    """Generate random test data."""
    return bytes([random.randint(0, 255) for _ in range(size)])

def run_loopback_test():
    """Run loopback test."""
    print("Opening serial port...")
    try:
        ser = serial.Serial(DEVICE, BAUDRATE, timeout=0.5)
    except Exception as e:
        print(f"✗ Failed to open {DEVICE}: {e}")
        return False
    
    print(f"✓ Opened {DEVICE} at {BAUDRATE} baud")
    time.sleep(0.2)  # Let port settle
    
    # Clear buffers
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    
    # Test statistics
    packets_sent = 0
    packets_received = 0
    bytes_sent = 0
    bytes_received = 0
    errors = 0
    
    print(f"\\nRunning loopback test for {DURATION} seconds...")
    print("Press Ctrl+C to stop early\\n")
    
    start_time = time.time()
    
    try:
        while time.time() - start_time < DURATION:
            # Generate test data
            test_data = generate_test_data(TEST_PACKET_SIZE)
            
            # Clear receive buffer
            ser.reset_input_buffer()
            
            # Send data
            ser.write(test_data)
            ser.flush()
            bytes_sent += len(test_data)
            packets_sent += 1
            
            # Small delay to allow loopback
            time.sleep(0.01)
            
            # Read back
            received = ser.read(len(test_data))
            bytes_received += len(received)
            
            if len(received) == len(test_data):
                packets_received += 1
                
                # Verify data
                if received != test_data:
                    errors += 1
                    print(f"⚠ Packet {packets_sent}: Data mismatch!")
                    if errors <= 3:  # Show first 3 errors
                        print(f"  Sent:     {test_data[:16].hex()}...")
                        print(f"  Received: {received[:16].hex()}...")
                else:
                    # Success - print progress every 10 packets
                    if packets_sent % 10 == 0:
                        elapsed = time.time() - start_time
                        throughput = bytes_sent / elapsed if elapsed > 0 else 0
                        print(f"  Packets: {packets_sent}, "
                              f"Bytes: {bytes_sent}, "
                              f"Throughput: {throughput:.0f} bytes/sec")
            else:
                errors += 1
                print(f"✗ Packet {packets_sent}: Incomplete receive "
                      f"({len(received)}/{len(test_data)} bytes)")
            
            # Small delay between packets
            time.sleep(0.01)
    
    except KeyboardInterrupt:
        print("\\n\\nTest interrupted by user")
    
    finally:
        ser.close()
    
    # Calculate statistics
    elapsed = time.time() - start_time
    success_rate = (packets_received / packets_sent * 100) if packets_sent > 0 else 0
    error_rate = (errors / packets_sent * 100) if packets_sent > 0 else 0
    throughput_bps = (bytes_sent * 8 / elapsed) if elapsed > 0 else 0
    
    # Print results
    print("\\n" + "=" * 40)
    print("Loopback Test Results")
    print("=" * 40)
    print(f"Duration:         {elapsed:.2f} seconds")
    print(f"Packets sent:     {packets_sent}")
    print(f"Packets received: {packets_received}")
    print(f"Bytes sent:       {bytes_sent}")
    print(f"Bytes received:   {bytes_received}")
    print(f"Success rate:     {success_rate:.1f}%")
    print(f"Error rate:       {error_rate:.1f}%")
    print(f"Throughput:       {throughput_bps:.0f} bits/sec ({throughput_bps/8:.0f} bytes/sec)")
    print("=" * 40)
    
    # Determine pass/fail
    if packets_sent < MIN_PACKETS:
        print(f"\\n✗ FAIL: Insufficient packets sent ({packets_sent} < {MIN_PACKETS})")
        return False
    elif success_rate < 95.0:
        print(f"\\n✗ FAIL: Success rate too low ({success_rate:.1f}% < 95%)")
        return False
    elif errors > 0:
        print(f"\\n⚠ PASS with warnings: {errors} data errors detected")
        return True
    else:
        print("\\n✓ PASS: All data transmitted correctly")
        return True

if __name__ == "__main__":
    success = run_loopback_test()
    sys.exit(0 if success else 1)
PYTHON_EOF

chmod +x "$TEST_SCRIPT"

# Run the test
echo ""
info "Running loopback test..."
echo ""

if python3 "$TEST_SCRIPT"; then
    echo ""
    success "Loopback test PASSED"
    rm -f "$TEST_SCRIPT"
    exit 0
else
    echo ""
    error "Loopback test FAILED"
    echo ""
    echo "Troubleshooting:"
    echo "  1. Verify jumper wire connects P9.11 to P9.13"
    echo "  2. Check for loose connections"
    echo "  3. Verify UART4 is enabled: ./verify_uart4.sh"
    echo "  4. Try manual test:"
    echo "     Terminal 1: cat /dev/ttyO4"
    echo "     Terminal 2: echo \"test\" > /dev/ttyO4"
    echo "  5. Check kernel messages: dmesg | grep -i uart"
    rm -f "$TEST_SCRIPT"
    exit 1
fi

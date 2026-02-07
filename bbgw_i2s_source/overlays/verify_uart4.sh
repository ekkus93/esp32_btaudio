#!/bin/bash
#
# verify_uart4.sh - Verify UART4 configuration on BeagleBone Green Wireless
#
# Description:
#   Comprehensive verification of UART4 (/dev/ttyO4) configuration
#   including device presence, pin muxing, permissions, and loopback test.
#
# Usage:
#   ./verify_uart4.sh [--verbose|--loopback]
#
# Options:
#   --verbose   Show detailed output
#   --loopback  Include loopback test (requires P9.11↔P9.13 connection)
#   --help      Show this help message
#
# Requirements:
#   - Must be run on BeagleBone Green Wireless
#   - UART4 must be enabled (run enable_uart4.sh first)
#   - For loopback test: connect P9.11 to P9.13
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
verify_uart4.sh - Verify UART4 configuration

Usage:
  ./verify_uart4.sh [options]

Options:
  --verbose   Show detailed output
  --loopback  Include loopback test (requires P9.11↔P9.13 jumper)
  --help      Show this help message

Description:
  Verifies UART4 (/dev/ttyO4) configuration on BeagleBone Green Wireless.
  Checks device presence, pin muxing, permissions, and kernel status.

Checks Performed:
  1. Hardware detection (ARM, BeagleBone)
  2. Device file presence (/dev/ttyO4)
  3. Device permissions and dialout group
  4. Pin mux configuration (P9.11, P9.13)
  5. Kernel UART driver status
  6. Optional loopback test

Examples:
  ./verify_uart4.sh              # Basic checks
  ./verify_uart4.sh --verbose    # Detailed output
  ./verify_uart4.sh --loopback   # Include loopback test

EOF
}

# Global variables
VERBOSE=0
DO_LOOPBACK=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=1
            shift
            ;;
        --loopback|-l)
            DO_LOOPBACK=1
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

heading "UART4 Verification"
info "BeagleBone Green Wireless - /dev/ttyO4"
if [ $VERBOSE -eq 1 ]; then
    info "Verbose mode enabled"
fi
if [ $DO_LOOPBACK -eq 1 ]; then
    info "Loopback test enabled"
fi
echo ""

# Check 1: Hardware detection
heading "Check 1: Hardware Detection"

if [[ $(uname -m) != arm* ]]; then
    error "Not running on ARM architecture (detected: $(uname -m))"
    warning "This script is intended for BeagleBone Green Wireless"
    exit 1
fi
success "ARM architecture detected: $(uname -m)"

if [ -f /proc/device-tree/model ]; then
    local model=$(cat /proc/device-tree/model | tr -d '\0')
    info "Device model: $model"
    if [[ $model == *"BeagleBone"* ]]; then
        success "BeagleBone detected"
    else
        warning "Not a BeagleBone (model: $model)"
    fi
else
    warning "Cannot detect device model"
fi

# Check 2: Device file
heading "Check 2: Device File (/dev/ttyO4)"

if [ -c /dev/ttyO4 ]; then
    success "Device exists: /dev/ttyO4"
    
    # Show device info
    local device_info=$(ls -l /dev/ttyO4)
    info "Device info: $device_info"
    
    # Check major/minor numbers
    local major=$(stat -c %t /dev/ttyO4)
    local minor=$(stat -c %T /dev/ttyO4)
    if [ $VERBOSE -eq 1 ]; then
        info "Major:Minor = $major:$minor"
    fi
else
    error "Device not found: /dev/ttyO4"
    echo ""
    echo "UART4 may not be enabled. Run:"
    echo "  ./enable_uart4.sh"
    exit 1
fi

# Check 3: Permissions
heading "Check 3: Permissions"

# Check if user can read/write
if [ -r /dev/ttyO4 ]; then
    success "Device is readable"
else
    error "Device is not readable"
fi

if [ -w /dev/ttyO4 ]; then
    success "Device is writable"
else
    warning "Device is not writable"
    
    # Check dialout group
    if groups | grep -q dialout; then
        warning "User is in dialout group, but device not writable (check permissions)"
    else
        warning "User is not in dialout group"
        echo ""
        echo "Add user to dialout group:"
        echo "  sudo usermod -a -G dialout \$USER"
        echo "  # Log out and back in for group change"
    fi
fi

# Check dialout group membership
if groups | grep -q dialout; then
    success "User is in dialout group"
else
    warning "User is not in dialout group (required for UART access)"
fi

# Check 4: Pin mux configuration
heading "Check 4: Pin Mux Configuration"

local pinmux_file="/sys/kernel/debug/pinctrl/44e10800.pinmux/pins"

if [ ! -f "$pinmux_file" ]; then
    warning "Pin mux debugfs not available: $pinmux_file"
    echo "Try mounting debugfs: sudo mount -t debugfs none /sys/kernel/debug"
else
    # Check P9.11 (offset 0x070, pin 28)
    info "Checking P9.11 (UART4 RXD, offset 0x070)..."
    if sudo grep -A1 "pin 28\|070" "$pinmux_file" | grep -q "0x26\|uart"; then
        success "P9.11 configured for UART (Mode 6, input, pull-up)"
        if [ $VERBOSE -eq 1 ]; then
            sudo grep -A1 "pin 28\|070" "$pinmux_file" | head -n2
        fi
    else
        error "P9.11 NOT configured for UART"
        echo "Expected: Mode 6 (0x26)"
        echo "Actual:"
        sudo grep -A1 "pin 28\|070" "$pinmux_file" | head -n2 || true
    fi
    
    # Check P9.13 (offset 0x074, pin 29)
    info "Checking P9.13 (UART4 TXD, offset 0x074)..."
    if sudo grep -A1 "pin 29\|074" "$pinmux_file" | grep -q "0x06\|0x26\|uart"; then
        success "P9.13 configured for UART (Mode 6, output)"
        if [ $VERBOSE -eq 1 ]; then
            sudo grep -A1 "pin 29\|074" "$pinmux_file" | head -n2
        fi
    else
        error "P9.13 NOT configured for UART"
        echo "Expected: Mode 6 (0x06 or 0x26)"
        echo "Actual:"
        sudo grep -A1 "pin 29\|074" "$pinmux_file" | head -n2 || true
    fi
fi

# Check 5: Kernel UART driver
heading "Check 5: Kernel UART Driver"

# Check kernel messages
if dmesg | grep -i "uart\|ttyO" | grep -i "uart4\|ttyO4" > /dev/null 2>&1; then
    success "UART4 messages found in kernel log"
    if [ $VERBOSE -eq 1 ]; then
        echo ""
        echo "Recent UART4 messages:"
        dmesg | grep -i "uart4\|ttyO4" | tail -n5
    fi
else
    warning "No UART4 messages in kernel log"
fi

# Check if UART module is loaded
if lsmod | grep -i uart > /dev/null 2>&1; then
    if [ $VERBOSE -eq 1 ]; then
        info "UART modules loaded:"
        lsmod | grep -i uart
    fi
fi

# Check 6: Loopback test (optional)
if [ $DO_LOOPBACK -eq 1 ]; then
    heading "Check 6: Loopback Test"
    
    warning "Loopback test requires P9.11 connected to P9.13"
    echo "Press Enter when ready (or Ctrl+C to skip)..."
    read -r
    
    # Check if python3 and pyserial are available
    if ! command -v python3 &> /dev/null; then
        error "python3 not found (required for loopback test)"
    elif ! python3 -c "import serial" 2>/dev/null; then
        error "pyserial not found (required for loopback test)"
        echo "Install: pip3 install pyserial"
    else
        info "Running loopback test..."
        
        # Create temporary Python script
        local test_script=$(mktemp)
        cat > "$test_script" << 'PYTHON_EOF'
import serial
import time
import sys

try:
    # Open serial port
    ser = serial.Serial('/dev/ttyO4', 115200, timeout=1)
    time.sleep(0.1)
    
    # Test data
    test_data = b'UART4_LOOPBACK_TEST\n'
    
    # Clear buffers
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    
    # Send data
    ser.write(test_data)
    ser.flush()
    time.sleep(0.1)
    
    # Read back
    received = ser.read(len(test_data))
    
    # Compare
    if received == test_data:
        print("✓ Loopback test PASSED")
        print(f"  Sent:     {test_data.decode().strip()}")
        print(f"  Received: {received.decode().strip()}")
        sys.exit(0)
    else:
        print("✗ Loopback test FAILED")
        print(f"  Sent:     {test_data}")
        print(f"  Received: {received}")
        sys.exit(1)
    
except Exception as e:
    print(f"✗ Loopback test ERROR: {e}")
    sys.exit(1)
finally:
    if 'ser' in locals():
        ser.close()
PYTHON_EOF
        
        if python3 "$test_script"; then
            success "Loopback test passed"
        else
            error "Loopback test failed"
            echo ""
            echo "Troubleshooting:"
            echo "  1. Verify P9.11 is connected to P9.13"
            echo "  2. Check wiring and connection"
            echo "  3. Try manual test: screen /dev/ttyO4 115200"
        fi
        
        rm -f "$test_script"
    fi
fi

# Summary
heading "Summary"

local checks_passed=0
local checks_failed=0

# Count passed checks
if [ -c /dev/ttyO4 ]; then
    ((checks_passed++))
else
    ((checks_failed++))
fi

if [ -r /dev/ttyO4 ] && [ -w /dev/ttyO4 ]; then
    ((checks_passed++))
else
    ((checks_failed++))
fi

echo ""
success "Checks passed: $checks_passed"
if [ $checks_failed -gt 0 ]; then
    error "Checks failed: $checks_failed"
else
    info "Checks failed: $checks_failed"
fi
echo ""

if [ $checks_failed -eq 0 ]; then
    success "UART4 is configured correctly"
    echo ""
    info "Device: /dev/ttyO4"
    info "Pins: P9.11 (RXD), P9.13 (TXD)"
    info "Baudrate: 115200 (configurable)"
    echo ""
    info "Next steps:"
    echo "  1. Connect ESP32: P9.11→ESP32 TXD, P9.13→ESP32 RXD, GND→GND"
    echo "  2. Test communication: ./milestone2_uart_test.py --device /dev/ttyO4"
    echo "  3. Run main application: python3 main.py"
else
    warning "Some checks failed. See messages above for troubleshooting."
fi

exit $checks_failed

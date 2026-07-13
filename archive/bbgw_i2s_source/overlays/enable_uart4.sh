#!/bin/bash
#
# enable_uart4.sh - Enable UART4 on BeagleBone Green Wireless
#
# Description:
#   Enables UART4 (/dev/ttyO4) on P9.11 (RXD) and P9.13 (TXD)
#   for ESP32 communication. Supports multiple methods:
#   1. config-pin (preferred, modern kernels)
#   2. Device Tree overlay (BB-BBGW-UART4-00A0.dtbo)
#   3. Universal cape (already enabled on most systems)
#
# Usage:
#   ./enable_uart4.sh [--method=<config-pin|overlay|auto>]
#
# Options:
#   --method=config-pin   Use config-pin command (preferred)
#   --method=overlay      Use Device Tree overlay
#   --method=auto         Auto-detect best method (default)
#   --help                Show this help message
#
# Requirements:
#   - Must be run on BeagleBone Green Wireless
#   - Root privileges for some operations (sudo)
#
# Author: bbgw_i2s_source project
# Date: 2026-02-07
#

set -e  # Exit on error

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
enable_uart4.sh - Enable UART4 on BeagleBone Green Wireless

Usage:
  ./enable_uart4.sh [options]

Options:
  --method=config-pin   Use config-pin command (preferred, non-persistent)
  --method=overlay      Use Device Tree overlay (persistent after reboot)
  --method=auto         Auto-detect best method (default)
  --help                Show this help message

Description:
  Enables UART4 on P9.11 (RXD) and P9.13 (TXD) for ESP32 communication.
  Creates /dev/ttyO4 device for serial communication at 115200 baud.

Methods:
  1. config-pin (Modern kernels, Debian 10+)
     - Fast, immediate activation
     - Non-persistent (reset after reboot)
     - Recommended for development

  2. Device Tree overlay (Universal)
     - Persistent across reboots
     - Requires overlay file in /lib/firmware/
     - Requires /boot/uEnv.txt modification

  3. Auto (Default)
     - Tries config-pin first
     - Falls back to overlay if needed

Examples:
  ./enable_uart4.sh                    # Auto-detect method
  ./enable_uart4.sh --method=config-pin # Use config-pin
  ./enable_uart4.sh --method=overlay   # Use overlay
  sudo ./enable_uart4.sh               # Run with sudo if needed

Verification:
  After enabling, verify with:
    ls -l /dev/ttyO4                   # Check device exists
    cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A1 070
    ./verify_uart4.sh                  # Comprehensive check

EOF
}

# Parse arguments
METHOD="auto"
while [[ $# -gt 0 ]]; do
    case $1 in
        --method=*)
            METHOD="${1#*=}"
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

# Validate method
case "$METHOD" in
    config-pin|overlay|auto)
        ;;
    *)
        error "Invalid method: $METHOD"
        echo "Valid methods: config-pin, overlay, auto"
        exit 1
        ;;
esac

heading "Enable UART4 on BeagleBone Green Wireless"
info "Method: $METHOD"
echo ""

# Check if running on BeagleBone
if [[ $(uname -m) != arm* ]]; then
    error "Not running on ARM architecture"
    warning "This script is intended for BeagleBone Green Wireless"
    exit 1
fi
success "BeagleBone detected"

# Method 1: config-pin (preferred)
enable_via_config_pin() {
    heading "Method 1: config-pin"
    
    # Check if config-pin is available
    if ! command -v config-pin &> /dev/null; then
        warning "config-pin not found"
        return 1
    fi
    success "config-pin found"
    
    # Enable P9.11 as UART
    info "Configuring P9.11 (RXD) for UART..."
    if config-pin P9.11 uart 2>&1; then
        success "P9.11 configured as UART"
    else
        error "Failed to configure P9.11"
        return 1
    fi
    
    # Enable P9.13 as UART
    info "Configuring P9.13 (TXD) for UART..."
    if config-pin P9.13 uart 2>&1; then
        success "P9.13 configured as UART"
    else
        error "Failed to configure P9.13"
        return 1
    fi
    
    # Wait for device to appear
    sleep 1
    
    # Check if device exists
    if [ -c /dev/ttyO4 ]; then
        success "/dev/ttyO4 created"
        return 0
    else
        warning "/dev/ttyO4 not found (may need kernel module)"
        return 1
    fi
}

# Method 2: Device Tree overlay
enable_via_overlay() {
    heading "Method 2: Device Tree Overlay"
    
    # Check if overlay file exists
    if [ ! -f /lib/firmware/BB-BBGW-UART4-00A0.dtbo ]; then
        warning "Overlay file not found: /lib/firmware/BB-BBGW-UART4-00A0.dtbo"
        echo ""
        echo "To create the overlay:"
        echo "  1. Compile: dtc -O dtb -o BB-BBGW-UART4-00A0.dtbo -b 0 -@ BB-BBGW-UART4-00A0.dts"
        echo "  2. Install: sudo cp BB-BBGW-UART4-00A0.dtbo /lib/firmware/"
        return 1
    fi
    success "Overlay file found"
    
    # Check /boot/uEnv.txt
    if [ ! -f /boot/uEnv.txt ]; then
        error "/boot/uEnv.txt not found"
        return 1
    fi
    
    # Check if already enabled in uEnv.txt
    if grep -q "BB-BBGW-UART4" /boot/uEnv.txt; then
        success "UART4 overlay already enabled in /boot/uEnv.txt"
    else
        warning "UART4 overlay not enabled in /boot/uEnv.txt"
        echo ""
        echo "To enable persistently, add to /boot/uEnv.txt:"
        echo "  uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo"
        echo ""
        echo "Then reboot: sudo reboot"
        return 1
    fi
    
    return 0
}

# Method 3: Check if universal cape already enabled it
check_universal_cape() {
    heading "Method 3: Universal Cape (Pre-enabled)"
    
    # Check if /dev/ttyO4 already exists
    if [ -c /dev/ttyO4 ]; then
        success "/dev/ttyO4 already exists"
        info "UART4 may already be enabled via universal cape"
        return 0
    else
        warning "/dev/ttyO4 not found"
        return 1
    fi
}

# Main logic
case "$METHOD" in
    config-pin)
        if enable_via_config_pin; then
            success "UART4 enabled successfully via config-pin"
        else
            error "Failed to enable UART4 via config-pin"
            exit 1
        fi
        ;;
    overlay)
        if enable_via_overlay; then
            success "UART4 enabled successfully via overlay"
        else
            error "Failed to enable UART4 via overlay"
            exit 1
        fi
        ;;
    auto)
        # Try methods in order of preference
        if check_universal_cape; then
            success "UART4 already enabled (universal cape)"
        elif enable_via_config_pin; then
            success "UART4 enabled successfully via config-pin"
        elif enable_via_overlay; then
            success "UART4 enabled successfully via overlay"
        else
            error "Failed to enable UART4 via all methods"
            echo ""
            echo "Troubleshooting:"
            echo "  1. Check kernel messages: dmesg | grep -i uart"
            echo "  2. List UARTs: ls -l /dev/ttyO*"
            echo "  3. Check pin mux: cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep 070"
            exit 1
        fi
        ;;
esac

# Final verification
heading "Verification"

# Check device
if [ -c /dev/ttyO4 ]; then
    success "Device exists: /dev/ttyO4"
    ls -l /dev/ttyO4
else
    error "Device not found: /dev/ttyO4"
    exit 1
fi

# Check permissions
if [ -r /dev/ttyO4 ] && [ -w /dev/ttyO4 ]; then
    success "Device is readable and writable"
elif groups | grep -q dialout; then
    success "User in dialout group"
else
    warning "User not in dialout group"
    echo ""
    echo "Add user to dialout group:"
    echo "  sudo usermod -a -G dialout \$USER"
    echo "  # Log out and back in for group change to take effect"
fi

# Check pin mux (if debugfs available)
if [ -f /sys/kernel/debug/pinctrl/44e10800.pinmux/pins ]; then
    info "Pin mux configuration:"
    echo ""
    echo "P9.11 (RXD, offset 0x070):"
    sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A1 "pin 28\|070" | head -n2 || true
    echo ""
    echo "P9.13 (TXD, offset 0x074):"
    sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A1 "pin 29\|074" | head -n2 || true
fi

echo ""
heading "Summary"
success "UART4 enabled successfully"
echo ""
info "Device: /dev/ttyO4"
info "Pins: P9.11 (RXD), P9.13 (TXD)"
info "Baudrate: 115200 (configurable in software)"
echo ""
info "Next steps:"
echo "  1. Test loopback: ./test_uart4_loopback.sh"
echo "  2. Connect ESP32: P9.11→ESP32 TXD, P9.13→ESP32 RXD, GND→GND"
echo "  3. Test communication: ./milestone2_uart_test.py --device /dev/ttyO4"
echo ""

if [ "$METHOD" = "config-pin" ]; then
    warning "Note: config-pin changes are NOT persistent after reboot"
    echo "  To make persistent, add to /boot/uEnv.txt or use overlay method"
fi

exit 0

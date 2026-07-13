#!/bin/bash
#
# verify_mcasp.sh - Verify McASP/I2S Device Tree overlay installation
#
# Description:
#   Comprehensive verification script for BeagleBone Green Wireless
#   McASP I2S configuration. Checks Device Tree overlay loading,
#   pin muxing, ALSA device, and provides troubleshooting guidance.
#
# Usage:
#   ./verify_mcasp.sh [--verbose|--pins|--alsa|--all]
#
# Options:
#   --verbose  Show detailed output
#   --pins     Check pin muxing only
#   --alsa     Check ALSA device only
#   --all      Run all checks (default)
#   --help     Show this help message
#
# Requirements:
#   - Must be run on BeagleBone Green Wireless
#   - Device Tree overlay must be installed and loaded
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
verify_mcasp.sh - Verify McASP/I2S Device Tree overlay

Usage:
  ./verify_mcasp.sh [options]

Options:
  --verbose  Show detailed output
  --pins     Check pin muxing only
  --alsa     Check ALSA device only
  --all      Run all checks (default)
  --help     Show this help message

Description:
  Verifies that the BeagleBone Green Wireless McASP I2S Device Tree
  overlay is correctly installed and configured.

Checks:
  1. BeagleBone hardware detection
  2. Device Tree overlay file presence
  3. Kernel boot messages (McASP initialization)
  4. Pin mux configuration (P9.31, P9.29, P9.28)
  5. ALSA device presence and configuration
  6. McASP driver status

Requirements:
  - BeagleBone Green Wireless (or compatible)
  - Root privileges for some checks (sudo)
  - BB-BBGW-I2S overlay installed in /lib/firmware/

Examples:
  ./verify_mcasp.sh              # Run all checks
  ./verify_mcasp.sh --verbose    # Detailed output
  ./verify_mcasp.sh --pins       # Pin muxing only
  ./verify_mcasp.sh --alsa       # ALSA device only

EOF
}

# Global variables
VERBOSE=0
CHECK_MODE="all"

# Parse arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --verbose|-v)
                VERBOSE=1
                shift
                ;;
            --pins)
                CHECK_MODE="pins"
                shift
                ;;
            --alsa)
                CHECK_MODE="alsa"
                shift
                ;;
            --all)
                CHECK_MODE="all"
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
}

# Check if running on BeagleBone
check_hardware() {
    heading "Hardware Detection"
    
    # Check if running on ARM
    if [[ $(uname -m) != arm* ]]; then
        error "Not running on ARM architecture (detected: $(uname -m))"
        warning "This script is intended for BeagleBone Green Wireless"
        return 1
    fi
    success "ARM architecture detected: $(uname -m)"
    
    # Check for BeagleBone identifier
    if [ -f /proc/device-tree/model ]; then
        local model=$(cat /proc/device-tree/model | tr -d '\0')
        info "Device model: $model"
        
        if [[ $model == *"BeagleBone"* ]]; then
            success "BeagleBone detected"
        else
            warning "Not a BeagleBone (model: $model)"
            warning "Continuing anyway..."
        fi
    else
        warning "Cannot detect device model"
    fi
    
    # Check kernel version
    info "Kernel version: $(uname -r)"
    
    return 0
}

# Check Device Tree overlay file
check_overlay_file() {
    heading "Device Tree Overlay File Check"
    
    local overlay_found=0
    
    # Check for full overlay
    if [ -f /lib/firmware/BB-BBGW-I2S-00A0.dtbo ]; then
        success "Found: /lib/firmware/BB-BBGW-I2S-00A0.dtbo"
        local size=$(stat -c%s /lib/firmware/BB-BBGW-I2S-00A0.dtbo)
        info "  Size: ${size} bytes"
        overlay_found=1
    else
        warning "Not found: /lib/firmware/BB-BBGW-I2S-00A0.dtbo"
    fi
    
    # Check for simple overlay
    if [ -f /lib/firmware/BB-BBGW-I2S-SIMPLE-00A0.dtbo ]; then
        success "Found: /lib/firmware/BB-BBGW-I2S-SIMPLE-00A0.dtbo"
        local size=$(stat -c%s /lib/firmware/BB-BBGW-I2S-SIMPLE-00A0.dtbo)
        info "  Size: ${size} bytes"
        overlay_found=1
    else
        warning "Not found: /lib/firmware/BB-BBGW-I2S-SIMPLE-00A0.dtbo"
    fi
    
    if [ $overlay_found -eq 0 ]; then
        error "No I2S overlay files found in /lib/firmware/"
        echo ""
        echo "Installation instructions:"
        echo "  1. Compile overlay: dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts"
        echo "  2. Install: sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/"
        echo "  3. Set permissions: sudo chmod 644 /lib/firmware/BB-BBGW-I2S-00A0.dtbo"
        return 1
    fi
    
    # Check /boot/uEnv.txt
    if [ -f /boot/uEnv.txt ]; then
        info "Checking /boot/uEnv.txt for overlay configuration..."
        
        if grep -q "BB-BBGW-I2S" /boot/uEnv.txt; then
            success "I2S overlay referenced in /boot/uEnv.txt"
            
            if [ $VERBOSE -eq 1 ]; then
                echo ""
                echo "Relevant lines from /boot/uEnv.txt:"
                grep -n "BB-BBGW-I2S\|overlay" /boot/uEnv.txt | grep -v "^#" | head -n5
            fi
        else
            warning "I2S overlay NOT found in /boot/uEnv.txt"
            echo ""
            echo "Add one of these lines to /boot/uEnv.txt:"
            echo "  uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo"
            echo "or:"
            echo "  cape_enable=bone_capemgr.enable_partno=BB-BBGW-I2S"
        fi
    else
        warning "/boot/uEnv.txt not found"
    fi
    
    return 0
}

# Check kernel messages for McASP
check_kernel_messages() {
    heading "Kernel Messages (McASP Initialization)"
    
    # Check for McASP messages
    if dmesg | grep -i mcasp > /dev/null 2>&1; then
        success "McASP messages found in kernel log"
        echo ""
        echo "Recent McASP messages:"
        dmesg | grep -i mcasp | tail -n10
    else
        error "No McASP messages in kernel log"
        echo ""
        echo "This indicates the Device Tree overlay did NOT load."
        echo ""
        echo "Troubleshooting:"
        echo "  1. Check /boot/uEnv.txt has correct overlay path"
        echo "  2. Verify overlay file exists in /lib/firmware/"
        echo "  3. Check for Device Tree errors: dmesg | grep -i 'device tree'"
        echo "  4. Reboot after making changes"
        return 1
    fi
    
    # Check for errors
    if dmesg | grep -i "mcasp\|audio" | grep -i error > /dev/null 2>&1; then
        warning "Found McASP errors in kernel log:"
        dmesg | grep -i "mcasp\|audio" | grep -i error | tail -n5
    fi
    
    return 0
}

# Check pin muxing
check_pin_mux() {
    heading "Pin Mux Configuration"
    
    local pinmux_file="/sys/kernel/debug/pinctrl/44e10800.pinmux/pins"
    
    if [ ! -f "$pinmux_file" ]; then
        error "Pin mux debugfs not available: $pinmux_file"
        echo ""
        echo "Try mounting debugfs:"
        echo "  sudo mount -t debugfs none /sys/kernel/debug"
        return 1
    fi
    
    # Check P9.31 (offset 0x990, pin 100)
    info "Checking P9.31 (BCLK/ACLKX)..."
    if sudo grep -A1 "pin 100\|990" "$pinmux_file" | grep -q "00000000\|mcasp"; then
        success "P9.31 configured (Mode 0)"
        if [ $VERBOSE -eq 1 ]; then
            sudo grep -A1 "pin 100\|990" "$pinmux_file" | head -n2
        fi
    else
        error "P9.31 NOT configured for McASP"
        echo "Expected: Mode 0 (0x00000000)"
        echo "Actual:"
        sudo grep -A1 "pin 100\|990" "$pinmux_file" | head -n2
    fi
    
    # Check P9.29 (offset 0x994, pin 101)
    info "Checking P9.29 (WS/FSX)..."
    if sudo grep -A1 "pin 101\|994" "$pinmux_file" | grep -q "00000000\|mcasp"; then
        success "P9.29 configured (Mode 0)"
        if [ $VERBOSE -eq 1 ]; then
            sudo grep -A1 "pin 101\|994" "$pinmux_file" | head -n2
        fi
    else
        error "P9.29 NOT configured for McASP"
        echo "Expected: Mode 0 (0x00000000)"
        echo "Actual:"
        sudo grep -A1 "pin 101\|994" "$pinmux_file" | head -n2
    fi
    
    # Check P9.28 (offset 0x99c, pin 103)
    info "Checking P9.28 (DOUT/AXR1)..."
    if sudo grep -A1 "pin 103\|99c" "$pinmux_file" | grep -q "00000002\|mcasp"; then
        success "P9.28 configured (Mode 2)"
        if [ $VERBOSE -eq 1 ]; then
            sudo grep -A1 "pin 103\|99c" "$pinmux_file" | head -n2
        fi
    else
        error "P9.28 NOT configured for McASP"
        echo "Expected: Mode 2 (0x00000002)"
        echo "Actual:"
        sudo grep -A1 "pin 103\|99c" "$pinmux_file" | head -n2
    fi
    
    return 0
}

# Check ALSA device
check_alsa() {
    heading "ALSA Device Configuration"
    
    # Check if aplay is available
    if ! command -v aplay &> /dev/null; then
        error "aplay command not found"
        echo "Install ALSA utils: sudo apt install alsa-utils"
        return 1
    fi
    
    # List playback devices
    info "ALSA playback devices:"
    if aplay -l 2>/dev/null | grep -i "bbgw\|mcasp" > /dev/null; then
        success "BBGW I2S device found"
        echo ""
        aplay -l | grep -A2 "card\|device"
    else
        warning "BBGW I2S device NOT found in ALSA"
        echo ""
        echo "Available devices:"
        aplay -l 2>/dev/null || echo "  (none)"
        echo ""
        echo "This may indicate:"
        echo "  - Using simple overlay (no ALSA sound card)"
        echo "  - ALSA configuration issue"
        echo "  - McASP driver not loaded"
        return 1
    fi
    
    # Check for card 0
    if aplay -l 2>/dev/null | grep "^card 0:" > /dev/null; then
        info "Default card (hw:0,0) is available"
        
        # Try to get card info
        if [ $VERBOSE -eq 1 ]; then
            echo ""
            info "Card 0 details:"
            aplay -L | grep -A5 "^hw:CARD=.*,DEV=0" | head -n6 || true
        fi
    fi
    
    return 0
}

# Check McASP driver
check_driver() {
    heading "McASP Driver Status"
    
    # Check if McASP module is loaded
    if lsmod | grep -i mcasp > /dev/null 2>&1; then
        success "McASP kernel module loaded"
        if [ $VERBOSE -eq 1 ]; then
            lsmod | grep -i mcasp
        fi
    else
        warning "McASP module not visible in lsmod"
        info "(May be built into kernel)"
    fi
    
    # Check for McASP in /proc/asound
    if [ -d /proc/asound ]; then
        if ls /proc/asound/ | grep -i "bbgw\|card" > /dev/null 2>&1; then
            success "Sound card registered in /proc/asound"
            if [ $VERBOSE -eq 1 ]; then
                echo ""
                ls -la /proc/asound/ | grep -v "^total"
            fi
        else
            warning "No sound card in /proc/asound"
        fi
    fi
    
    return 0
}

# Main verification
main() {
    parse_args "$@"
    
    info "========================================="
    info "BeagleBone Green Wireless McASP I2S Verification"
    info "========================================="
    info "Mode: $CHECK_MODE"
    if [ $VERBOSE -eq 1 ]; then
        info "Verbose output enabled"
    fi
    
    local checks_passed=0
    local checks_failed=0
    
    # Run checks based on mode
    case "$CHECK_MODE" in
        all)
            check_hardware && ((checks_passed++)) || ((checks_failed++))
            check_overlay_file && ((checks_passed++)) || ((checks_failed++))
            check_kernel_messages && ((checks_passed++)) || ((checks_failed++))
            check_pin_mux && ((checks_passed++)) || ((checks_failed++))
            check_alsa && ((checks_passed++)) || ((checks_failed++))
            check_driver && ((checks_passed++)) || ((checks_failed++))
            ;;
        pins)
            check_pin_mux && ((checks_passed++)) || ((checks_failed++))
            ;;
        alsa)
            check_alsa && ((checks_passed++)) || ((checks_failed++))
            ;;
    esac
    
    # Summary
    heading "Verification Summary"
    success "Checks passed: $checks_passed"
    if [ $checks_failed -gt 0 ]; then
        error "Checks failed: $checks_failed"
    else
        info "Checks failed: $checks_failed"
    fi
    echo ""
    
    if [ $checks_failed -eq 0 ]; then
        success "All checks passed! McASP I2S is configured correctly."
        echo ""
        info "Next steps:"
        info "  1. Test ALSA playback: speaker-test -D hw:0,0 -c 2 -r 48000"
        info "  2. Connect ESP32 hardware (P9.31→GPIO26, P9.29→GPIO25, P9.28→GPIO22)"
        info "  3. Run Milestone 1 test: ./milestone1_tone_test.py"
    else
        warning "Some checks failed. See messages above for troubleshooting."
        echo ""
        info "Common issues:"
        info "  - Overlay not enabled in /boot/uEnv.txt → edit and reboot"
        info "  - Overlay file missing → copy to /lib/firmware/"
        info "  - Pin conflicts → disable conflicting capes"
        info "  - ALSA not configured → may need custom asound.conf"
    fi
    
    exit $checks_failed
}

# Run main
main "$@"

#!/bin/bash
#
# compile_overlays.sh - Compile BeagleBone Device Tree overlays
#
# Description:
#   Compiles Device Tree source (.dts) files to binary blobs (.dtbo)
#   for BeagleBone Green Wireless I2S configuration.
#
# Usage:
#   ./compile_overlays.sh [--all|--full|--simple]
#
# Options:
#   --all      Compile all overlays (default)
#   --full     Compile BB-BBGW-I2S-00A0.dts only
#   --simple   Compile BB-BBGW-I2S-SIMPLE-00A0.dts only
#   --help     Show this help message
#
# Requirements:
#   - device-tree-compiler package
#   - Run from overlays/ directory
#
# Author: bbgw_i2s_source project
# Date: 2026-02-07
#

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print colored messages
error() {
    echo -e "${RED}ERROR: $1${NC}" >&2
}

success() {
    echo -e "${GREEN}SUCCESS: $1${NC}"
}

warning() {
    echo -e "${YELLOW}WARNING: $1${NC}"
}

info() {
    echo -e "$1"
}

# Show help
show_help() {
    cat << EOF
compile_overlays.sh - Compile BeagleBone Device Tree overlays

Usage:
  ./compile_overlays.sh [--all|--full|--simple|--help]

Options:
  --all      Compile all overlays (default)
  --full     Compile BB-BBGW-I2S-00A0.dts only (recommended)
  --simple   Compile BB-BBGW-I2S-SIMPLE-00A0.dts only (fallback)
  --help     Show this help message

Description:
  Compiles Device Tree source (.dts) files to binary blobs (.dtbo)
  for BeagleBone Green Wireless McASP I2S configuration.

Requirements:
  - device-tree-compiler package must be installed
    Install: sudo apt install device-tree-compiler
  - Must be run from the overlays/ directory

Examples:
  ./compile_overlays.sh              # Compile all overlays
  ./compile_overlays.sh --full       # Compile full overlay only
  ./compile_overlays.sh --simple     # Compile simple overlay only

Output:
  - BB-BBGW-I2S-00A0.dtbo         (full overlay with ALSA)
  - BB-BBGW-I2S-SIMPLE-00A0.dtbo  (minimal pin mux only)

EOF
}

# Check prerequisites
check_prerequisites() {
    info "Checking prerequisites..."
    
    # Check for dtc (device tree compiler)
    if ! command -v dtc &> /dev/null; then
        error "device-tree-compiler not found"
        echo ""
        echo "Install with:"
        echo "  sudo apt update"
        echo "  sudo apt install device-tree-compiler"
        exit 1
    fi
    
    success "dtc found: $(dtc --version | head -n1)"
    
    # Check for required .dts files
    if [ ! -f "BB-BBGW-I2S-00A0.dts" ] && [ ! -f "BB-BBGW-I2S-SIMPLE-00A0.dts" ]; then
        error "No .dts source files found in current directory"
        echo ""
        echo "Make sure you are in the overlays/ directory containing:"
        echo "  - BB-BBGW-I2S-00A0.dts"
        echo "  - BB-BBGW-I2S-SIMPLE-00A0.dts"
        exit 1
    fi
    
    echo ""
}

# Compile a single overlay
compile_overlay() {
    local dts_file=$1
    local dtbo_file="${dts_file%.dts}.dtbo"
    
    info "Compiling: ${dts_file} → ${dtbo_file}"
    
    if [ ! -f "$dts_file" ]; then
        warning "File not found: $dts_file (skipping)"
        return 1
    fi
    
    # Compile with dtc
    # Options:
    #   -O dtb       : Output Device Tree Blob (binary)
    #   -o <file>    : Output file
    #   -b 0         : Boot CPU (0 for ARM)
    #   -@           : Generate symbols for overlays
    if dtc -O dtb -o "$dtbo_file" -b 0 -@ "$dts_file" 2>&1 | tee compile.log; then
        # Check if output file was created
        if [ -f "$dtbo_file" ]; then
            local size=$(stat -c%s "$dtbo_file")
            success "Compiled successfully: $dtbo_file (${size} bytes)"
            
            # Check for warnings in compile log
            if grep -i "warning" compile.log > /dev/null; then
                warning "Compilation produced warnings (usually safe to ignore):"
                grep -i "warning" compile.log | head -n5
                echo "  (Check compile.log for full details)"
            fi
            rm -f compile.log
            return 0
        else
            error "Compilation succeeded but output file not created: $dtbo_file"
            rm -f compile.log
            return 1
        fi
    else
        error "Compilation failed: $dts_file"
        echo ""
        echo "See compile.log for details"
        return 1
    fi
}

# Main compilation
main() {
    local mode="all"
    
    # Parse arguments
    case "${1:-}" in
        --all)
            mode="all"
            ;;
        --full)
            mode="full"
            ;;
        --simple)
            mode="simple"
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        "")
            mode="all"
            ;;
        *)
            error "Unknown option: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
    
    info "========================================="
    info "BeagleBone Green Wireless I2S Overlay Compiler"
    info "========================================="
    echo ""
    
    check_prerequisites
    
    local success_count=0
    local fail_count=0
    
    # Compile overlays based on mode
    case "$mode" in
        all)
            info "Mode: Compile all overlays"
            echo ""
            
            if compile_overlay "BB-BBGW-I2S-00A0.dts"; then
                ((success_count++))
            else
                ((fail_count++))
            fi
            echo ""
            
            if compile_overlay "BB-BBGW-I2S-SIMPLE-00A0.dts"; then
                ((success_count++))
            else
                ((fail_count++))
            fi
            ;;
        full)
            info "Mode: Compile full overlay only"
            echo ""
            
            if compile_overlay "BB-BBGW-I2S-00A0.dts"; then
                ((success_count++))
            else
                ((fail_count++))
            fi
            ;;
        simple)
            info "Mode: Compile simple overlay only"
            echo ""
            
            if compile_overlay "BB-BBGW-I2S-SIMPLE-00A0.dts"; then
                ((success_count++))
            else
                ((fail_count++))
            fi
            ;;
    esac
    
    echo ""
    info "========================================="
    info "Compilation Summary"
    info "========================================="
    success "Successful: $success_count"
    if [ $fail_count -gt 0 ]; then
        error "Failed: $fail_count"
    else
        info "Failed: $fail_count"
    fi
    echo ""
    
    if [ $success_count -gt 0 ]; then
        info "Next steps:"
        info "1. Copy .dtbo files to BeagleBone:"
        info "   scp *.dtbo debian@<bbgw-ip>:~"
        echo ""
        info "2. On BeagleBone, install to /lib/firmware:"
        info "   sudo cp *.dtbo /lib/firmware/"
        info "   sudo chmod 644 /lib/firmware/BB-BBGW-I2S-*.dtbo"
        echo ""
        info "3. Enable in /boot/uEnv.txt:"
        info "   sudo nano /boot/uEnv.txt"
        info "   # Add line:"
        info "   uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo"
        echo ""
        info "4. Reboot BeagleBone:"
        info "   sudo reboot"
        echo ""
        info "See README.md for detailed instructions."
    fi
    
    if [ $fail_count -gt 0 ]; then
        exit 1
    fi
    
    exit 0
}

# Run main
main "$@"

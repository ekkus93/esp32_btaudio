#!/bin/bash
#
# BeagleBone Green Wireless I2S Source — Automated Setup Script
#
# This script automates the installation and configuration of bbgw_i2s_source
# on a BeagleBone Green Wireless running Debian Linux.
#
# Usage:
#   bash setup_bbgw.sh
#
# What this script does:
#   1. Update system packages
#   2. Install Python 3, pip, venv, and build tools
#   3. Install ALSA and device tree compiler
#   4. Install Python dependencies in virtual environment
#   5. Configure Device Tree overlays (McASP, UART4)
#   6. Create audio directory
#   7. Create config.yaml from template
#   8. Add user to dialout group
#   9. Prompt for reboot (required for Device Tree changes)
#
# Prerequisites:
#   - BeagleBone Green Wireless with Debian Linux
#   - Internet connection (for package installation)
#   - Root/sudo access
#
# Notes:
#   - Device Tree overlays will be configured but require reboot
#   - UART4 will be enabled on P9.11 (RXD) and P9.13 (TXD)
#   - McASP overlay configuration may need manual adjustment
#   - See docs/BBGW_DEVICE_TREE_GUIDE.md for custom overlay creation

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "============================================================"
echo "BeagleBone Green Wireless I2S Source — Setup Script"
echo "============================================================"
echo ""

# Check if running on BeagleBone
if [ ! -f /etc/dogtag ]; then
    echo -e "${YELLOW}Warning: This doesn't appear to be a BeagleBone.${NC}"
    echo -e "${YELLOW}This script is designed for BeagleBone Green Wireless.${NC}"
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Setup cancelled."
        exit 1
    fi
fi

# Detect user (debian or root)
if [ "$USER" = "root" ]; then
    BBGW_USER="debian"
    BBGW_HOME="/home/debian"
else
    BBGW_USER="$USER"
    BBGW_HOME="$HOME"
fi

echo -e "${GREEN}Setup will be performed for user: $BBGW_USER${NC}"
echo ""

# ============================================================
# Step 1: Update System Packages
# ============================================================
echo "Step 1: Updating system packages..."
sudo apt update
sudo apt upgrade -y
echo -e "${GREEN}✓ System packages updated${NC}"
echo ""

# ============================================================
# Step 2: Install System Dependencies
# ============================================================
echo "Step 2: Installing system dependencies..."
sudo apt install -y python3 python3-pip python3-venv python3-dev
sudo apt install -y build-essential git
sudo apt install -y alsa-utils libasound2-dev
sudo apt install -y device-tree-compiler  # For Device Tree overlays
echo -e "${GREEN}✓ System dependencies installed${NC}"
echo ""

# ============================================================
# Step 3: Create Python Virtual Environment
# ============================================================
echo "Step 3: Creating Python virtual environment..."
VENV_DIR="$BBGW_HOME/esp32_btaudio/bbgw_i2s_source/venv"

if [ -d "$VENV_DIR" ]; then
    echo -e "${YELLOW}Virtual environment already exists. Skipping creation.${NC}"
else
    cd "$BBGW_HOME/esp32_btaudio/bbgw_i2s_source"
    python3 -m venv venv
    echo -e "${GREEN}✓ Virtual environment created${NC}"
fi
echo ""

# ============================================================
# Step 4: Install Python Dependencies
# ============================================================
echo "Step 4: Installing Python dependencies..."
cd "$BBGW_HOME/esp32_btaudio/bbgw_i2s_source"
source venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
deactivate
echo -e "${GREEN}✓ Python dependencies installed${NC}"
echo ""

# ============================================================
# Step 5: Configure UART4 Device Tree
# ============================================================
echo "Step 5: Configuring UART4 Device Tree overlay..."

# Check if UART4 is already enabled in /boot/uEnv.txt
if grep -q "BB-UART4" /boot/uEnv.txt; then
    echo -e "${YELLOW}UART4 overlay already configured in /boot/uEnv.txt${NC}"
else
    echo "Enabling UART4 in /boot/uEnv.txt..."
    
    # Backup /boot/uEnv.txt
    sudo cp /boot/uEnv.txt /boot/uEnv.txt.backup
    
    # Add UART4 overlay
    echo "uboot_overlay_addr4=/lib/firmware/BB-UART4-00A0.dtbo" | sudo tee -a /boot/uEnv.txt > /dev/null
    
    echo -e "${GREEN}✓ UART4 overlay enabled${NC}"
    echo -e "${YELLOW}⚠ Reboot required for UART4 to take effect${NC}"
fi
echo ""

# ============================================================
# Step 6: Configure McASP Device Tree for I2S
# ============================================================
echo "Step 6: Configuring McASP Device Tree for I2S..."
echo -e "${YELLOW}⚠ McASP overlay configuration is complex.${NC}"
echo -e "${YELLOW}⚠ This setup script provides basic guidance.${NC}"
echo -e "${YELLOW}⚠ See docs/BBGW_DEVICE_TREE_GUIDE.md for detailed instructions.${NC}"
echo ""

# Check for existing I2S overlays
if ls /lib/firmware/BB-*I2S*.dtbo 1> /dev/null 2>&1; then
    echo -e "${GREEN}Found existing I2S overlay(s):${NC}"
    ls /lib/firmware/BB-*I2S*.dtbo
    echo ""
    echo "To enable an existing overlay, edit /boot/uEnv.txt:"
    echo "  sudo nano /boot/uEnv.txt"
    echo "  Add: uboot_overlay_addr5=/lib/firmware/BB-I2S0-00A0.dtbo"
    echo ""
else
    echo -e "${YELLOW}No existing I2S overlays found.${NC}"
    echo "You will need to create a custom Device Tree overlay for McASP."
    echo "See docs/BBGW_DEVICE_TREE_GUIDE.md for instructions."
    echo ""
fi

echo -e "${YELLOW}⚠ Manual McASP configuration required after this script.${NC}"
echo ""

# ============================================================
# Step 7: Add User to dialout Group
# ============================================================
echo "Step 7: Adding user to dialout group (for UART access)..."

if groups $BBGW_USER | grep -q '\bdialout\b'; then
    echo -e "${YELLOW}User $BBGW_USER already in dialout group${NC}"
else
    sudo usermod -a -G dialout $BBGW_USER
    echo -e "${GREEN}✓ User $BBGW_USER added to dialout group${NC}"
    echo -e "${YELLOW}⚠ Log out and back in for group change to take effect${NC}"
fi
echo ""

# ============================================================
# Step 8: Create Audio Directory
# ============================================================
echo "Step 8: Creating audio directory..."

AUDIO_DIR="$BBGW_HOME/audio"
if [ -d "$AUDIO_DIR" ]; then
    echo -e "${YELLOW}Audio directory already exists: $AUDIO_DIR${NC}"
else
    mkdir -p "$AUDIO_DIR"
    echo -e "${GREEN}✓ Audio directory created: $AUDIO_DIR${NC}"
fi
echo ""

# ============================================================
# Step 9: Create config.yaml
# ============================================================
echo "Step 9: Creating config.yaml from template..."

CONFIG_FILE="$BBGW_HOME/esp32_btaudio/bbgw_i2s_source/config.yaml"
TEMPLATE_FILE="$BBGW_HOME/esp32_btaudio/bbgw_i2s_source/config.yaml.template"

if [ -f "$CONFIG_FILE" ]; then
    echo -e "${YELLOW}config.yaml already exists. Skipping creation.${NC}"
else
    if [ -f "$TEMPLATE_FILE" ]; then
        cp "$TEMPLATE_FILE" "$CONFIG_FILE"
        echo -e "${GREEN}✓ config.yaml created from template${NC}"
    else
        echo -e "${RED}✗ Template file not found: $TEMPLATE_FILE${NC}"
        echo "Please create config.yaml manually."
    fi
fi
echo ""

# ============================================================
# Step 10: Verify ALSA
# ============================================================
echo "Step 10: Verifying ALSA installation..."

if aplay -l > /dev/null 2>&1; then
    echo -e "${GREEN}✓ ALSA installed correctly${NC}"
    echo "Available ALSA devices:"
    aplay -l
else
    echo -e "${RED}✗ ALSA not working correctly${NC}"
    echo "Check ALSA installation: sudo apt install --reinstall alsa-utils"
fi
echo ""

# ============================================================
# Summary
# ============================================================
echo "============================================================"
echo "Setup Complete!"
echo "============================================================"
echo ""
echo -e "${GREEN}✓ System packages updated${NC}"
echo -e "${GREEN}✓ Python dependencies installed${NC}"
echo -e "${GREEN}✓ UART4 Device Tree configured${NC}"
echo -e "${GREEN}✓ User added to dialout group${NC}"
echo -e "${GREEN}✓ Audio directory created${NC}"
echo -e "${GREEN}✓ config.yaml created${NC}"
echo ""
echo -e "${YELLOW}⚠ IMPORTANT: Manual steps required:${NC}"
echo ""
echo "1. Configure McASP Device Tree overlay for I2S:"
echo "   - See: docs/BBGW_DEVICE_TREE_GUIDE.md"
echo "   - Create custom overlay or use existing BB-I2S0 overlay"
echo "   - Edit /boot/uEnv.txt to enable overlay"
echo ""
echo "2. Verify UART4 device after reboot:"
echo "   ls -l /dev/ttyO4"
echo ""
echo "3. Verify ALSA McASP device after reboot:"
echo "   aplay -l"
echo ""
echo "4. REBOOT REQUIRED for Device Tree changes to take effect:"
echo "   sudo reboot"
echo ""
echo "After reboot, test the application:"
echo "   cd $BBGW_HOME/esp32_btaudio/bbgw_i2s_source"
echo "   source venv/bin/activate"
echo "   python3 main.py"
echo ""
echo "============================================================"
echo ""

# Prompt for reboot
read -p "Reboot now to apply Device Tree changes? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Rebooting..."
    sudo reboot
else
    echo "Please reboot manually when ready: sudo reboot"
fi

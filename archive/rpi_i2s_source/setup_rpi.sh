#!/bin/bash
# Raspberry Pi I2S Source - Setup Script
# Run this script on the Raspberry Pi to configure the environment
# Usage: bash setup_rpi.sh

set -e  # Exit on any error

echo "========================================="
echo "Raspberry Pi I2S Source - Setup Script"
echo "========================================="
echo ""

# Check if running on Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
    echo "WARNING: This doesn't appear to be a Raspberry Pi!"
    echo "Some configuration steps may fail."
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Step 1: Update system packages
echo "[1/6] Updating system packages..."
sudo apt update
sudo apt upgrade -y

# Step 2: Install Python dependencies
echo "[2/6] Installing Python and development tools..."
sudo apt install -y python3-pip python3-venv python3-dev
sudo apt install -y build-essential  # For compiling Python packages

# Step 3: Install I2S/Audio packages
echo "[3/6] Installing I2S/Audio packages..."
read -p "Use ALSA I2S driver (recommended for MVP)? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo apt install -y alsa-utils libasound2-dev
    echo "ALSA installed. Uncomment 'pyalsaaudio' in requirements.txt before pip install."
else
    read -p "Use pigpio I2S driver (advanced)? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo apt install -y pigpio python3-pigpio
        sudo systemctl enable pigpiod
        sudo systemctl start pigpiod
        echo "pigpio installed and daemon started."
    fi
fi

# Step 4: Configure UART (disable Bluetooth on UART)
echo "[4/6] Configuring UART..."
if ! grep -q "dtoverlay=disable-bt" /boot/config.txt 2>/dev/null && \
   ! grep -q "dtoverlay=disable-bt" /boot/firmware/config.txt 2>/dev/null; then
    read -p "Disable Bluetooth on UART (required for UART communication)? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        # Try /boot/config.txt first (older systems), then /boot/firmware/config.txt (newer)
        if [ -f /boot/config.txt ]; then
            echo "dtoverlay=disable-bt" | sudo tee -a /boot/config.txt
            echo "Added dtoverlay=disable-bt to /boot/config.txt"
        elif [ -f /boot/firmware/config.txt ]; then
            echo "dtoverlay=disable-bt" | sudo tee -a /boot/firmware/config.txt
            echo "Added dtoverlay=disable-bt to /boot/firmware/config.txt"
        else
            echo "ERROR: Could not find config.txt in /boot or /boot/firmware"
            exit 1
        fi
        
        # Also disable Bluetooth service
        sudo systemctl disable hciuart 2>/dev/null || true
        sudo systemctl disable bluetooth 2>/dev/null || true
        
        echo "UART configured. Reboot required to take effect."
        REBOOT_REQUIRED=1
    fi
else
    echo "UART already configured (dtoverlay=disable-bt found)."
fi

# Step 5: Create Python virtual environment
echo "[5/6] Creating Python virtual environment..."
INSTALL_DIR="/home/pi/rpi_i2s_source"
if [ ! -d "$INSTALL_DIR" ]; then
    echo "ERROR: Project directory not found: $INSTALL_DIR"
    echo "Please clone the repository first:"
    echo "  cd /home/pi"
    echo "  git clone https://github.com/ekkus93/esp32_btaudio.git"
    echo "  cd esp32_btaudio/rpi_i2s_source"
    exit 1
fi

cd "$INSTALL_DIR"

if [ ! -d "venv" ]; then
    python3 -m venv venv
    echo "Virtual environment created."
else
    echo "Virtual environment already exists."
fi

# Activate venv and install dependencies
source venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
echo "Python dependencies installed."

# Step 6: Create audio directory and config
echo "[6/6] Creating audio directory and configuration..."
mkdir -p /home/pi/audio
echo "Audio directory created: /home/pi/audio"

if [ ! -f "config.yaml" ]; then
    cp config.yaml.template config.yaml
    echo "Configuration file created: config.yaml"
    echo "Edit config.yaml to customize settings."
else
    echo "Configuration file already exists: config.yaml"
fi

# Add user to dialout group for UART access
if ! groups pi | grep -q dialout; then
    sudo usermod -a -G dialout pi
    echo "Added user 'pi' to 'dialout' group for UART access."
    echo "Log out and log back in for group changes to take effect."
    REBOOT_REQUIRED=1
fi

# Summary
echo ""
echo "========================================="
echo "Setup Complete!"
echo "========================================="
echo ""
echo "Project directory: $INSTALL_DIR"
echo "Virtual environment: $INSTALL_DIR/venv"
echo "Audio directory: /home/pi/audio"
echo ""

if [ "$REBOOT_REQUIRED" = "1" ]; then
    echo "⚠️  REBOOT REQUIRED for UART/group changes to take effect."
    echo ""
    read -p "Reboot now? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo reboot
    else
        echo "Please reboot manually: sudo reboot"
    fi
else
    echo "✅ No reboot required."
fi

echo ""
echo "Next steps:"
echo "  1. Verify UART device exists: ls -l /dev/serial0"
echo "  2. Connect ESP32 hardware (I2S + UART wiring)"
echo "  3. Start the application: cd $INSTALL_DIR && source venv/bin/activate && python main.py"
echo "  4. Access web UI: http://<raspberry-pi-ip>:5000"
echo ""

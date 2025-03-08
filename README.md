1️⃣ Install ESP-IDF
ESP-ADF depends on ESP-IDF, so install it first.

Step 1: Install Dependencies
Run the following command to install system packages required for ESP-IDF:

```bash
sudo apt update && sudo apt install -y \
    git wget curl flex bison gperf python3 python3-pip python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util
```

Step 2: Clone ESP-IDF Repository
ESP-ADF only supports certain ESP-IDF versions. Check the ESP-ADF documentation for compatibility. The most stable version is usually ESP-IDF v4.4 or later.

```bash
cd ~
git clone -b v5.1.2 --recursive https://github.com/espressif/esp-idf.git esp-idf
```

⚠️ Replace v5.1.2 with the latest supported version.

Step 3: Install ESP-IDF
Run the ESP-IDF installation script:

```bash
cd ~/esp-idf
./install.sh
```

Step 4: Export ESP-IDF Environment Variables
Run the following command to set up the environment:

```bash
source ~/esp-idf/export.sh
```

To make this permanent, add it to your .bashrc or .zshrc:

```bash
echo 'source ~/esp-idf/export.sh' >> ~/.bashrc
```

2️⃣ Install ESP-ADF
ESP-ADF extends ESP-IDF, so now we install it.

Step 1: Clone ESP-ADF Repository
```bash
cd ~
git clone --recursive https://github.com/espressif/esp-adf.git esp-adf
```

Step 2: Set Up ESP-ADF Environment
ESP-ADF must use the correct ESP-IDF version. To configure this, run:

bash
Copy
Edit
export ADF_PATH=~/esp-adf
source $ADF_PATH/esp-idf/export.sh
For permanent configuration, add this to .bashrc:

```bash
echo 'export ADF_PATH=~/esp-adf' >> ~/.bashrc
echo 'source $ADF_PATH/esp-idf/export.sh' >> ~/.bashrc
```

3️⃣ Create a New ESP-ADF Project
You can start with an example project or create a new one.

Option 1: Using an Example
ESP-ADF comes with several examples inside ~/esp-adf/examples. Try running an example:

```bash
cd ~/esp-adf/examples/get-started/play_mp3
idf.py set-target esp32
idf.py menuconfig
idf.py flash monitor
```

Option 2: Creating a New ESP-ADF Project
To create a fresh project:

```bash
cd ~
idf.py create-project my-audio-project
cd my-audio-project
```

Then, copy the necessary ESP-ADF components into your project:

```bash
mkdir components
cp -r ~/esp-adf/components/audio_pipeline components/
```

Modify CMakeLists.txt to include ESP-ADF libraries.

4️⃣ Build & Flash the Project
Step 1: Configure the Target
```bash
idf.py set-target esp32
```

Step 2: Configure the Project
```bash
idf.py menuconfig
```

Set up Audio Pipeline, Wi-Fi, or Bluetooth settings as needed.
Step 3: Build & Flash
```bash
idf.py build flash monitor
```

pair 48:78:5e:d9:35:a3
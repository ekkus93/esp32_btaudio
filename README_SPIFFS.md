# Building and Flashing the SPIFFS Image

## 1. Prepare the Data Folder  
Place your MP3 file at:  
```
/home/phil/work/esp32/esp32_btaudio/data/sound.mp3
```

## 2. Build the SPIFFS Image  
Run the following command:
```
python ${ADF_PATH}/esp-idf/components/spiffs/spiffsgen.py 1048576 data spiffs.bin
```

## 3. Upload SPIFFS Image
```
python ${ADF_PATH}/esp-idf/components/esptool_py/esptool/esptool.py --port /dev/ttyUSB0 write_flash 0x190000 spiffs.bin
```
# Building and Flashing the SPIFFS Image

## 1. Prepare the Data Folder  
Place your MP3 file at:  
```
/home/phil/work/esp32/esp32_btaudio/data/sound.mp3
```

## 2. Build the SPIFFS Image  
Run the following command:
```
#mkspiffs -c data -b 4096 -p 256 -s 0x1F0000 spiffs.bin
python /home/phil/.platformio/packages/framework-espidf/components/spiffs/spiffsgen.py 2031616     /home/phil/work/esp32/esp32_btaudio/data     spiffs.bin
```

## 3. Upload SPIFFS Image
```
/home/phil/.platformio/packages/tool-esptoolpy/esptool.py --port /dev/ttyUSB0 write_flash 0x210000 spiffs.bin
```
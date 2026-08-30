### idf.py set-target esp32

### idf.py add-dependency "espressif/esp32-camera^2.0.8"

unset IDF_TARGET

idf.py fullclean

idf.py set-target esp32
idf.py build
idf.py flash monitor

#enable psram
idf.py menuconfig
Component config -> ESP PSRAM -> 
Component config -> ESP System Settings
Component config -> Bluetooth
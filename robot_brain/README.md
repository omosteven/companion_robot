# Install the driver

idf.py add-dependency "atanisoft/esp_lcd_ili9488^1.1.1"

idf.py set-target esp32s3

idf.py build

idf.py fullclean

idf.py -p /dev/cu.usbmodem5AE70714221 flash
idf.py -p /dev/cu.usbmodem5AE70714221 monitor

idf.py flash monitor

idf.py menuconfig

TURN PSRAM 
Component Config->ESP PSRAM->SPIRAM config->
Mode->Octal -> Press S to save

TURN COMPILER OPTIMIZATION
Compiler options->Optimization level ->Optimize for performance (-02)

download
idf.py add-dependency "espressif/esp_jpeg^1.3.1"

idf.py add-dependency "espressif/esp_new_jpeg^1.0.2"
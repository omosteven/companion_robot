# COMPANION ROBOT
Note: An Official Name Will Be Given To It Later. Also this is a project in progress, so documentation will be updated often as it progresses.

## VIDEOS
You can access some videos and images of the robot under videos_images folder while the development is still ongoing.
Also displayed here below:
<div style="display:flex; width:100%">

![alt text](<videos_images/2026-09-05 22.21.52.jpg>)
![alt text](<videos_images/2026-09-05 22.21.56.jpg>)
![alt text](<videos_images/2026-09-05 22.22.22.jpg>)
</div>
<video controls src="videos_images/IMG_0663.MOV" title="Video1"></video>
<video controls src="videos_images/IMG_0734.MOV" title="Video2"></video>

## HARDWARE
- Servo (MG996R) x 2
- ESP32S3 (Freenove: comes with 8MB PSRAM)
- ESP32 Camera + Board
- TOF Sensor
- Infrared Sensor
- Temperature + Humidity Sensor
- Low RPM GEARED 6v Motor * 2
- Dual Motor Driver (TB6612 DRV8833)
- Jumper Cables
- Elastic Strings
- LCD Display
- LIPO Battery (7.4v)
- INMP411 Microphone * 2
- 4 ohms Speaker * 2
- Digital Amplifier (PAM8403)
- Stereo DAC Module (UDA1334A)
- IMU MPU6050 Sensor
- LM2596 DC-DC 5V Step Down Voltage Regulator
- DC-DC Bulk Voltage Step Down Converter
- Capacitors, Resistors

## SOFTWARE
- ESP-IDF Package
- ESP-Camera
- C/C++
- LLM
- OpenCV + Computer Vision
- SLAM
- Sound Localization
- PID & Self-Balance Algorithm
- Real-time Decision Making Algorithm
- Control System (Differential Steering, Balancing, etc)
- AutoDesk Fusion360

## TOOLS
- 3D Printer (Creality)
- Filament
- Multimeter
- Screws & Screw driver set
- Measurement Meter

## COMMUNICATION SYSTEM
- Microphone -> Brain : I2S
- LCD -> Brain : SPI
- Camera -> Brain: UART
- Brain -> DAC -> Amplifier -> Speaker: I2C

## FOLDER STRUCTURE
- robot_brain
    - main
        - main.cpp
        - idf_component.yml
    - components
        - audio
        - camera
        - display
        - imu
        - neck
        - vision
        - wheel
- robot_camera
    - main
        - main.c
        - CMakeLists.txt
        - idf_component.yml
- cad_files

## WANT TO CONTRIBUTE? - TECHNICAL SKILLS NEEDED
- Machine Learning
- Computer Vision
- Reinforcement Learning
- C++/C
- Electronics Engineering
- Embedded Systems

## THINGS IT SHOULD DO
- MOVE AROUND
- Interact With Human
- Listen to Commands (Come, Go, Questions)
- Create Alarm, Reminders
- More things: In documention progress


## WHEEL 
Motor Pin Out
- Red : Motor Power +
- Black : Encoder Power -> GND
- Yellow: Encoder Phase A (signal)
- Green: Encoder Phase B (signal)
- Purple: Encoder Power (3.3v)
- White: Motor Power - 

Driver
VM - Motor Voltage
GND - Ground
VCC - Logic Voltage - 3.3v
STBY - PULLED HIGH to enable the driver
PWM (A/B) - Speed Control Motor A/B -> GPIO PWM PIN
AIN1 - Direction 1 Motor A -> GPIO PIN
AIN2 - Direction 2 Motor A -> GPIO PIN
BIN1 - Direction 1 Motor B -> GPIO PIN
BIN2 - Direction 2 Motor B -> GPIO PIN

            LCD        DRIVER    MOTOR (+ ENCODER)           ESP          BATTERY
            ----       AO1 ->    Motor 1 Red/White
            ----        AO2 ->    Motor 1 White/Red

                    BO1 ->    Motor 2 Red/White
                    BO2 ->    Motor 2 White/Red

                    ------    Motor 1 Purple             -> ESP VCC
                    ------    Motor 1 Black              -> ESP GND  -> NEGATIVE
                    ------    Motor 1 Yellow (Phase A)   -> GPIO
                    ------    Motor 1 Green (Phase B)    -> GPIO

                    ------    Motor 2 Purple             -> ESP VCC
                    ------    Motor 2 Black              -> ESP GND  -> NEGATIVE
                    ------    Motor 2 Yellow (Phase A)   -> GPIO
                    ------    Motor 2 Green (Phase B)    -> GPIO
                    VM        -------------------        ------      -> POSITIVE
                    GND       -------------------        -> ESP GND  -> NEGATIVE
                    VCC       -------------------        -> ESP VCC

                    STBY      -------------------        -> GPIO(High)
                    PWMA      -------------------        -> GPIO
                    PWMB      -------------------        -> GPIO
                    AIN1      -------------------        -> GPIO
                    AIN2      -------------------        -> GPIO
                    BIN1      -------------------        -> GPIO
                    BIN2      -------------------        -> GPIO








DRIVER    MOTOR (+ ENCODER)           ESP          BATTERY
AO1 ->    Motor 1 Red/White
AO2 ->    Motor 1 White/Red

BO1 ->    Motor 2 Red/White
BO2 ->    Motor 2 White/Red

------    Motor 1 Purple             -> ESP VCC
------    Motor 1 Black              -> ESP GND  -> NEGATIVE
------    Motor 1 Yellow (Phase A)   -> GPIO 16
------    Motor 1 Green (Phase B)    -> GPIO 15


------    Motor 2 Purple             -> ESP VCC
------    Motor 2 Black              -> ESP GND  -> NEGATIVE
------    Motor 2 Yellow (Phase A)   -> GPIO 7
------    Motor 2 Green (Phase B)    -> GPIO 6
VM        -------------------        ------      -> POSITIVE
GND       -------------------        -> ESP GND  -> NEGATIVE
VCC       -------------------        -> ESP VCC

STBY      -------------------        -> GPIO(High) 8
PWMA      -------------------        -> GPIO 5
PWMB      -------------------        -> GPIO 4
AIN1      -------------------        -> GPIO 2
AIN2      -------------------        -> GPIO 1
BIN1      -------------------        -> GPIO 42
BIN2      -------------------        -> GPIO 41

ESP32S3 FREENOVE BOARD DATASHEET
Free GPIO(23): 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42
Strapping GPIOs(must not be pulled high/low strongly  during powerup but can use as regular): 0, 3, 45, 46
Internal GPIOs(not usable): 26 - 32, 33-37, 19, 20, 47, 48




## DEVELOPER
- Steven Omole-Adebomi


## 
<!-- Forward : Phase A goes High before Phase B
Backward: Phase B goes High before Phase A

A is low->Rise and B is Low) Increment
A is low->Rise and B is High) Decrement -->
# COMPANION ROBOT
Note: An Official Name Will Be Given To It Later. Also this is a project in progress, so documentation will be updated often as it progresses.

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

## DEVELOPER
- Steven Omole-Adebomi
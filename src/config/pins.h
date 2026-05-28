#pragma once

// Sensor & Actuator Pins
#define PIN_ULTRASONIC_TRIG   12    // AJ-SR04M Trigger (strapping pin!)
#define PIN_ULTRASONIC_ECHO   13    // AJ-SR04M Echo (pull-down 10kΩ)
#define PIN_RAIN_SENSOR       GPIO_NUM_15  // Rain DO — EXT0 wake (active LOW, strapping pin!)
#define PIN_STATUS_LED        33    // On-board red LED
#define PIN_FLASH_LED         4     // Camera flash LED

// OV2640 Camera Pins (AI-Thinker Board)
#define CAM_PIN_PWDN          32
#define CAM_PIN_RESET         -1    // Not connected
#define CAM_PIN_XCLK          0
#define CAM_PIN_SIOD          26    // SDA
#define CAM_PIN_SIOC          27    // SCL
#define CAM_PIN_D7            35    // Y9
#define CAM_PIN_D6            34    // Y8
#define CAM_PIN_D5            39    // Y7
#define CAM_PIN_D4            36    // Y6
#define CAM_PIN_D3            21    // Y5
#define CAM_PIN_D2            19    // Y4
#define CAM_PIN_D1            18    // Y3
#define CAM_PIN_D0            5     // Y2
#define CAM_PIN_VSYNC         25
#define CAM_PIN_HREF          23
#define CAM_PIN_PCLK          22

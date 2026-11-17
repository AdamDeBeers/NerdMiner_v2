#ifndef _ADAM_LCD1602_H
#define _ADAM_LCD1602_H

// Adam: ESP32 devkit (esp32dev) + 16x2 character LCD with PCF8574 I2C backpack
#ifndef SDA_PIN
#define SDA_PIN 21
#endif
#ifndef SCL_PIN
#define SCL_PIN 22
#endif
#ifndef LCD1602_ADDR
#define LCD1602_ADDR 0x27
#endif

#define PIN_BUTTON_1 0

#define ADAM_LCD1602_DISPLAY

#endif

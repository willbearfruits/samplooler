// Pin map: the Samplooler's S3 wiring (firmware/main/pins.h in github.com/willbearfruits/samplooler), which is also
// the ESP32-S3 SuperMini kit's. Runs on the S3 boards: the Stack, the Mini and the breadboard kit.
#pragma once
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "builds for the ESP32-S3 only"
#endif

#define PIN_BOOT      0    // BOOT button, active low: a second REC
#define PIN_JOY_Y     1    // joystick VRy, ADC1 (ADC2 stops working while Wi-Fi runs)
#define PIN_JOY_X     2    // joystick VRx, ADC1
#define PIN_REC       3    // record button to GND (on the kit: the joystick's click)
#define PIN_ENC_A     4
#define PIN_ENC_B     5
#define PIN_ENC_SW    6    // active low
#define PIN_I2S_BCLK  7    // -> INMP441 SCK, PCM5102A BCK, MAX98357A BCLK
#define PIN_I2S_WS    8    // -> INMP441 WS,  PCM5102A LCK, MAX98357A LRC
#define PIN_I2S_DOUT  9    // -> PCM5102A DIN (and the MAX98357A's DIN)
#define PIN_I2S_DIN   10   // <- INMP441 SD
#define PIN_AMP_SD    11   // speaker amp: driven high once the I2S clock is good
#define PIN_I2C_SDA   12   // MPU6050 at 0x68, OLED at 0x3C
#define PIN_I2C_SCL   13
#define PIN_LED       48   // the SuperMini's WS2812 RGB LED
#define OLED_W        128
#define OLED_H        64
#define HAS_USB       1    // native USB: MIDI + sound card
#define ENC_STEPS     4    // counts per detent
#define ENC_DIR      -1    // the Stack board's encoder; the kit's counts the other way (set 1)

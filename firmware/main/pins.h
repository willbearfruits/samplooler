// Pin maps. The S3 map is identical to the ESP32-S3 SuperMini kit
// (~/Projects/kit/docs/pinmap.md). The C3 map fits the ESP32-C3 SuperMini and
// the 01Space "ESP32-C3 0.42 OLED" board, whose screen is already on 5/6.
#pragma once
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3
#define PIN_BOOT      9    // BOOT on the C3 is GPIO 9 (strapping: held across a reset = download mode)
#define PIN_I2S_BCLK  4    // -> INMP441 SCK, PCM5102A BCK
#define PIN_I2S_WS    3    // -> INMP441 WS,  PCM5102A LCK
#define PIN_I2S_DOUT  10   // -> PCM5102A DIN
#define PIN_I2S_DIN   7    // <- INMP441 SD
#define PIN_I2C_SDA   5    // 0.42" OLED (0x3C) + MPU6050 (0x68)
#define PIN_I2C_SCL   6
#define OLED_ENABLE   1
#define OLED_W        72   // 0.42" panel
#define OLED_H        40
#define PIN_AMP_SD    -1   // no speaker amp
#define HAS_CONTROLS  1    // encoder: tap = menu / dive / back, hold = leave the menu, turn = choose or adjust
#define HAS_JOY       0    // no joystick
#define PIN_REC       -1   // no separate record button: BOOT records
#define HAS_USB       0    // the C3 has no USB OTG
#define PIN_ENC_A     0    // encoder CLK
#define PIN_ENC_B     1    // encoder DT
#define PIN_ENC_SW    20   // encoder push, active low (internal pull-up)
#define ENC_STEPS     4    // counts per detent
// Left alone: 8 (blue LED, strapping high), 2 (strapping), 18/19 USB. Free: 21.
#else
#define PIN_BOOT      0    // BOOT button, active low
#define PIN_I2S_BCLK  7    // -> INMP441 SCK, PCM5102A BCK
#define PIN_I2S_WS    8    // -> INMP441 WS,  PCM5102A LCK
#define PIN_I2S_DOUT  9    // -> PCM5102A DIN   (PCM5102A: SCK to GND, XSMT bridged H, or it is silent)
#define PIN_I2S_DIN   10   // <- INMP441 SD     (L/R to GND or VDD, both accepted)
#define PIN_I2C_SDA   12   // MPU6050 at 0x68, plus the kit's OLED at 0x3C
#define PIN_I2C_SCL   13
#define OLED_ENABLE   1
#define OLED_W        128  // the kit's 0.96" panel
#define OLED_H        64
#define PIN_AMP_SD    11   // kit speaker amp: driven high so the speaker plays; set -1 to leave it off
#define HAS_CONTROLS  1    // encoder: tap = menu / dive / back, hold = leave the menu, turn = choose or adjust
#define HAS_JOY       0    // 1 on a kit with the KY-023 joystick: Y sets the speed when tilt is off, X the pitch
#define PIN_REC       3    // record button to GND (the Mini board's REC; the joystick's old click pin)
#define HAS_USB       1    // native USB: console + MIDI + sound card
#define PIN_JOY_X     2    // ADC1 (ADC2 dies with WiFi)   (HAS_JOY only)
#define PIN_JOY_Y     1
#define PIN_JOY_SW    3    // active low
#define PIN_ENC_A     4
#define PIN_ENC_B     5
#define PIN_ENC_SW    6    // active low: menu
#define ENC_STEPS     4    // counts per detent
#endif

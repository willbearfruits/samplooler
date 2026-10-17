// Pin map, identical to the ESP32-S3 SuperMini kit (~/Projects/kit/docs/pinmap.md).
#pragma once

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
#define PIN_JOY_X     2    // ADC1 (ADC2 dies with WiFi)   (HAS_JOY only)
#define PIN_JOY_Y     1
#define PIN_JOY_SW    3    // active low
#define PIN_ENC_A     4
#define PIN_ENC_B     5
#define PIN_ENC_SW    6    // active low: menu
#define ENC_STEPS     4    // counts per detent
#define ENC_DIR      -1    // the stack board's encoder counts the other way round from the kit's

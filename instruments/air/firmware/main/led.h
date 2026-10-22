// The SuperMini's WS2812 RGB LED on GPIO 48, through the RMT. UI task only.
#pragma once
#include <stdint.h>
void led_init(void);
void led_set(uint8_t r, uint8_t g, uint8_t b);

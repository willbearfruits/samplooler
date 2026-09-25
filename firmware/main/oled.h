// SSD1306 over I2C (address 0x3C): the 0.42" 72x40 panel or the 0.96" 128x64.
// Framebuffer pushed in one transfer from the UI task. Never called from the audio task.
#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "pins.h"

bool oled_init(i2c_master_bus_handle_t bus);
bool oled_present(void);
void oled_clear(void);
void oled_pixel(int x, int y, bool on);
bool oled_get(int x, int y);
void oled_rect(int x, int y, int w, int h, bool on);     // filled
void oled_frame(int x, int y, int w, int h);             // 1 px outline
void oled_text(int x, int y, const char *s);             // 5x7 font, 6 px advance
void oled_text2(int x, int y, const char *s);            // same font doubled: 10x14, 12 px advance
void oled_line(int x0, int y0, int x1, int y1, bool on);
void oled_circle(int cx, int cy, int r, bool on);        // outline
void oled_disc(int cx, int cy, int r, bool on);          // filled
void oled_invert(int x, int y, int w, int h);            // XOR a block
void oled_flush(void);

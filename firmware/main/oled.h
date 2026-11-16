// SSD1306 over I2C (address 0x3C): the 0.96" 128x64 panel.
// One framebuffer, drawn and flushed by the screen's task alone (main.c's ui_task; before it starts, by app_main).
// Never called from the audio task.
#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "pins.h"
#include "font.h"

void oled_fast(bool on);                                 // before oled_init: the panel's bus at 800 kHz instead of 400
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
void oled_rrect(int x, int y, int w, int h, int r, bool on);   // filled, rounded corners
void oled_rframe(int x, int y, int w, int h, int r);           // rounded outline
void oled_dither(int x, int y, int w, int h, int level, bool on);  // level 0..4 of a 2x2 pattern: a grey
void oled_hline_pat(int x, int y, int w, uint8_t pat, bool on);    // dotted line, pat bit i = pixel x+i

// Proportional fonts (font.h). Return the x after the text; _w measures. Tracking adds px between glyphs.
int  oled_ftext(const gfx_font_t *f, int x, int y, const char *s);
int  oled_ftext_ex(const gfx_font_t *f, int x, int y, const char *s, int tracking);
int  oled_ftext_w(const gfx_font_t *f, const char *s, int tracking);
int  oled_ftext_c(const gfx_font_t *f, int cx, int y, const char *s, int tracking);   // centred on cx
int  oled_ftext_r(const gfx_font_t *f, int rx, int y, const char *s, int tracking);   // right edge at rx
int  oled_ftext2(const gfx_font_t *f, int x, int y, const char *s, int tracking);     // twice the size; returns the x after
int  oled_ftext2_w(const gfx_font_t *f, const char *s, int tracking);
void oled_clip(int x, int y, int w, int h);              // everything drawn stays inside this window, until
void oled_clip_off(void);                                // the whole panel again
// A capsule with text: filled (white on black text) or outlined. Returns its width.
int  oled_pill(const gfx_font_t *f, int x, int y, const char *s, bool filled);
int  oled_pill_w(const gfx_font_t *f, const char *s);
void oled_flush(void);                                   // sends what changed since the last one, a page at a time
void oled_dim(bool on);                                  // a quarter of the light, for a panel left lying (from the next flush)
int  oled_sent_bytes(void);                              // bytes the last flush put on the wire

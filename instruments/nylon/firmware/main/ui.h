// The drawing kit: the header band, meters, pills, sprites, particles, easing. Pure drawing on oled.c's
// frame buffer, no device code: tools/sim builds it as is.
//
// The 128x64 screen: rows 0..15 are the header band (yellow on two-colour panels: keep only header things
// there); rows 16..63 are the scene. The core may draw over the bottom 18 rows (view_t.cover).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "oled.h"
#include "font.h"

// The header band: on the left a pill (filled for something live: REC, LOOP) or, with left NULL, the
// looper's state (instruments with INST.looper) or the instrument's name; the sound's name (with * once
// edited) towards the right; a freeze padlock when motion is held; the output meter at the right edge.
void ui_head(const char *left, bool filled);
void ui_meter(int x, int y, int w, int h, float v, bool clip);   // segmented, LED style
void ui_pill_r(int rx, int y, const char *s, bool filled);       // right edge at rx (F_SMALL)
void ui_box(int cy, const char *s);                              // a message in a rounded box, centred
void ui_slider(int x, int y, int w, float frac);                 // a dotted track and a thumb, 7 rows
void ui_vbar(int x, int y, int w, int h, float v);               // outlined, filled from the bottom
void ui_arc(int cx, int cy, int r, float a0, float a1);          // degrees, 0 = right, clockwise on screen
void ui_note_name(int midi, char *b, int n);                     // 69 -> "A4"
int  ui_text_fit(const gfx_font_t *f, int x, int y, int w, const char *s);   // cut with ".." to fit w
// Word-wrap s into at most max lines of w px; returns the lines used, or max + 1 if it does not fit.
int  ui_wrap(const gfx_font_t *f, const char *s, int w, char lines[][48], int max);
// The looper at (x, y), w wide, 9 rows: a state pill and the loop's progress (instruments with INST.looper)
void ui_loop(int x, int y, int w);

// Sprites as text: one string per row, '#' a lit pixel, anything else nothing. Width = strlen(rows[0]).
void ui_sprite(int x, int y, const char *const *rows, int h, bool flip_x, bool on);

// Easing, t in 0..1
float ease_out(float t);           // cubic
float ease_in_out(float t);        // smoothstep
float ease_back(float t);          // overshoots, then settles
float ease_bounce(float t);

// Particles: a fixed pool, stepped and drawn by the caller (in draw(), with dt = 1/25)
enum { PART_DOT, PART_SQUARE, PART_STREAK, PART_RING, PART_SPARK };
typedef struct { float x, y, vx, vy, life, max; uint8_t kind, size; } particle_t;
#define PARTS_MAX 96
typedef struct { particle_t p[PARTS_MAX]; int n; } parts_t;
void parts_emit(parts_t *ps, float x, float y, float vx, float vy, float life, int kind, int size);
void parts_burst(parts_t *ps, float x, float y, int count, float speed, float life, int kind, uint32_t *seed);
void parts_step(parts_t *ps, float dt, float gravity, float drag);
void parts_draw(const parts_t *ps);

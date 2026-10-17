// Proportional bitmap fonts, drawn as ASCII art in fonts/*.txt and converted by tools/fontconv.py into
// font_data.c. The format and the faces come from TWOFOLD's gfx component:
//   tiny   3x5 condensed caps + digits, for scale ticks and dense readouts
//   small  5 px caps, proportional, with kerning: labels
//   med    7 px caps + lowercase with descenders (9 rows): titles, values
//   big    18 px numerals (0-9 . - + :), 2 px stroke: the hero value
// Bits are column-major, LSB = top row of each byte. Missing glyphs advance by the space width.
#pragma once
#include <stdint.h>

typedef struct { uint16_t off; uint8_t w; uint8_t adv; } gfx_glyph_t;     // adv = w + spacing
typedef struct { uint8_t a, b; int8_t k; } gfx_kern_t;
#define GFX_FONT_UPPER_FALLBACK 0x01   // lowercase falls back to uppercase

typedef struct gfx_font {
    uint8_t h;               // rows per glyph (<= 32)
    uint8_t bpc;             // bytes per column
    uint8_t first, last;     // code range
    uint8_t cap;             // cap height
    uint8_t space;           // space advance
    uint8_t flags;
    uint8_t nkern;
    const gfx_glyph_t *glyphs;   // last - first + 1 entries (w = 0: missing)
    const uint8_t *bits;
    const gfx_kern_t *kern;      // sorted by (a, b)
} gfx_font_t;

extern const gfx_font_t gfx_font_tiny, gfx_font_small, gfx_font_med, gfx_font_big;
#define F_TINY  (&gfx_font_tiny)
#define F_SMALL (&gfx_font_small)
#define F_MED   (&gfx_font_med)
#define F_BIG   (&gfx_font_big)

// Symbol codes in the text fonts (string literals: "VOL " S_INF).
#define S_INF    "\x80"
#define S_RIGHT  "\x81"
#define S_LEFT   "\x82"
#define S_UP     "\x83"
#define S_DOWN   "\x84"

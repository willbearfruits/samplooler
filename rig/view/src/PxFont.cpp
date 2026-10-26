#include "PxFont.h"

#include <cmath>

namespace slrig {

namespace {
struct PxGlyphData { uint8_t code, w; uint16_t off; };
struct PxKernData { uint8_t a, b; int8_t d; };
struct PxFaceData {
    const char* name;
    int height, cap, spacing, space;
    bool upper;
    const PxGlyphData* glyphs;
    int nglyphs;
    const uint32_t* rows;
    const PxKernData* kern;
    int nkern;
};
#include "PxFontData.inc"

float g_ratio = 1.0f;

const PxGlyphData* findGlyph(const PxFaceData& f, int code)
{
    for (int i = 0; i < f.nglyphs; i++)
        if (f.glyphs[i].code == code) return &f.glyphs[i];
    return nullptr;
}

const PxGlyphData* glyphFor(const PxFaceData& f, int code)
{
    const PxGlyphData* g = findGlyph(f, code);
    if (!g && code >= 'a' && code <= 'z') g = findGlyph(f, code - 32);
    if (!g && code >= 'A' && code <= 'Z') g = findGlyph(f, code + 32);
    return g;
}

int kernOf(const PxFaceData& f, int a, int b)
{
    for (int i = 0; i < f.nkern; i++)
        if (f.kern[i].a == a && f.kern[i].b == b) return f.kern[i].d;
    return 0;
}

// UTF-8 to the faces' codes: ASCII, plus the firmware's symbols at 0x80..0x8f
int nextCode(const char*& s)
{
    unsigned char c = static_cast<unsigned char>(*s);
    if (c < 0x80) { s++; return c; }
    unsigned cp = 0;
    int n = 0;
    if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; n = 1; }
    else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; n = 2; }
    else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; n = 3; }
    s++;
    for (int i = 0; i < n && (static_cast<unsigned char>(*s) & 0xc0) == 0x80; i++, s++)
        cp = (cp << 6) | (static_cast<unsigned char>(*s) & 0x3f);
    switch (cp) {
    case 0x221e: return 0x80;    // ∞
    case 0x25b6: return 0x81;    // ▶ (right triangle)
    case 0x25c0: return 0x82;    // ◀
    case 0x25b2: return 0x83;    // ▲
    case 0x25bc: return 0x84;    // ▼
    case 0x00b7: return 0x85;    // ·
    case 0x00b0: return 0x86;    // °
    case 0x00d7: return 0x87;    // ×
    case 0x00b1: return 0x88;    // ±
    case 0x2192: return 0x89;    // →
    case 0x25cf: return 0x8f;    // ● (rec)
    default: return '?';
    }
}

float snap(float v) { return std::round(v * g_ratio) / g_ratio; }
} // namespace

void pxSetRatio(float r) { g_ratio = r > 0 ? r : 1.0f; }

float pxHeight(PxFace face, float cell) { return PX_FACES[face].height * cell; }

float pxWidth(PxFace face, const char* text, float cell)
{
    const PxFaceData& f = PX_FACES[face];
    int w = 0, prev = -1;
    for (const char* s = text; *s;) {
        int code = nextCode(s);
        if (prev >= 0) w += kernOf(f, prev, code);
        const PxGlyphData* g = code == ' ' ? nullptr : glyphFor(f, code);
        w += (g ? g->w : f.space) + f.spacing;
        prev = code;
    }
    if (w > 0) w -= f.spacing;
    return w * cell;
}

float pxText(NVGcontext* vg, PxFace face, float x, float y, float cell, const char* text, NVGcolor colour)
{
    const PxFaceData& f = PX_FACES[face];
    float cellDev = cell * g_ratio;
    float gapDev = cellDev >= 3.0f ? std::fmax(1.0f, std::round(cellDev * 0.16f)) : 0.0f;
    float gap = gapDev / g_ratio;
    x = snap(x);
    y = snap(y);
    int col = 0, prev = -1;
    nvgSave(vg);
    nvgShapeAntiAlias(vg, 0);
    nvgBeginPath(vg);
    for (const char* s = text; *s;) {
        int code = nextCode(s);
        if (prev >= 0) col += kernOf(f, prev, code);
        prev = code;
        const PxGlyphData* g = code == ' ' ? nullptr : glyphFor(f, code);
        if (!g) { col += f.space + f.spacing; continue; }
        for (int r = 0; r < f.height; r++) {
            uint32_t bits = f.rows[g->off + r];
            if (!bits) continue;
            if (gap > 0) {
                for (int c = 0; c < g->w; c++)
                    if (bits & (1u << (g->w - 1 - c)))
                        nvgRect(vg, x + (col + c) * cell, y + r * cell, cell - gap, cell - gap);
            } else {
                int c = 0;
                while (c < g->w) {          // runs of lit pixels as one rect
                    if (!(bits & (1u << (g->w - 1 - c)))) { c++; continue; }
                    int c0 = c;
                    while (c < g->w && (bits & (1u << (g->w - 1 - c)))) c++;
                    nvgRect(vg, x + (col + c0) * cell, y + r * cell, (c - c0) * cell, cell);
                }
            }
        }
        col += g->w + f.spacing;
    }
    nvgFillColor(vg, colour);
    nvgFill(vg);
    nvgRestore(vg);
    return col > 0 ? (col - f.spacing) * cell : 0.0f;
}

float pxTextR(NVGcontext* vg, PxFace face, float right, float y, float cell, const char* text, NVGcolor colour)
{
    float w = pxWidth(face, text, cell);
    pxText(vg, face, right - w, y, cell, text, colour);
    return w;
}

float pxTextC(NVGcontext* vg, PxFace face, float cx, float y, float cell, const char* text, NVGcolor colour)
{
    float w = pxWidth(face, text, cell);
    pxText(vg, face, cx - w * 0.5f, y, cell, text, colour);
    return w;
}

} // namespace slrig

// Pixel lettering in the firmware's own faces (firmware/main/fonts), as the app draws its labels: every lit font
// pixel is a square, with a small gap once the squares are big enough.
#pragma once
#include <cstdint>
#include <nanovg.h>

namespace slrig {

enum PxFace { PX_TINY = 0, PX_SMALL = 1, PX_MED = 2, PX_BIG = 3 };

void  pxSetRatio(float devicePixelsPerUnit);                     // once a frame
float pxWidth(PxFace face, const char* text, float cell);         // in drawing units
float pxHeight(PxFace face, float cell);
// Draws with the top-left at (x, y), snapped to device pixels; returns the advance. cell = one font pixel.
float pxText(NVGcontext* vg, PxFace face, float x, float y, float cell, const char* text, NVGcolor colour);
// Right-aligned / centred helpers
float pxTextR(NVGcontext* vg, PxFace face, float right, float y, float cell, const char* text, NVGcolor colour);
float pxTextC(NVGcontext* vg, PxFace face, float cx, float y, float cell, const char* text, NVGcolor colour);

} // namespace slrig

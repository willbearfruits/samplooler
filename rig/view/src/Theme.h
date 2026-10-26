// The Samplooler app's palette (app/ui/app.css) for NanoVG and GL.
#pragma once
#include <nanovg.h>

namespace slrig::theme {

inline NVGcolor hex(unsigned v, float a = 1.0f)
{
    return nvgRGBAf(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, a);
}

// app.css :root
constexpr unsigned BG = 0x060606, PANEL = 0x0d0d0c, INK = 0xece8de, MUTE = 0x9a968c, DIM = 0x5e5b55,
                   LINE = 0x232220, PH = 0x3a3835, STRIPE = 0x0e0e0d, BAD = 0xef5b5b, GOOD = 0x8fe0a8,
                   FIELD = 0x121211, NAVON = 0x161614, LOGBG = 0x000000, LOGTXT = 0xb9b5aa;
// the instruments' colours (the app's .strip.all): the Samplooler's is the first
constexpr unsigned YELLOW = 0xf2c94c, SKY = 0x7cc6f2, ORANGE = 0xf29b50, VIOLET = 0xa990f0, PINK = 0xf08ec0,
                   RED = 0xef5b5b, SALMON = 0xff9a8a, SAND = 0xe8dcc0, MINT = 0x8fe0a8, MAGENTA = 0xe46fd0,
                   TEAL = 0x5fd9cf, LIME = 0xc8e660;
constexpr unsigned STRIP[12] = {YELLOW, SKY, ORANGE, VIOLET, PINK, RED, SALMON, SAND, MINT, MAGENTA, TEAL, LIME};
constexpr float STRIP_AT[13] = {0, .09f, .18f, .27f, .36f, .45f, .54f, .63f, .72f, .81f, .90f, .95f, 1};

// what this tool uses them for
constexpr unsigned ACCENT = YELLOW;                   // the Samplooler's colour
constexpr unsigned AX_X = RED, AX_Y = MINT, AX_Z = SKY; // device axes, in plots and in 3D
constexpr unsigned GRAV = YELLOW;
constexpr unsigned PITCH = YELLOW, ROLL = VIOLET, YAW = TEAL;
constexpr unsigned BY_VIEW = MUTE, BY_CLI = SAND, BY_MCP = PINK, BY_OSC = TEAL;

// the OLED panel
constexpr unsigned OLED_YELLOW = 0xf5c63c, OLED_BLUE = 0x5aaef5, OLED_WHITE = 0xe9f1ff, OLED_OFF = 0x0a0e17;

} // namespace slrig::theme

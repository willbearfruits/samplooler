// A small immediate-mode UI on NanoVG, in the app's look: outlined buttons lettered in the pixel face, thin
// sliders, segmented controls, scroll areas. Widgets are drawn and hit-tested in the same call each frame.
#pragma once
#include "PxFont.h"
#include "Theme.h"

#include <nanovg.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace slrig {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool has(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    Rect shrink(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect shrink(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    // cut a strip off one side and return it; this rect keeps the rest
    Rect cutTop(float a) { Rect s{x, y, w, a}; y += a; h -= a; return s; }
    Rect cutBottom(float a) { Rect s{x, y + h - a, w, a}; h -= a; return s; }
    Rect cutLeft(float a) { Rect s{x, y, a, h}; x += a; w -= a; return s; }
    Rect cutRight(float a) { Rect s{x + w - a, y, a, h}; w -= a; return s; }
    Rect intersect(const Rect& o) const;
};

struct UiInput {
    float mx = -1, my = -1;
    bool down = false, pressed = false, released = false, dbl = false;
    bool rdown = false, rpressed = false;
    float wheel = 0;
    int mods = 0;                    // GLFW_MOD_*
    std::vector<int> keys;           // GLFW keys pressed (or repeated) this frame
};

enum BtnStyle { BTN_OUTLINE, BTN_SOLID, BTN_QUIET, BTN_ON };

class Ui {
public:
    NVGcontext* vg = nullptr;
    int sans = -1, bold = -1, mono = -1;
    UiInput in;
    uint64_t hot = 0, active = 0;
    double now = 0;
    float pxRatio = 1;
    float dragX0 = 0, dragY0 = 0, dragV0 = 0;
    bool wheelUsed = false;

    void begin(const UiInput& input, double t);
    void end();
    static uint64_t id(std::string_view s, int k = 0);

    // clipping (scroll areas): widgets outside the clip are not hit
    void pushClip(Rect r);
    void popClip();
    bool over(Rect r) const;
    // press inside, release inside: true once
    bool clickBehaviour(uint64_t wid, Rect r);
    bool pressedIn(Rect r) const { return in.pressed && over(r) && active == 0; }

    // drawing
    void fill(Rect r, NVGcolor c, float radius = 0);
    void stroke(Rect r, NVGcolor c, float width = 1.5f, float radius = 0);
    void hline(float x0, float x1, float y, NVGcolor c, float width = 1.0f);
    void vline(float x, float y0, float y1, NVGcolor c, float width = 1.0f);
    float text(float x, float y, std::string_view s, int font, float size, NVGcolor c,
               int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    float textW(std::string_view s, int font, float size);
    std::string fit(const std::string& s, int font, float size, float maxW);
    float wrap(float x, float y, float w, const std::string& s, int font, float size, NVGcolor c, float lineH = 1.35f);
    float wrapHeight(float w, const std::string& s, int font, float size, float lineH = 1.35f);
    // a section label, as the app's .label: pixel caps, then a rule to the right edge
    void label(Rect r, const char* s, NVGcolor c = theme::hex(theme::MUTE));

    // widgets
    bool button(uint64_t wid, Rect r, const char* pxLabel, int style = BTN_OUTLINE,
                NVGcolor accent = theme::hex(theme::ACCENT), bool enabled = true);
    // a slider; returns true while the value changes; `done` is set on release (send the last value then)
    bool slider(uint64_t wid, Rect r, float& v, float lo, float hi, float step, float def, bool& done,
                NVGcolor accent = theme::hex(theme::ACCENT));
    // segments in a row; returns the index clicked or -1
    int segmented(uint64_t wid, Rect r, const std::vector<std::string>& labels, int cur,
                  NVGcolor accent = theme::hex(theme::ACCENT));
    float segmentedMinWidth(const std::vector<std::string>& labels);
    // the width a button needs for its pixel label (2 px cells)
    static float btnW(const char* pxLabel, float pad = 18) { return pxWidth(PX_SMALL, pxLabel, 2) + pad; }

    struct Scroll {
        float off = 0, content = 0;
    };
    // the wheel scrolls it while the mouse is inside; returns the y where content starts
    float beginScroll(uint64_t wid, Rect r, Scroll& s);
    void endScroll(Rect r, Scroll& s);

private:
    std::vector<Rect> clips_;
};

} // namespace slrig

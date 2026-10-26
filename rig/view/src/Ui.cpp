#include "Ui.h"

#include <algorithm>
#include <cmath>

namespace slrig {

using namespace theme;

Rect Rect::intersect(const Rect& o) const
{
    float x0 = std::max(x, o.x), y0 = std::max(y, o.y), x1 = std::min(r(), o.r()), y1 = std::min(b(), o.b());
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

void Ui::begin(const UiInput& input, double t)
{
    in = input;
    now = t;
    hot = 0;
    wheelUsed = false;
    clips_.clear();
}

void Ui::end()
{
    if (in.released && active != 0) active = 0;
}

uint64_t Ui::id(std::string_view s, int k)
{
    uint64_t h = 1469598103934665603ull ^ static_cast<uint64_t>(k) * 0x9e3779b97f4a7c15ull;
    for (char c : s) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
    return h ? h : 1;
}

void Ui::pushClip(Rect r)
{
    if (!clips_.empty()) r = r.intersect(clips_.back());
    clips_.push_back(r);
    nvgScissor(vg, r.x, r.y, r.w, r.h);
}

void Ui::popClip()
{
    if (!clips_.empty()) clips_.pop_back();
    if (clips_.empty()) nvgResetScissor(vg);
    else nvgScissor(vg, clips_.back().x, clips_.back().y, clips_.back().w, clips_.back().h);
}

bool Ui::over(Rect r) const
{
    if (!clips_.empty()) r = r.intersect(clips_.back());
    return r.has(in.mx, in.my);
}

bool Ui::clickBehaviour(uint64_t wid, Rect r)
{
    bool inside = over(r);
    if (inside && (active == 0 || active == wid)) hot = wid;
    if (inside && in.pressed && active == 0) active = wid;
    if (active == wid && in.released) {
        active = 0;
        return inside;
    }
    return false;
}

// ------------------------------------------------------------------------------------------- drawing

void Ui::fill(Rect r, NVGcolor c, float radius)
{
    nvgBeginPath(vg);
    if (radius > 0) nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius);
    else nvgRect(vg, r.x, r.y, r.w, r.h);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

void Ui::stroke(Rect r, NVGcolor c, float width, float radius)
{
    float h = width * 0.5f;
    nvgBeginPath(vg);
    if (radius > 0) nvgRoundedRect(vg, r.x + h, r.y + h, r.w - width, r.h - width, radius);
    else nvgRect(vg, r.x + h, r.y + h, r.w - width, r.h - width);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

void Ui::hline(float x0, float x1, float y, NVGcolor c, float width)
{
    nvgBeginPath(vg);
    nvgMoveTo(vg, x0, y);
    nvgLineTo(vg, x1, y);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

void Ui::vline(float x, float y0, float y1, NVGcolor c, float width)
{
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, y0);
    nvgLineTo(vg, x, y1);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

float Ui::text(float x, float y, std::string_view s, int font, float size, NVGcolor c, int align)
{
    if (s.empty()) return x;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, align);
    nvgFillColor(vg, c);
    return nvgText(vg, x, y, s.data(), s.data() + s.size());
}

float Ui::textW(std::string_view s, int font, float size)
{
    if (s.empty()) return 0;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    return nvgTextBounds(vg, 0, 0, s.data(), s.data() + s.size(), nullptr);
}

std::string Ui::fit(const std::string& s, int font, float size, float maxW)
{
    if (textW(s, font, size) <= maxW) return s;
    std::string t = s;
    while (!t.empty() && textW(t + "…", font, size) > maxW) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xc0) == 0x80) t.pop_back();
        if (!t.empty() && static_cast<unsigned char>(t.back()) >= 0xc0) t.pop_back();
    }
    return t + "…";
}

float Ui::wrapHeight(float w, const std::string& s, int font, float size, float lineH)
{
    if (s.empty()) return 0;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    nvgTextLineHeight(vg, lineH);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    float b[4];
    nvgTextBoxBounds(vg, 0, 0, w, s.c_str(), nullptr, b);
    return b[3] - b[1];
}

float Ui::wrap(float x, float y, float w, const std::string& s, int font, float size, NVGcolor c, float lineH)
{
    if (s.empty()) return 0;
    float h = wrapHeight(w, s, font, size, lineH);
    nvgFillColor(vg, c);
    nvgTextBox(vg, x, y, w, s.c_str(), nullptr);
    nvgTextLineHeight(vg, 1.0f);
    return h;
}

void Ui::label(Rect r, const char* s, NVGcolor c)
{
    float cell = 2.0f;
    float y = std::round(r.cy() - pxHeight(PX_SMALL, cell) * 0.5f);
    float w = pxText(vg, PX_SMALL, r.x, y, cell, s, c);
    if (r.r() > r.x + w + 12) hline(r.x + w + 12, r.r(), std::round(r.cy()) + 0.25f, hex(LINE), 1.5f);
}

// ------------------------------------------------------------------------------------------- widgets

bool Ui::button(uint64_t wid, Rect r, const char* pxLabel, int style, NVGcolor accent, bool enabled)
{
    bool clicked = enabled && clickBehaviour(wid, r);
    bool isHot = enabled && hot == wid, isActive = active == wid;
    NVGcolor ink = hex(INK), line = hex(LINE);
    NVGcolor lab = ink;
    float a = enabled ? 1.0f : 0.4f;
    switch (style) {
    case BTN_SOLID: {
        NVGcolor f = accent;
        if (isHot) f = nvgLerpRGBA(accent, nvgRGBf(1, 1, 1), 0.12f);
        f.a *= a;
        fill(r, f, 3);
        lab = nvgRGBAf(0, 0, 0, a);
        break;
    }
    case BTN_ON: {
        NVGcolor f = accent;
        f.a = 0.16f * a;
        fill(r, f, 3);
        NVGcolor s = accent;
        s.a *= a;
        stroke(r, s, 1.5f, 3);
        lab = accent;
        lab.a *= a;
        break;
    }
    case BTN_QUIET:
        if (isHot) stroke(r, line, 1.5f, 3);
        lab = isHot ? ink : hex(MUTE);
        lab.a *= a;
        break;
    default:
        stroke(r, isActive ? accent : isHot ? hex(MUTE) : line, 1.5f, 3);
        lab.a *= a;
        break;
    }
    float cell = 2.0f;
    float w = pxWidth(PX_SMALL, pxLabel, cell);
    if (w > r.w - 8) { cell = 1.0f; w = pxWidth(PX_SMALL, pxLabel, cell); }
    pxText(vg, PX_SMALL, std::round(r.cx() - w * 0.5f), std::round(r.cy() - pxHeight(PX_SMALL, cell) * 0.5f), cell,
           pxLabel, lab);
    return clicked;
}

bool Ui::slider(uint64_t wid, Rect r, float& v, float lo, float hi, float step, float def, bool& done, NVGcolor accent)
{
    done = false;
    bool inside = over(r);
    if (inside && (active == 0 || active == wid)) hot = wid;
    bool changed = false;
    auto valueAt = [&](float mx) {
        float t = std::clamp((mx - r.x - 6) / std::max(1.0f, r.w - 12), 0.0f, 1.0f);
        float nv = lo + t * (hi - lo);
        if (step > 0) nv = lo + std::round((nv - lo) / step) * step;
        return std::clamp(nv, lo, hi);
    };
    if (inside && in.pressed && active == 0) {
        active = wid;
        if (in.dbl) {               // a double click: back to the default
            v = def;
            changed = true;
            done = true;
        } else {
            float nv = valueAt(in.mx);
            dragX0 = in.mx;
            dragV0 = nv;
            if (nv != v) { v = nv; changed = true; }
        }
    } else if (active == wid && in.down) {
        float nv;
        if (in.mods & 0x0001) {     // shift: a tenth of the speed, from where the drag began
            nv = dragV0 + (in.mx - dragX0) / std::max(1.0f, r.w - 12) * (hi - lo) * 0.1f;
            if (step > 0) nv = lo + std::round((nv - lo) / step) * step;
            nv = std::clamp(nv, lo, hi);
        } else {
            nv = valueAt(in.mx);
            dragX0 = in.mx;
            dragV0 = nv;
        }
        if (nv != v) { v = nv; changed = true; }
    }
    if (active == wid && in.released) {
        active = 0;
        done = true;
    }

    // a thin track, the part away from the default filled, a tick at the default, a square thumb
    float span = hi - lo;
    auto X = [&](float val) { return r.x + 6 + (span > 0 ? (val - lo) / span : 0) * (r.w - 12); };
    float cy = std::round(r.cy()) + 0.5f;
    hline(r.x + 6, r.r() - 6, cy, hex(PH), 2.0f);
    float x0 = X(std::clamp(def, lo, hi)), x1 = X(std::clamp(v, lo, hi));
    bool isDef = std::fabs(v - def) <= std::max(1e-6f, step * 0.01f);
    NVGcolor fillc = isDef ? hex(MUTE) : accent;
    if (std::fabs(x1 - x0) > 0.5f) hline(std::min(x0, x1), std::max(x0, x1), cy, fillc, 2.0f);
    vline(std::round(x0) + 0.5f, cy - 5, cy + 5, hex(DIM), 1.5f);
    bool isHot = hot == wid || active == wid;
    float tw = isHot ? 10.0f : 8.0f;
    Rect th{std::round(x1 - tw * 0.5f), std::round(cy - tw * 0.5f), tw, tw};
    fill(th, isDef ? hex(isHot ? INK : MUTE) : accent, 1.5f);
    return changed;
}

float Ui::segmentedMinWidth(const std::vector<std::string>& labels)
{
    float w = 0;
    for (auto& l : labels) w += textW(l, mono, 11.5f) + 12;
    return w + 3.0f * static_cast<float>(labels.empty() ? 0 : labels.size() - 1);
}

int Ui::segmented(uint64_t wid, Rect r, const std::vector<std::string>& labels, int cur, NVGcolor accent)
{
    int n = static_cast<int>(labels.size());
    if (n == 0) return -1;
    int clicked = -1;
    float gap = 3;
    // widths in proportion to the labels, filling the row
    std::vector<float> ws(n);
    float sum = 0;
    for (int i = 0; i < n; i++) { ws[i] = textW(labels[i], mono, 11.5f) + 12; sum += ws[i]; }
    float extra = (r.w - gap * (n - 1) - sum) / n;
    float x = r.x;
    for (int i = 0; i < n; i++) {
        float w = std::max(10.0f, ws[i] + extra);
        Rect s{std::round(x), r.y, std::round(x + w) - std::round(x), r.h};
        uint64_t sid = wid ^ (0x51ed27ull * static_cast<uint64_t>(i + 1));
        if (clickBehaviour(sid, s)) clicked = i;
        bool on = i == cur;
        bool isHot = hot == sid;
        if (on) fill(s, accent, 2);
        else stroke(s, isHot ? hex(MUTE) : hex(LINE), 1.5f, 2);
        std::string lab = fit(labels[i], mono, 11.5f, s.w - 6);
        text(s.cx(), s.cy() + 0.5f, lab, mono, 11.5f, on ? nvgRGBf(0, 0, 0) : hex(isHot ? INK : MUTE),
             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        x += w + gap;
    }
    return clicked;
}

float Ui::beginScroll(uint64_t wid, Rect r, Scroll& s)
{
    (void)wid;
    float maxOff = std::max(0.0f, s.content - r.h);
    if (!wheelUsed && in.wheel != 0 && over(r)) {
        s.off -= in.wheel * 48.0f;
        wheelUsed = true;
    }
    s.off = std::clamp(s.off, 0.0f, maxOff);
    pushClip(r);
    return r.y - s.off;
}

void Ui::endScroll(Rect r, Scroll& s)
{
    popClip();
    if (s.content > r.h + 1) {
        float t = r.h / s.content;
        float bh = std::max(24.0f, r.h * t);
        float by = r.y + (r.h - bh) * (s.off / std::max(1.0f, s.content - r.h));
        fill({r.r() - 3, by, 3, bh}, hex(PH), 1.5f);
    }
}

} // namespace slrig

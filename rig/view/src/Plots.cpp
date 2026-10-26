// The OLED mirror, the tilt and roll curves, and the scrolling plots.
#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace slrig {

using namespace theme;

static const float WINDOWS[4] = {2, 5, 10, 30};

// ------------------------------------------------------------------------------------------- the OLED mirror

void App::updateOledImage(int cellDev)
{
    bool grid = pixelGrid_ && cellDev >= 3;
    if (oledImage_ >= 0 && oledImageScale_ == cellDev && oledImageVersion_ == m_.fbVersion && oledImagePanel_ == panel_ &&
        oledImageGrid_ == grid)
        return;
    int W = 128 * cellDev, H = 64 * cellDev;
    oledRgba_.assign(static_cast<size_t>(W) * H * 4, 0);
    unsigned top = panel_ == 0 ? OLED_YELLOW : panel_ == 2 ? OLED_WHITE : OLED_BLUE;
    unsigned bottom = panel_ == 2 ? OLED_WHITE : OLED_BLUE;
    int gap = grid ? std::max(1, static_cast<int>(std::lround(cellDev * 0.18))) : 0;
    auto put = [&](uint8_t* p, unsigned c, float k) {
        p[0] = static_cast<uint8_t>(((c >> 16) & 255) * k);
        p[1] = static_cast<uint8_t>(((c >> 8) & 255) * k);
        p[2] = static_cast<uint8_t>((c & 255) * k);
        p[3] = 255;
    };
    const uint8_t offR = (OLED_OFF >> 16) & 255, offG = (OLED_OFF >> 8) & 255, offB = OLED_OFF & 255;
    for (int y = 0; y < 64; y++) {
        unsigned col = y < 16 ? top : bottom;
        for (int x = 0; x < 128; x++) {
            bool lit = m_.haveScreen && ((m_.fb[(y >> 3) * 128 + x] >> (y & 7)) & 1);
            for (int dy = 0; dy < cellDev; dy++)
                for (int dx = 0; dx < cellDev; dx++) {
                    uint8_t* p = &oledRgba_[((static_cast<size_t>(y) * cellDev + dy) * W + x * cellDev + dx) * 4];
                    bool inGap = gap && (dx >= cellDev - gap || dy >= cellDev - gap);
                    if (inGap) { p[0] = 2; p[1] = 3; p[2] = 6; p[3] = 255; }
                    else if (lit) put(p, col, 1.0f);
                    else {          // an unlit pixel: the glass, a trace of the panel's colour
                        p[0] = static_cast<uint8_t>(offR + ((col >> 16) & 255) * 0.045f);
                        p[1] = static_cast<uint8_t>(offG + ((col >> 8) & 255) * 0.045f);
                        p[2] = static_cast<uint8_t>(offB + (col & 255) * 0.045f);
                        p[3] = 255;
                    }
                }
        }
    }
    if (oledImage_ >= 0 && oledImageScale_ == cellDev) nvgUpdateImage(vg_, oledImage_, oledRgba_.data());
    else {
        if (oledImage_ >= 0) nvgDeleteImage(vg_, oledImage_);
        oledImage_ = nvgCreateImageRGBA(vg_, W, H, NVG_IMAGE_NEAREST, oledRgba_.data());
    }
    oledImageScale_ = cellDev;
    oledImageVersion_ = m_.fbVersion;
    oledImagePanel_ = panel_;
    oledImageGrid_ = grid;
}

void App::drawOled(Rect r, int scale)
{
    Rect head = r.cutTop(22);
    // the toggles first, then the label up to them
    static const char* PANELS[3] = {"YELLOW + BLUE", "BLUE", "WHITE"};
    float bx = head.r();
    {
        const char* lab = PANELS[panel_];
        float w = pxWidth(PX_SMALL, lab, 1) + 16;
        Rect b{bx - w, head.y + 1, w, 20};
        if (ui_.button(Ui::id("oled.panel"), b, lab, BTN_OUTLINE)) panel_ = (panel_ + 1) % 3;
        bx -= w + 6;
        w = pxWidth(PX_SMALL, "GRID", 1) + 16;
        Rect g{bx - w, head.y + 1, w, 20};
        if (ui_.button(Ui::id("oled.grid"), g, "GRID", pixelGrid_ ? BTN_ON : BTN_OUTLINE, hex(MUTE))) pixelGrid_ = !pixelGrid_;
        bx -= w + 12;
    }
    char cap[32];
    std::snprintf(cap, sizeof cap, "SCREEN  %d×", scale);
    ui_.label({head.x, head.y, bx - head.x, head.h}, cap);

    // black glass, 14 px round the pixels
    float pw = 128.0f * scale, ph = 64.0f * scale;
    Rect glass{std::round(r.x), std::round(r.y + 2), pw + 28, ph + 28};
    ui_.fill(glass, nvgRGBf(0, 0, 0), 3);
    Rect pix{glass.x + 14, glass.y + 14, pw, ph};
    // snap to device pixels so every OLED pixel is the same whole number of device pixels
    float pr = ui_.pxRatio;
    pix.x = std::round(pix.x * pr) / pr;
    pix.y = std::round(pix.y * pr) / pr;
    int cellDev = std::max(1, static_cast<int>(std::lround(scale * pr)));
    updateOledImage(cellDev);
    pix.w = 128.0f * cellDev / pr;
    pix.h = 64.0f * cellDev / pr;
    if (oledImage_ >= 0) {
        NVGpaint p = nvgImagePattern(vg_, pix.x, pix.y, pix.w, pix.h, 0, oledImage_, 1.0f);
        nvgBeginPath(vg_);
        nvgRect(vg_, pix.x, pix.y, pix.w, pix.h);
        nvgFillPaint(vg_, p);
        nvgFill(vg_);
    }
    if (!m_.haveScreen)
        ui_.text(pix.cx(), pix.cy(), m_.connected ? "no screen yet" : "", ui_.sans, 14, hex(DIM),
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

// ------------------------------------------------------------------------------------------- curves

static float niceUp(float v)
{
    if (!(v > 0)) return 1;
    float e = std::pow(10.0f, std::floor(std::log10(v)));
    float m = v / e;
    float n = m <= 1 ? 1 : m <= 1.5f ? 1.5f : m <= 2 ? 2 : m <= 2.5f ? 2.5f : m <= 3 ? 3 : m <= 4 ? 4 : m <= 5 ? 5 : m <= 6 ? 6 : m <= 8 ? 8 : 10;
    return n * e;
}

void App::drawCurveTilt(Rect r)
{
    NVGcontext* vg = vg_;
    Rect head = r.cutTop(20);
    ui_.label(head, "TILT → SPEED");
    r.cutTop(6);
    ui_.fill(r, hex(PANEL), 3);
    ui_.stroke(r, hex(LINE), 1.5f, 3);
    const Curve& c = m_.curve;
    if (!c.valid) {
        ui_.text(r.cx(), r.cy(), m_.connected ? "waiting for the curve" : "", ui_.sans, 13, hex(DIM),
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }
    Rect p = r.shrink(10, 10);
    p.cutLeft(26);
    p.cutBottom(14);
    p.cutTop(14);
    float amax = 0, smax = 0;
    for (float a : c.angle) amax = std::max(amax, std::fabs(a));
    for (float s : c.speed) smax = std::max(smax, std::fabs(s));
    if (c.speedMax > smax) smax = c.speedMax;
    smax = niceUp(smax * 1.05f);
    if (amax <= 0) amax = 60;
    auto X = [&](float a) { return p.x + (a + amax) / (2 * amax) * p.w; };
    auto Y = [&](float s) { return p.y + (1 - (s + smax) / (2 * smax)) * p.h; };

    // the dead zone (and the hysteresis past it)
    {
        float d = c.dead;
        ui_.fill({X(-d), p.y, X(d) - X(-d), p.h}, hex(INK, 0.06f));
        if (c.hyst > 0) {
            ui_.fill({X(-d - c.hyst), p.y, X(-d) - X(-d - c.hyst), p.h}, hex(INK, 0.03f));
            ui_.fill({X(d), p.y, X(d + c.hyst) - X(d), p.h}, hex(INK, 0.03f));
        }
        if (X(d) - X(-d) > 22) pxTextC(vg, PX_TINY, X(0), p.b() - 8, 1, "DEAD", hex(DIM));
    }
    // detents
    for (auto& d : c.detents) {
        if (d.range) {
            ui_.fill({X(std::min(d.lo, d.hi)), p.y, std::max(1.0f, std::fabs(X(d.hi) - X(d.lo))), p.h}, hex(ACCENT, 0.10f));
        }
        float y = Y(d.speed);
        nvgBeginPath(vg);
        nvgMoveTo(vg, p.x, y);
        nvgLineTo(vg, p.r(), y);
        nvgStrokeColor(vg, hex(ACCENT, 0.25f));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    // axes, full angle
    ui_.hline(p.x, p.r(), std::round(Y(0)) + 0.5f, hex(PH), 1);
    ui_.vline(std::round(X(0)) + 0.5f, p.y, p.b(), hex(PH), 1);
    for (float s : {-1.0f, 1.0f}) {
        float y = std::round(Y(s * 1.0f)) + 0.5f;
        if (1.0f < smax) {
            for (float x = p.x; x < p.r(); x += 6) ui_.hline(x, std::min(x + 2, p.r()), y, hex(LINE), 1);
        }
        float xf = std::round(X(s * c.full)) + 0.5f;
        for (float yy = p.y; yy < p.b(); yy += 6) ui_.vline(xf, yy, std::min(yy + 2, p.b()), hex(LINE), 1);
    }
    char b[48];
    std::snprintf(b, sizeof b, "%g×", static_cast<double>(smax));
    ui_.text(p.x - 4, p.y + 4, b, ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui_.text(p.x - 4, Y(1), "1×", ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui_.text(p.x - 4, Y(0), "0", ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    std::snprintf(b, sizeof b, "-%g×", static_cast<double>(smax));
    ui_.text(p.x - 4, p.b() - 4, b, ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    std::snprintf(b, sizeof b, "%+.0f°", static_cast<double>(-c.full));
    ui_.text(X(-c.full), p.b() + 9, b, ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    std::snprintf(b, sizeof b, "%+.0f°", static_cast<double>(c.full));
    ui_.text(X(c.full), p.b() + 9, b, ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    pxText(vg, PX_TINY, p.x + 2, p.y - 10, 1, "NOSE UP", hex(DIM));
    pxTextR(vg, PX_TINY, p.r() - 2, p.y - 10, 1, "NOSE DOWN", hex(DIM));

    // the curve
    ui_.pushClip(p.shrink(-2));
    nvgBeginPath(vg);
    for (size_t i = 0; i < c.angle.size(); i++) {
        float x = X(c.angle[i]), y = Y(c.speed[i]);
        if (i == 0) nvgMoveTo(vg, x, y);
        else if (c.speed[i] * c.speed[i - 1] < 0) { nvgMoveTo(vg, x, y); }      // the jump through 0
        else nvgLineTo(vg, x, y);
    }
    nvgStrokeColor(vg, hex(INK, 0.85f));
    nvgStrokeWidth(vg, 1.5f);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);

    // the live point and its last second
    if (m_.connected && m_.state.valid) {
        TuneItem* axis = m_.findTune("tilt.axis");
        bool useRoll = axis && shownValue(*axis) >= 0.5f;
        const Series& s = m_.states;
        double t1 = s.lastT();
        int i0 = s.lowerBound(t1 - 1.2);
        for (int i = i0; i < s.count; i++) {
            float age = static_cast<float>((t1 - s.tAt(i)) / 1.2);
            float a = s.vAt(i, useRoll ? S_ROLL : S_PITCH), sp = s.vAt(i, S_SPEED_CMD);
            nvgBeginPath(vg);
            nvgCircle(vg, X(std::clamp(a, -amax, amax)), Y(std::clamp(sp, -smax, smax)), 1.6f);
            nvgFillColor(vg, hex(ACCENT, 0.5f * (1 - age)));
            nvgFill(vg);
        }
        float a = useRoll ? m_.state.roll : m_.state.pitch, sp = m_.state.speed_cmd;
        float x = X(std::clamp(a, -amax, amax)), y = Y(std::clamp(sp, -smax, smax));
        ui_.vline(x, p.y, p.b(), hex(ACCENT, 0.25f), 1);
        nvgBeginPath(vg);
        nvgCircle(vg, x, y, 4.5f);
        nvgFillColor(vg, hex(ACCENT));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, x, y, 7.5f);
        nvgStrokeColor(vg, hex(ACCENT, 0.35f));
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
        ui_.popClip();
        std::snprintf(b, sizeof b, "%+.1f°  %+.2f×", static_cast<double>(a), static_cast<double>(sp));
        ui_.text(head.r(), head.cy(), b, ui_.mono, 12, hex(ACCENT), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        return;
    }
    ui_.popClip();
}

void App::drawCurveRoll(Rect r)
{
    NVGcontext* vg = vg_;
    Rect head = r.cutTop(20);
    ui_.label(head, "ROLL → FX");
    r.cutTop(6);
    ui_.fill(r, hex(PANEL), 3);
    ui_.stroke(r, hex(LINE), 1.5f, 3);
    const Curve& c = m_.curve;
    if (!c.valid || c.rAngle.empty()) {
        ui_.text(r.cx(), r.cy(), m_.connected ? "waiting for the curve" : "", ui_.sans, 13, hex(DIM),
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }
    Rect p = r.shrink(10, 10);
    p.cutLeft(18);
    p.cutBottom(14);
    p.cutTop(14);
    float amax = 0;
    for (float a : c.rAngle) amax = std::max(amax, std::fabs(a));
    if (amax <= 0) amax = 60;
    auto X = [&](float a) { return p.x + (a + amax) / (2 * amax) * p.w; };
    auto Y = [&](float v) { return p.y + (1 - v) * p.h; };
    ui_.fill({X(-c.rDead), p.y, X(c.rDead) - X(-c.rDead), p.h}, hex(INK, 0.06f));
    if (X(c.rDead) - X(-c.rDead) > 22) pxTextC(vg, PX_TINY, X(0), p.b() - 8, 1, "DEAD", hex(DIM));
    ui_.hline(p.x, p.r(), std::round(Y(0)) + 0.5f, hex(PH), 1);
    ui_.vline(std::round(X(0)) + 0.5f, p.y, p.b(), hex(PH), 1);
    ui_.text(p.x - 4, p.y + 4, "1", ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui_.text(p.x - 4, p.b() - 4, "0", ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    char b[48];
    for (float s : {-1.0f, 1.0f}) {
        float xf = std::round(X(s * c.rFull)) + 0.5f;
        for (float yy = p.y; yy < p.b(); yy += 6) ui_.vline(xf, yy, std::min(yy + 2, p.b()), hex(LINE), 1);
        std::snprintf(b, sizeof b, "%+.0f°", static_cast<double>(s * c.rFull));
        ui_.text(xf, p.b() + 9, b, ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    pxText(vg, PX_TINY, p.x + 2, p.y - 10, 1, "DELAY", hex(VIOLET));
    pxTextR(vg, PX_TINY, p.r() - 2, p.y - 10, 1, "STUTTER", hex(ORANGE));
    auto curve = [&](const std::vector<float>& v, unsigned col) {
        if (v.size() != c.rAngle.size()) return;
        nvgBeginPath(vg);
        for (size_t i = 0; i < v.size(); i++) {
            if (i == 0) nvgMoveTo(vg, X(c.rAngle[i]), Y(v[i]));
            else nvgLineTo(vg, X(c.rAngle[i]), Y(v[i]));
        }
        nvgStrokeColor(vg, hex(col, 0.9f));
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    };
    curve(c.delay, VIOLET);
    curve(c.stutter, ORANGE);
    if (m_.connected && m_.state.valid) {
        float a = m_.state.roll;
        float v = std::max(m_.state.delay, m_.state.stutter);
        unsigned col = m_.state.delay > 0 ? VIOLET : m_.state.stutter > 0 ? ORANGE : INK;
        float x = X(std::clamp(a, -amax, amax)), y = Y(v);
        ui_.vline(x, p.y, p.b(), hex(col, 0.25f), 1);
        nvgBeginPath(vg);
        nvgCircle(vg, x, y, 4.5f);
        nvgFillColor(vg, hex(col));
        nvgFill(vg);
        std::snprintf(b, sizeof b, "%+.1f°  %s %.2f", static_cast<double>(a),
                      m_.state.delay > 0 ? "delay" : m_.state.stutter > 0 ? "stutter" : "off", static_cast<double>(v));
        ui_.text(head.r(), head.cy(), b, ui_.mono, 12, hex(col == INK ? MUTE : col), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
}

// ------------------------------------------------------------------------------------------- the plots

float App::yRange(const Series& s, const Trace* tr, int ntr, double t0, double t1, float minSpan, bool symmetric,
                  float& lo, float& hi)
{
    float mn = 1e30f, mx = -1e30f;
    int i0 = s.lowerBound(t0);
    for (int i = i0; i < s.count; i++) {
        if (s.tAt(i) > t1) break;
        for (int k = 0; k < ntr; k++) {
            float v = s.vAt(i, tr[k].ch);
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
    }
    if (mn > mx) { mn = -minSpan; mx = minSpan; }
    if (symmetric) {
        float m = niceUp(std::max({std::fabs(mn), std::fabs(mx), minSpan}) * 1.08f);
        lo = -m;
        hi = m;
    } else {
        float span = std::max(mx - mn, minSpan);
        float mid = (mx + mn) * 0.5f;
        lo = mid - span * 0.55f;
        hi = mid + span * 0.55f;
    }
    return hi - lo;
}

void App::plotSeries(Rect r, const char* title, const char* unit, const Series& s, const Trace* tr, int ntr,
                     float lo, float hi, bool symmetric, double t0, double t1, const char* fmt)
{
    NVGcontext* vg = vg_;
    ui_.fill(r, hex(PANEL), 3);
    ui_.stroke(r, hex(LINE), 1.5f, 3);
    // auto range, eased so the axis does not jump
    if (!(lo < hi)) {
        float minSpan = hi > 0 ? hi : 1;
        float nlo, nhi;
        yRange(s, tr, ntr, t0, t1, minSpan, symmetric, nlo, nhi);
        auto& st = plotRange_[title];
        if (st.second <= st.first) st = {nlo, nhi};
        else if (nhi > st.second || nlo < st.first) st = {std::min(nlo, st.first), std::max(nhi, st.second)};
        else {
            st.first += (nlo - st.first) * 0.04f;
            st.second += (nhi - st.second) * 0.04f;
        }
        lo = st.first;
        hi = st.second;
    }
    Rect p = r.shrink(8, 6);
    Rect head = p.cutTop(16);
    p.cutLeft(34);
    p.cutTop(2);
    float tw = pxText(vg, PX_SMALL, head.x, head.y + 3, 2, title, hex(MUTE));
    if (unit && *unit) ui_.text(head.x + tw + 6, head.cy() + 1, unit, ui_.sans, 12, hex(DIM));
    // legend with the newest values, right-aligned
    {
        float x = head.r();
        char b[48];
        for (int k = ntr - 1; k >= 0; k--) {
            float v = s.count ? s.vAt(s.count - 1, tr[k].ch) : 0;
            std::snprintf(b, sizeof b, fmt, static_cast<double>(v));
            x -= ui_.textW(b, ui_.mono, 11.5f);
            ui_.text(x, head.cy() + 1, b, ui_.mono, 11.5f, hex(INK));
            x -= 4;
            float nw = ui_.textW(tr[k].name, ui_.mono, 11.5f);
            x -= nw;
            ui_.text(x, head.cy() + 1, tr[k].name, ui_.mono, 11.5f, hex(tr[k].colour));
            x -= 14;
        }
    }
    auto Y = [&](float v) { return p.y + (1 - (v - lo) / (hi - lo)) * p.h; };
    // grid: zero, the range's ends, the seconds
    char b[32];
    for (float v : {lo, 0.0f, hi}) {
        if (v < lo || v > hi) continue;
        float y = std::round(Y(v)) + 0.5f;
        ui_.hline(p.x, p.r(), y, hex(v == 0.0f ? PH : LINE), 1);
        float av = std::fabs(v);
        if (av >= 100) std::snprintf(b, sizeof b, "%.0f", static_cast<double>(v));
        else if (av >= 10) std::snprintf(b, sizeof b, "%.0f", static_cast<double>(v));
        else std::snprintf(b, sizeof b, "%.3g", static_cast<double>(v));
        ui_.text(p.x - 5, std::clamp(y, p.y + 5, p.b() - 5), b, ui_.mono, 10.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    double span = t1 - t0;
    double step = span <= 5 ? 1 : span <= 10 ? 2 : 5;
    for (double t = std::ceil(t0 / step) * step; t < t1; t += step) {
        float x = std::round(p.x + static_cast<float>((t - t0) / span) * p.w) + 0.5f;
        ui_.vline(x, p.y, p.b(), hex(STRIPE), 1);
    }
    plotUnderlay_ = {p.x, p.y, p.w, p.h};
    plotLo_ = lo;
    plotHi_ = hi;
    if (plotUnderlayFn_) plotUnderlayFn_(p, lo, hi);
    // the traces: per pixel column, min and max, so a dense stream draws as it is
    ui_.pushClip(p);
    int i0 = std::max(0, s.lowerBound(t0) - 1);
    int cols = std::max(1, static_cast<int>(p.w));
    for (int k = 0; k < ntr; k++) {
        nvgBeginPath(vg);
        int lastCol = -1;
        float cmin = 0, cmax = 0, clast = 0;
        bool started = false;
        auto flush = [&](int col) {
            float x = p.x + col + 0.5f;
            if (!started) { nvgMoveTo(vg, x, Y(cmin)); started = true; }
            else nvgLineTo(vg, x, Y(cmin));
            if (cmax != cmin) nvgLineTo(vg, x, Y(cmax));
            nvgLineTo(vg, x, Y(clast));
        };
        for (int i = i0; i < s.count; i++) {
            double t = s.tAt(i);
            if (t > t1) break;
            int col = static_cast<int>((t - t0) / span * cols);
            float v = s.vAt(i, tr[k].ch);
            if (col != lastCol) {
                if (lastCol != -1 || col >= 0) {
                    if (lastCol != -1) flush(lastCol);
                }
                lastCol = col;
                cmin = cmax = clast = v;
            } else {
                cmin = std::min(cmin, v);
                cmax = std::max(cmax, v);
                clast = v;
            }
        }
        if (lastCol != -1) flush(lastCol);
        nvgStrokeColor(vg, hex(tr[k].colour, 0.95f));
        nvgStrokeWidth(vg, 1.25f);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    ui_.popClip();
    plotUnderlayFn_ = nullptr;
}

void App::drawPlots(Rect r)
{
    Rect head = r.cutTop(20);
    // the window: 2 5 10 30 s, and freeze
    {
        static const std::vector<std::string> W = {"2 s", "5 s", "10 s", "30 s"};
        Rect seg{head.r() - 176, head.y, 176, 20};
        int k = ui_.segmented(Ui::id("plots.win"), seg, W, plotWindowIdx_, hex(MUTE));
        if (k >= 0) plotWindowIdx_ = k;
        float fw = pxWidth(PX_SMALL, "FREEZE", 1) + 16;
        Rect fz{seg.x - 8 - fw, head.y, fw, 20};
        if (ui_.button(Ui::id("plots.freeze"), fz, "FREEZE", plotsFrozen_ ? BTN_ON : BTN_OUTLINE, hex(ORANGE))) {
            plotsFrozen_ = !plotsFrozen_;
            frozenAt_ = m_.plotNow(now_);
        }
        ui_.label({head.x, head.y, fz.x - head.x - 12, head.h}, "SIGNALS");
    }
    r.cutTop(8);
    double t1 = plotsFrozen_ ? frozenAt_ : m_.plotNow(now_);
    double t0 = t1 - WINDOWS[plotWindowIdx_];
    float gap = 10;
    float cw = std::floor((r.w - 2 * gap) / 3), rh = std::floor((r.h - gap) / 2);
    auto cell = [&](int cx, int cy) { return Rect{r.x + cx * (cw + gap), r.y + cy * (rh + gap), cw, rh}; };

    static const Trace ACC[] = {{M_AX, AX_X, "x"}, {M_AY, AX_Y, "y"}, {M_AZ, AX_Z, "z"}};
    static const Trace GYR[] = {{M_GX, AX_X, "x"}, {M_GY, AX_Y, "y"}, {M_GZ, AX_Z, "z"}};
    static const Trace ANG[] = {{S_PITCH, PITCH, "pitch"}, {S_ROLL, ROLL, "roll"}, {S_YAW, YAW, "yaw"}};
    static const Trace SPD[] = {{S_SPEED_CMD, MUTE, "asked"}, {S_SPEED_NOW, ACCENT, "now"}};
    static const Trace TIL[] = {{S_TILT_U, ACCENT, "u"}};
    static const Trace LEV[] = {{S_IN, MINT, "in"}, {S_OUT, ORANGE, "out"}};

    plotSeries(cell(0, 0), "ACCEL", "g", m_.motion, ACC, 3, 1.1f, 1.1f, true, t0, t1, "%+.2f");
    plotSeries(cell(1, 0), "GYRO", "°/s", m_.motion, GYR, 3, 20.0f, 20.0f, true, t0, t1, "%+.1f");
    plotSeries(cell(2, 0), "ANGLES", "°", m_.states, ANG, 3, 30.0f, 30.0f, true, t0, t1, "%+.1f");
    plotSeries(cell(0, 1), "SPEED", "×", m_.states, SPD, 2, 1.0f, 1.0f, true, t0, t1, "%+.2f");
    // tilt_u with the dead zone as a band
    {
        float dead = 5, full = 45;
        if (m_.curve.valid) { dead = m_.curve.dead; full = m_.curve.full; }
        else {
            if (TuneItem* d = m_.findTune("tilt.dead")) dead = shownValue(*d);
            if (TuneItem* f = m_.findTune("tilt.full")) full = shownValue(*f);
        }
        float du = full > 0 ? dead / full : 0;
        plotUnderlayFn_ = [this, du](Rect p, float lo, float hi) {
            auto Y = [&](float v) { return p.y + (1 - (v - lo) / (hi - lo)) * p.h; };
            ui_.fill({p.x, Y(du), p.w, Y(-du) - Y(du)}, hex(INK, 0.06f));
            for (float v : {-1.0f, 1.0f}) {
                float y = std::round(Y(v)) + 0.5f;
                for (float x = p.x; x < p.r(); x += 6) ui_.hline(x, std::min(x + 2, p.r()), y, hex(PH), 1);
            }
        };
        plotSeries(cell(1, 1), "TILT U", "dead zone shaded", m_.states, TIL, 1, -1.25f, 1.25f, true, t0, t1, "%+.3f");
    }
    plotUnderlayFn_ = [this](Rect p, float lo, float hi) {
        float y = p.y + (1 - (1.0f - lo) / (hi - lo)) * p.h;
        ui_.hline(p.x, p.r(), std::round(y) + 0.5f, hex(BAD, 0.4f), 1);
    };
    plotSeries(cell(2, 1), "LEVELS", "peak", m_.states, LEV, 2, 0.0f, 1.08f, false, t0, t1, "%.2f");
}

} // namespace slrig

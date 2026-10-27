// The right-hand panel: TUNE, GESTURES, SETTINGS, SESSION.
#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace slrig {

using namespace theme;

static std::string upper(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static const char* TABS[TAB_N] = {"TUNE", "GESTURES", "SETTINGS", "SESSION"};

// a JSON value, short: numbers to 4 figures, lists with spaces, objects as "k v" pairs
static std::string compactJson(const json& v, int depth = 0)
{
    if (v.is_null()) return "-";
    if (v.is_boolean()) return v.get<bool>() ? "yes" : "no";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) {
        char b[32];
        double d = v.get<double>();
        std::snprintf(b, sizeof b, std::fabs(d) >= 1000 ? "%.0f" : "%.4g", d);
        return b;
    }
    if (v.is_string()) return v.get<std::string>();
    std::string out;
    if (v.is_array()) {
        for (auto& e : v) out += (out.empty() ? "" : "  ") + compactJson(e, depth + 1);
        return out;
    }
    for (auto it = v.begin(); it != v.end(); ++it) {
        std::string inner = compactJson(it.value(), depth + 1);
        if (it.value().is_object() && depth < 2) inner = "(" + inner + ")";
        out += (out.empty() ? "" : depth ? ", " : "  ") + it.key() + " " + inner;
    }
    return out;
}

void App::drawRight(Rect r)
{
    // tabs, as the app's nav
    Rect bar = r.cutTop(34);
    float x = bar.x;
    for (int i = 0; i < TAB_N; i++) {
        float w = pxWidth(PX_SMALL, TABS[i], 2) + 20;
        Rect t{x, bar.y + 2, w, 30};
        bool on = tab_ == i;
        uint64_t id = Ui::id("tab", i);
        if (ui_.clickBehaviour(id, t)) tab_ = static_cast<Tab>(i);
        if (on) ui_.fill(t, hex(NAVON), 3);
        pxText(vg_, PX_SMALL, t.x + 10, std::round(t.cy() - 5), 2, TABS[i], on || ui_.hot == id ? hex(INK) : hex(MUTE));
        x += w + 4;
    }
    // a count beside GESTURES: events in the last few seconds light it
    r.cutTop(12);
    switch (tab_) {
    case TAB_TUNE: drawTune(r); break;
    case TAB_GESTURES: drawGestures(r); break;
    case TAB_SETTINGS: drawSettings(r); break;
    case TAB_SESSION: drawSession(r); break;
    default: break;
    }
}

// ------------------------------------------------------------------------------------------- TUNE

void App::drawTune(Rect r)
{
    if (m_.tune.empty()) {
        ui_.text(r.x, r.y + 12, m_.connected ? "waiting for the tuning table" : "no daemon", ui_.sans, 14, hex(DIM));
        return;
    }
    // the bar: A/B, keep, save, reset
    {
        Rect row = r.cutTop(30);
        static const std::vector<std::string> AB = {"A", "B"};
        Rect seg = row.cutLeft(76);
        int k = ui_.segmented(Ui::id("tune.ab"), seg, AB, abSlot_, hex(ACCENT));
        if (k >= 0) {
            abSlot_ = k;
            if (abStored_[k]) run("tune.ab", {{"slot", AB[k]}, {"store", false}});
        }
        row.cutLeft(6);
        // narrow: shorter words
        bool narrow = row.w < Ui::btnW("KEEP AS A") + Ui::btnW("RESET") + Ui::btnW("SAVE TO BOARD", 20) + 18;
        char keep[24];
        std::snprintf(keep, sizeof keep, narrow ? "KEEP %s" : "KEEP AS %s", AB[abSlot_].c_str());
        Rect kb = row.cutLeft(Ui::btnW(keep));
        if (ui_.button(Ui::id("tune.keep"), kb, keep)) {
            int slot = abSlot_;
            run("tune.ab", {{"slot", AB[slot]}, {"store", true}}, [this, slot](bool ok, const json&, const json&) {
                if (ok) abStored_[slot] = true;
            });
        }
        const char* saveLab = narrow ? "SAVE" : "SAVE TO BOARD";
        Rect save = row.cutRight(Ui::btnW(saveLab, 20));
        if (ui_.button(Ui::id("tune.save"), save, saveLab, BTN_SOLID)) run("tune.save");
        row.cutRight(6);
        Rect reset = row.cutRight(Ui::btnW("RESET"));
        if (ui_.button(Ui::id("tune.reset"), reset, "RESET", BTN_QUIET)) run("tune.reset");
    }
    r.cutTop(10);
    {
        Rect row = r.cutTop(24);
        int changed = 0;
        for (auto& it : m_.tune) changed += it.changed();
        char b[96];
        std::snprintf(b, sizeof b, "%zu values, %d changed from v1%s", m_.tune.size(), changed,
                      abStored_[0] || abStored_[1] ? "" : ". Keep a set as A, then B, to compare");
        if (m_.presets.empty()) ui_.text(row.x, row.cy(), b, ui_.sans, 13, changed ? hex(ACCENT) : hex(DIM));
        else {
            // presets: a click loads one (tune.preset n)
            float x = pxText(vg_, PX_SMALL, row.x, std::round(row.cy() - 5), 2, "PRESET", hex(DIM)) + row.x + 8;
            for (int i = 0; i < static_cast<int>(m_.presets.size()); i++) {
                std::string name = upper(m_.presets[i]);
                float w = Ui::btnW(name.c_str(), 14);
                if (x + w > row.r()) break;
                Rect pb{x, row.y, w, 24};
                if (ui_.button(Ui::id("tune.preset", i), pb, name.c_str(), BTN_QUIET)) run("tune.preset", {{"n", i}});
                x += w + 2;
            }
            r.cutTop(4);
            Rect row2 = r.cutTop(20);
            if (ui_.textW(b, ui_.sans, 13) > row2.w) {
                std::snprintf(b, sizeof b, "%zu values, %d changed from v1", m_.tune.size(), changed);
            }
            ui_.text(row2.x, row2.cy(), b, ui_.sans, 13, changed ? hex(ACCENT) : hex(DIM));
        }
    }
    r.cutTop(6);

    // the help box at the bottom: the item under the mouse, else the focused one
    Rect help = r.cutBottom(86);
    r.cutBottom(8);

    // the list
    static const char* ORDER[] = {"imu", "motion", "tilt", "roll", "joy", "enc", "rec", "mic", "usb", "rig"};
    std::vector<std::string> groups(std::begin(ORDER), std::end(ORDER));
    for (auto& it : m_.tune)
        if (std::find(groups.begin(), groups.end(), it.group) == groups.end()) groups.push_back(it.group);

    const float rowH = 30, headH = 30, nameW = 128, valW = 76;
    // where the focused row sits, to scroll to it
    if (scrollToFocus_ && !focusTune_.empty()) {
        float y = 0;
        for (auto& g : groups) {
            bool any = false;
            for (auto& it : m_.tune) {
                if (it.group != g) continue;
                if (!any) { y += headH; any = true; }
                if (it.name == focusTune_) {
                    if (scrollCenter_) tuneScroll_.off = std::max(0.0f, y - r.h * 0.4f);
                    else if (y < tuneScroll_.off) tuneScroll_.off = y - headH;
                    else if (y + rowH > tuneScroll_.off + r.h) tuneScroll_.off = y + rowH - r.h + 8;
                }
                y += rowH;
            }
        }
        scrollToFocus_ = false;
        scrollCenter_ = false;
    }
    float y = ui_.beginScroll(Ui::id("tune.scroll"), r, tuneScroll_);
    float y0 = y;
    const TuneItem* hoverItem = nullptr;
    for (auto& g : groups) {
        int n = 0, ch = 0;
        for (auto& it : m_.tune) if (it.group == g) { n++; ch += it.changed(); }
        if (!n) continue;
        Rect gh{r.x, y, r.w - 8, headH};
        float gy = std::round(gh.y + 12);
        float w = pxText(vg_, PX_SMALL, gh.x, gy, 2, upper(g).c_str(), hex(ACCENT));
        ui_.hline(gh.x + w + 10, gh.r(), gy + 5.5f, hex(LINE), 1.5f);
        if (ch) {
            char b[24];
            std::snprintf(b, sizeof b, " %d changed ", ch);
            float tw = ui_.textW(b, ui_.sans, 12);
            ui_.fill({gh.r() - tw - 2, gy - 3, tw + 2, 16}, hex(BG));
            ui_.text(gh.r(), gy + 5, b, ui_.sans, 12, hex(ACCENT), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        y += headH;
        for (auto& it : m_.tune) {
            if (it.group != g) continue;
            Rect row{r.x, y, r.w - 8, rowH};
            y += rowH;
            if (row.b() < r.y || row.y > r.b()) continue;          // off screen
            bool focus = it.name == focusTune_;
            bool hover = ui_.over(row);
            if (hover) hoverItem = &it;
            double fl = now_ - focusFlash_;
            if (focus && fl < 1.2) ui_.fill(row, hex(ACCENT, 0.16f * static_cast<float>(1.2 - fl)), 2);
            else if (focus) ui_.fill(row, hex(NAVON), 2);
            if (hover && ui_.in.pressed) focusTune_ = it.name;
            float v = shownValue(it);
            bool changed = std::fabs(v - it.def) > std::max(1e-6f, it.step * 0.01f);
            if (changed) {
                nvgBeginPath(vg_);
                nvgCircle(vg_, row.x + 3, row.cy(), 2.5f);
                nvgFillColor(vg_, hex(ACCENT));
                nvgFill(vg_);
            }
            ui_.text(row.x + 10, row.cy(), ui_.fit(it.shortName(), ui_.mono, 12.5f, nameW - 12), ui_.mono, 12.5f,
                     changed ? hex(INK) : hex(MUTE));
            Rect ctl{row.x + nameW, row.y + 5, row.w - nameW - valW - 8, rowH - 10};
            uint64_t wid = Ui::id(it.name);
            if (it.kind == "action") {
                Rect b{ctl.x, ctl.y, 64, ctl.h};
                if (ui_.button(wid, b, "RUN")) {
                    focusTune_ = it.name;
                    run("tune.set", {{"name", it.name}, {"value", 1}});
                }
            } else if ((it.kind == "enum" || it.kind == "bool") && !it.labels.empty() &&
                       ui_.segmentedMinWidth(it.labels) <= ctl.w) {
                int cur = static_cast<int>(std::lround(v - it.min));
                int k = ui_.segmented(wid, ctl, it.labels, cur);
                if (k >= 0 && k != cur) { focusTune_ = it.name; setTune(it, it.min + k, true); }
            } else if (it.kind == "enum" || it.kind == "bool") {
                // too many words for segments: a stepper
                Rect lb{ctl.x, ctl.y, 30, ctl.h}, rb{ctl.x + 34, ctl.y, 30, ctl.h};
                if (ui_.button(wid ^ 1, lb, "-")) { focusTune_ = it.name; stepTune(it, -1, false); }
                if (ui_.button(wid ^ 2, rb, "+")) { focusTune_ = it.name; stepTune(it, 1, false); }
                // where it sits among its choices
                int n = static_cast<int>(it.labels.size()), k = static_cast<int>(std::lround(v - it.min));
                float dx = ctl.x + 76, dw = std::max(8.0f, (ctl.r() - dx) / std::max(1, n));
                for (int j = 0; j < n && dx + dw * j < ctl.r(); j++)
                    ui_.fill({std::round(dx + dw * j), ctl.cy() - 1.5f, std::max(2.0f, dw - 3), 3},
                             j == k ? hex(ACCENT) : hex(PH), 1);
            } else {
                float nv = v;
                bool done = false;
                if (ui_.slider(wid, ctl, nv, it.min, it.max, it.step, it.def, done)) {
                    focusTune_ = it.name;
                    setTune(it, nv, done);
                } else if (done) {
                    setTune(it, nv, true);
                }
            }
            if (it.kind != "action")
                ui_.text(row.r() - 2, row.cy(), ui_.fit(it.valueText(v), ui_.mono, 12.5f, valW), ui_.mono, 12.5f,
                         changed ? hex(ACCENT) : hex(INK), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        y += 6;
    }
    tuneScroll_.content = y - y0 + 8;
    ui_.endScroll(r, tuneScroll_);

    // help
    const TuneItem* hi = hoverItem ? hoverItem : m_.findTune(focusTune_);
    ui_.fill(help, hex(PANEL), 3);
    ui_.stroke(help, hex(LINE), 1.5f, 3);
    Rect hin = help.shrink(12, 10);
    if (hi) {
        float x = ui_.text(hin.x, hin.y + 8, hi->name, ui_.mono, 13, hex(INK));
        char b[160];
        std::string u = hi->unitText();
        if (hi->kind == "float" || hi->kind == "int")
            std::snprintf(b, sizeof b, "%g..%g%s%s, step %g, v1 %s", static_cast<double>(hi->min), static_cast<double>(hi->max),
                          u.empty() ? "" : " ", u.c_str(), static_cast<double>(hi->step), hi->valueText(hi->def).c_str());
        else if (hi->kind == "action") std::snprintf(b, sizeof b, "an action");
        else std::snprintf(b, sizeof b, "v1 %s", hi->valueText(hi->def).c_str());
        ui_.text(x + 12, hin.y + 8, ui_.fit(b, ui_.sans, 12.5f, hin.r() - x - 12), ui_.sans, 12.5f, hex(DIM));
        ui_.wrap(hin.x, hin.y + 20, hin.w, hi->help.empty() ? "(no help line)" : hi->help, ui_.sans, 14, hex(MUTE));
    } else {
        ui_.wrap(hin.x, hin.y, hin.w,
                 "Point at a value for its help. Click a row, then the arrow keys: up and down move, left and right "
                 "step it (shift: ten steps). Double-click a slider for v1. Every change goes to the board as tune.set.",
                 ui_.sans, 13.5f, hex(DIM));
    }
}

// ------------------------------------------------------------------------------------------- GESTURES

void App::drawGestures(Rect r)
{
    if (m_.gestures.empty()) {
        ui_.text(r.x, r.y + 12, m_.connected ? "no gestures yet" : "no daemon", ui_.sans, 14, hex(DIM));
        return;
    }
    // the timeline at the bottom
    Rect tl = r.cutBottom(std::clamp(r.h * 0.36f, 170.0f, 250.0f));
    r.cutBottom(10);

    static const char* ORDER[] = {"tilt", "roll", "enc", "rec", "joy", "menu"};
    std::vector<std::string> groups(std::begin(ORDER), std::end(ORDER));
    for (auto& g : m_.gestures)
        if (std::find(groups.begin(), groups.end(), g.group) == groups.end()) groups.push_back(g.group);

    float y = ui_.beginScroll(Ui::id("gest.scroll"), r, gestScroll_);
    float y0 = y;
    const float nameW = 112;
    for (auto& gname : groups) {
        int n = 0;
        for (auto& g : m_.gestures) n += g.group == gname;
        if (!n) continue;
        float gy = std::round(y + 10);
        float w = pxText(vg_, PX_SMALL, r.x, gy, 2, upper(gname).c_str(), hex(ACCENT));
        ui_.hline(r.x + w + 10, r.r() - 8, gy + 5.5f, hex(LINE), 1.5f);
        y += 28;
        for (auto& g : m_.gestures) {
            if (g.group != gname) continue;
            float textW = r.w - 8 - nameW - 70;
            float ruleH = g.rule.empty() ? 0 : ui_.wrapHeight(textW, g.rule, ui_.sans, 12.5f, 1.3f);
            float chipsH = g.tune.empty() ? 0 : 22;
            float h = std::max(26.0f, 8 + std::max(18.0f, ruleH) + chipsH + 6);
            Rect row{r.x, y, r.w - 8, h};
            y += h + 2;
            if (row.b() < r.y || row.y > r.b()) continue;
            double age = now_ - g.flashLocal;
            if (age < 0.9) ui_.fill(row, hex(ACCENT, 0.22f * static_cast<float>(1 - age / 0.9)), 2);
            ui_.text(row.x + 4, row.y + 13, g.name, ui_.mono, 12.5f, age < 0.9 ? hex(ACCENT) : hex(INK));
            char b[32];
            std::snprintf(b, sizeof b, "%d", g.count);
            ui_.text(row.r() - 2, row.y + 13, b, ui_.mono, 13, g.count ? hex(INK) : hex(DIM),
                     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            if (g.count && g.lastT >= 0 && m_.boardNow > 0) {
                double ago = m_.plotNow(now_) - g.lastT;
                if (ago < 0) ago = 0;
                if (ago < 60) std::snprintf(b, sizeof b, "%.1f s ago", ago);
                else std::snprintf(b, sizeof b, "%.0f min ago", ago / 60);
                ui_.text(row.r() - 2, row.y + 30, b, ui_.sans, 11.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            }
            float tx = row.x + nameW;
            if (!g.rule.empty()) {
                nvgTextLineHeight(vg_, 1.3f);
                ui_.wrap(tx, row.y + 5, textW, g.rule, ui_.sans, 12.5f, hex(MUTE), 1.3f);
            }
            float cx = tx, cy = row.y + 8 + std::max(18.0f, ruleH);
            for (auto& tn : g.tune) {
                float cw = ui_.textW(tn, ui_.mono, 11) + 12;
                if (cx + cw > row.r() - 70) break;
                Rect chip{cx, cy, cw, 18};
                uint64_t id = Ui::id("gest.chip." + g.name + tn);
                bool click = ui_.clickBehaviour(id, chip);
                const TuneItem* ti = m_.findTune(tn);
                bool changed = ti && ti->changed();
                ui_.stroke(chip, ui_.hot == id ? hex(MUTE) : changed ? hex(ACCENT, 0.7f) : hex(LINE), 1, 2);
                ui_.text(chip.cx(), chip.cy() + 0.5f, tn, ui_.mono, 11, changed ? hex(ACCENT) : hex(MUTE),
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                if (click) {        // to the value in TUNE
                    tab_ = TAB_TUNE;
                    focusTune_ = tn;
                    scrollToFocus_ = true;
                    scrollCenter_ = true;
                    focusFlash_ = now_;
                }
                cx += cw + 4;
            }
        }
        y += 6;
    }
    gestScroll_.content = y - y0 + 8;
    ui_.endScroll(r, gestScroll_);

    // the timeline: one lane a group, the last 20 s
    Rect head = tl.cutTop(20);
    ui_.label(head, "EVENTS, LAST 20 S");
    tl.cutTop(6);
    ui_.fill(tl, hex(PANEL), 3);
    ui_.stroke(tl, hex(LINE), 1.5f, 3);
    Rect in = tl.shrink(10, 8);
    static const char* LANES[] = {"tilt", "roll", "enc", "rec", "joy", "menu"};
    static const unsigned LANEC[] = {ACCENT, VIOLET, SKY, RED, MINT, SAND};
    const int NL = 6;
    float laneH = 15;
    Rect lanes = in.cutTop(laneH * NL);
    in.cutTop(6);
    double t1 = m_.plotNow(now_), t0 = t1 - 20;
    float lx = lanes.x + 40;
    float lw = lanes.w - 40;
    for (int i = 0; i < NL; i++) {
        float ly = lanes.y + i * laneH + laneH * 0.5f;
        pxText(vg_, PX_TINY, lanes.x, std::round(ly - 2.5f), 1, upper(LANES[i]).c_str(), hex(DIM));
        ui_.hline(lx, lanes.r(), std::round(ly) + 0.5f, hex(STRIPE), 1);
    }
    for (int s = 0; s <= 20; s += 5) {
        float x = std::round(lx + lw * (s / 20.0f)) + 0.5f;
        ui_.vline(x, lanes.y, lanes.b(), hex(LINE), 1);
    }
    for (auto& e : m_.events) {
        if (e.t < t0 || e.t > t1 + 0.5) continue;
        std::string grp = e.name.substr(0, e.name.find('.'));
        int lane = NL - 1;
        for (int i = 0; i < NL; i++) if (grp == LANES[i]) lane = i;
        float x = lx + static_cast<float>((e.t - t0) / 20.0) * lw;
        float ly = lanes.y + lane * laneH + laneH * 0.5f;
        float age = static_cast<float>(now_ - e.local);
        float a = age < 0.6f ? 1.0f : 0.75f;
        ui_.fill({std::round(x) - 1.5f, ly - 5, 3, 10}, hex(LANEC[lane], a), 1);
    }
    // the newest events, as text
    float ey = in.y + 7;
    int shown = 0, maxShown = std::max(0, static_cast<int>(in.h / 17));
    for (auto it = m_.events.rbegin(); it != m_.events.rend() && shown < maxShown; ++it, ++shown) {
        const Event& e = *it;
        char b[128];
        std::snprintf(b, sizeof b, "%8.2f", e.t);
        float x = ui_.text(in.x, ey, b, ui_.mono, 11.5f, hex(DIM)) + 10;
        x = ui_.text(x, ey, e.name, ui_.mono, 12, shown == 0 ? hex(INK) : hex(LOGTXT)) + 10;
        std::string extra;
        if (e.v != 0) { std::snprintf(b, sizeof b, "v=%g ", static_cast<double>(e.v)); extra += b; }
        if (e.ms) { std::snprintf(b, sizeof b, "%d ms ", e.ms); extra += b; }
        extra += e.text;
        ui_.text(x, ey, ui_.fit(extra, ui_.mono, 11.5f, in.r() - x), ui_.mono, 11.5f, hex(DIM));
        ey += 17;
    }
}

// ------------------------------------------------------------------------------------------- SETTINGS

void App::drawSettings(Rect r)
{
    if (m_.settings.empty()) {
        ui_.text(r.x, r.y + 12, m_.connected ? "waiting for the board's settings" : "no daemon", ui_.sans, 14, hex(DIM));
        return;
    }
    static const char* ORDER[] = {"PLAY", "TILT", "SOUND", "VOICE", "SLOTS", "LINK", "SETTINGS"};
    std::vector<std::string> groups(std::begin(ORDER), std::end(ORDER));
    for (auto& s : m_.settings)
        if (std::find(groups.begin(), groups.end(), s.group) == groups.end()) groups.push_back(s.group);
    float y = ui_.beginScroll(Ui::id("set.scroll"), r, setScroll_);
    float y0 = y;
    const float nameW = 132;
    for (auto& g : groups) {
        int n = 0;
        for (auto& s : m_.settings) n += s.group == g;
        if (!n) continue;
        float gy = std::round(y + 10);
        float w = pxText(vg_, PX_SMALL, r.x, gy, 2, g.c_str(), hex(ACCENT));
        ui_.hline(r.x + w + 10, r.r() - 8, gy + 5.5f, hex(LINE), 1.5f);
        y += 30;
        for (auto& s : m_.settings) {
            if (s.group != g) continue;
            float helpW = r.w - 8 - nameW;
            float hh = s.help.empty() ? 0 : ui_.wrapHeight(helpW, s.help, ui_.sans, 12.5f, 1.3f);
            float h = 34 + hh + 8;
            Rect row{r.x, y, r.w - 8, h};
            y += h;
            if (row.b() < r.y || row.y > r.b()) continue;
            pxText(vg_, PX_SMALL, row.x, std::round(row.y + 12), 2, upper(s.name).c_str(), hex(MUTE));
            uint64_t id = Ui::id("set." + s.tag + s.name);
            Rect c{row.x + nameW, row.y + 4, 34, 26};
            if (s.act()) {
                Rect b{c.x, c.y, 72, 26};
                if (ui_.button(id, b, "RUN")) {
                    if (!s.confirm.empty()) confirm_ = {true, s.i, s.name, s.confirm};
                    else run("settings.step", {{"index", s.i}, {"delta", 1}});
                }
            } else {
                if (ui_.button(id ^ 1, c, "-")) run("settings.step", {{"index", s.i}, {"delta", -1}});
                Rect p{c.r() + 4, c.y, 34, 26};
                if (ui_.button(id ^ 2, p, "+")) run("settings.step", {{"index", s.i}, {"delta", 1}});
            }
            ui_.text(row.r() - 2, row.y + 17, ui_.fit(s.text, ui_.mono, 13, row.w - nameW - 120), ui_.mono, 13, hex(INK),
                     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            if (!s.help.empty()) {
                nvgTextLineHeight(vg_, 1.3f);
                ui_.wrap(row.x + nameW, row.y + 36, helpW, s.help, ui_.sans, 12.5f, hex(DIM), 1.3f);
            }
            ui_.hline(row.x, row.r(), std::round(row.b() - 1) + 0.5f, hex(LINE), 1);
        }
        y += 10;
    }
    setScroll_.content = y - y0 + 8;
    ui_.endScroll(r, setScroll_);
}

void App::drawConfirm(Rect r)
{
    ui_.fill(r, hex(BG, 0.75f));
    Rect card{r.x + 16, r.y + 80, r.w - 32, 150};
    ui_.fill(card, hex(PANEL), 4);
    ui_.stroke(card, hex(ACCENT), 1.5f, 4);
    pxText(vg_, PX_SMALL, card.x + 18, card.y + 18, 2, upper(confirm_.name).c_str(), hex(ACCENT));
    ui_.wrap(card.x + 18, card.y + 46, card.w - 36, confirm_.text, ui_.sans, 15, hex(INK));
    Rect run_{card.r() - 18 - 90, card.b() - 18 - 30, 90, 30};
    Rect cancel{run_.x - 8 - 90, run_.y, 90, 30};
    if (ui_.button(Ui::id("confirm.run"), run_, "RUN", BTN_SOLID)) {
        run("settings.step", {{"index", confirm_.index}, {"delta", 1}});
        confirm_.open = false;
    }
    if (ui_.button(Ui::id("confirm.cancel"), cancel, "CANCEL", BTN_QUIET)) confirm_.open = false;
    // a click outside closes it
    if (ui_.in.pressed && !card.has(ui_.in.mx, ui_.in.my) && r.has(ui_.in.mx, ui_.in.my)) confirm_.open = false;
}

// ------------------------------------------------------------------------------------------- SESSION

void App::drawSession(Rect r)
{
    float y = ui_.beginScroll(Ui::id("sess.scroll"), r, sessScroll_);
    float y0 = y;
    float w = r.w - 8;
    bool rec = m_.sessionState == "recording";
    bool testRunning = m_.prompt.active;

    // record
    ui_.label({r.x, y, w, 20}, "RECORD");
    y += 28;
    {
        Rect b{r.x, y, 150, 36};
        if (!rec) {
            if (ui_.button(Ui::id("rec.start"), b, "● RECORD", BTN_SOLID, hex(BAD), m_.connected && !testRunning))
                run("record.start", {{"name", "take"}});
        } else {
            double secs = m_.sessionSecs + (now_ - m_.sessionRxLocal);
            char lab[32];
            std::snprintf(lab, sizeof lab, "STOP  %02d:%02d", static_cast<int>(secs) / 60, static_cast<int>(secs) % 60);
            if (ui_.button(Ui::id("rec.stop"), b, lab, BTN_ON, hex(BAD), !testRunning)) run("record.stop");
        }
        std::string note = rec ? "into " + m_.sessionName
                               : "motion, state, screen and sound, with the tuning and settings of the moment";
        nvgTextLineHeight(vg_, 1.3f);
        ui_.wrap(b.r() + 14, y + 2, w - 164, note, ui_.sans, 13, hex(rec ? INK : DIM), 1.3f);
        y += 48;
    }

    // guided tests
    ui_.label({r.x, y, w, 20}, "GUIDED TESTS");
    y += 28;
    if (m_.tests.empty()) {
        ui_.text(r.x, y + 8, m_.connected ? "no tests listed" : "no daemon", ui_.sans, 13.5f, hex(DIM));
        y += 26;
    }
    for (auto& t : m_.tests) {
        float hw = w - 84;
        float hh = ui_.wrapHeight(hw, t.help, ui_.sans, 12.5f, 1.3f);
        Rect row{r.x, y, w, 26 + hh + 8};
        bool runningThis = testRunning && m_.prompt.test == t.name;
        if (runningThis) ui_.fill(row, hex(GOOD, 0.08f), 2);
        float x = ui_.text(row.x, row.y + 12, t.title, ui_.bold, 14.5f, hex(INK)) + 10;
        char b[48];
        std::snprintf(b, sizeof b, "%d step%s, %.0f s", t.steps, t.steps == 1 ? "" : "s", t.secs);
        ui_.text(x, row.y + 12, b, ui_.mono, 11.5f, hex(DIM));
        nvgTextLineHeight(vg_, 1.3f);
        ui_.wrap(row.x, row.y + 24, hw, t.help, ui_.sans, 12.5f, hex(MUTE), 1.3f);
        Rect bt{row.r() - 72, row.y + 4, 72, 26};
        if (runningThis) {
            if (ui_.button(Ui::id("test.abort." + t.name), bt, "ABORT", BTN_ON, hex(GOOD))) run("test.abort");
        } else if (ui_.button(Ui::id("test.run." + t.name), bt, "RUN", BTN_OUTLINE, hex(ACCENT),
                              m_.connected && !testRunning && !rec)) {
            run("test.run", {{"name", t.name}});
        }
        y += row.h + 4;
    }
    y += 10;

    // sessions
    ui_.label({r.x, y, w, 20}, "SESSIONS");
    y += 28;
    if (m_.sessions.empty()) {
        ui_.text(r.x, y + 8, "none recorded yet", ui_.sans, 13.5f, hex(DIM));
        y += 26;
    }
    for (size_t i = 0; i < m_.sessions.size(); i++) {
        const Session& s = m_.sessions[i];
        Rect row{r.x, y, w, 26};
        y += 27;
        uint64_t id = Ui::id("sess." + s.name);
        bool sel = s.name == selSession_;
        if (ui_.clickBehaviour(id, row) && !sel) {
            selSession_ = s.name;
            std::string name = s.name;
            m_.analysis = json();
            m_.analysisFor = name;
            run("session.analyze", {{"name", name}}, [this, name](bool ok, const json& a, const json& e) {
                m_.analysisFor = name;
                m_.analysis = ok ? a : json{{"error", js(e, "message", "failed")}};
            });
        }
        if (sel) ui_.fill(row, hex(NAVON), 2);
        else if (ui_.hot == id) ui_.stroke(row, hex(LINE), 1, 2);
        ui_.text(row.x + 8, row.cy(), ui_.fit(s.name, ui_.mono, 12.5f, w - (s.test.empty() ? 70 : 140)), ui_.mono, 12.5f,
                 sel ? hex(INK) : hex(MUTE));
        char b[64];
        std::snprintf(b, sizeof b, "%.1f s", s.secs);
        ui_.text(row.r() - 8, row.cy(), b, ui_.mono, 12, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        if (!s.test.empty()) ui_.text(row.r() - 70, row.cy(), s.test, ui_.mono, 12, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    y += 12;

    // the analysis of the selected one
    if (!selSession_.empty()) {
        ui_.label({r.x, y, w, 20}, "ANALYSIS");
        y += 26;
        ui_.text(r.x, y + 6, selSession_, ui_.mono, 12.5f, hex(INK));
        y += 20;
        const json& a = m_.analysis;
        if (m_.analysisFor != selSession_ || a.is_null()) {
            ui_.text(r.x, y + 8, "analysing…", ui_.sans, 13.5f, hex(DIM));
            y += 26;
        } else if (a.is_object() && a.contains("error")) {
            ui_.text(r.x, y + 8, js(a, "error"), ui_.sans, 13.5f, hex(BAD));
            y += 26;
        } else {
            std::string summary = js(a, "summary");
            if (!summary.empty()) {
                nvgTextLineHeight(vg_, 1.3f);
                y += ui_.wrap(r.x, y, w, summary, ui_.sans, 13, hex(MUTE), 1.3f) + 8;
            }
            if (a.is_object() && a.contains("table") && a["table"].is_object()) {
                const json& t = a["table"];
                std::vector<std::string> cols;
                if (t.contains("columns") && t["columns"].is_array())
                    for (auto& c : t["columns"]) cols.push_back(jtext(c, 20));
                int nc = std::max<int>(1, static_cast<int>(cols.size()));
                float first = std::min(w * 0.42f, 168.0f);
                float cw = (w - first) / std::max(1, nc - 1);
                auto colX = [&](int c) { return c == 0 ? r.x : r.x + first + (c - 1) * cw; };
                for (int c = 0; c < nc && c < static_cast<int>(cols.size()); c++)
                    ui_.text(c == 0 ? colX(c) : colX(c) + cw - 6, y + 8, cols[c], ui_.sans, 12, hex(DIM),
                             (c == 0 ? NVG_ALIGN_LEFT : NVG_ALIGN_RIGHT) | NVG_ALIGN_MIDDLE);
                y += 18;
                ui_.hline(r.x, r.x + w, std::round(y) + 0.5f, hex(LINE), 1.5f);
                y += 2;
                if (t.contains("rows") && t["rows"].is_array())
                    for (auto& row : t["rows"]) {
                        if (!row.is_array()) continue;
                        for (int c = 0; c < nc && c < static_cast<int>(row.size()); c++) {
                            std::string v = jtext(row[c], 24);
                            if (c == 0) ui_.text(colX(0), y + 10, ui_.fit(v, ui_.sans, 13, first - 8), ui_.sans, 13, hex(MUTE));
                            else ui_.text(colX(c) + cw - 6, y + 10, v, ui_.mono, 12.5f,
                                          c == nc - 1 ? hex(DIM) : hex(INK), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                        }
                        y += 21;
                        ui_.hline(r.x, r.x + w, std::round(y) + 0.5f, hex(LINE), 1);
                    }
            } else if (a.is_object()) {
                // slrigd's analysis: sections (rest, drift, encoder, ...) of findings {metric, value, note} and the
                // tuning values they suggest
                bool any = false;
                // a test's analysis has its findings at the top; a session's, in sections
                std::vector<std::pair<std::string, const json*>> secs;
                auto hasFindings = [](const json& j) {
                    return j.is_object() && j.contains("findings") && j["findings"].is_array();
                };
                if (hasFindings(a)) secs.push_back({js(a, "test"), &a});
                for (auto it = a.begin(); it != a.end(); ++it)
                    if (hasFindings(it.value())) secs.push_back({it.key(), &it.value()});
                for (auto& [title, secp] : secs) {
                    const json& sec = *secp;
                    any = true;
                    y += 4;
                    std::string head = title.empty() ? "FINDINGS" : upper(title);
                    float gw = pxText(vg_, PX_SMALL, r.x, std::round(y + 6), 2, head.c_str(), hex(ACCENT));
                    ui_.hline(r.x + gw + 10, r.x + w, std::round(y + 11) + 0.5f, hex(LINE), 1.5f);
                    y += 22;
                    for (auto& f : sec["findings"]) {
                        std::string metric = js(f, "metric"), note = js(f, "note");
                        std::string val = f.contains("value") ? compactJson(f["value"]) : "";
                        float mw = std::min(w * 0.46f, ui_.textW(metric, ui_.sans, 13) + 4);
                        ui_.text(r.x, y + 9, ui_.fit(metric, ui_.sans, 13, w * 0.46f), ui_.sans, 13, hex(MUTE));
                        ui_.text(r.x + w, y + 9, ui_.fit(val, ui_.mono, 12.5f, w - mw - 12), ui_.mono, 12.5f, hex(INK),
                                 NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                        y += 19;
                        if (!note.empty()) {
                            nvgTextLineHeight(vg_, 1.3f);
                            y += ui_.wrap(r.x + 10, y, w - 10, note, ui_.sans, 12, hex(DIM), 1.3f) + 4;
                        }
                        ui_.hline(r.x, r.x + w, std::round(y + 2) + 0.5f, hex(LINE), 1);
                        y += 6;
                    }
                    if (sec.contains("suggest") && sec["suggest"].is_object() && !sec["suggest"].empty()) {
                        const json& sg = sec["suggest"];
                        Rect ab{r.x + w - 84, y + 2, 84, 24};
                        pxText(vg_, PX_SMALL, r.x, std::round(ab.cy() - 5), 2, "SUGGESTS", hex(MUTE));
                        if (ui_.button(Ui::id("apply." + selSession_ + title), ab, "APPLY", BTN_OUTLINE, hex(ACCENT),
                                       m_.connected))
                            for (auto s2 = sg.begin(); s2 != sg.end(); ++s2)
                                if (s2.value().is_number()) run("tune.set", {{"name", s2.key()}, {"value", s2.value()}});
                        y += 30;
                        for (auto s2 = sg.begin(); s2 != sg.end(); ++s2) {
                            const TuneItem* ti = m_.findTune(s2.key());
                            std::string now = ti ? "now " + ti->valueText(shownValue(*ti)) : "";
                            float x = ui_.text(r.x + 10, y + 8, s2.key(), ui_.mono, 12.5f, hex(INK)) + 10;
                            x = ui_.text(x, y + 8, compactJson(s2.value()), ui_.mono, 12.5f, hex(ACCENT)) + 10;
                            ui_.text(r.x + w, y + 8, now, ui_.sans, 12, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                            y += 18;
                        }
                        y += 6;
                    }
                }
                if (!any)       // anything else: every field as a row
                    for (auto it = a.begin(); it != a.end(); ++it) {
                        if (it.key() == "summary" || it.value().is_null()) continue;
                        ui_.text(r.x, y + 10, it.key(), ui_.sans, 13, hex(MUTE));
                        ui_.text(r.x + w, y + 10, ui_.fit(compactJson(it.value()), ui_.mono, 12.5f, w * 0.6f), ui_.mono,
                                 12.5f, hex(INK), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                        y += 21;
                        ui_.hline(r.x, r.x + w, std::round(y) + 0.5f, hex(LINE), 1);
                    }
            }
        }
    }
    sessScroll_.content = y - y0 + 12;
    ui_.endScroll(r, sessScroll_);
}

} // namespace slrig

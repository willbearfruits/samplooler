#include "App.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define NANOVG_GL3 1
#include <nanovg_gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace slrig {

using namespace theme;

static const char* TAB_NAMES[TAB_N] = {"TUNE", "GESTURES", "SETTINGS", "SESSION"};

App::App(Options o) : opt_(std::move(o)), net_(opt_.socket)
{
    for (int i = 0; i < TAB_N; i++) {
        std::string t = TAB_NAMES[i];
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (t == opt_.tab) tab_ = static_cast<Tab>(i);
    }
}

App::~App() { shutdown(); }

bool App::init(NVGcontext* vg, std::string& err)
{
    vg_ = vg;
    ui_.vg = vg;
    std::string fonts = opt_.assets + "/fonts/";
    ui_.sans = nvgCreateFont(vg, "sans", (fonts + "AtkinsonHyperlegible-Regular.ttf").c_str());
    ui_.bold = nvgCreateFont(vg, "bold", (fonts + "AtkinsonHyperlegible-Bold.ttf").c_str());
    ui_.mono = nvgCreateFont(vg, "mono", (fonts + "JetBrainsMono-Regular.ttf").c_str());
    if (ui_.sans < 0 || ui_.bold < 0 || ui_.mono < 0) {
        err = "fonts not found in " + fonts;
        return false;
    }
    // the mono face lacks a few symbols the sans has: fall back to it
    nvgAddFallbackFontId(vg, ui_.mono, ui_.sans);
    sceneOk_ = scene_.init(opt_.assets + "/stack.mesh", sceneErr_);
    if (!sceneOk_) std::fprintf(stderr, "slrig-view: 3D view off: %s\n", sceneErr_.c_str());
    net_.start();
    return true;
}

void App::shutdown()
{
    net_.stop();
    if (vg_) {
        if (sceneImage_ >= 0) nvgDeleteImage(vg_, sceneImage_);
        if (oledImage_ >= 0) nvgDeleteImage(vg_, oledImage_);
        sceneImage_ = oledImage_ = -1;
    }
    if (sceneOk_) scene_.destroy();
    sceneOk_ = false;
    vg_ = nullptr;
}

bool App::haveInitialData() const
{
    return m_.connected && sawTuneList_ && sawCurve_ && sawSettings_ && m_.haveScreen && m_.state.valid;
}

// ------------------------------------------------------------------------------------------- network

int App::run(const std::string& cmd, json args, Reply cb)
{
    int id = nextId_++;
    if (cb) pending_[id] = std::move(cb);
    net_.send(json{{"id", id}, {"op", "run"}, {"cmd", cmd}, {"args", std::move(args)}});
    if (!net_.connected()) pending_.erase(id);
    return id;
}

void App::op(json j) { net_.send(j); }

static const json& itemsOf(const json& r)
{
    static const json empty = json::array();
    if (r.is_array()) return r;
    if (r.is_object() && r.contains("items") && r["items"].is_array()) return r["items"];
    return empty;
}

void App::onConnected()
{
    pending_.clear();
    curveInFlight_ = false;
    op({{"id", 1}, {"op", "hello"}, {"client", "view"}});
    op({{"id", 2}, {"op", "subscribe"},
        {"streams", {"motion", "state", "screen", "events", "tune", "settings", "status", "log", "cmds", "prompt",
                     "session"}}});
    run("tune.list", json::object(), [this](bool ok, const json& r, const json&) {
        if (!ok) return;
        m_.setTuneItems(itemsOf(r));
        if (r.is_object() && r.contains("presets"))
            for (auto& p : r["presets"]) m_.presets.push_back(p.is_string() ? p.get<std::string>() : p.dump());
        sawTuneList_ = true;
    });
    run("settings.list", json::object(), [this](bool ok, const json& r, const json&) {
        if (ok) { m_.setSettingItems(itemsOf(r)); sawSettings_ = true; }
    });
    run("gestures", json::object(), [this](bool ok, const json& r, const json&) {
        if (!ok) return;
        std::vector<Gesture> next;
        for (auto& g : itemsOf(r)) {
            Gesture x;
            x.name = js(g, "name");
            x.group = js(g, "group", x.name.substr(0, x.name.find('.')));
            x.rule = js(g, "rule");
            if (g.contains("tune") && g["tune"].is_array())
                for (auto& t : g["tune"]) if (t.is_string()) x.tune.push_back(t.get<std::string>());
            x.count = ji(g, "count");
            x.lastT = jd(g, "last_t", -1);
            if (Gesture* old = m_.findGesture(x.name)) { x.flashLocal = old->flashLocal; x.count = std::max(x.count, old->count); }
            next.push_back(std::move(x));
        }
        for (auto& old : m_.gestures)        // events seen before the list came keep their place
            if (std::none_of(next.begin(), next.end(), [&](const Gesture& g) { return g.name == old.name; }))
                next.push_back(old);
        m_.gestures = std::move(next);
        sawGestures_ = true;
    });
    requestCurve();
    run("session.list", json::object(), [this](bool ok, const json& r, const json&) {
        if (!ok) return;
        m_.sessions.clear();
        for (auto& s : itemsOf(r))
            m_.sessions.push_back({js(s, "name"), js(s, "date"), js(s, "test"), jd(s, "secs")});
        if (selSession_.empty() && !m_.sessions.empty()) {
            selSession_ = m_.sessions.back().name;
            std::string name = selSession_;
            run("session.analyze", {{"name", name}}, [this, name](bool ok2, const json& a, const json& e) {
                m_.analysisFor = name;
                m_.analysis = ok2 ? a : json{{"error", js(e, "message", "failed")}};
            });
        }
    });
    run("test.list", json::object(), [this](bool ok, const json& r, const json&) {
        if (!ok) return;
        m_.tests.clear();
        for (auto& t : itemsOf(r))
            m_.tests.push_back({js(t, "name"), js(t, "title", js(t, "name")), js(t, "help"), ji(t, "steps"), jd(t, "secs")});
    });
    run("log.tail", {{"n", 40}}, [this](bool ok, const json& r, const json&) {
        if (!ok || !r.is_object() || !r.contains("lines") || !r["lines"].is_array()) return;
        std::deque<LogLine> old;
        for (auto& l : r["lines"]) {
            std::string lv = js(l, "level", "info");
            std::string text = js(l, "text");
            int level = lv == "error" ? 2 : lv == "warn" || lv == "warning" ? 1 : 0;
            if (level == 0 && text.size() > 2 && text[1] == ' ') level = text[0] == 'W' ? 1 : text[0] == 'E' ? 2 : 0;
            old.push_back({now_, text, level});
        }
        for (auto& l : m_.logs) old.push_back(l);
        m_.logs = std::move(old);
    });
}

void App::requestCurve()
{
    curveDirty_ = false;
    curveInFlight_ = true;
    curveAskedAt_ = now_;
    run("curve", {{"n", 241}}, [this](bool ok, const json& r, const json&) {
        curveInFlight_ = false;
        if (!ok || !r.is_object()) return;
        Curve c;
        auto floats = [](const json& o, const char* k) {
            std::vector<float> v;
            if (o.is_object() && o.contains(k) && o[k].is_array())
                for (auto& x : o[k]) v.push_back(x.is_number() ? x.get<float>() : 0.0f);
            return v;
        };
        if (r.contains("tilt")) {
            const json& t = r["tilt"];
            c.angle = floats(t, "angle");
            c.speed = floats(t, "speed");
            c.dead = jf(t, "dead", 5);
            c.full = jf(t, "full", 45);
            c.hyst = jf(t, "hyst", 0);
            c.sign = jf(t, "sign", 1);
            c.speedMin = jf(t, "speed_min", 0);
            c.speedMax = jf(t, "speed_max", 0);
            if (t.contains("detents") && t["detents"].is_array())
                for (auto& d : t["detents"]) {
                    Curve::Det x;
                    if (d.is_number()) x.speed = d.get<float>();
                    else { x.speed = jf(d, "speed"); x.lo = jf(d, "lo"); x.hi = jf(d, "hi"); x.range = d.contains("lo"); }
                    c.detents.push_back(x);
                }
        }
        if (r.contains("roll")) {
            const json& t = r["roll"];
            c.rAngle = floats(t, "angle");
            c.delay = floats(t, "delay");
            c.stutter = floats(t, "stutter");
            c.rDead = jf(t, "dead", 15);
            c.rFull = jf(t, "full", 45);
        }
        c.valid = !c.angle.empty() && c.angle.size() == c.speed.size();
        m_.curve = std::move(c);
        sawCurve_ = true;
    });
}

void App::handleReply(const json& msg)
{
    int id = ji(msg, "id", -1);
    bool ok = jb(msg, "ok");
    if (!ok && id > 2) {
        const json& e = msg.contains("error") ? msg["error"] : json();
        std::string text = js(e, "message", "failed");
        std::string hint = js(e, "hint");
        m_.addLog(now_, "view: " + text + (hint.empty() ? "" : " (" + hint + ")"), 2);
        toast_ = text;
        toastAt_ = now_;
    }
    if (id == 1 && ok && msg.contains("result")) m_.daemonName = js(msg["result"], "daemon", "slrigd");
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    Reply cb = std::move(it->second);
    pending_.erase(it);
    static const json none;
    cb(ok, msg.contains("result") ? msg["result"] : none, msg.contains("error") ? msg["error"] : none);
}

void App::update(double now)
{
    now_ = now;
    std::vector<NetClient::Msg> msgs;
    net_.drain(msgs);
    for (auto& mm : msgs) {
        const json& j = mm.j;
        if (j.is_object() && j.contains("id") && !j.contains("ev")) { handleReply(j); continue; }
        unsigned ch = m_.apply(j, now, mm.rx);
        if (ch & CH_CONNECT) {
            sawTuneList_ = sawCurve_ = sawGestures_ = sawSettings_ = false;
            onConnected();
        }
        if (ch & CH_DISCONNECT) { pending_.clear(); curveInFlight_ = false; }
        if (ch & (CH_TUNE | CH_SETTINGS)) curveDirty_ = true;
        if (ch & CH_TUNE) {
            for (auto& it : m_.tune)            // a push that confirms what we sent ends the wait
                if (it.pendingUntil > now && std::fabs(it.value - it.pending) <= std::max(1e-6f, it.step * 0.01f))
                    it.pendingUntil = 0;
        }
        if (ch & CH_SESSION_DONE)
            run("session.list", json::object(), [this](bool ok, const json& r, const json&) {
                if (!ok) return;
                m_.sessions.clear();
                for (auto& s : itemsOf(r)) m_.sessions.push_back({js(s, "name"), js(s, "date"), js(s, "test"), jd(s, "secs")});
            });
    }
    if (curveDirty_ && m_.connected && (!curveInFlight_ || now - curveAskedAt_ > 2.0) && now - curveAskedAt_ > 0.08)
        requestCurve();
    // slider drags: the newest value, at most every 50 ms
    for (auto it = tuneSend_.begin(); it != tuneSend_.end();) {
        auto& [name, w] = *it;
        if (w.second < 0 || now - w.second >= 0.05) {
            run("tune.set", {{"name", name}, {"value", w.first}});
            it = tuneSend_.erase(it);
        } else ++it;
    }
}

// ------------------------------------------------------------------------------------------- tuning edits

float App::shownValue(const TuneItem& it) const { return now_ < it.pendingUntil ? it.pending : it.value; }

void App::setTune(TuneItem& it, float v, bool final)
{
    it.pending = v;
    it.pendingUntil = now_ + 1.5;
    auto f = tuneSend_.find(it.name);
    double last = f != tuneSend_.end() ? f->second.second : -1;
    tuneSend_[it.name] = {v, final ? -1.0 : (last < 0 ? now_ - 1.0 : last)};
    if (final) tuneSend_[it.name].second = -1;
}

void App::stepTune(TuneItem& it, int dir, bool big)
{
    if (it.kind == "action") return;
    float st = it.step > 0 ? it.step : (it.max - it.min) / 100.0f;
    if (big && it.kind == "float") st *= 10;
    float v = std::clamp(shownValue(it) + dir * st, it.min, it.max);
    setTune(it, v, true);
}

// ------------------------------------------------------------------------------------------- input

void App::mouseMove(double x, double y)
{
    in_.mx = static_cast<float>(x);
    in_.my = static_cast<float>(y);
}

void App::mouseButton(int button, bool down, int mods, double now)
{
    in_.mods = mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (down) {
            in_.pressed = true;
            in_.down = true;
            float dx = in_.mx - lastClickX_, dy = in_.my - lastClickY_;
            in_.dbl = now - lastClick_ < 0.35 && dx * dx + dy * dy < 25;
            lastClick_ = in_.dbl ? -1 : now;
            lastClickX_ = in_.mx;
            lastClickY_ = in_.my;
        } else {
            in_.released = true;
            in_.down = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        in_.rdown = down;
        if (down) in_.rpressed = true;
    }
}

void App::scroll(double dy) { in_.wheel += static_cast<float>(dy); }

void App::key(int key, int mods)
{
    in_.mods = mods;
    in_.keys.push_back(key);
}

// ------------------------------------------------------------------------------------------- layout

App::Layout App::layout(float W, float H) const
{
    Layout L;
    const float g = 16;
    Rect all{0, 6, W, H - 6};
    L.header = all.cutTop(46);
    L.status = all.cutBottom(26);
    all = all.shrink(g, 0);
    all.h -= 8;
    float rightW = std::clamp(std::round(W * 0.27f), 360.0f, 460.0f);
    L.right = all.cutRight(rightW);
    all.cutRight(g);
    float logH = H >= 960 ? 118 : H >= 860 ? 100 : 84;
    L.logs = all.cutBottom(logH);
    all.cutBottom(12);
    // plots: two rows across the left side
    float plotsH = std::clamp(std::round(all.h * 0.36f), 200.0f, 300.0f);
    L.plots = all.cutBottom(plotsH);
    all.cutBottom(14);
    // the top row: the 3D view, and the OLED with the curves beside it
    int scale = 4;
    auto colW = [](int s) { return 128.0f * s + 28.0f; };
    float oledBlockH = [&](int s) { return 22.0f + 64.0f * s + 28.0f; }(4);
    if (all.w - colW(4) < 520 || all.h - oledBlockH < 150) scale = 3;
    if (all.w - colW(scale) < 380 || all.h - (22.0f + 64.0f * scale + 28.0f) < 120) scale = 2;
    L.oledScale = scale;
    Rect col = all.cutRight(colW(scale));
    all.cutRight(g);
    L.view3d = all;
    L.oled = col.cutTop(22.0f + 64.0f * scale + 28.0f);
    col.cutTop(10);
    float half = std::floor((col.w - 12) * 0.5f);
    L.curveTilt = col.cutLeft(half);
    col.cutLeft(12);
    L.curveRoll = col;
    return L;
}

// ------------------------------------------------------------------------------------------- the frame

void App::frame(unsigned target, int winW, int winH, int fbW, int fbH, double now)
{
    auto t0 = std::chrono::steady_clock::now();
    now_ = now;
    float pxRatio = winW > 0 ? static_cast<float>(fbW) / static_cast<float>(winW) : 1.0f;
    Layout L = layout(static_cast<float>(winW), static_cast<float>(winH));
    view3d_ = L.view3d;

    // keys first, so this frame shows what they did
    for (int k : in_.keys) {
        bool shift = in_.mods & GLFW_MOD_SHIFT, ctrl = in_.mods & GLFW_MOD_CONTROL;
        if (confirm_.open) {
            if (k == GLFW_KEY_ESCAPE) confirm_.open = false;
            if (k == GLFW_KEY_ENTER) { run("settings.step", {{"index", confirm_.index}, {"delta", 1}}); confirm_.open = false; }
            continue;
        }
        if (k >= GLFW_KEY_1 && k <= GLFW_KEY_4) tab_ = static_cast<Tab>(k - GLFW_KEY_1);
        else if (k == GLFW_KEY_TAB) tab_ = static_cast<Tab>((tab_ + (shift ? TAB_N - 1 : 1)) % TAB_N);
        else if (k == GLFW_KEY_R && !ctrl) scene_.resetCamera();
        else if (k == GLFW_KEY_G) pixelGrid_ = !pixelGrid_;
        else if (k == GLFW_KEY_P) panel_ = (panel_ + 1) % 3;
        else if (k == GLFW_KEY_A) showAxes_ = !showAxes_;
        else if (k == GLFW_KEY_F) { plotsFrozen_ = !plotsFrozen_; frozenAt_ = m_.plotNow(now); }
        else if (k == GLFW_KEY_LEFT_BRACKET) plotWindowIdx_ = std::max(0, plotWindowIdx_ - 1);
        else if (k == GLFW_KEY_RIGHT_BRACKET) plotWindowIdx_ = std::min(3, plotWindowIdx_ + 1);
        else if (k == GLFW_KEY_Q && ctrl) quit_ = true;
        else if (tab_ == TAB_TUNE && (k == GLFW_KEY_UP || k == GLFW_KEY_DOWN) && !m_.tune.empty()) {
            int i = 0;
            for (int j = 0; j < static_cast<int>(m_.tune.size()); j++) if (m_.tune[j].name == focusTune_) i = j;
            i = std::clamp(i + (k == GLFW_KEY_DOWN ? 1 : -1), 0, static_cast<int>(m_.tune.size()) - 1);
            if (focusTune_.empty()) i = 0;
            focusTune_ = m_.tune[i].name;
            scrollToFocus_ = true;
        } else if (tab_ == TAB_TUNE && (k == GLFW_KEY_LEFT || k == GLFW_KEY_RIGHT)) {
            if (TuneItem* it = m_.findTune(focusTune_)) stepTune(*it, k == GLFW_KEY_RIGHT ? 1 : -1, shift);
        }
    }

    // the 3D view, into its texture
    if (sceneOk_ && L.view3d.w > 20 && L.view3d.h > 20) {
        SceneInput si;
        bool live = m_.connected && m_.state.valid;
        si.live = live;
        si.q = live ? m_.state.quat : Quat{};
        si.haveRef = live && m_.haveRef;
        si.ref = m_.refQuat;
        si.acc = m_.acc;
        si.haveAcc = live && m_.motion.count > 0;
        si.fb = m_.haveScreen ? m_.fb.data() : nullptr;
        si.fbVersion = m_.fbVersion;
        si.panel = panel_;
        si.axes = showAxes_;
        si.gravity = showGravity_;
        int w = static_cast<int>(std::round(L.view3d.w * pxRatio)), h = static_cast<int>(std::round(L.view3d.h * pxRatio));
        scene_.render(si, w, h);
        if (scene_.texGeneration() != sceneImageGen_ || sceneImage_ < 0) {
            if (sceneImage_ >= 0) nvgDeleteImage(vg_, sceneImage_);
            sceneImage_ = nvglCreateImageFromHandleGL3(vg_, scene_.texture(), scene_.texW(), scene_.texH(),
                                                       static_cast<int>(NVG_IMAGE_FLIPY) | static_cast<int>(NVG_IMAGE_NODELETE));
            sceneImageGen_ = scene_.texGeneration();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, fbW, fbH);
    glClearColor(((BG >> 16) & 255) / 255.0f, ((BG >> 8) & 255) / 255.0f, (BG & 255) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(vg_, static_cast<float>(winW), static_cast<float>(winH), pxRatio);
    pxSetRatio(pxRatio);
    ui_.pxRatio = pxRatio;
    ui_.begin(in_, now);

    // the strip: every instrument's colour, as the app's header
    for (int i = 0; i < 12; i++) {
        float x0 = std::round(STRIP_AT[i] * winW), x1 = std::round(STRIP_AT[i + 1] * winW);
        ui_.fill({x0, 0, x1 - x0, 4}, hex(STRIP[i]));
    }
    drawHeader(L.header);
    drawRight(L.right);
    draw3dView(L.view3d);
    drawOled(L.oled, L.oledScale);
    drawCurveTilt(L.curveTilt);
    drawCurveRoll(L.curveRoll);
    drawPlots(L.plots);
    drawLogs(L.logs);
    drawStatus(L.status);
    if (confirm_.open) drawConfirm(L.right);
    if (!toast_.empty() && now - toastAt_ < 3.0) {
        float a = static_cast<float>(std::clamp(3.0 - (now - toastAt_), 0.0, 1.0));
        float tw = ui_.textW(toast_, ui_.sans, 14) + 32;
        Rect t{std::round(winW * 0.5f - tw * 0.5f), static_cast<float>(winH) - 26 - 52, tw, 36};
        ui_.fill(t, nvgRGBAf(0.086f, 0.086f, 0.078f, a), 3);
        ui_.stroke(t, hex(BAD, a), 1.5f, 3);
        ui_.text(t.cx(), t.cy(), toast_, ui_.sans, 14, hex(INK, a), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    ui_.end();
    nvgEndFrame(vg_);

    in_.pressed = in_.released = in_.dbl = in_.rpressed = false;
    in_.wheel = 0;
    in_.keys.clear();

    cpuLast_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    cpuHist_[cpuHistI_] = cpuLast_;
    cpuHistI_ = (cpuHistI_ + 1) % 120;
    cpuHistN_ = std::min(120, cpuHistN_ + 1);
}

void App::presented(double now)
{
    if (m_.lastStateRx > 0 && m_.lastStateRx != drawnStateRx_) {
        float lat = static_cast<float>((now - m_.lastStateRx) * 1000.0);
        drawnStateRx_ = m_.lastStateRx;
        latLast_ = lat;
        latAvg_ = latAvg_ <= 0 ? lat : latAvg_ * 0.9f + lat * 0.1f;
        latWinMax_ = std::max(latWinMax_, lat);
    }
    if (now - latWindow_ >= 1.0) {
        latMax_ = latWinMax_;
        latWinMax_ = 0;
        latWindow_ = now;
    }
}

// ------------------------------------------------------------------------------------------- header and status

void App::drawHeader(Rect r)
{
    NVGcontext* vg = vg_;
    float x = r.x + 16;
    float cell = 2.0f;
    float ty = std::round(r.cy() - pxHeight(PX_MED, cell) * 0.5f) + 1;
    x += pxText(vg, PX_MED, x, ty, cell, "SAMPLOOLER", hex(ACCENT)) + 10;
    x += pxText(vg, PX_MED, x, ty, cell, "RIG", hex(INK)) + 22;

    // the board chip, as the app's .bchip
    std::string who;
    const Status& st = m_.status;
    bool board = m_.connected && st.valid && st.source != "none";
    if (board) {
        who = st.version.empty() ? "" : st.version;
        if (!st.serial.empty()) who += (who.empty() ? "" : "  ·  ") + st.serial;
    }
    std::string name = board ? (st.boardName.empty() ? "BOARD" : st.boardName) : (m_.connected ? "NO BOARD" : "NO DAEMON");
    for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    float nameW = pxWidth(PX_SMALL, name.c_str(), 2);
    float whoW = who.empty() ? 0 : ui_.textW(who, ui_.sans, 13.5f) + 12;
    Rect chip{x, r.cy() - 17, 18 + 10 + nameW + whoW + 14, 34};
    ui_.stroke(chip, board ? hex(ACCENT) : hex(LINE), 1.5f, 3);
    NVGcolor dot = board ? hex(GOOD) : m_.connected ? hex(YELLOW) : hex(DIM);
    nvgBeginPath(vg);
    nvgCircle(vg, chip.x + 15, chip.cy(), 3.5f);
    nvgFillColor(vg, dot);
    nvgFill(vg);
    pxText(vg, PX_SMALL, chip.x + 28, std::round(chip.cy() - 5), 2, name.c_str(), board ? hex(INK) : hex(MUTE));
    if (!who.empty()) ui_.text(chip.x + 28 + nameW + 12, chip.cy() + 0.5f, who, ui_.sans, 13.5f, hex(MUTE));
    x = chip.r() + 14;
    if (board) {
        std::string src = st.source == "twin" ? "TWIN" : "USB";
        float w = pxWidth(PX_SMALL, src.c_str(), 2) + 14;
        Rect tag{x, r.cy() - 11, w, 22};
        ui_.stroke(tag, hex(LINE), 1.5f, 2);
        pxText(vg, PX_SMALL, tag.x + 7, std::round(tag.cy() - 5), 2, src.c_str(), hex(MUTE));
        x = tag.r() + 10;
    }

    // the right side: recording, then the session
    float rx = r.r() - 16;
    bool rec = m_.sessionState == "recording" || st.recording;
    if (rec) {
        double secs = m_.sessionSecs + (now_ - m_.sessionRxLocal);
        char b[64];
        std::snprintf(b, sizeof b, "%02d:%02d", static_cast<int>(secs) / 60, static_cast<int>(secs) % 60);
        float w = pxWidth(PX_SMALL, b, 2);
        pxText(vg, PX_SMALL, rx - w, std::round(r.cy() - 5), 2, b, hex(INK));
        rx -= w + 10;
        float wr = pxWidth(PX_SMALL, "REC", 2);
        bool blink = std::fmod(now_, 1.0) < 0.6;
        pxText(vg, PX_SMALL, rx - wr, std::round(r.cy() - 5), 2, "REC", hex(BAD));
        rx -= wr + 10;
        nvgBeginPath(vg);
        nvgCircle(vg, rx - 4, r.cy(), 5);
        nvgFillColor(vg, hex(BAD, blink ? 1.0f : 0.35f));
        nvgFill(vg);
        rx -= 16;
        if (!m_.sessionName.empty()) {
            ui_.text(rx, r.cy() + 0.5f, m_.sessionName, ui_.mono, 12.5f, hex(MUTE), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            rx -= ui_.textW(m_.sessionName, ui_.mono, 12.5f) + 12;
        }
    }
    if (m_.connected && m_.state.valid) {
        const State& s = m_.state;
        std::string flags;
        if (s.frozen) flags += "FROZEN  ";
        if (s.injected || m_.injected) flags += "INJECTED  ";
        if (s.menu) flags += "MENU  ";
        if (!flags.empty()) pxTextR(vg, PX_SMALL, rx - 6, std::round(r.cy() - 5), 2, flags.c_str(), hex(ORANGE));
    }
    ui_.hline(r.x, r.r(), r.b() - 0.75f, hex(LINE), 1.5f);
}

void App::drawStatus(Rect r)
{
    ui_.fill(r, hex(PANEL));
    ui_.hline(r.x, r.r(), r.y + 0.75f, hex(LINE), 1.5f);
    float x = r.x + 16, cy = r.cy() + 0.5f;
    const float fs = 12.5f;
    auto sep = [&] { ui_.vline(std::round(x + 8) + 0.5f, r.y + 7, r.b() - 7, hex(LINE), 1); x += 17; };
    auto put = [&](const std::string& s, unsigned col) { x = ui_.text(x, cy, s, ui_.mono, fs, hex(col)); };
    const Status& st = m_.status;
    char b[160];
    if (!m_.connected) {
        nvgBeginPath(vg_);
        nvgCircle(vg_, x + 3, cy, 3);
        nvgFillColor(vg_, hex(DIM));
        nvgFill(vg_);
        x += 12;
        put("no daemon", MUTE);
        sep();
        put(net_.path(), DIM);
        sep();
        put("retrying every second", DIM);
    } else {
        std::string src = st.valid ? st.source : "?";
        nvgBeginPath(vg_);
        nvgCircle(vg_, x + 3, cy, 3);
        nvgFillColor(vg_, src == "none" ? hex(YELLOW) : hex(GOOD));
        nvgFill(vg_);
        x += 12;
        put(src, src == "none" ? YELLOW : INK);
        sep();
        if (!st.boardName.empty()) {
            std::snprintf(b, sizeof b, "%s %s  %s", st.boardName.c_str(), st.version.c_str(), st.serial.c_str());
            put(b, MUTE);
            sep();
        }
        std::snprintf(b, sizeof b, "motion %.0f/s", static_cast<double>(m_.rMotion.perSec));
        put(b, MUTE);
        std::snprintf(b, sizeof b, "  state %.0f/s", static_cast<double>(m_.rState.perSec));
        put(b, MUTE);
        std::snprintf(b, sizeof b, "  screen %.1f/s", static_cast<double>(m_.rScreen.perSec));
        put(b, MUTE);
        sep();
        long drops = st.dropMotion + st.dropState + st.dropScreen;
        std::snprintf(b, sizeof b, "drops %ld/%ld/%ld", st.dropMotion, st.dropState, st.dropScreen);
        put(b, drops ? ORANGE : DIM);
        if (m_.state.valid) {
            sep();
            std::snprintf(b, sizeof b, "cpu %.0f%%  heap %.0f KB  i2c %d", static_cast<double>(m_.state.cpu),
                          static_cast<double>(m_.state.heap_kb), m_.state.i2c_fail);
            put(b, m_.state.i2c_fail ? ORANGE : DIM);
        }
        if (st.recording || m_.sessionState == "recording") {
            sep();
            put("● recording", BAD);
        }
    }
    // the right: this viewer's own numbers
    float mx = 0;
    for (int i = 0; i < cpuHistN_; i++) mx = std::max(mx, cpuHist_[i]);
    std::snprintf(b, sizeof b, "frame %.2f ms (max %.2f)", static_cast<double>(cpuLast_), static_cast<double>(mx));
    std::string right = b;
    if (latLast_ > 0) {
        std::snprintf(b, sizeof b, "   rx→draw %.1f ms (max %.1f)", static_cast<double>(latAvg_),
                      static_cast<double>(std::max(latMax_, latWinMax_)));
        right += b;
    }
    ui_.text(r.r() - 16, cy, right, ui_.mono, fs, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
}

// ------------------------------------------------------------------------------------------- logs

void App::drawLogs(Rect r)
{
    float half = std::floor((r.w - 16) * 0.5f);
    Rect a = r;
    Rect left = a.cutLeft(half);
    a.cutLeft(16);
    Rect right = a;
    const float lh = 17.0f, fs = 12.5f;
    // who ran what
    {
        Rect head = left.cutTop(18);
        ui_.label(head, "COMMANDS");
        left.cutTop(6);
        ui_.fill(left, hex(LOGBG), 3);
        ui_.stroke(left, hex(LINE), 1.5f, 3);
        Rect in = left.shrink(10, 6);
        ui_.pushClip(in);
        int rows = std::max(1, static_cast<int>(in.h / lh));
        int n = static_cast<int>(m_.cmds.size());
        float y = in.y + lh * 0.5f + std::max(0, rows - n) * 0.0f;
        for (int i = std::max(0, n - rows); i < n; i++) {
            const CmdLine& c = m_.cmds[i];
            unsigned col = c.by == "mcp" ? BY_MCP : c.by == "cli" ? BY_CLI : c.by == "osc" ? BY_OSC : BY_VIEW;
            float x = in.x;
            bool mcp = c.by == "mcp";
            double age = now_ - c.local;
            if (mcp && age < 2.0) ui_.fill({in.x - 6, y - lh * 0.5f, in.w + 12, lh}, hex(BY_MCP, 0.10f * static_cast<float>(2.0 - age)));
            std::string by = c.by;
            for (auto& ch : by) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            Rect tag{x, std::round(y - 6.5f), 34, 13};
            if (mcp) ui_.fill(tag, hex(BY_MCP), 2);
            else ui_.stroke(tag, hex(col, 0.6f), 1, 2);
            pxTextC(vg_, PX_TINY, tag.cx(), tag.y + 4, 1, by.c_str(), mcp ? nvgRGBf(0, 0, 0) : hex(col));
            x += 42;
            x = ui_.text(x, y, c.cmd, ui_.mono, fs, mcp ? hex(INK) : hex(LOGTXT)) + 8;
            char ms[32];
            std::snprintf(ms, sizeof ms, "%d ms", c.ms);
            float msw = ui_.textW(ms, ui_.mono, 11.5f);
            std::string args = ui_.fit(c.args, ui_.mono, fs, in.r() - x - msw - 34);
            ui_.text(x, y, args, ui_.mono, fs, hex(DIM));
            if (!c.ok) ui_.text(in.r() - msw - 10, y, "fail", ui_.mono, 11.5f, hex(BAD), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            ui_.text(in.r(), y, ms, ui_.mono, 11.5f, hex(DIM), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            y += lh;
        }
        if (n == 0) ui_.text(in.x, in.y + lh * 0.5f, m_.connected ? "nothing run yet" : "", ui_.mono, fs, hex(DIM));
        ui_.popClip();
    }
    // the board's log
    {
        Rect head = right.cutTop(18);
        ui_.label(head, "BOARD LOG");
        right.cutTop(6);
        ui_.fill(right, hex(LOGBG), 3);
        ui_.stroke(right, hex(LINE), 1.5f, 3);
        Rect in = right.shrink(10, 6);
        ui_.pushClip(in);
        int rows = std::max(1, static_cast<int>(in.h / lh));
        int n = static_cast<int>(m_.logs.size());
        float y = in.y + lh * 0.5f;
        for (int i = std::max(0, n - rows); i < n; i++) {
            const LogLine& l = m_.logs[i];
            unsigned col = l.level == 2 ? BAD : l.level == 1 ? YELLOW : LOGTXT;
            ui_.text(in.x, y, ui_.fit(l.text, ui_.mono, fs, in.w), ui_.mono, fs, hex(col));
            y += lh;
        }
        ui_.popClip();
    }
}

// ------------------------------------------------------------------------------------------- the 3D view

void App::draw3dView(Rect r)
{
    NVGcontext* vg = vg_;
    ui_.fill(r, hex(0x0a0a09), 3);
    if (sceneImage_ >= 0 && sceneOk_) {
        NVGpaint p = nvgImagePattern(vg, r.x, r.y, r.w, r.h, 0, sceneImage_, 1.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, r.x, r.y, r.w, r.h, 3);
        nvgFillPaint(vg, p);
        nvgFill(vg);
    } else {
        ui_.text(r.cx(), r.cy(), "3D view off: " + sceneErr_, ui_.sans, 14, hex(MUTE), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    ui_.stroke(r, hex(LINE), 1.5f, 3);

    // orbit, zoom, reset
    uint64_t vid = Ui::id("view3d");
    bool inside = ui_.over(r);
    if (inside && ui_.in.pressed && ui_.active == 0 && !ui_.in.dbl) {
        ui_.active = vid;
        orbitX_ = ui_.in.mx;
        orbitY_ = ui_.in.my;
    }
    if (ui_.active == vid && ui_.in.down) {
        scene_.orbit((ui_.in.mx - orbitX_) * 0.45f, (ui_.in.my - orbitY_) * 0.35f);
        orbitX_ = ui_.in.mx;
        orbitY_ = ui_.in.my;
    }
    if (inside && ui_.in.dbl) scene_.resetCamera();
    if (inside && ui_.in.wheel != 0 && !ui_.wheelUsed) { scene_.zoom(ui_.in.wheel); ui_.wheelUsed = true; }

    pxText(vg, PX_SMALL, r.x + 14, r.y + 14, 2, "STACK  REV C", hex(MUTE));
    bool live = m_.connected && m_.state.valid;
    if (!m_.connected) { drawNoDaemon(r); }
    else if (!live) {
        ui_.text(r.cx(), r.y + 46, "connected; waiting for the board's state", ui_.sans, 14, hex(MUTE),
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    // labels at the arrow tips
    if (live && scene_.tipsValid) {
        auto tip = [&](V3 p, const char* s, unsigned col) {
            float sx, sy;
            if (!scene_.project(p, sx, sy)) return;
            sx = r.x + sx / ui_.pxRatio;
            sy = r.y + sy / ui_.pxRatio;
            if (!r.shrink(8).has(sx, sy)) return;
            pxTextC(vg, PX_SMALL, sx, std::round(sy - 5), 2, s, hex(col));
        };
        if (showAxes_) {
            tip(scene_.tipX, "X", AX_X);
            tip(scene_.tipY, "Y", AX_Y);
            tip(scene_.tipZ, "Z", AX_Z);
        }
        if (showGravity_) tip(scene_.tipG, "G", GRAV);
    }

    // toggles, top right
    float bx = r.r() - 12;
    auto tgl = [&](const char* lab, bool on, const char* key) {
        float w = pxWidth(PX_SMALL, lab, 1) + 16;
        Rect b{bx - w, r.y + 10, w, 22};
        bx -= w + 6;
        return ui_.button(Ui::id(key), b, lab, on ? BTN_ON : BTN_OUTLINE, hex(MUTE));
    };
    if (scene_.cameraMoved() && tgl("RESET VIEW", false, "3d.reset")) scene_.resetCamera();
    if (tgl("GRAVITY", showGravity_, "3d.grav")) showGravity_ = !showGravity_;
    if (tgl("AXES", showAxes_, "3d.axes")) showAxes_ = !showAxes_;

    // the numbers, bottom left
    if (live) {
        const State& s = m_.state;
        float x = r.x + 16, y = r.b() - 70;
        char b[64];
        auto row = [&](const char* name, float v, unsigned col, float yy) {
            pxText(vg, PX_SMALL, x, std::round(yy - 5), 2, name, hex(col));
            std::snprintf(b, sizeof b, "%+7.1f°", static_cast<double>(v));
            ui_.text(x + 62, yy, b, ui_.mono, 15, hex(INK));
        };
        row("PITCH", s.pitch, PITCH, y);
        row("ROLL", s.roll, ROLL, y + 22);
        row("YAW", s.yaw, YAW, y + 44);
        // speed, right of it
        float x2 = x + 178;
        pxText(vg, PX_SMALL, x2, std::round(y - 5), 2, "SPEED", hex(MUTE));
        std::snprintf(b, sizeof b, "%+.2f×", static_cast<double>(s.speed_now));
        ui_.text(x2 + 62, y, b, ui_.mono, 15, hex(INK));
        std::string zone = s.zone.empty() ? "-" : s.zone;
        for (auto& c : zone) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        pxText(vg, PX_SMALL, x2, std::round(y + 22 - 5), 2, "ZONE", hex(MUTE));
        pxText(vg, PX_SMALL, x2 + 62, std::round(y + 22 - 5), 2, zone.c_str(),
               zone == "DEAD" ? hex(DIM) : zone == "BACK" ? hex(VIOLET) : hex(ACCENT));
        pxText(vg, PX_SMALL, x2, std::round(y + 44 - 5), 2, "|A|", hex(MUTE));
        std::snprintf(b, sizeof b, "%.3f g", static_cast<double>(len(m_.acc)));
        ui_.text(x2 + 62, y + 44, b, ui_.mono, 15, hex(INK));

        // the ghost's legend, bottom right
        float gx = r.r() - 16, gy = r.b() - 20;
        if (m_.haveRef) {
            float ang = qangle(s.quat, m_.refQuat);
            std::snprintf(b, sizeof b, "%.1f°", static_cast<double>(ang));
            float w = ui_.text(gx, gy, b, ui_.mono, 13, hex(INK), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            (void)w;
            float tw = ui_.textW(b, ui_.mono, 13);
            ui_.text(gx - tw - 8, gy, "ghost: the host's fusion, off by", ui_.sans, 13, hex(MUTE),
                     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            gy -= 20;
        }
        std::string chips;
        if (s.rest) chips += "REST  ";
        if (s.frozen) chips += "FROZEN  ";
        if (s.injected || m_.injected) chips += "INJECTED  ";
        if (!chips.empty()) pxTextR(vg, PX_SMALL, gx, std::round(gy - 5), 2, chips.c_str(), hex(ORANGE));
    }
    if (m_.prompt.active) drawPrompt(r);
}

void App::drawNoDaemon(Rect r)
{
    Rect card{std::round(r.cx() - 210), std::round(r.y + r.h * 0.5f - 70), 420, 132};
    ui_.fill(card, hex(PANEL, 0.92f), 4);
    ui_.stroke(card, hex(LINE), 1.5f, 4);
    pxTextC(vg_, PX_SMALL, card.cx(), card.y + 20, 2, "NO DAEMON", hex(ACCENT));
    float y = card.y + 46;
    y += ui_.wrap(card.x + 22, y, card.w - 44, "Nothing answers at " + net_.path() +
                  ". Start slrigd, or tools/mock_daemon.py for a pretend board. The viewer connects by itself.",
                  ui_.sans, 14, hex(MUTE));
}

void App::drawPrompt(Rect r)
{
    const Prompt& p = m_.prompt;
    double left = std::max(0.0, p.secs - (now_ - p.rxLocal));
    double total = p.total > 0 ? p.total : std::max(p.secs, 1.0);
    unsigned col = p.phase == "go" ? GOOD : p.phase == "done" ? INK : ACCENT;
    float cw = std::min(r.w - 40, 640.0f);
    Rect card{std::round(r.cx() - cw * 0.5f), r.y + 44, cw, 0};
    float textW = card.w - 150;
    float th = ui_.wrapHeight(textW, p.text, ui_.bold, 30, 1.2f);
    card.h = std::max(118.0f, th + 64);
    ui_.fill(card, hex(0x0d0d0c, 0.93f), 4);
    ui_.stroke(card, hex(col, 0.9f), 1.5f, 4);
    // the countdown ring
    float cx = card.x + 64, cy = card.y + card.h * 0.5f, rad = 38;
    nvgBeginPath(vg_);
    nvgCircle(vg_, cx, cy, rad);
    nvgStrokeColor(vg_, hex(LINE));
    nvgStrokeWidth(vg_, 5);
    nvgStroke(vg_);
    float frac = static_cast<float>(std::clamp(left / total, 0.0, 1.0));
    if (frac > 0.001f) {
        nvgBeginPath(vg_);
        nvgArc(vg_, cx, cy, rad, -NVG_PI * 0.5f, -NVG_PI * 0.5f + 2 * NVG_PI * frac, NVG_CW);
        nvgStrokeColor(vg_, hex(col));
        nvgStrokeWidth(vg_, 5);
        nvgLineCap(vg_, NVG_ROUND);
        nvgStroke(vg_);
        nvgLineCap(vg_, NVG_BUTT);
    }
    char b[32];
    std::snprintf(b, sizeof b, "%d", static_cast<int>(std::ceil(left)));
    if (p.phase == "done") std::snprintf(b, sizeof b, "OK");
    float bw = pxWidth(PX_BIG, b, 2);
    pxText(vg_, PX_BIG, std::round(cx - bw * 0.5f), std::round(cy - 18), 2, b, hex(INK));
    // step, phase, the words
    float tx = card.x + 128;
    std::string ph = p.phase == "go" ? "GO" : p.phase == "done" ? "DONE" : "GET READY";
    std::snprintf(b, sizeof b, "STEP %d OF %d", p.step, p.of);
    float w = pxText(vg_, PX_SMALL, tx, card.y + 18, 2, ph.c_str(), hex(col));
    pxText(vg_, PX_SMALL, tx + w + 14, card.y + 18, 2, b, hex(MUTE));
    if (!p.test.empty()) {
        std::string t = p.test;
        for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        pxTextR(vg_, PX_SMALL, card.r() - 16, card.y + 18, 2, t.c_str(), hex(DIM));
    }
    nvgTextLineHeight(vg_, 1.2f);
    ui_.wrap(tx, card.y + 40, textW, p.text, ui_.bold, 30, hex(INK), 1.2f);
    // abort
    Rect ab{card.r() - 86, card.b() - 34, 74, 24};
    if (p.phase != "done" && ui_.button(Ui::id("prompt.abort"), ab, "ABORT", BTN_QUIET)) run("test.abort");
}

} // namespace slrig

#include "Model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace slrig {

// ------------------------------------------------------------------------------------------- json helpers

static const json* field(const json& j, const char* key)
{
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

float jf(const json& j, const char* key, float def)
{
    const json* v = field(j, key);
    if (!v) return def;
    if (v->is_number()) {
        float f = v->get<float>();
        return std::isfinite(f) ? f : def;
    }
    if (v->is_boolean()) return v->get<bool>() ? 1.0f : 0.0f;
    return def;
}

double jd(const json& j, const char* key, double def)
{
    const json* v = field(j, key);
    if (!v || !v->is_number()) return def;
    double d = v->get<double>();
    return std::isfinite(d) ? d : def;
}

int ji(const json& j, const char* key, int def)
{
    const json* v = field(j, key);
    if (!v) return def;
    if (v->is_number()) return static_cast<int>(std::lround(v->get<double>()));
    if (v->is_boolean()) return v->get<bool>() ? 1 : 0;
    return def;
}

bool jb(const json& j, const char* key, bool def)
{
    const json* v = field(j, key);
    if (!v) return def;
    if (v->is_boolean()) return v->get<bool>();
    if (v->is_number()) return v->get<double>() != 0;
    return def;
}

std::string js(const json& j, const char* key, const std::string& def)
{
    const json* v = field(j, key);
    if (!v || v->is_null()) return def;
    if (v->is_string()) return v->get<std::string>();
    return v->dump();
}

std::string jtext(const json& v, int maxLen)
{
    std::string s;
    if (v.is_string()) s = v.get<std::string>();
    else if (v.is_null()) s = "";
    else if (v.is_number_float()) {
        char b[32];
        double d = v.get<double>();
        if (std::fabs(d) >= 1000 || d == std::floor(d)) std::snprintf(b, sizeof b, "%.0f", d);
        else if (std::fabs(d) >= 10) std::snprintf(b, sizeof b, "%.1f", d);
        else std::snprintf(b, sizeof b, "%.3g", d);
        s = b;
    } else s = v.dump();
    if (static_cast<int>(s.size()) > maxLen) s = s.substr(0, maxLen - 1) + "…";
    return s;
}

// ------------------------------------------------------------------------------------------- series

void Series::init(int capacity, int channels)
{
    cap = capacity;
    ch = channels;
    t.assign(cap, 0.0);
    v.assign(static_cast<size_t>(cap) * ch, 0.0f);
    head = count = 0;
}

void Series::push(double tt, const float* vals)
{
    t[head] = tt;
    for (int c = 0; c < ch; c++) v[static_cast<size_t>(head) * ch + c] = vals[c];
    head = (head + 1) % cap;
    if (count < cap) count++;
}

int Series::lowerBound(double t0) const
{
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (tAt(mid) < t0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// ------------------------------------------------------------------------------------------- items

std::string TuneItem::shortName() const
{
    auto p = name.find('.');
    return p == std::string::npos ? name : name.substr(p + 1);
}

bool TuneItem::changed() const { return std::fabs(value - def) > std::max(1e-6f, step * 0.01f); }

std::string TuneItem::unitText() const
{
    if (unit == "deg") return "°";
    if (unit == "deg/s") return "°/s";
    return unit;
}

std::string TuneItem::valueText(float v) const
{
    char b[64];
    if ((kind == "enum" || kind == "bool") && !labels.empty()) {
        int k = static_cast<int>(std::lround(v - min));
        if (k >= 0 && k < static_cast<int>(labels.size())) {
            std::string u = unitText();
            bool numeric = !labels[k].empty() && (std::isdigit(static_cast<unsigned char>(labels[k][0])) ||
                                                  labels[k][0] == '-' || labels[k][0] == '+');
            return numeric && !u.empty() && labels[k].find(' ') == std::string::npos ? labels[k] + " " + u
                                                                                     : labels[k];
        }
    }
    if (kind == "action") return "";
    if (kind == "bool") return v >= 0.5f ? "on" : "off";
    if (kind == "int" || kind == "enum") std::snprintf(b, sizeof b, "%d", static_cast<int>(std::lround(v)));
    else {
        float st = step > 0 ? step : 0.01f;
        int dec = st >= 1 ? 0 : st >= 0.1f ? 1 : st >= 0.01f ? 2 : 3;
        std::snprintf(b, sizeof b, "%.*f", dec, static_cast<double>(v));
    }
    std::string u = unitText();
    if (u.empty()) return b;
    if (u == "°" || u == "%") return std::string(b) + u;
    return std::string(b) + " " + u;
}

// ------------------------------------------------------------------------------------------- the model

Model::Model()
{
    motion.init(32768, M_N);
    states.init(4096, S_N);
}

void Model::reset()
{
    motion.clear();
    states.clear();
    state = State{};
    status = Status{};
    haveRef = false;
    haveScreen = false;
    prompt = Prompt{};
    boardNow = boardNowLocal = 0;
}

void Model::addLog(double local, const std::string& text, int level)
{
    logs.push_back({local, text, level});
    while (logs.size() > 400) logs.pop_front();
}

double Model::plotNow(double localNow) const
{
    if (boardNowLocal <= 0) return boardNow;
    return boardNow + std::clamp(localNow - boardNowLocal, 0.0, 0.1);
}

TuneItem* Model::findTune(const std::string& name)
{
    for (auto& t : tune)
        if (t.name == name) return &t;
    return nullptr;
}

Gesture* Model::findGesture(const std::string& name)
{
    for (auto& g : gestures)
        if (g.name == name) return &g;
    return nullptr;
}

static void rateTick(Model::Rate& r, double now, int n)
{
    if (r.windowStart <= 0) r.windowStart = now;
    r.n += n;
    double dt = now - r.windowStart;
    if (dt >= 1.0) {
        r.perSec = static_cast<float>(r.n / dt);
        r.n = 0;
        r.windowStart = now;
    }
}

static Quat quatOf(const json& j, const char* key, bool* ok)
{
    const json* v = field(j, key);
    if (!v || !v->is_array() || v->size() != 4) { if (ok) *ok = false; return {}; }
    Quat q;
    float a[4];
    for (int i = 0; i < 4; i++) a[i] = (*v)[i].is_number() ? (*v)[i].get<float>() : 0.0f;
    q = qnorm({a[0], a[1], a[2], a[3]});
    if (ok) *ok = std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]) && std::isfinite(a[3]);
    return q;
}

static std::vector<std::string> strList(const json& j, const char* key)
{
    std::vector<std::string> out;
    const json* v = field(j, key);
    if (v && v->is_array())
        for (auto& e : *v) out.push_back(e.is_string() ? e.get<std::string>() : e.dump());
    return out;
}

static void readTune(Model& m, const json& items)
{
    if (!items.is_array()) return;
    std::vector<TuneItem> next;
    for (auto& it : items) {
        TuneItem t;
        t.i = ji(it, "i");
        t.name = js(it, "name");
        t.group = js(it, "group");
        if (t.group.empty()) t.group = t.name.substr(0, t.name.find('.'));
        t.kind = js(it, "kind", "float");
        t.unit = js(it, "unit");
        t.help = js(it, "help");
        t.min = jf(it, "min");
        t.max = jf(it, "max", 1);
        t.step = jf(it, "step");
        t.def = jf(it, "def");
        t.value = jf(it, "value", t.def);
        t.labels = strList(it, "labels");
        if (TuneItem* old = m.findTune(t.name)) { t.pending = old->pending; t.pendingUntil = old->pendingUntil; }
        next.push_back(std::move(t));
    }
    m.tune = std::move(next);
    m.tuneVersion++;
}

static void readSettings(Model& m, const json& items)
{
    if (!items.is_array()) return;
    m.settings.clear();
    for (auto& it : items) {
        SettingItem s;
        s.i = ji(it, "i");
        s.tag = js(it, "tag");
        s.name = js(it, "name");
        s.group = js(it, "group", "SETTINGS");
        s.kind = js(it, "kind", "step");
        s.text = js(it, "text");
        s.help = js(it, "help");
        s.confirm = js(it, "confirm");
        m.settings.push_back(std::move(s));
    }
    m.settingsVersion++;
}

void Model::setTuneItems(const json& items) { readTune(*this, items); }
void Model::setSettingItems(const json& items) { readSettings(*this, items); }

unsigned Model::apply(const json& msg, double localNow, double rxLocal)
{
    const std::string ev = js(msg, "ev");
    if (ev.empty()) return 0;
    unsigned ch = 0;

    auto noteBoardTime = [&](double t) {
        if (t < boardNow - 2.0) {          // the board's clock went back: a reboot or another source
            motion.clear();
            states.clear();
        }
        if (t >= boardNow) { boardNow = t; boardNowLocal = rxLocal; }
    };

    if (ev == "motion") {
        double t0 = jd(msg, "t0"), dt = jd(msg, "dt", motionDt);
        if (dt > 0) motionDt = dt;
        const json* acc = field(msg, "acc");
        const json* gyr = field(msg, "gyro");
        int n = acc && acc->is_array() ? static_cast<int>(acc->size()) : 0;
        for (int k = 0; k < n; k++) {
            float v[M_N] = {0, 0, 0, 0, 0, 0};
            const json& a = (*acc)[k];
            for (int c = 0; c < 3 && a.is_array() && c < static_cast<int>(a.size()); c++)
                v[c] = a[c].is_number() ? a[c].get<float>() : 0.0f;
            if (gyr && gyr->is_array() && k < static_cast<int>(gyr->size())) {
                const json& g = (*gyr)[k];
                for (int c = 0; c < 3 && g.is_array() && c < static_cast<int>(g.size()); c++)
                    v[3 + c] = g[c].is_number() ? g[c].get<float>() : 0.0f;
            }
            for (float& x : v) if (!std::isfinite(x)) x = 0;
            double t = t0 + k * motionDt;
            if (motion.count && t <= motion.lastT()) continue;
            motion.push(t, v);
            this->acc = {v[0], v[1], v[2]};
            this->gyro = {v[3], v[4], v[5]};
        }
        if (n) noteBoardTime(t0 + (n - 1) * motionDt);
        bool ok = false;
        Quat q = quatOf(msg, "ref_quat", &ok);
        if (ok) { refQuat = q; haveRef = true; }
        injected = jb(msg, "injected");
        rateTick(rMotion, localNow, n);
    } else if (ev == "state") {
        State& s = state;
        s.valid = true;
        s.t = jd(msg, "t");
        s.rxLocal = rxLocal;
        lastStateRx = rxLocal;
        s.state = js(msg, "state");
        s.mode = js(msg, "mode");
        s.zone = js(msg, "zone");
        bool ok = false;
        Quat q = quatOf(msg, "quat", &ok);
        if (ok) s.quat = q;
        s.pitch = jf(msg, "pitch"); s.roll = jf(msg, "roll"); s.yaw = jf(msg, "yaw");
        s.tilt_u = jf(msg, "tilt_u"); s.speed_cmd = jf(msg, "speed_cmd"); s.speed_now = jf(msg, "speed_now");
        s.pos = jf(msg, "pos"); s.loop_s = jf(msg, "loop_s"); s.delay = jf(msg, "delay"); s.stutter = jf(msg, "stutter");
        s.joy_x = jf(msg, "joy_x"); s.joy_y = jf(msg, "joy_y"); s.joy_rx = jf(msg, "joy_rx"); s.joy_ry = jf(msg, "joy_ry");
        s.enc = ji(msg, "enc"); s.in_clips = ji(msg, "in_clips"); s.i2c_fail = ji(msg, "i2c_fail");
        s.enc_down = jb(msg, "enc_down"); s.rec_down = jb(msg, "rec_down"); s.frozen = jb(msg, "frozen");
        s.menu = jb(msg, "menu"); s.injected = jb(msg, "injected"); s.joy_present = jb(msg, "joy_present");
        s.imu_present = jb(msg, "imu_present"); s.recording = jb(msg, "recording"); s.playing = jb(msg, "playing");
        s.rest = jb(msg, "rest");
        s.in_peak = jf(msg, "in_peak"); s.out_peak = jf(msg, "out_peak"); s.cpu = jf(msg, "cpu");
        s.heap_kb = jf(msg, "heap_kb");
        if (const json* b = field(msg, "bias"); b && b->is_array() && b->size() == 3)
            s.bias = {(*b)[0].is_number() ? (*b)[0].get<float>() : 0.0f, (*b)[1].is_number() ? (*b)[1].get<float>() : 0.0f,
                      (*b)[2].is_number() ? (*b)[2].get<float>() : 0.0f};
        float v[S_N] = {s.pitch, s.roll, s.yaw, s.speed_cmd, s.speed_now, s.tilt_u, s.in_peak, s.out_peak, s.delay, s.stutter};
        noteBoardTime(s.t);
        if (!states.count || s.t > states.lastT()) states.push(s.t, v);
        rateTick(rState, localNow, 1);
    } else if (ev == "screen") {
        std::string b64 = js(msg, "fb");
        static const std::string A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int val = 0, bits = -8;
        size_t o = 0;
        std::array<uint8_t, 1024> next{};
        for (char c : b64) {
            auto p = A.find(c);
            if (p == std::string::npos) continue;
            val = (val << 6) + static_cast<int>(p);
            bits += 6;
            if (bits >= 0) {
                if (o < next.size()) next[o++] = static_cast<uint8_t>((val >> bits) & 0xff);
                bits -= 8;
            }
        }
        if (o == 1024) {
            fb = next;
            fbVersion++;
            haveScreen = true;
            ch |= CH_SCREEN;
        }
        rateTick(rScreen, localNow, 1);
    } else if (ev == "event") {
        Event e;
        e.t = jd(msg, "t");
        e.local = localNow;
        e.name = js(msg, "name");
        e.text = js(msg, "text");
        e.v = jf(msg, "v");
        e.ms = ji(msg, "ms");
        if (Gesture* g = findGesture(e.name)) {
            g->count++;
            g->lastT = e.t;
            g->flashLocal = localNow;
        } else {
            Gesture ng;
            ng.name = e.name;
            ng.group = e.name.substr(0, e.name.find('.'));
            ng.count = 1;
            ng.lastT = e.t;
            ng.flashLocal = localNow;
            gestures.push_back(std::move(ng));
        }
        events.push_back(std::move(e));
        while (events.size() > 400) events.pop_front();
        ch |= CH_EVENT;
    } else if (ev == "tune") {
        readTune(*this, msg.is_object() && msg.contains("items") ? msg["items"] : json());
        ch |= CH_TUNE;
    } else if (ev == "settings") {
        readSettings(*this, msg.is_object() && msg.contains("items") ? msg["items"] : json());
        ch |= CH_SETTINGS;
    } else if (ev == "status") {
        Status& st = status;
        st.valid = true;
        st.source = js(msg, "source", "none");
        const json* b = field(msg, "board");
        if (b && b->is_object()) {
            st.boardName = js(*b, "name");
            st.serial = js(*b, "serial");
            st.version = js(*b, "version");
            st.rig = ji(*b, "rig");
        } else {
            st.boardName.clear(); st.serial.clear(); st.version.clear(); st.rig = 0;
        }
        const json* r = field(msg, "rates");
        st.motionHz = r ? jf(*r, "motion_hz") : 0;
        st.stateHz = r ? jf(*r, "state_hz") : 0;
        st.screenFps = r ? jf(*r, "screen_fps") : 0;
        const json* d = field(msg, "drops");
        st.dropMotion = d ? ji(*d, "motion") : 0;
        st.dropState = d ? ji(*d, "state") : 0;
        st.dropScreen = d ? ji(*d, "screen") : 0;
        st.recording = jb(msg, "recording");
        const json* osc = field(msg, "osc");
        st.oscOut = osc ? js(*osc, "out") : "";
        st.oscIn = osc ? js(*osc, "in") : "";
        ch |= CH_STATUS;
    } else if (ev == "log") {
        std::string lv = js(msg, "level", "info");
        std::string text = js(msg, "text");
        int level = lv == "error" || lv == "e" ? 2 : lv == "warn" || lv == "warning" || lv == "w" ? 1 : 0;
        if (level == 0 && text.size() > 2 && text[1] == ' ') level = text[0] == 'W' ? 1 : text[0] == 'E' ? 2 : 0;
        addLog(localNow, text, level);
    } else if (ev == "cmds") {
        CmdLine c;
        c.local = localNow;
        c.by = js(msg, "by", "?");
        c.cmd = js(msg, "cmd");
        const json* a = field(msg, "args");
        if (a && a->is_object() && !a->empty()) {
            std::string s;
            for (auto it = a->begin(); it != a->end(); ++it) {
                if (!s.empty()) s += " ";
                s += it.key() + "=" + jtext(it.value(), 24);
            }
            c.args = s;
        }
        c.ok = jb(msg, "ok", true);
        c.ms = ji(msg, "ms");
        cmds.push_back(std::move(c));
        while (cmds.size() > 300) cmds.pop_front();
    } else if (ev == "prompt") {
        Prompt& p = prompt;
        std::string test = js(msg, "test"), phase = js(msg, "phase");
        int step = ji(msg, "step", p.step);
        double secs = jd(msg, "secs");
        // the countdown's full length: what the first push of this step and phase said (or "total" if sent)
        bool same = p.active && test == p.test && phase == p.phase && step == p.step;
        p.total = jd(msg, "total", same ? std::max(p.total, secs) : secs);
        p.text = js(msg, "text");
        p.test = test;
        p.phase = phase;
        p.step = step;
        p.of = ji(msg, "of", p.of);
        p.secs = secs;
        p.rxLocal = localNow;
        p.active = !p.text.empty();
        ch |= CH_PROMPT;
    } else if (ev == "session") {
        std::string was = sessionState;
        sessionState = js(msg, "state", "idle");
        sessionName = js(msg, "name");
        sessionSecs = jd(msg, "secs");
        sessionRxLocal = localNow;
        if (was == "recording" && sessionState != "recording") ch |= CH_SESSION_DONE;
    } else if (ev == "_connected") {
        connected = true;
        connectedAt = localNow;
        ch |= CH_CONNECT;
    } else if (ev == "_disconnected") {
        if (connected) ch |= CH_DISCONNECT;
        connected = false;
        status.valid = false;
        status.source = "none";
        state.valid = false;
        prompt = Prompt{};
    }
    return ch;
}

} // namespace slrig

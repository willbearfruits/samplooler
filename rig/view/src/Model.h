// Everything the daemon told us, as the UI reads it. Filled on the UI thread from the network inbox.
#pragma once
#include "Math3d.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace slrig {

using json = nlohmann::json;

float jf(const json& j, const char* key, float def = 0.0f);
double jd(const json& j, const char* key, double def = 0.0);
int ji(const json& j, const char* key, int def = 0);
bool jb(const json& j, const char* key, bool def = false);
std::string js(const json& j, const char* key, const std::string& def = "");
// a JSON value as short text (for tables and the log strip)
std::string jtext(const json& v, int maxLen = 80);

// A time series ring: t plus `ch` channels per sample.
struct Series {
    int cap = 0, ch = 0, head = 0, count = 0;
    std::vector<double> t;
    std::vector<float> v;
    void init(int capacity, int channels);
    void clear() { head = count = 0; }
    void push(double tt, const float* vals);
    // i = 0 is the oldest
    int idx(int i) const { return (head - count + i + cap) % cap; }
    double tAt(int i) const { return t[idx(i)]; }
    float vAt(int i, int c) const { return v[idx(i) * ch + c]; }
    double lastT() const { return count ? tAt(count - 1) : 0.0; }
    // first index with t >= t0 (binary search)
    int lowerBound(double t0) const;
};

enum MotionCh { M_AX, M_AY, M_AZ, M_GX, M_GY, M_GZ, M_N };
enum StateCh { S_PITCH, S_ROLL, S_YAW, S_SPEED_CMD, S_SPEED_NOW, S_TILT_U, S_IN, S_OUT, S_DELAY, S_STUTTER, S_N };

struct TuneItem {
    int i = 0;
    std::string name, group, kind, unit, help;
    float min = 0, max = 1, step = 0, def = 0, value = 0;
    std::vector<std::string> labels;
    // the UI's own: a value set here and not yet confirmed by a push
    float pending = 0;
    double pendingUntil = 0;
    std::string shortName() const;          // without the group
    bool changed() const;
    std::string valueText(float v) const;    // the value with its unit (or the label)
    std::string unitText() const;            // "deg" shown as "°"
};

// the board's own settings: stepped (-/+) or an action ("act"), never set to a number
struct SettingItem {
    int i = 0;
    std::string tag, name, group, kind, text, help, confirm;
    bool act() const { return kind == "act" || kind == "action"; }
};

struct Gesture {
    std::string name, group, rule;
    std::vector<std::string> tune;
    int count = 0;
    double lastT = -1;        // board time
    double flashLocal = -10;  // local time of the last firing, for the flash
};

struct Event {
    double t = 0, local = 0;
    std::string name, text;
    float v = 0;
    int ms = 0;
};

struct LogLine {
    double local = 0;
    std::string text;
    int level = 0;            // 0 info, 1 warning, 2 error
};

struct CmdLine {
    double local = 0;
    std::string by, cmd, args;
    bool ok = true;
    int ms = 0;
};

struct Curve {
    bool valid = false;
    std::vector<float> angle, speed;
    float dead = 5, full = 45, hyst = 0, sign = 1, speedMin = 0, speedMax = 2;
    struct Det { float speed = 0, lo = 0, hi = 0; bool range = false; };
    std::vector<Det> detents;
    std::vector<float> rAngle, delay, stutter;
    float rDead = 15, rFull = 45;
};

struct Session {
    std::string name, date, test;
    double secs = 0;
};

struct TestDef {
    std::string name, title, help;
    int steps = 0;
    double secs = 0;
};

struct Prompt {
    bool active = false;
    std::string test, text, phase;
    int step = 0, of = 0;
    double secs = 0, total = 0, rxLocal = 0;
};

struct State {
    bool valid = false;
    double t = 0, rxLocal = 0;
    std::string state, mode, zone;
    Quat quat;
    float pitch = 0, roll = 0, yaw = 0, tilt_u = 0, speed_cmd = 0, speed_now = 0, pos = 0, loop_s = 0;
    float delay = 0, stutter = 0, joy_x = 0, joy_y = 0, joy_rx = 0, joy_ry = 0;
    int enc = 0, in_clips = 0, i2c_fail = 0;
    bool enc_down = false, rec_down = false, frozen = false, menu = false, injected = false, joy_present = false,
         imu_present = false, recording = false, playing = false, rest = false;
    float in_peak = 0, out_peak = 0, cpu = 0, heap_kb = 0;
    V3 bias;
};

struct Status {
    bool valid = false;
    std::string source = "none", boardName, serial, version, oscOut, oscIn;
    int rig = 0;
    float motionHz = 0, stateHz = 0, screenFps = 0;
    long dropMotion = 0, dropState = 0, dropScreen = 0;
    bool recording = false;
};

struct Model {
    Model();

    // connection
    bool connected = false;
    double connectedAt = 0;
    std::string daemonName;

    Status status;
    State state;
    Quat refQuat;
    bool haveRef = false;
    V3 acc{0, 0, 1};          // the latest accelerometer sample (device axes, g)
    V3 gyro;
    bool injected = false;
    Series motion, states;
    double motionDt = 0.002;

    std::array<uint8_t, 1024> fb{};
    uint32_t fbVersion = 0;
    bool haveScreen = false;

    std::vector<TuneItem> tune;
    std::vector<std::string> presets;
    uint32_t tuneVersion = 0;
    std::vector<SettingItem> settings;
    uint32_t settingsVersion = 0;
    std::vector<Gesture> gestures;
    std::deque<Event> events;
    Curve curve;
    std::vector<Session> sessions;
    std::vector<TestDef> tests;
    Prompt prompt;
    std::string sessionState = "idle", sessionName;
    double sessionSecs = 0, sessionRxLocal = 0;
    json analysis;            // the last session.analyze result
    std::string analysisFor;

    std::deque<LogLine> logs;
    std::deque<CmdLine> cmds;

    // what arrived, per second (measured here)
    struct Rate { double windowStart = 0; int n = 0; float perSec = 0; };
    Rate rMotion, rState, rScreen;

    // the newest board time seen, and the local time it arrived: the plots' clock
    double boardNow = 0, boardNowLocal = 0;
    double lastStateRx = 0;    // local time the newest state arrived (latency)

    // returns which kinds changed (bit flags below)
    unsigned apply(const json& msg, double localNow, double rxLocal);
    void addLog(double local, const std::string& text, int level);
    void setTuneItems(const json& items);       // a tune.list reply, or a tune push
    void setSettingItems(const json& items);
    void reset();
    double plotNow(double localNow) const;
    TuneItem* findTune(const std::string& name);
    Gesture* findGesture(const std::string& name);
};

enum Changed : unsigned {
    CH_TUNE = 1, CH_SETTINGS = 2, CH_SESSION_DONE = 4, CH_EVENT = 8, CH_SCREEN = 16, CH_STATUS = 32, CH_CONNECT = 64,
    CH_DISCONNECT = 128, CH_PROMPT = 256,
};

} // namespace slrig

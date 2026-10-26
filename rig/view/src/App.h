// slrig-view: the rig's window. One App draws the whole frame (the 3D view, the OLED, plots, curves, the tabs,
// the logs, the status bar) the same way in the live GLFW window and in --shot.
#pragma once
#include "Model.h"
#include "Net.h"
#include "Scene3D.h"
#include "Ui.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace slrig {

struct Options {
    std::string socket;
    std::string tab = "tune";
    std::string assets;
    bool shot = false;
};

enum Tab { TAB_TUNE, TAB_GESTURES, TAB_SETTINGS, TAB_SESSION, TAB_N };

class App {
public:
    explicit App(Options o);
    ~App();
    bool init(NVGcontext* vg, std::string& err);
    void shutdown();

    void update(double now);                                  // the network: never blocks
    // draws a frame into framebuffer `target` (fbW x fbH pixels, window w x h units)
    void frame(unsigned target, int winW, int winH, int fbW, int fbH, double now);
    void presented(double now);                               // the frame is on the screen (latency)

    // input (GLFW codes)
    void mouseMove(double x, double y);
    void mouseButton(int button, bool down, int mods, double now);
    void scroll(double dy);
    void key(int key, int mods);
    bool quitRequested() const { return quit_; }

    // stats
    float cpuMs() const { return cpuLast_; }
    Model& model() { return m_; }
    bool haveInitialData() const;

private:
    using Reply = std::function<void(bool ok, const json& result, const json& error)>;
    int run(const std::string& cmd, json args = json::object(), Reply cb = {});
    void op(json j);
    void onConnected();
    void handleReply(const json& msg);
    void requestCurve();

    // layout and panels
    struct Layout {
        Rect header, view3d, oled, curveTilt, curveRoll, plots, logs, status, right;
        int oledScale = 4;
    };
    Layout layout(float w, float h) const;
    void drawHeader(Rect r);
    void drawStatus(Rect r);
    void draw3dView(Rect r);
    void drawOled(Rect r, int scale);
    void drawCurveTilt(Rect r);
    void drawCurveRoll(Rect r);
    void drawPlots(Rect r);
    void drawLogs(Rect r);
    void drawRight(Rect r);
    void drawTune(Rect r);
    void drawGestures(Rect r);
    void drawSettings(Rect r);
    void drawSession(Rect r);
    void drawPrompt(Rect r);
    void drawConfirm(Rect r);
    void drawNoDaemon(Rect r);

    // plot helpers
    struct Trace { int ch; unsigned colour; const char* name; };
    void plotSeries(Rect r, const char* title, const char* unit, const Series& s, const Trace* tr, int ntr,
                    float lo, float hi, bool symmetric, double t0, double t1, const char* fmt);
    float yRange(const Series& s, const Trace* tr, int ntr, double t0, double t1, float minSpan, bool symmetric,
                 float& lo, float& hi);

    std::map<std::string, std::pair<float, float>> plotRange_;   // eased y ranges, by title
    std::function<void(Rect, float, float)> plotUnderlayFn_;     // drawn under a plot's traces (once)
    Rect plotUnderlay_;
    float plotLo_ = 0, plotHi_ = 1;

    void setTune(TuneItem& it, float v, bool final);
    void stepTune(TuneItem& it, int dir, bool big);
    float shownValue(const TuneItem& it) const;
    void updateOledImage(int scale);

    Options opt_;
    NetClient net_;
    Model m_;
    Scene3D scene_;
    Ui ui_;
    NVGcontext* vg_ = nullptr;
    bool sceneOk_ = false;
    std::string sceneErr_;
    int sceneImage_ = -1;
    unsigned sceneImageGen_ = 0;

    // requests
    int nextId_ = 10;
    std::map<int, Reply> pending_;
    bool curveDirty_ = false, curveInFlight_ = false;
    double curveAskedAt_ = 0;
    std::map<std::string, std::pair<float, double>> tuneSend_;   // name -> (value waiting, last sent)
    bool sawTuneList_ = false, sawCurve_ = false, sawGestures_ = false, sawSettings_ = false;

    // UI state
    Tab tab_ = TAB_TUNE;
    UiInput in_;
    double lastClick_ = 0;
    float lastClickX_ = 0, lastClickY_ = 0;
    bool orbiting_ = false;
    float orbitX_ = 0, orbitY_ = 0;
    Rect view3d_;
    std::string focusTune_;
    bool scrollToFocus_ = false;
    double focusFlash_ = -10;
    Ui::Scroll tuneScroll_, gestScroll_, setScroll_, sessScroll_;
    std::string selSession_;
    int plotWindowIdx_ = 1;                                 // 2 5 10 30 s
    bool plotsFrozen_ = false;
    double frozenAt_ = 0;
    int panel_ = 0;                                         // OLED colours: 0 two-colour, 1 blue, 2 white
    bool pixelGrid_ = true;
    bool showAxes_ = true, showGravity_ = true;
    struct Confirm { bool open = false; int index = -1; std::string name, text; } confirm_;
    int abSlot_ = 0;
    bool abStored_[2] = {false, false};
    std::string toast_;
    double toastAt_ = -10;
    bool quit_ = false;
    double now_ = 0;

    // the OLED mirror image
    int oledImage_ = -1;
    int oledImageScale_ = 0;
    uint32_t oledImageVersion_ = 0xffffffffu;
    int oledImagePanel_ = -1;
    bool oledImageGrid_ = false;
    std::vector<uint8_t> oledRgba_;

    // stats
    float cpuLast_ = 0;
    float cpuHist_[120] = {};
    int cpuHistN_ = 0, cpuHistI_ = 0;
    double drawnStateRx_ = 0;
    float latLast_ = 0, latMax_ = 0, latAvg_ = 0;
    double latWindow_ = 0;
    float latWinMax_ = 0;
};

} // namespace slrig

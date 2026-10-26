// slrig-view: the Samplooler Rig's viewer. The live GLFW window, or --shot: one frame offscreen through EGL
// (surfaceless, no display server) into a PNG. Both draw with the same App.
#include "App.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <nanovg.h>
#define NANOVG_GL3 1
#include <nanovg_gl.h>
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace slrig;

namespace {

struct Args {
    std::string socket, shot, tab = "tune";
    int w = 1600, h = 1000, waitMs = 1500, bench = 0;
    float scale = 1.0f;
};

void usage()
{
    std::printf(
        "slrig-view [options]   the Samplooler Rig's viewer (a client of slrigd)\n"
        "  --socket PATH      the daemon's socket (default $XDG_RUNTIME_DIR/slrig/daemon.sock)\n"
        "  --shot OUT.png     render one frame offscreen (EGL, no window) into OUT.png and exit\n"
        "  --size WxH         window or shot size in units (default 1600x1000)\n"
        "  --scale F          shot only: device pixels per unit, to check HiDPI (default 1)\n"
        "  --wait-ms N        shot only: how long to collect data first (default 1500)\n"
        "  --tab NAME         tune, gestures, settings or session\n"
        "  --bench N          shot only: render N frames first and print the frame times\n"
        "keys: 1-4 tabs, up/down/left/right tune the focused value, R reset the view, A axes, G pixel grid,\n"
        "      P panel colours, F freeze the plots, [ ] plot window, ctrl+Q quit\n");
}

bool parse(int argc, char** argv, Args& a)
{
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        auto val = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "slrig-view: %s needs a value\n", name); std::exit(2); }
            return argv[++i];
        };
        if (s == "--help" || s == "-h") { usage(); std::exit(0); }
        else if (s == "--socket") a.socket = val("--socket");
        else if (s == "--shot") a.shot = val("--shot");
        else if (s == "--tab") a.tab = val("--tab");
        else if (s == "--wait-ms") a.waitMs = std::max(0, std::atoi(val("--wait-ms")));
        else if (s == "--bench") a.bench = std::max(0, std::atoi(val("--bench")));
        else if (s == "--scale") a.scale = std::clamp(static_cast<float>(std::atof(val("--scale"))), 0.5f, 4.0f);
        else if (s == "--size") {
            if (std::sscanf(val("--size"), "%dx%d", &a.w, &a.h) != 2 || a.w < 320 || a.h < 240) {
                std::fprintf(stderr, "slrig-view: --size wants WxH, at least 320x240\n");
                return false;
            }
        } else {
            std::fprintf(stderr, "slrig-view: unknown option %s (--help lists them)\n", s.c_str());
            return false;
        }
    }
    if (a.tab != "tune" && a.tab != "gestures" && a.tab != "settings" && a.tab != "session") {
        std::fprintf(stderr, "slrig-view: --tab is tune, gestures, settings or session\n");
        return false;
    }
    return true;
}

std::string exeDir()
{
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    std::string p = buf;
    auto s = p.rfind('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}

bool exists(const std::string& p) { return access(p.c_str(), R_OK) == 0; }

std::string findAssets()
{
    std::string d = exeDir();
    for (const std::string& c : {d + "/assets", d + "/../assets", d + "/../share/slrig-view/assets",
                                 std::string(SLRIG_VIEW_ASSETS)})
        if (exists(c + "/stack.mesh") || exists(c + "/fonts/AtkinsonHyperlegible-Regular.ttf")) return c;
    return SLRIG_VIEW_ASSETS;
}

double now() { return NetClient::now(); }

// ------------------------------------------------------------------------------------------- --shot

int runShot(const Args& a, Options opt)
{
    auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay dpy = getDisplay ? getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr) : EGL_NO_DISPLAY;
    EGLint major = 0, minor = 0;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
        std::fprintf(stderr, "slrig-view: no EGL surfaceless display\n");
        return 1;
    }
    EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg = nullptr;
    EGLint ncfg = 0;
    eglChooseConfig(dpy, cfgAttr, &cfg, 1, &ncfg);
    eglBindAPI(EGL_OPENGL_API);
    EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
                        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, ncfg ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        std::fprintf(stderr, "slrig-view: no GL 3.3 core context through EGL\n");
        return 1;
    }
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(eglGetProcAddress))) {
        std::fprintf(stderr, "slrig-view: GL entry points missing\n");
        return 1;
    }
    std::fprintf(stderr, "slrig-view: %s, %s (EGL %d.%d, surfaceless)\n", glGetString(GL_VERSION), glGetString(GL_RENDERER),
                 major, minor);

    int fbW = static_cast<int>(std::lround(a.w * a.scale)), fbH = static_cast<int>(std::lround(a.h * a.scale));
    GLuint fbo = 0, color = 0, ds = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &color);
    glGenRenderbuffers(1, &ds);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, fbW, fbH);
    glBindRenderbuffer(GL_RENDERBUFFER, ds);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbW, fbH);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ds);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "slrig-view: the offscreen framebuffer is incomplete\n");
        return 1;
    }

    NVGcontext* vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!vg) { std::fprintf(stderr, "slrig-view: NanoVG failed\n"); return 1; }
    int code = 0;
    {
        opt.shot = true;
        App app(opt);
        std::string err;
        if (!app.init(vg, err)) {
            std::fprintf(stderr, "slrig-view: %s\n", err.c_str());
            nvgDeleteGL3(vg);
            return 1;
        }
        // collect data for wait-ms (frames drawn now and then warm the caches; plots need the history)
        double t0 = now(), lastDraw = 0;
        while (now() - t0 < a.waitMs / 1000.0) {
            app.update(now());
            if (now() - lastDraw > 0.25) {
                app.frame(fbo, a.w, a.h, fbW, fbH, now());
                app.presented(now());
                lastDraw = now();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if (!app.model().connected)
            std::fprintf(stderr, "slrig-view: no daemon at %s; the shot shows that\n", opt.socket.c_str());

        if (a.bench > 0) {
            GLuint q = 0;
            glGenQueries(1, &q);
            std::vector<double> cpu, gpu, wall;
            for (int i = 0; i < a.bench; i++) {
                app.update(now());
                auto w0 = std::chrono::steady_clock::now();
                glBeginQuery(GL_TIME_ELAPSED, q);
                app.frame(fbo, a.w, a.h, fbW, fbH, now());
                glEndQuery(GL_TIME_ELAPSED);
                glFinish();
                double wms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count();
                GLuint64 ns = 0;
                glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
                app.presented(now());
                cpu.push_back(app.cpuMs());
                gpu.push_back(ns / 1e6);
                wall.push_back(wms);
            }
            glDeleteQueries(1, &q);
            auto stat = [](std::vector<double> v, const char* name) {
                double sum = 0, mx = 0;
                for (double x : v) { sum += x; mx = std::max(mx, x); }
                std::sort(v.begin(), v.end());
                std::printf("  %-28s mean %6.3f ms   median %6.3f   p95 %6.3f   max %6.3f\n", name, sum / v.size(),
                            v[v.size() / 2], v[std::min(v.size() - 1, v.size() * 95 / 100)], mx);
            };
            std::printf("slrig-view bench: %d frames at %dx%d (%dx%d px)\n", a.bench, a.w, a.h, fbW, fbH);
            stat(cpu, "CPU (frame(), no finish)");
            stat(gpu, "GPU (GL_TIME_ELAPSED)");
            stat(wall, "CPU + GPU (with glFinish)");
        }

        app.update(now());
        app.frame(fbo, a.w, a.h, fbW, fbH, now());
        glFinish();
        app.presented(now());
        app.frame(fbo, a.w, a.h, fbW, fbH, now());       // once more: the status bar shows the first one's numbers
        glFinish();

        std::vector<unsigned char> px(static_cast<size_t>(fbW) * fbH * 4), flip(px.size());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < fbH; y++)
            std::memcpy(&flip[static_cast<size_t>(y) * fbW * 4], &px[static_cast<size_t>(fbH - 1 - y) * fbW * 4],
                        static_cast<size_t>(fbW) * 4);
        for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;
        if (!stbi_write_png(a.shot.c_str(), fbW, fbH, 4, flip.data(), fbW * 4)) {
            std::fprintf(stderr, "slrig-view: cannot write %s\n", a.shot.c_str());
            code = 1;
        } else {
            std::fprintf(stderr, "slrig-view: wrote %s (%dx%d)\n", a.shot.c_str(), fbW, fbH);
        }
        app.shutdown();
    }
    nvgDeleteGL3(vg);
    glDeleteFramebuffers(1, &fbo);
    GLuint rbs[] = {color, ds};
    glDeleteRenderbuffers(2, rbs);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return code;
}

// ------------------------------------------------------------------------------------------- the window

App* appOf(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

int runLive(const Args& a, const Options& opt)
{
    if (!glfwInit()) {
        std::fprintf(stderr, "slrig-view: GLFW init failed (no display? --shot works without one)\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_SAMPLES, 0);
    GLFWwindow* win = glfwCreateWindow(a.w, a.h, "Samplooler Rig", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "slrig-view: no window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "slrig-view: GL 3.3 core entry points missing\n");
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(1);
    NVGcontext* vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    int code = 0;
    {
        App app(opt);
        std::string err;
        if (!vg || !app.init(vg, err)) {
            std::fprintf(stderr, "slrig-view: %s\n", err.c_str());
            code = 1;
        } else {
            glfwSetWindowUserPointer(win, &app);
            glfwSetCursorPosCallback(win, [](GLFWwindow* w, double x, double y) { appOf(w)->mouseMove(x, y); });
            glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int b, int action, int mods) {
                appOf(w)->mouseButton(b, action == GLFW_PRESS, mods, now());
            });
            glfwSetScrollCallback(win, [](GLFWwindow* w, double, double dy) { appOf(w)->scroll(dy); });
            glfwSetKeyCallback(win, [](GLFWwindow* w, int key, int, int action, int mods) {
                if (action == GLFW_PRESS || action == GLFW_REPEAT) appOf(w)->key(key, mods);
            });
            while (!glfwWindowShouldClose(win) && !app.quitRequested()) {
                glfwPollEvents();
                double t = now();
                app.update(t);
                int w = 0, h = 0, fw = 0, fh = 0;
                glfwGetWindowSize(win, &w, &h);
                glfwGetFramebufferSize(win, &fw, &fh);
                if (w <= 0 || h <= 0 || fw <= 0 || fh <= 0) {
                    glfwWaitEventsTimeout(0.05);
                    continue;
                }
                app.frame(0, w, h, fw, fh, t);
                glfwSwapBuffers(win);
                app.presented(now());
            }
            app.shutdown();
        }
    }
    if (vg) nvgDeleteGL3(vg);
    glfwDestroyWindow(win);
    glfwTerminate();
    return code;
}

} // namespace

int main(int argc, char** argv)
{
    Args a;
    if (!parse(argc, argv, a)) return 2;
    Options opt;
    opt.socket = a.socket.empty() ? NetClient::defaultPath() : a.socket;
    opt.tab = a.tab;
    opt.assets = findAssets();
    if (!a.shot.empty()) return runShot(a, opt);
    return runLive(a, opt);
}

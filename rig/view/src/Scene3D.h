// The Stack in 3D: the rev C mesh (assets/stack.mesh) turned by the board's quaternion, the OLED as a live
// texture on its active area, axis and gravity arrows, a soft ground shadow and grid, and a faint ghost for the
// host's reference fusion. Plain GL 3.3 core; draws into a multisampled buffer and blits into the target.
#pragma once
#include "Math3d.h"

#include <cstdint>
#include <string>
#include <vector>

namespace slrig {

struct SceneInput {
    Quat q;                         // the board's fusion: device axes into the world (z up)
    bool haveRef = false;
    Quat ref;                       // the host's reference fusion
    V3 acc{0, 0, 1};                // latest accelerometer sample, device axes, g
    bool haveAcc = false;
    const uint8_t* fb = nullptr;    // 1024 bytes, SSD1306 page format
    uint32_t fbVersion = 0;
    int panel = 0;                  // 0 two-colour (yellow band + blue), 1 blue, 2 white
    bool axes = true, gravity = true;
    bool live = true;               // false: no data, the board is drawn dimmed
};

class Scene3D {
public:
    bool init(const std::string& meshPath, std::string& err);
    void destroy();
    // Draws a w x h (framebuffer pixels) view into texture(): the 2D layer shows it. Leaves framebuffer 0 bound.
    void render(const SceneInput& in, int w, int h);
    unsigned texture() const { return resolveTex_; }
    int texW() const { return msW_; }
    int texH() const { return msH_; }
    unsigned texGeneration() const { return texGeneration_; }   // changes when the texture is made again

    // camera (orbit about the stack's centre)
    void orbit(float dxDeg, float dyDeg);
    void zoom(float steps);
    void resetCamera();
    void setCamera(float yawDeg, float elevDeg, float zoom);     // zoom 1 = the stack fits
    bool cameraMoved() const;

    // after render(): a world point to viewport pixels (0,0 = top-left of the viewport, in framebuffer pixels)
    bool project(V3 world, float& sx, float& sy) const;
    // where the labels go: arrow tips in the world (valid after render)
    V3 tipX, tipY, tipZ, tipG;
    bool tipsValid = false;
    float ghostAlpha = 0;           // how much of the ghost showed (0..1)
    int triangles() const { return triCount_; }

private:
    bool buildShaders(std::string& err);
    void ensureTargets(int w, int h);
    void drawArrow(V3 from, V3 dir, float length, const float rgb[3], float alpha, bool xray);
    M4 boardModel(Quat q) const;

    // mesh
    unsigned vao_ = 0, vbo_ = 0, ibo_ = 0;
    int opaqueCount_ = 0, transCount_ = 0, triCount_ = 0;   // index counts
    float scale_ = 1.0f / 256.0f;
    V3 screen_[4];
    // arrow
    unsigned arrowVao_ = 0, arrowVbo_ = 0;
    int arrowVerts_ = 0;
    // quads
    unsigned quadVao_ = 0, quadVbo_ = 0;
    // the ghost: the boards' outlines and the screen's, as lines
    unsigned lineVao_ = 0, lineVbo_ = 0;
    int lineVerts_ = 0;
    // shaders
    unsigned progMesh_ = 0, progGround_ = 0, progOled_ = 0, progShadow_ = 0, progLine_ = 0;
    // targets
    unsigned msFbo_ = 0, msColor_ = 0, msDepth_ = 0, resolveFbo_ = 0, resolveTex_ = 0, texGeneration_ = 0;
    int msW_ = 0, msH_ = 0, samples_ = 4;
    unsigned shFbo_ = 0, shTex_ = 0;
    int shSize_ = 256;
    unsigned oledTex_ = 0;
    uint32_t oledVersion_ = 0xffffffffu;
    // camera
    float yaw_ = 0, elev_ = 38, zoom_ = 1;
    M4 view_, proj_, vp_;
    V3 eye_;
    int vw_ = 1, vh_ = 1;
    bool ready_ = false;
};

} // namespace slrig

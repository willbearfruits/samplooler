#include "Scene3D.h"

#include "Theme.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace slrig {

namespace {

const char* MESH_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
uniform mat4 uModel;
uniform mat4 uVP;
uniform float uScale;
out vec3 vW;
out vec4 vC;
void main() {
    vec4 w = uModel * vec4(aPos * uScale, 1.0);
    vW = w.xyz;
    vC = aCol;
    gl_Position = uVP * w;
}
)";

// Flat shading from the screen-space derivatives: no normals in the file, both sides lit.
const char* MESH_FS = R"(#version 330 core
in vec3 vW;
in vec4 vC;
uniform vec3 uEye;
uniform vec4 uTint;
uniform float uUseTint;
uniform float uAlpha;
uniform float uDim;
uniform float uFlat;
out vec4 o;
void main() {
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    vec3 v = normalize(uEye - vW);
    if (dot(n, v) < 0.0) n = -n;
    vec3 base = mix(vC.rgb, uTint.rgb, uUseTint);
    vec3 L1 = normalize(vec3(-0.45, -0.60, 0.85));
    vec3 L2 = normalize(vec3(0.70, 0.45, 0.30));
    float d1 = max(dot(n, L1), 0.0);
    float d2 = max(dot(n, L2), 0.0);
    float hemi = 0.5 + 0.5 * n.z;
    vec3 amb = mix(vec3(0.07, 0.07, 0.08), vec3(0.30, 0.30, 0.32), hemi);
    vec3 h = normalize(L1 + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.30;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.10;
    vec3 lit = base * (amb + d1 * vec3(1.0, 0.97, 0.92) * 0.95 + d2 * vec3(0.70, 0.78, 0.90) * 0.35) + spec + rim;
    vec3 c = mix(lit, base, uFlat) * uDim;
    o = vec4(c, mix(vC.a, uTint.a, uUseTint) * uAlpha);
}
)";

const char* SHADOW_FS = R"(#version 330 core
in vec3 vW;
in vec4 vC;
out vec4 o;
void main() { o = vec4(1.0); }
)";

const char* GROUND_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
out vec3 vW;
void main() { vW = aPos; gl_Position = uVP * vec4(aPos, 1.0); }
)";

const char* GROUND_FS = R"(#version 330 core
in vec3 vW;
uniform vec3 uBg;
uniform vec3 uPool;
uniform vec3 uGrid;
uniform sampler2D uShadow;
uniform float uShadowExt;
uniform float uShadowK;
out vec4 o;
float grid(vec2 p, float s) {
    vec2 q = p / s;
    vec2 g = abs(fract(q - 0.5) - 0.5) / max(fwidth(q), vec2(1e-4));
    return 1.0 - min(min(g.x, g.y), 1.0);
}
void main() {
    vec2 p = vW.xy;
    float r = length(p);
    vec3 c = mix(uBg, uPool, 1.0 - smoothstep(20.0, 240.0, r));
    float fade = 1.0 - smoothstep(70.0, 260.0, r);
    c = mix(c, uGrid, grid(p, 10.0) * 0.30 * fade);
    c = mix(c, uGrid, grid(p, 50.0) * 0.65 * fade);
    vec2 uv = p / (2.0 * uShadowExt) + 0.5;
    const vec2 k[12] = vec2[](vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
                              vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
                              vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));
    float s = 0.0;
    float rad = 6.0 / (2.0 * uShadowExt);
    for (int i = 0; i < 12; i++) s += texture(uShadow, uv + k[i] * rad).r;
    s = s / 12.0;
    float core = texture(uShadow, uv).r;
    float sh = clamp(0.65 * s + 0.25 * core, 0.0, 1.0) * uShadowK;
    c *= 1.0 - sh;
    o = vec4(c, 1.0);
}
)";

const char* OLED_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uVP;
out vec2 vUv;
void main() { vUv = aUv; gl_Position = uVP * uModel * vec4(aPos, 1.0); }
)";

// The panel glows on its own: no lighting. Pixel (0, 0) is the uv (0, 0) corner.
const char* OLED_FS = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uFb;
uniform vec3 uTop;
uniform vec3 uBottom;
uniform vec3 uOff;
uniform float uTwo;
uniform float uDim;
out vec4 o;
void main() {
    vec2 px = vUv * vec2(128.0, 64.0);
    float lit = texture(uFb, vUv).r;
    vec3 col = (uTwo > 0.5 && px.y < 16.0) ? uTop : uBottom;
    vec2 f = fract(px);
    vec2 w = fwidth(px);
    float cell = 1.0 / max(w.x, w.y);
    float gap = cell > 3.0 ? 0.14 : 0.0;
    vec2 e = smoothstep(vec2(gap * 0.5), vec2(gap * 0.5) + w, f) * smoothstep(vec2(gap * 0.5), vec2(gap * 0.5) + w, 1.0 - f);
    float in_px = gap > 0.0 ? e.x * e.y : 1.0;
    vec3 off = uOff + col * 0.035;
    vec3 c = mix(off, col, lit * in_px);
    o = vec4(c * uDim, 1.0);
}
)";

const char* LINE_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uVP;
void main() { gl_Position = uVP * uModel * vec4(aPos, 1.0); }
)";

const char* LINE_FS = R"(#version 330 core
uniform vec4 uColor;
out vec4 o;
void main() { o = uColor; }
)";

unsigned compile(unsigned type, const char* src, std::string& err)
{
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        err += log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

unsigned link(const char* vs, const char* fs, std::string& err)
{
    unsigned v = compile(GL_VERTEX_SHADER, vs, err), f = compile(GL_FRAGMENT_SHADER, fs, err);
    if (!v || !f) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        err += log;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

int U(unsigned prog, const char* name) { return glGetUniformLocation(prog, name); }

void rgb(unsigned hex, float out[3])
{
    out[0] = ((hex >> 16) & 255) / 255.0f;
    out[1] = ((hex >> 8) & 255) / 255.0f;
    out[2] = (hex & 255) / 255.0f;
}

// Device axes (PROTOCOL.md "Frames"): x to the screen's right edge, y to its top edge (the nose), z out of the
// screen, right-handed, as the mesh is stored. The quaternion takes them into the world (z up) and the world is
// drawn as it is: the player stands on the world's -y side.

constexpr float GROUND_Z = -58.0f;
constexpr float SHADOW_EXT = 95.0f;

} // namespace

bool Scene3D::buildShaders(std::string& err)
{
    progMesh_ = link(MESH_VS, MESH_FS, err);
    progShadow_ = link(MESH_VS, SHADOW_FS, err);
    progGround_ = link(GROUND_VS, GROUND_FS, err);
    progOled_ = link(OLED_VS, OLED_FS, err);
    progLine_ = link(LINE_VS, LINE_FS, err);
    return progMesh_ && progShadow_ && progGround_ && progOled_ && progLine_;
}

bool Scene3D::init(const std::string& meshPath, std::string& err)
{
    std::ifstream f(meshPath, std::ios::binary);
    if (!f) { err = "cannot open " + meshPath; return false; }
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    auto rd = [&](void* dst, size_t n) {
        if (pos + n > data.size()) return false;
        std::memcpy(dst, data.data() + pos, n);
        pos += n;
        return true;
    };
    char magic[8];
    uint32_t version = 0, groups = 0;
    float bounds[6], corners[12], normal[3];
    if (!rd(magic, 8) || std::memcmp(magic, "SLMESH1\0", 8) != 0) { err = meshPath + ": not a stack.mesh"; return false; }
    if (!rd(&version, 4) || !rd(&groups, 4) || !rd(&scale_, 4) || !rd(bounds, 24) || !rd(corners, 48) || !rd(normal, 12)) {
        err = meshPath + ": short header";
        return false;
    }
    if (version != 1) { err = meshPath + ": version " + std::to_string(version); return false; }
    for (int i = 0; i < 4; i++) screen_[i] = {corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2]};

    struct Vtx { int16_t x, y, z, pad; uint8_t r, g, b, a; };
    struct Group { std::vector<Vtx> v; std::vector<uint32_t> idx; bool trans; };
    std::vector<Group> gs;
    for (uint32_t g = 0; g < groups; g++) {
        float col[4];
        uint32_t flags, nv, ni;
        if (!rd(col, 16) || !rd(&flags, 4) || !rd(&nv, 4) || !rd(&ni, 4)) { err = meshPath + ": short group"; return false; }
        Group gr;
        gr.trans = col[3] < 0.999f;
        gr.v.resize(nv);
        std::vector<int16_t> p(static_cast<size_t>(nv) * 3);
        gr.idx.resize(ni);
        if (!rd(p.data(), p.size() * 2) || !rd(gr.idx.data(), static_cast<size_t>(ni) * 4)) {
            err = meshPath + ": short data";
            return false;
        }
        auto b = [](float c) { return static_cast<uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
        for (uint32_t i = 0; i < nv; i++)
            gr.v[i] = {p[i * 3], p[i * 3 + 1], p[i * 3 + 2], 0, b(col[0]), b(col[1]), b(col[2]), b(col[3])};
        gs.push_back(std::move(gr));
    }
    std::stable_partition(gs.begin(), gs.end(), [](const Group& g) { return !g.trans; });
    std::vector<Vtx> verts;
    std::vector<uint32_t> idx;
    for (auto& g : gs) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.insert(verts.end(), g.v.begin(), g.v.end());
        for (uint32_t i : g.idx) idx.push_back(base + i);
        (g.trans ? transCount_ : opaqueCount_) += static_cast<int>(g.idx.size());
    }
    triCount_ = static_cast<int>(idx.size() / 3);

    if (!buildShaders(err)) return false;

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vtx)), verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(Vtx), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), reinterpret_cast<void*>(8));
    glBindVertexArray(0);

    // the arrow: +z, length 1, from 0
    {
        std::vector<float> a;
        const int N = 24;
        const float rs = 0.014f, rc = 0.042f, zc = 0.86f;
        auto P = [&](float x, float y, float z) { a.push_back(x); a.push_back(y); a.push_back(z); };
        for (int i = 0; i < N; i++) {
            float a0 = 6.2831853f * i / N, a1 = 6.2831853f * (i + 1) / N;
            float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            P(rs * c0, rs * s0, 0); P(rs * c1, rs * s1, 0); P(rs * c1, rs * s1, zc);
            P(rs * c0, rs * s0, 0); P(rs * c1, rs * s1, zc); P(rs * c0, rs * s0, zc);
            P(rc * c0, rc * s0, zc); P(rc * c1, rc * s1, zc); P(0, 0, 1);
            P(rc * c0, rc * s0, zc); P(0, 0, zc); P(rc * c1, rc * s1, zc);
            P(rs * c0, rs * s0, 0); P(0, 0, 0); P(rs * c1, rs * s1, 0);
        }
        arrowVerts_ = static_cast<int>(a.size() / 3);
        glGenVertexArrays(1, &arrowVao_);
        glBindVertexArray(arrowVao_);
        glGenBuffers(1, &arrowVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, arrowVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(a.size() * 4), a.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
        glDisableVertexAttribArray(1);
        glVertexAttrib4f(1, 1, 1, 1, 1);
        glBindVertexArray(0);
    }
    // quads: the ground (4 verts) and the OLED (4 verts with uv)
    {
        const float G = 600.0f;
        V3 n{0, 0, 0.035f};
        V3 c[4] = {screen_[0] + n, screen_[1] + n, screen_[2] + n, screen_[3] + n};
        float q[] = {
            -G, -G, GROUND_Z, 0, 0,  G, -G, GROUND_Z, 0, 0,  G, G, GROUND_Z, 0, 0,  -G, G, GROUND_Z, 0, 0,
            c[0].x, c[0].y, c[0].z, 0, 0,  c[1].x, c[1].y, c[1].z, 1, 0,
            c[2].x, c[2].y, c[2].z, 1, 1,  c[3].x, c[3].y, c[3].z, 0, 1,
        };
        glGenVertexArrays(1, &quadVao_);
        glBindVertexArray(quadVao_);
        glGenBuffers(1, &quadVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, reinterpret_cast<void*>(12));
        glBindVertexArray(0);
    }
    // the ghost's lines: both boards as slabs (94 x 46 x 1.6 mm, mid planes 12.6 mm apart, as tools/build_mesh.py
    // puts them) and the screen
    {
        std::vector<float> l;
        auto L = [&](V3 a, V3 b) { l.insert(l.end(), {a.x, a.y, a.z, b.x, b.y, b.z}); };
        const float hx = 47.0f, hy = 23.0f, ht = 0.8f;
        for (float zc : {-6.3f, 6.3f}) {
            V3 c[8];
            for (int i = 0; i < 8; i++)
                c[i] = {(i & 1) ? hx : -hx, (i & 2) ? hy : -hy, zc + ((i & 4) ? ht : -ht)};
            for (int i = 0; i < 8; i++)
                for (int bit : {1, 2, 4})
                    if (!(i & bit)) L(c[i], c[i | bit]);
        }
        V3 n{0, 0, 0.05f};
        for (int i = 0; i < 4; i++) L(screen_[i] + n, screen_[(i + 1) % 4] + n);
        lineVerts_ = static_cast<int>(l.size() / 3);
        glGenVertexArrays(1, &lineVao_);
        glBindVertexArray(lineVao_);
        glGenBuffers(1, &lineVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(l.size() * 4), l.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
        glBindVertexArray(0);
    }
    // the OLED texture and the shadow target
    glGenTextures(1, &oledTex_);
    glBindTexture(GL_TEXTURE_2D, oledTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 64, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &shTex_);
    glBindTexture(GL_TEXTURE_2D, shTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, shSize_, shSize_, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &shFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    ready_ = true;
    return true;
}

void Scene3D::destroy()
{
    unsigned vaos[] = {vao_, arrowVao_, quadVao_, lineVao_};
    glDeleteVertexArrays(4, vaos);
    unsigned bufs[] = {vbo_, ibo_, arrowVbo_, quadVbo_, lineVbo_};
    glDeleteBuffers(5, bufs);
    for (unsigned p : {progMesh_, progGround_, progOled_, progShadow_, progLine_})
        if (p) glDeleteProgram(p);
    unsigned fbos[] = {msFbo_, shFbo_, resolveFbo_};
    glDeleteFramebuffers(3, fbos);
    unsigned rbs[] = {msColor_, msDepth_};
    glDeleteRenderbuffers(2, rbs);
    unsigned tex[] = {shTex_, oledTex_, resolveTex_};
    glDeleteTextures(3, tex);
    ready_ = false;
}

void Scene3D::ensureTargets(int w, int h)
{
    if (w == msW_ && h == msH_ && msFbo_) return;
    if (!msFbo_) {
        glGenFramebuffers(1, &msFbo_);
        glGenRenderbuffers(1, &msColor_);
        glGenRenderbuffers(1, &msDepth_);
        glGenFramebuffers(1, &resolveFbo_);
        glGenTextures(1, &resolveTex_);
    }
    int maxSamples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(4, maxSamples);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    glBindTexture(GL_TEXTURE_2D, resolveTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    msW_ = w;
    msH_ = h;
    texGeneration_++;
}

M4 Scene3D::boardModel(Quat q) const { return m_quat(q); }

void Scene3D::orbit(float dxDeg, float dyDeg)
{
    yaw_ += dxDeg;
    elev_ = std::clamp(elev_ + dyDeg, -85.0f, 89.0f);
}

void Scene3D::zoom(float steps) { zoom_ = std::clamp(zoom_ * std::pow(0.9f, steps), 0.25f, 4.0f); }

void Scene3D::resetCamera()
{
    yaw_ = 0;
    elev_ = 38;
    zoom_ = 1;
}

void Scene3D::setCamera(float yawDeg, float elevDeg, float zoom)
{
    yaw_ = yawDeg;
    elev_ = std::clamp(elevDeg, -85.0f, 89.0f);
    zoom_ = std::clamp(zoom, 0.25f, 4.0f);
}

bool Scene3D::cameraMoved() const { return yaw_ != 0 || elev_ != 38 || zoom_ != 1; }

bool Scene3D::project(V3 world, float& sx, float& sy) const
{
    float c[4];
    xform4(vp_, world, c);
    if (c[3] <= 1e-4f) return false;
    sx = (c[0] / c[3] * 0.5f + 0.5f) * vw_;
    sy = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * vh_;
    return true;
}

void Scene3D::drawArrow(V3 from, V3 dir, float length, const float rgb3[3], float alpha, bool xray)
{
    if (len(dir) < 1e-6f || length < 0.5f) return;
    M4 model = m_translate(from) * m_align_z(dir) * m_scale({length, length, length});
    glUseProgram(progMesh_);
    glUniformMatrix4fv(U(progMesh_, "uModel"), 1, GL_FALSE, model.m);
    glUniform1f(U(progMesh_, "uScale"), 1.0f);
    glUniform4f(U(progMesh_, "uTint"), rgb3[0], rgb3[1], rgb3[2], 1.0f);
    glUniform1f(U(progMesh_, "uUseTint"), 1.0f);
    glUniform1f(U(progMesh_, "uFlat"), 0.35f);
    glBindVertexArray(arrowVao_);
    if (xray) {      // the part inside the board, faintly
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_GREATER);
        glUniform1f(U(progMesh_, "uAlpha"), 0.30f * alpha);
        glDrawArrays(GL_TRIANGLES, 0, arrowVerts_);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }
    glUniform1f(U(progMesh_, "uAlpha"), alpha);
    if (alpha < 1.0f) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, arrowVerts_);
    glUniform1f(U(progMesh_, "uFlat"), 0.0f);
}

void Scene3D::render(const SceneInput& in, int w, int h)
{
    if (!ready_ || w < 8 || h < 8) return;
    ensureTargets(w, h);
    vw_ = w;
    vh_ = h;

    // camera: fit the stack (with its arrows) in the view, then the user's zoom
    const float fovy = 28.0f;
    float aspect = static_cast<float>(w) / static_cast<float>(h);
    float tv = std::tan(fovy * 0.5f * 3.14159265f / 180.0f), th = tv * aspect;
    float fit = std::max(60.0f / th, 48.0f / tv);
    float dist = fit * zoom_;
    float ey = elev_ * 3.14159265f / 180.0f, ez = yaw_ * 3.14159265f / 180.0f;
    V3 target{0, 0, -4};
    eye_ = target + V3{dist * std::cos(ey) * std::sin(ez), -dist * std::cos(ey) * std::cos(ez), dist * std::sin(ey)};
    V3 up = std::fabs(elev_) > 88.0f ? V3{std::sin(ez), std::cos(ez), 0} : V3{0, 0, 1};
    view_ = m_lookat(eye_, target, up);
    proj_ = m_perspective(fovy, aspect, 8.0f, 4000.0f);
    vp_ = proj_ * view_;

    M4 model = boardModel(in.q);
    float dim = in.live ? 1.0f : 0.55f;

    // the OLED texture, when the screen changed
    if (in.fb && in.fbVersion != oledVersion_) {
        uint8_t px[128 * 64];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 128; x++)
                px[y * 128 + x] = ((in.fb[(y >> 3) * 128 + x] >> (y & 7)) & 1) ? 255 : 0;
        glBindTexture(GL_TEXTURE_2D, oledTex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 64, GL_RED, GL_UNSIGNED_BYTE, px);
        oledVersion_ = in.fbVersion;
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    // 1. the shadow: the stack seen from straight above, as coverage
    glBindFramebuffer(GL_FRAMEBUFFER, shFbo_);
    glViewport(0, 0, shSize_, shSize_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    M4 shVP = m_ortho(-SHADOW_EXT, SHADOW_EXT, -SHADOW_EXT, SHADOW_EXT, -400, 400);
    glUseProgram(progShadow_);
    glUniformMatrix4fv(U(progShadow_, "uModel"), 1, GL_FALSE, model.m);
    glUniformMatrix4fv(U(progShadow_, "uVP"), 1, GL_FALSE, shVP.m);
    glUniform1f(U(progShadow_, "uScale"), scale_);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, opaqueCount_ + transCount_, GL_UNSIGNED_INT, nullptr);

    // 2. the view
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, w, h);
    float bg[3], pool[3], grid[3];
    rgb(0x0a0a09, bg);
    rgb(0x1a1a18, pool);
    rgb(0x3a3835, grid);
    glClearColor(bg[0], bg[1], bg[2], 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    // the ground
    glUseProgram(progGround_);
    glUniformMatrix4fv(U(progGround_, "uVP"), 1, GL_FALSE, vp_.m);
    glUniform3fv(U(progGround_, "uBg"), 1, bg);
    glUniform3fv(U(progGround_, "uPool"), 1, pool);
    glUniform3fv(U(progGround_, "uGrid"), 1, grid);
    glUniform1f(U(progGround_, "uShadowExt"), SHADOW_EXT);
    glUniform1f(U(progGround_, "uShadowK"), 0.72f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shTex_);
    glUniform1i(U(progGround_, "uShadow"), 0);
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // the stack, opaque parts
    glUseProgram(progMesh_);
    glUniformMatrix4fv(U(progMesh_, "uVP"), 1, GL_FALSE, vp_.m);
    glUniformMatrix4fv(U(progMesh_, "uModel"), 1, GL_FALSE, model.m);
    glUniform1f(U(progMesh_, "uScale"), scale_);
    glUniform3f(U(progMesh_, "uEye"), eye_.x, eye_.y, eye_.z);
    glUniform1f(U(progMesh_, "uUseTint"), 0.0f);
    glUniform4f(U(progMesh_, "uTint"), 1, 1, 1, 1);
    glUniform1f(U(progMesh_, "uAlpha"), 1.0f);
    glUniform1f(U(progMesh_, "uDim"), dim);
    glUniform1f(U(progMesh_, "uFlat"), 0.0f);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, opaqueCount_, GL_UNSIGNED_INT, nullptr);

    // the screen
    {
        float top[3], bottom[3], off[3];
        unsigned panelHex = in.panel == 2 ? theme::OLED_WHITE : theme::OLED_BLUE;
        rgb(in.panel == 0 ? theme::OLED_YELLOW : panelHex, top);
        rgb(panelHex, bottom);
        rgb(theme::OLED_OFF, off);
        glUseProgram(progOled_);
        glUniformMatrix4fv(U(progOled_, "uVP"), 1, GL_FALSE, vp_.m);
        glUniformMatrix4fv(U(progOled_, "uModel"), 1, GL_FALSE, model.m);
        glUniform3fv(U(progOled_, "uTop"), 1, top);
        glUniform3fv(U(progOled_, "uBottom"), 1, bottom);
        glUniform3fv(U(progOled_, "uOff"), 1, off);
        glUniform1f(U(progOled_, "uTwo"), in.panel == 0 ? 1.0f : 0.0f);
        glUniform1f(U(progOled_, "uDim"), in.fb ? dim : 0.0f);
        glBindTexture(GL_TEXTURE_2D, oledTex_);
        glUniform1i(U(progOled_, "uFb"), 0);
        glBindVertexArray(quadVao_);
        glDrawArrays(GL_TRIANGLE_FAN, 4, 4);
    }

    // the solder mask over copper and core, as KiCad draws it
    glUseProgram(progMesh_);
    glBindVertexArray(vao_);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glDrawElements(GL_TRIANGLES, transCount_, GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(static_cast<uintptr_t>(opaqueCount_) * 4));
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    // arrows: the device axes on the board, and gravity as the accelerometer reads it (in the world)
    tipsValid = false;
    if (in.live && (in.axes || in.gravity)) {
        V3 o = xform(model, {0, 0, 0});
        float cx[3], cy[3], cz[3], cg[3];
        rgb(theme::AX_X, cx);
        rgb(theme::AX_Y, cy);
        rgb(theme::AX_Z, cz);
        rgb(theme::GRAV, cg);
        const float L = 58.0f;
        V3 ex = norm(xform(model, {1, 0, 0}) - o), ey2 = norm(xform(model, {0, 1, 0}) - o),
           ez2 = norm(xform(model, {0, 0, 1}) - o);
        if (in.axes) {
            drawArrow(o, ex, L, cx, 1.0f, true);
            drawArrow(o, ey2, L * 0.7f, cy, 1.0f, true);
            drawArrow(o, ez2, L * 0.7f, cz, 1.0f, true);
        }
        tipX = o + ex * (L + 5);
        tipY = o + ey2 * (L * 0.7f + 5);
        tipZ = o + ez2 * (L * 0.7f + 5);
        if (in.gravity && in.haveAcc) {
            V3 a = in.acc;
            float g = len(a);
            if (g > 0.05f && std::isfinite(g)) {
                V3 down = -norm(xform(model, a) - o);       // the measured gravity, in the world
                float gl = 40.0f * std::clamp(g, 0.2f, 2.5f);
                drawArrow(o, down, gl, cg, 1.0f, true);
                tipG = o + down * (gl + 5);
            } else tipG = o;
        } else tipG = o;
        tipsValid = true;
    }

    // the ghost: where the host's reference fusion has the boards, as outlines (faint where the stack hides them)
    ghostAlpha = 0;
    if (in.live && in.haveRef) {
        float ang = qangle(in.q, in.ref);
        ghostAlpha = std::clamp((ang - 1.0f) / 3.0f, 0.0f, 1.0f);
        if (ghostAlpha > 0.01f) {
            M4 gm = boardModel(in.ref);
            glUseProgram(progLine_);
            glUniformMatrix4fv(U(progLine_, "uModel"), 1, GL_FALSE, gm.m);
            glUniformMatrix4fv(U(progLine_, "uVP"), 1, GL_FALSE, vp_.m);
            glBindVertexArray(lineVao_);
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_GREATER);
            glUniform4f(U(progLine_, "uColor"), 0.82f, 0.90f, 1.0f, 0.22f * ghostAlpha);
            glDrawArrays(GL_LINES, 0, lineVerts_);
            glDepthFunc(GL_LEQUAL);
            glUniform4f(U(progLine_, "uColor"), 0.86f, 0.93f, 1.0f, 0.85f * ghostAlpha);
            glDrawArrays(GL_LINES, 0, lineVerts_);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    // resolve the samples into the texture the 2D layer draws
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
}

} // namespace slrig

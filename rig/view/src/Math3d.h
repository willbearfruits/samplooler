// Small vector, matrix and quaternion helpers for the 3D view (column-major 4x4, as GL wants them).
#pragma once
#include <cmath>

namespace slrig {

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    constexpr V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator-() const { return {-x, -y, -z}; }
};
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) { float l = len(a); return l > 1e-12f ? a * (1.0f / l) : V3{0, 0, 0}; }

// w x y z; rotates device-axis vectors into the world: q v q*
struct Quat {
    float w = 1, x = 0, y = 0, z = 0;
};
inline Quat qmul(Quat a, Quat b)
{
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat qconj(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }
inline Quat qnorm(Quat q)
{
    float l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(l > 1e-9f) || !std::isfinite(l)) return {};
    return {q.w / l, q.x / l, q.y / l, q.z / l};
}
inline V3 qrot(Quat q, V3 v)
{
    Quat p = qmul(qmul(q, Quat{0, v.x, v.y, v.z}), qconj(q));
    return {p.x, p.y, p.z};
}
// the angle between two orientations, degrees
inline float qangle(Quat a, Quat b)
{
    float d = std::fabs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
    if (d > 1) d = 1;
    return 2.0f * std::acos(d) * 57.29578f;
}

struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int r, int c) { return m[c * 4 + r]; }
    float at(int r, int c) const { return m[c * 4 + r]; }
};
inline M4 operator*(const M4& a, const M4& b)
{
    M4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.at(i, k) * b.at(k, j);
            r.at(i, j) = s;
        }
    return r;
}
inline V3 xform(const M4& a, V3 p)
{
    return {a.at(0, 0) * p.x + a.at(0, 1) * p.y + a.at(0, 2) * p.z + a.at(0, 3),
            a.at(1, 0) * p.x + a.at(1, 1) * p.y + a.at(1, 2) * p.z + a.at(1, 3),
            a.at(2, 0) * p.x + a.at(2, 1) * p.y + a.at(2, 2) * p.z + a.at(2, 3)};
}
// clip-space w too, for projecting to the screen
inline void xform4(const M4& a, V3 p, float out[4])
{
    for (int r = 0; r < 4; r++) out[r] = a.at(r, 0) * p.x + a.at(r, 1) * p.y + a.at(r, 2) * p.z + a.at(r, 3);
}
inline M4 m_translate(V3 t)
{
    M4 r;
    r.at(0, 3) = t.x; r.at(1, 3) = t.y; r.at(2, 3) = t.z;
    return r;
}
inline M4 m_scale(V3 s)
{
    M4 r;
    r.at(0, 0) = s.x; r.at(1, 1) = s.y; r.at(2, 2) = s.z;
    return r;
}
inline M4 m_quat(Quat q)
{
    M4 r;
    float w = q.w, x = q.x, y = q.y, z = q.z;
    r.at(0, 0) = 1 - 2 * (y * y + z * z); r.at(0, 1) = 2 * (x * y - w * z);     r.at(0, 2) = 2 * (x * z + w * y);
    r.at(1, 0) = 2 * (x * y + w * z);     r.at(1, 1) = 1 - 2 * (x * x + z * z); r.at(1, 2) = 2 * (y * z - w * x);
    r.at(2, 0) = 2 * (x * z - w * y);     r.at(2, 1) = 2 * (y * z + w * x);     r.at(2, 2) = 1 - 2 * (x * x + y * y);
    return r;
}
// a rotation that takes +z onto `dir` (unit)
inline M4 m_align_z(V3 dir)
{
    V3 z = norm(dir);
    V3 helper = std::fabs(z.z) < 0.9f ? V3{0, 0, 1} : V3{1, 0, 0};
    V3 x = norm(cross(helper, z));
    V3 y = cross(z, x);
    M4 r;
    r.at(0, 0) = x.x; r.at(1, 0) = x.y; r.at(2, 0) = x.z;
    r.at(0, 1) = y.x; r.at(1, 1) = y.y; r.at(2, 1) = y.z;
    r.at(0, 2) = z.x; r.at(1, 2) = z.y; r.at(2, 2) = z.z;
    return r;
}
inline M4 m_perspective(float fovy_deg, float aspect, float zn, float zf)
{
    float f = 1.0f / std::tan(fovy_deg * 0.5f * 3.14159265f / 180.0f);
    M4 r;
    r.m[0] = f / aspect; r.m[5] = f; r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf); r.m[15] = 0;
    return r;
}
inline M4 m_ortho(float l, float r_, float b, float t, float zn, float zf)
{
    M4 r;
    r.m[0] = 2 / (r_ - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (zf - zn);
    r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(zf + zn) / (zf - zn);
    return r;
}
inline M4 m_lookat(V3 eye, V3 at, V3 up)
{
    V3 f = norm(at - eye), s = norm(cross(f, up)), u = cross(s, f);
    M4 r;
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(0, 3) = -dot(s, eye); r.at(1, 3) = -dot(u, eye); r.at(2, 3) = dot(f, eye);
    return r;
}

} // namespace slrig

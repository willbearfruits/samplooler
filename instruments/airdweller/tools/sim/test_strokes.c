// Strokes for the tests: a box held in a hand. The box turns about axes fixed in the room (x to the right, y away
// from you, z up). Its orientation is integrated at 1 kHz, so gravity moves through the box's own axes as it turns, and
// the accelerometer reads gravity plus the box's own acceleration on a 15 cm arm (r alpha + r w^2): a stroke that stops
// hard gives a jolt, a smooth one does not. The mic callback (st_mic, or st_motion from a callback of your own) fills
// SI.gyro, SI.acc, SI.burst and SI.gburst every tick, in the box's axes (the sim's screen axes).
//
//   st_reset();                                  still, flat, screen up; every stroke forgotten
//   st_pose(pitch, roll);                        hold the box tipped (front edge up: +) and rolled (right edge down: +)
//   st_swing(t, ST_LEFT, 600);                   a hit: speeds up over 70 ms to 600 deg/s, stops in 15 ms, then the
//                                                hand comes back as far, smoothly, at 0.3 of the speed
//   st_strike(t, dir, peak, rise, stop, ret, ret_s);   the same with every time given (ret 0: it does not come back;
//                                                ret_s 0: as long as it takes to come back as far as it went)
//   st_stroke(t, dir, peak, dur, ret);           a smooth turn (a half sine) and its return: no sharp stop, a sweep
//   st_glide(t, dir, dps, dur);                  a slow steady turn (a drag), eased in and out over 50 ms
//   st_shake(t, dir, hz, dps, dur);              a shake: turns back and forth as a sine
//   st_hold(dir, dps);                           a steady turn on top, until st_hold(.., 0)
//   st_flat_acc(1);                              the accelerometer reads gravity only (a turn with no jolt)
// While nothing is scripted the model leaves SI.acc and SI.gyro alone (sim_tilt and a test's own shaking work); the
// moment it goes quiet it leaves the gyro at 0 and the accelerometer at its last pose's gravity.
// Directions are taken against where the box points (its heading: x across it, y along it, z the room's up, so a
// stroke means the same after the box has been swept round): ST_FRONT tips the front edge down, ST_BACK up; ST_LEFT
// sweeps round to the left (about the vertical), ST_RIGHT to the right; ST_TWIST rolls about the forearm (the right
// edge goes down).
#include "sim.h"
#include <math.h>
#include <string.h>

const vec3_t ST_FRONT = { -1, 0, 0 }, ST_BACK = { 1, 0, 0 }, ST_LEFT = { 0, 0, 1 }, ST_RIGHT = { 0, 0, -1 }, ST_TWIST = { 0, 1, 0 };

enum { K_STRIKE, K_SMOOTH, K_GLIDE, K_SHAKE };
typedef struct { int kind; float t; vec3_t dir; float peak, a, b, ret, ret_s; } st_t;
static st_t script[512];
static int nscript, first;
static vec3_t hold_dir; static float hold_dps;
static int acc_flat;
static float pose_pitch, pose_roll;
static float R[3][3];                         // the box's axes in the room: column j is box axis j
static vec3_t w_prev;                          // the last sample's turn rate, box axes, rad/s
static float t_model = -1.0f;                  // the next sample's time
static const float ARM = 0.15f;                // m: the box ahead of the pivot along its own y

static void pose_set(void)
{
    float p = pose_pitch * 0.0174533f, r = pose_roll * 0.0174533f;
    // tip about the room's x (front edge up for p > 0), then roll about the box's y (right edge down for r > 0)
    float cp = cosf(p), sp = sinf(p), cr = cosf(r), sr = sinf(r);
    float Rx[3][3] = { { 1, 0, 0 }, { 0, cp, -sp }, { 0, sp, cp } };
    float Ry[3][3] = { { cr, 0, sr }, { 0, 1, 0 }, { -sr, 0, cr } };
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float s = 0; for (int k = 0; k < 3; k++) s += Rx[i][k] * Ry[k][j]; R[i][j] = s; }
    w_prev = (vec3_t){ 0, 0, 0 };
}
void st_reset(void) { nscript = first = 0; hold_dps = 0.0f; acc_flat = 0; pose_pitch = pose_roll = 0.0f; pose_set(); t_model = -1.0f; }   // SI.acc: the test's
static vec3_t up_now(void) { return (vec3_t){ R[2][0], R[2][1], R[2][2] }; }
void st_pose(float pitch, float roll) { pose_pitch = pitch; pose_roll = roll; pose_set(); SI.acc = up_now(); SI.gyro = (vec3_t){ 0, 0, 0 }; }
static void add(st_t s) { if (nscript < 512) script[nscript++] = s; }
void st_strike(float t, vec3_t dir, float peak, float rise, float stop, float ret, float ret_s)
{
    if (ret > 0.0f && ret_s <= 0.0f) ret_s = 0.5f * (rise + stop) / (ret * 0.6366198f);   // back as far as it went
    add((st_t){ K_STRIKE, t, dir, peak, rise, stop, ret, ret_s });
}
void st_swing(float t, vec3_t dir, float peak) { st_strike(t, dir, peak, 0.07f, 0.015f, 0.3f, 0.0f); }
void st_stroke(float t, vec3_t dir, float peak, float dur, float ret) { add((st_t){ K_SMOOTH, t, dir, peak, dur, 0, ret, 1.5f * dur }); }
void st_glide(float t, vec3_t dir, float dps, float dur) { add((st_t){ K_GLIDE, t, dir, dps, dur, 0, 0, 0 }); }
void st_shake(float t, vec3_t dir, float hz, float dps, float dur) { add((st_t){ K_SHAKE, t, dir, dps, dur, hz, 0, 0 }); }
void st_hold(vec3_t dir, float dps) { hold_dir = dir; hold_dps = dps; }
void st_flat_acc(int on) { acc_flat = on; }

static float ease(float u) { return u <= 0 ? 0 : u >= 1 ? 1 : 0.5f - 0.5f * cosf(3.14159265f * u); }
static float st_len(const st_t *s)
{
    switch (s->kind) {
    case K_STRIKE: return s->a + s->b + (s->ret > 0 ? 0.02f + s->ret_s : 0);
    case K_SMOOTH: return s->a + 0.02f + s->ret_s;
    default:       return s->a;
    }
}
// the turn rate in the room's axes, deg/s
static vec3_t rate_at(float t)
{
    vec3_t g = { hold_dir.x * hold_dps, hold_dir.y * hold_dps, hold_dir.z * hold_dps };
    while (first < nscript && t - script[first].t > st_len(&script[first]) + 0.05f) first++;
    for (int i = first; i < nscript; i++) {
        const st_t *s = &script[i];
        float dt = t - s->t, a = 0;
        if (dt < 0 || dt > st_len(s)) continue;
        switch (s->kind) {
        case K_STRIKE:
            if (dt < s->a) a = s->peak * ease(dt / s->a);
            else if (dt < s->a + s->b) a = s->peak * (1.0f - ease((dt - s->a) / s->b));
            else if (s->ret > 0 && dt > s->a + s->b + 0.02f) a = -s->ret * s->peak * sinf(3.14159265f * (dt - s->a - s->b - 0.02f) / s->ret_s);
            break;
        case K_SMOOTH:
            if (dt < s->a) a = s->peak * sinf(3.14159265f * dt / s->a);
            else if (s->ret > 0 && dt > s->a + 0.02f) a = -s->ret * s->peak * sinf(3.14159265f * (dt - s->a - 0.02f) / s->ret_s);
            break;
        case K_GLIDE: a = s->peak * ease(dt / 0.05f) * ease((s->a - dt) / 0.05f); break;
        case K_SHAKE: a = s->peak * ease(dt / 0.05f) * ease((s->a - dt) / 0.05f) * sinf(6.2831853f * s->b * dt); break;
        }
        g.x += a * s->dir.x; g.y += a * s->dir.y; g.z += a * s->dir.z;
    }
    return g;
}
static vec3_t cross(vec3_t a, vec3_t b) { return (vec3_t){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static void orthonormalise(void)
{
    // Gram-Schmidt on the columns
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < j; k++) { float d = 0; for (int i = 0; i < 3; i++) d += R[i][j] * R[i][k]; for (int i = 0; i < 3; i++) R[i][j] -= d * R[i][k]; }
        float n = 0; for (int i = 0; i < 3; i++) n += R[i][j] * R[i][j];
        n = 1.0f / sqrtf(n); for (int i = 0; i < 3; i++) R[i][j] *= n;
    }
}
// one sample: turn the box by the room rate over 1 ms, then what the gyro and the accelerometer read (box axes)
static void sample(float t, vec3_t *gyro_dps, vec3_t *acc_g)
{
    const float dt = 0.001f, D2R = 0.0174533f;
    vec3_t hw = rate_at(t);
    // the heading: along = the box's y levelled (or, the box upright, its x levelled turned a quarter), across = along x up
    vec3_t along = { R[0][1], R[1][1], 0.0f };
    float an = sqrtf(along.x * along.x + along.y * along.y);
    if (an > 0.3f) { along.x /= an; along.y /= an; }
    else { float cx = R[0][0], cy = R[1][0], cn = sqrtf(cx * cx + cy * cy) + 1e-9f; along = (vec3_t){ -cy / cn, cx / cn, 0.0f }; }
    vec3_t across = { along.y, -along.x, 0.0f };
    vec3_t ww = { hw.x * across.x + hw.y * along.x, hw.x * across.y + hw.y * along.y, hw.z };
    vec3_t w = { ww.x * D2R, ww.y * D2R, ww.z * D2R };
    // dR/dt = [w]x R
    float Rn[3][3];
    for (int j = 0; j < 3; j++) {
        vec3_t c = { R[0][j], R[1][j], R[2][j] }, d = cross(w, c);
        Rn[0][j] = c.x + dt * d.x; Rn[1][j] = c.y + dt * d.y; Rn[2][j] = c.z + dt * d.z;
    }
    memcpy(R, Rn, sizeof R);
    orthonormalise();
    // into the box's axes: v_box = R^T v_room
    vec3_t wb = { R[0][0] * w.x + R[1][0] * w.y + R[2][0] * w.z, R[0][1] * w.x + R[1][1] * w.y + R[2][1] * w.z, R[0][2] * w.x + R[1][2] * w.y + R[2][2] * w.z };
    vec3_t up = { R[2][0], R[2][1], R[2][2] };
    vec3_t al = { (wb.x - w_prev.x) / dt, (wb.y - w_prev.y) / dt, (wb.z - w_prev.z) / dt };
    w_prev = wb;
    vec3_t p = { 0, ARM, 0 };
    vec3_t a1 = cross(al, p), a2 = cross(wb, cross(wb, p));
    float k = acc_flat ? 0.0f : 1.0f / 9.81f;
    *acc_g = (vec3_t){ up.x + k * (a1.x + a2.x), up.y + k * (a1.y + a2.y), up.z + k * (a1.z + a2.z) };
    *gyro_dps = (vec3_t){ wb.x / D2R, wb.y / D2R, wb.z / D2R };
}

// the motion for the tick that starts at t: ten samples at 1 kHz, the last one also as the tick's gyro and acc
static bool active_at(float t)
{
    if (hold_dps != 0.0f) return true;
    for (int i = first; i < nscript; i++) if (t + 0.012f >= script[i].t && t - 0.01f <= script[i].t + st_len(&script[i])) return true;
    return false;
}
static bool was_active;
// the box as the accelerometer has it now (a test's sim_tilt, or the last pose): the room's up from it, the box's y
// levelled as the room's y
static void pose_from_acc(vec3_t a)
{
    float n = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (n < 0.3f) return;
    vec3_t z = { a.x / n, a.y / n, a.z / n }, y = { -z.y * z.x, 1.0f - z.y * z.y, -z.y * z.z };
    float yn = sqrtf(y.x * y.x + y.y * y.y + y.z * y.z);
    if (yn < 0.2f) { vec3_t x = { 1.0f - z.x * z.x, -z.x * z.y, -z.x * z.z }; float xn = sqrtf(x.x * x.x + x.y * x.y + x.z * x.z); x.x /= xn; x.y /= xn; x.z /= xn; y = cross(z, x); }
    else { y.x /= yn; y.y /= yn; y.z /= yn; }
    vec3_t x = cross(y, z);
    float rows[3][3] = { { x.x, x.y, x.z }, { y.x, y.y, y.z }, { z.x, z.y, z.z } };   // the room's axes in the box's
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R[i][j] = rows[i][j];
    w_prev = (vec3_t){ 0, 0, 0 };
}
void st_motion(float t)
{
    if (!active_at(t)) {                                              // quiet: the test's own SI.acc and SI.gyro stand
        if (was_active) { SI.gyro = (vec3_t){ 0, 0, 0 }; SI.acc = up_now(); was_active = false; }
        t_model = t + 0.01f;
        return;
    }
    if (!was_active) pose_from_acc(SI.acc);                           // it starts moving from where the box is
    was_active = true;
    if (t_model < 0.0f || fabsf(t - t_model) > 0.0105f) t_model = t;
    for (int i = 0; i < 10; i++) sample(t_model + 0.001f * (float)(i + 1), &SI.gburst[i], &SI.burst[i]);   // the newest at the tick's end, as the FIFO
    t_model += 0.01f;
    SI.nburst = 10;
    SI.gyro = SI.gburst[9];
    SI.acc = SI.burst[9];
}

// a quiet room, and the strokes (called per sample: the motion is set once per 10 ms)
static float st_last = -1.0f;
float st_mic(float t, void *u)
{
    (void)u;
    uint32_t f = (uint32_t)(t * (float)AUDIO_SR + 0.5f);
    if (f % TICK_FRAMES == 0 && (t > st_last || t < st_last - 0.001f)) { st_motion(t); st_last = t; }   // at each tick's first sample
    return 0.0008f * sim_noise();
}

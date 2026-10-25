// Strokes for the tests (AIR's model): a stroke is a half sine of gyro rate along its axis, then the hand coming back
// the other way, slower; the accelerometer's 1 kHz burst carries the jolt that goes with it (the box on a 15 cm arm:
// r w^2 + r dw/dt). The sim has no gyro hook of its own, so the mic callback (st_mic) sets SI.gyro and SI.burst every
// tick, before the controls are read. Use it as SI.mic, or call st_motion(t) from a mic callback of your own.
//
//   st_reset();                          forget every stroke
//   st_swing(sim_t + 0.1f, ST_LEFT, 600); a stroke to the left, 600 deg/s at its peak, 0.12 s, the usual return
//   st_stroke(t, dir, peak, dur, ret);   the same with its own length and return (ret x peak back, 0: none)
//   st_hold(dir, dps);                   a steady turn on top (a twist, a slow tilt), until st_hold(.., 0)
//   st_flat_acc(true);                   the accelerometer stays at 1 g whatever the gyro does (a turn with no jolt)
#include "sim.h"
#include <math.h>

const vec3_t ST_FRONT = { 1, 0, 0 }, ST_BACK = { -1, 0, 0 }, ST_LEFT = { 0, -1, 0 }, ST_RIGHT = { 0, 1, 0 }, ST_TWIST = { 0, 0, 1 };

typedef struct { float t; vec3_t dir; float peak, dur, ret; } st_t;
static st_t script[512];
static int nscript, first;
static vec3_t hold_dir; static float hold_dps;
static int acc_flat;

void st_reset(void) { nscript = first = 0; hold_dps = 0.0f; acc_flat = 0; }
void st_stroke(float t, vec3_t dir, float peak, float dur, float ret) { if (nscript < 512) script[nscript++] = (st_t){ t, dir, peak, dur, ret }; }
void st_swing(float t, vec3_t dir, float peak) { st_stroke(t, dir, peak, 0.12f, 0.5f); }
void st_hold(vec3_t dir, float dps) { hold_dir = dir; hold_dps = dps; }
void st_flat_acc(int on) { acc_flat = on; }

static vec3_t gyro_at(float t)
{
    vec3_t g = { hold_dir.x * hold_dps, hold_dir.y * hold_dps, hold_dir.z * hold_dps };
    while (first < nscript && t - script[first].t > script[first].dur * 2.5f + 0.1f) first++;
    for (int i = first; i < nscript; i++) {
        const st_t *s = &script[i];
        float dt = t - s->t, a = 0;
        if (dt < 0) continue;
        if (dt < s->dur) a = s->peak * sinf(3.14159f * dt / s->dur);
        else if (dt > s->dur + 0.02f && dt < s->dur * 2.5f + 0.02f) a = -s->ret * s->peak * sinf(3.14159f * (dt - s->dur - 0.02f) / (1.5f * s->dur));
        g.x += a * s->dir.x; g.y += a * s->dir.y; g.z += a * s->dir.z;
    }
    return g;
}
static float gyro_mag_at(float t) { vec3_t g = gyro_at(t); return sqrtf(g.x * g.x + g.y * g.y + g.z * g.z); }

// the motion for the tick that starts at t: the gyro now, the 1 kHz accelerometer for the next 10 ms
void st_motion(float t)
{
    SI.gyro = gyro_at(t);
    const float r = 0.15f, g0 = 9.81f;
    for (int i = 0; i < 10; i++) {
        float tt = t + 0.001f * (float)i;
        float w = gyro_mag_at(tt) * 0.0174533f, w1 = gyro_mag_at(tt + 0.001f) * 0.0174533f;
        float a = acc_flat ? 0.0f : (r * w * w + r * fabsf(w1 - w) * 1000.0f) / g0;
        SI.burst[i] = (vec3_t){ SI.acc.x, SI.acc.y, SI.acc.z + a };
    }
    SI.nburst = 10;
}

// a quiet room, and the strokes (called per sample: the motion is set once per 10 ms)
static float st_last = -1.0f;
float st_mic(float t, void *u)
{
    (void)u;
    if (t - st_last >= 0.0099f || t < st_last) { st_motion(t); st_last = t; }
    return 0.0008f * sim_noise();
}

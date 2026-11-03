// Small DSP pieces for the instruments. Everything here is safe in the audio task: no allocation,
// no double, and no libm in the per-sample functions (the coefficient helpers call expf/tanf once
// per change, which is fine at block rate).
//
// House rules for the audio path (the Samplooler's and TWOFOLD's):
//  - every control that reaches the sound is smoothed (smooth_t, a ramp per block, or a one-pole per sample):
//    no zipper noise, no clicks at any parameter edge (tools/sim checks every param, min to max in one step)
//  - voices are stolen with a short fade, never cut
//  - nominal peaks around -12 dBFS; the master limiter (master_process) is a safety net, not a sound
//  - recursive states that decay to nothing are flushed (flush_denormal) so silence costs no CPU
//  - coefficients at block rate, libm only there; sin_turns() and friends per sample
#pragma once
#include <stdint.h>
#include <math.h>
#include "core.h"

#define TWO_PI 6.28318531f

static inline float clampf(float v, float lo, float hi) { return !(v >= lo) ? lo : v > hi ? hi : v; }
static inline int   clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstep(float e0, float e1, float x) { float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

// xorshift32: noise and dice
static inline uint32_t rng_next(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline float rng_bi(uint32_t *s) { return (float)(int32_t)rng_next(s) * (1.0f / 2147483648.0f); }   // -1..1
static inline float rng_uni(uint32_t *s) { return (float)(rng_next(s) >> 8) * (1.0f / 16777216.0f); }      // 0..1

// Sine of a phase in turns (any value). Parabola plus one correction step: about 0.1 % error.
static inline float sin_turns(float p)
{
    p -= (float)(int)p;
    if (p < 0.0f) p += 1.0f;
    float x = 2.0f * p - 1.0f;                 // sin(2 pi p) = -sin(pi x)
    float y = 4.0f * x * (1.0f - fabsf(x));
    y = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - (float)(int)p : p; }

// tanh-like soft clip, odd, |y| < 1
static inline float soft_tanh(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float midi_hz(float n) { return 440.0f * exp2f((n - 69.0f) / 12.0f); }   // libm: not per sample

// One-pole low pass. coef from a cutoff in Hz, or from a time constant in seconds.
static inline float onepole_hz(float fc) { return 1.0f - expf(-TWO_PI * fc / (float)AUDIO_SR); }
static inline float onepole_s(float t)   { return 1.0f - expf(-1.0f / (t * (float)AUDIO_SR)); }
static inline float onepole(float *z, float x, float a) { *z += a * (x - *z); return *z; }

// Envelope follower with separate attack and release coefficients (onepole_s).
static inline float follow(float *z, float x, float att, float rel) { float a = fabsf(x); *z += (a > *z ? att : rel) * (a - *z); return *z; }

// Topology-preserving state variable filter (Simper). Set with svf_set (tanf), run per sample.
typedef struct { float a1, a2, a3, k, ic1, ic2; } svf_t;
static inline void svf_set(svf_t *f, float fc, float q)
{
    fc = clampf(fc, 10.0f, 0.45f * (float)AUDIO_SR);
    float g = tanf(3.14159265f * fc / (float)AUDIO_SR);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k)); f->a2 = g * f->a1; f->a3 = g * f->a2;
}
// returns the band pass with unity gain at the centre; *lp gets the low pass if non-NULL
static inline float svf_bp(svf_t *f, float x, float *lp)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1; f->ic2 = 2.0f * v2 - f->ic2;
    if (lp) *lp = v2;
    return f->k * v1;
}
static inline float svf_lp(svf_t *f, float x) { float lp; svf_bp(f, x, &lp); return lp; }
static inline float svf_hp(svf_t *f, float x) { float lp; float bp = svf_bp(f, x, &lp); return x - bp - lp; }

// A struck two-pole resonator: rings at f with a decay of t60 seconds. Strike with an amplitude.
typedef struct { float c1, c2, y1, y2, norm; } reso_t;
static inline void reso_set(reso_t *r, float f, float t60)
{
    float w = TWO_PI * clampf(f, 20.0f, 0.45f * (float)AUDIO_SR) / (float)AUDIO_SR;
    float rr = expf(-6.9078f / (t60 * (float)AUDIO_SR));
    r->c1 = 2.0f * rr * cosf(w); r->c2 = -rr * rr; r->norm = sinf(w);
}
static inline void  reso_strike(reso_t *r, float amp) { r->y1 += amp * r->norm; }
static inline float reso_run(reso_t *r) { float y = r->c1 * r->y1 + r->c2 * r->y2; r->y2 = r->y1; r->y1 = y; return y; }

// DC blocker
typedef struct { float x1, y1; } dcb_t;
static inline float dcb_run(dcb_t *d, float x) { float y = x - d->x1 + 0.995f * d->y1; d->x1 = x; d->y1 = y; return y; }

// Delay line with a linear-interpolated read, length a power of two.
typedef struct { float *buf; uint32_t mask, w; } dline_t;
static inline void  dline_write(dline_t *d, float x) { d->buf[d->w & d->mask] = x; d->w++; }
static inline float dline_read(const dline_t *d, float delay)   // samples back from the last write, >= 1
{
    int   i = (int)delay; float fr = delay - (float)i;
    float a = d->buf[(d->w - (uint32_t)i) & d->mask], b = d->buf[(d->w - (uint32_t)i - 1) & d->mask];
    return a + (b - a) * fr;
}

// One-pole smoother for a control: call smooth_to() per sample (or per block with a bigger k).
typedef struct { float y, k; } smooth_t;
static inline void  smooth_init(smooth_t *s, float v, float time_s) { s->y = v; s->k = onepole_s(time_s); }
static inline float smooth_to(smooth_t *s, float target) { s->y += s->k * (target - s->y); return s->y; }

// Anything this small in a feedback state is silence: zero it so it never goes denormal.
static inline float flush_denormal(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

// 2^x for per-sample pitch work, |error| < 0.03 % over -8..8 (no libm).
static inline float fast_exp2(float x)
{
    if (x < -30.0f) return 0.0f;
    if (x > 30.0f) x = 30.0f;
    int   i = (int)(x + 31.0f) - 31;            // floor
    float f = x - (float)i;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    union { float f; int32_t i; } u = { .f = p };
    u.i += i << 23;
    return u.f;
}

// RBJ biquad, transposed direct form II. Design at block rate (cosf/sinf), run per sample.
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
enum { BQ_LP, BQ_HP, BQ_BP, BQ_NOTCH, BQ_PEAK, BQ_LSHELF, BQ_HSHELF };
static inline void biquad_set(biquad_t *q, int type, float fc, float Q, float gain_db)
{
    fc = clampf(fc, 10.0f, 0.45f * (float)AUDIO_SR);
    float w = TWO_PI * fc / (float)AUDIO_SR, c = cosf(w), s = sinf(w), al = s / (2.0f * Q);
    float A = powf(10.0f, gain_db / 40.0f), b0, b1, b2, a0, a1, a2;
    switch (type) {
    default:
    case BQ_LP:    b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_HP:    b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_BP:    b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_NOTCH: b0 = 1; b1 = -2 * c; b2 = 1; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_PEAK:  b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; break;
    case BQ_LSHELF: { float r = 2 * sqrtf(A) * al;
        b0 = A * ((A + 1) - (A - 1) * c + r); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - r);
        a0 = (A + 1) + (A - 1) * c + r; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - r; break; }
    case BQ_HSHELF: { float r = 2 * sqrtf(A) * al;
        b0 = A * ((A + 1) + (A - 1) * c + r); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - r);
        a0 = (A + 1) - (A - 1) * c + r; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - r; break; }
    }
    float n = 1.0f / a0;
    q->b0 = b0 * n; q->b1 = b1 * n; q->b2 = b2 * n; q->a1 = a1 * n; q->a2 = a2 * n;
}
static inline float biquad_run(biquad_t *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

// Attack / decay / sustain / release, linear attack, exponential decay and release. Rates in seconds.
typedef struct { float y, a, d, s, r; int stage; } adsr_t;     // stage 0 idle, 1 attack, 2 decay/sustain, 3 release
static inline void adsr_set(adsr_t *e, float a, float d, float s, float r)
{ e->a = 1.0f / (fmaxf(a, 0.0005f) * (float)AUDIO_SR); e->d = onepole_s(fmaxf(d, 0.001f) / 4.0f); e->s = s; e->r = onepole_s(fmaxf(r, 0.001f) / 4.0f); }
static inline void  adsr_gate(adsr_t *e, bool on) { e->stage = on ? 1 : (e->stage ? 3 : 0); }
static inline float adsr_run(adsr_t *e)
{
    switch (e->stage) {
    case 1: e->y += e->a; if (e->y >= 1.0f) { e->y = 1.0f; e->stage = 2; } break;
    case 2: e->y += e->d * (e->s - e->y); break;
    case 3: e->y -= e->r * e->y; if (e->y < 1e-5f) { e->y = 0; e->stage = 0; } break;
    }
    return e->y;
}

// Band-limited saw and square (polyBLEP). dt = f / AUDIO_SR, phase in turns.
static inline float poly_blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float saw_blep(float ph, float dt) { return 2.0f * ph - 1.0f - poly_blep(ph, dt); }
static inline float square_blep(float ph, float dt)
{
    float sq = ph < 0.5f ? 1.0f : -1.0f;
    float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
    return sq + poly_blep(ph, dt) - poly_blep(p2, dt);
}

// Equal-power pan / crossfade gains for t in 0..1 (no libm: a parabola close to sin/cos).
static inline void xfade_gains(float t, float *a, float *b) { t = clampf(t, 0.0f, 1.0f); *b = t * (2.0f - t); float u = 1.0f - t; *a = u * (2.0f - u); }

// ---------------------------------------------------------------- master (front.c runs it last)
// Volume, a 10 Hz DC blocker, a peak limiter (instant attack, 80 ms release, ceiling -1 dBFS) and a soft
// knee above 0.75 that never passes 1. Below the ceiling it is a straight wire.
typedef struct { float vol_s, dcx, dcy, g, peak, gr; } master_t;
static inline float soft_knee(float x)
{
    float a = fabsf(x);
    if (a <= 0.75f) return x;
    float u = (a - 0.75f) * 4.0f, y = 0.75f + 0.25f * u / (1.0f + u);
    return x < 0 ? -y : y;
}
static inline void master_process(master_t *m, float *io, int n, float volume)
{
    const float ceil_ = 0.891f, rel = 0.00039f, dc = 0.998f;
    // the volume glides (about 50 ms to settle 63 %), so an encoder's steps sound like a fader, not a staircase
    float k = 1.0f - (float)n * (1.0f / 1600.0f); if (k < 0.0f) k = 0.0f;
    float v1 = volume + (m->vol_s - volume) * k;
    if (fabsf(v1 - volume) < 1e-4f) v1 = volume;
    float v0 = m->vol_s, dv = (v1 - v0) / (float)n, pk = 0, gmin = 1.0f;
    m->vol_s = v1;
    if (m->g <= 0.0f || m->g > 1.0f) m->g = 1.0f;
    for (int i = 0; i < n; i++) {
        float x = io[i] * (v0 + dv * (float)i);
        float y = x - m->dcx + dc * m->dcy; m->dcx = x; m->dcy = flush_denormal(y);
        float a = fabsf(y), want = a > ceil_ ? ceil_ / a : 1.0f;
        if (want < m->g) m->g = want; else m->g += rel * (want - m->g);
        if (m->g < gmin) gmin = m->g;
        float o = soft_knee(y * m->g);
        io[i] = o;
        if (fabsf(o) > pk) pk = fabsf(o);
    }
    if (pk > m->peak) m->peak = pk;
    m->gr = gmin;
}

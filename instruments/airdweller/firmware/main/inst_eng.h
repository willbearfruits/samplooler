// AIRDWELLER's audio task inside: inst_engine.c (the queues, the grid, the loops, the bus) calls the kit (inst_drums.c),
// the material (inst_material.c) and the notes (inst_notes.c). Everything here runs in the audio task, except the init
// calls.
#pragma once
#include "inst_ad.h"
#include "dsp.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

// 1 - exp(-n / (t * SR)) without libm: a one-pole's step over n samples at a time constant t (DWELL's)
static inline float kblock(float t_s, int n)
{
    float x = (float)n / (t_s * (float)AUDIO_SR);
    if (x < 0.15f) return x - 0.5f * x * x + x * x * x * (1.0f / 6.0f);
    return 1.0f - fast_exp2(-1.442695f * x);
}
// exp(-x) for 0 <= x < 0.05 to a part in 10^9: a decay per sample close to 1, where fast_exp2's 0.03 % is too coarse
static inline float decay_of(float x) { return 1.0f - x + x * x * (0.5f - x * (1.0f / 6.0f)); }
// the per-sample decay for a time constant in seconds
static inline float tau_k(float t_s) { return decay_of(1.0f / (t_s * (float)AUDIO_SR)); }
static inline float flushf(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

// No libm in the audio task (libm lives in flash): these instead, at a hit or a block.
// sin and cos of 2 pi w, 0 <= w <= 0.5, to a few parts in 10^6 (sin_turns is 3 % off near 0: no good for an angle)
static inline void sincos_turns(float w, float *s, float *c)
{
    float th = TWO_PI * w, sg = 1.0f;
    if (th > 1.5707963f) { th = 3.14159265f - th; sg = -1.0f; }
    float t2 = th * th;
    *s = th * (1.0f - t2 * (1.0f / 6.0f) * (1.0f - t2 * (1.0f / 20.0f) * (1.0f - t2 * (1.0f / 42.0f) * (1.0f - t2 * (1.0f / 72.0f)))));
    *c = sg * (1.0f - t2 * 0.5f * (1.0f - t2 * (1.0f / 12.0f) * (1.0f - t2 * (1.0f / 30.0f) * (1.0f - t2 * (1.0f / 56.0f) * (1.0f - t2 * (1.0f / 90.0f))))));
}
// 1 / sqrt(x) (the bit trick and two Newton steps: a few parts in 10^6), sqrt(x)
static inline float rsqrt_f(float x)
{
    union { float f; uint32_t i; } u = { .f = x };
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    y *= 1.5f - 0.5f * x * y * y;
    y *= 1.5f - 0.5f * x * y * y;
    return y;
}
static inline float sqrt_f(float x) { return x > 1e-30f ? x * rsqrt_f(x) : 0.0f; }
// log2 x for x > 0, to about 10^-6 (the exponent, then 2 atanh of the mantissa's (m - 1) / (m + 1))
static inline float log2_f(float x)
{
    union { float f; uint32_t i; } u = { .f = x };
    float e = (float)((int)((u.i >> 23) & 255) - 127);
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    float m = u.f, z = (m - 1.0f) / (m + 1.0f), z2 = z * z;
    return e + 2.8853901f * z * (1.0f + z2 * (0.33333333f + z2 * (0.2f + z2 * (0.14285714f + z2 * 0.11111111f))));
}
// sin(pi/2 u) without a branch (sin_turns branches on the sign, which mispredicts half the time on audio): the folder
static inline float fold_sin(float u)
{
    float p = 0.25f * u + 256.0f;
    p -= (float)(int)p;
    float x = 2.0f * p - 1.0f, y = 4.0f * x * (1.0f - fabsf(x));
    y = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}
// the SVF's coefficients from a cutoff, tan by a Pade approximant (0.8 % at 0.4 SR): a sweep, or a filter set at a hit
static inline void svf_fc(svf_t *f, float fc)
{
    fc = clampf(fc, 20.0f, 0.4f * (float)AUDIO_SR);
    float x = 3.14159265f * fc / (float)AUDIO_SR, x2 = x * x;
    float g = x * (15.0f - x2) / (15.0f - 6.0f * x2);
    f->a1 = 1.0f / (1.0f + g * (g + f->k)); f->a2 = g * f->a1; f->a3 = g * f->a2;
}

// ---------------------------------------------------------------- the drag (inst_ad.h's DRAG), as each voice follows it
// The engine reads eng_drag_* once a block into an edrag_t, scaled by eng_drag_depth. Each voice keeps the gesture that
// started it (gid) and a follower: while its gid is the one dragged it takes the drag as its target, otherwise it keeps
// the amount and the bend it had and lets the hold go. The amount and the bend move through a spring (6.5 Hz, damping
// 0.6: a pull overshoots a little and settles, rubber rather than a knob); the hold glides (15 ms up, 60 ms down).
typedef struct { int gid; float amt, hold, bend; } edrag_t;     // gid 0: none; amt, hold 0..1; bend semitones
typedef struct { uint8_t gid, live; float ta, tb, th, a, av, b, bv, h; } dfol_t;
static inline void dfol_init(dfol_t *f, int gid) { *f = (dfol_t){ 0 }; f->gid = (uint8_t)gid; }
// one block: true when anything moved (the voice works its drag out again)
static inline bool dfol_run(dfol_t *f, const edrag_t *d, int n)
{
    if (!f->gid) return false;
    if (f->gid == d->gid) { f->ta = d->amt; f->tb = d->bend; f->th = d->hold; f->live = 1; }
    else f->th = 0.0f;
    if (!f->live) return false;
    if (f->a == f->ta && f->av == 0.0f && f->b == f->tb && f->bv == 0.0f && f->h == f->th) {   // at rest: nothing to do
        if (f->gid != d->gid) f->live = 0;
        return false;
    }
    const float dt = (float)n * (1.0f / (float)AUDIO_SR), w2 = 1668.0f, zw = 49.0f;   // (2 pi 6.5)^2, 2 x 0.6 x 2 pi 6.5
    f->av += (w2 * (f->ta - f->a) - zw * f->av) * dt; f->a += f->av * dt;
    f->bv += (w2 * (f->tb - f->b) - zw * f->bv) * dt; f->b += f->bv * dt;
    f->h += kblock(f->th > f->h ? 0.015f : 0.06f, n) * (f->th - f->h);
    bool rest = fabsf(f->ta - f->a) < 1e-4f && fabsf(f->av) < 1e-3f && fabsf(f->tb - f->b) < 1e-3f && fabsf(f->bv) < 1e-2f && fabsf(f->th - f->h) < 1e-4f;
    if (rest) { f->a = f->ta; f->av = 0.0f; f->b = f->tb; f->bv = 0.0f; f->h = f->th; }
    return true;
}
// what a hold does to a decay: its rate times 2^-6h (held: 64 times longer)
static inline float hold_rate(float h) { return fast_exp2(-6.0f * clampf(h, 0.0f, 1.0f)); }

// ---------------------------------------------------------------- the kit (inst_drums.c)
void kit_init(void);
void kit_hit(int voice, int vel, float tune_st, int delay, int gid);   // delay: samples into this block; gid: the gesture
int  kit_run(float *acc, int n, float squeeze, const edrag_t *dg);     // adds every voice into acc; returns how many sound
void kit_silence(void);

// ---------------------------------------------------------------- the material (inst_material.c)
void mat_init(void);
void mat_strike_pad(int pad, int vel, int delay, int gid);    // PAINT: this box's own swing (gid: its gesture, 0 a loop's)
void mat_strike_voice(int voice, int vel, int delay);         // the other hand's hit (DV_*): it also ducks the bow
void mat_run(float *acc, int n, bool on, const edrag_t *dg);   // adds the material into acc (on: PAINT or PUMP)
void mat_silence(void);

// ---------------------------------------------------------------- the notes (inst_notes.c)
// ev: this block's note events in the order they came, each at its sample (off); the voices render between them.
typedef struct { nev_t e; int off; } nblk_t;
void notes_init(void);
int  notes_run(float *acc, int n, const nblk_t *ev, int ne, const edrag_t *dg);   // adds the notes into acc; returns how many sound
void notes_silence(void);
extern float notes_pk;                                        // the last block's peak (the engine reports eng_note_lvl)

// AIRDWELLER's audio task inside: inst_engine.c (the queues, the grid, the loops, the bus) calls the kit (inst_drums.c)
// and the material (inst_material.c). Everything here runs in the audio task, except the two init calls.
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
// the SVF's coefficients from a cutoff, tan by a Pade approximant (0.8 % at 0.4 SR): a sweep, or a filter set at a hit
static inline void svf_fc(svf_t *f, float fc)
{
    fc = clampf(fc, 20.0f, 0.4f * (float)AUDIO_SR);
    float x = 3.14159265f * fc / (float)AUDIO_SR, x2 = x * x;
    float g = x * (15.0f - x2) / (15.0f - 6.0f * x2);
    f->a1 = 1.0f / (1.0f + g * (g + f->k)); f->a2 = g * f->a1; f->a3 = g * f->a2;
}

// ---------------------------------------------------------------- the kit (inst_drums.c)
void kit_init(void);
void kit_hit(int voice, int vel, float tune_st, int delay);   // delay: samples into this block
int  kit_run(float *acc, int n, float squeeze);               // adds every voice into acc; returns how many sound
void kit_silence(void);

// ---------------------------------------------------------------- the material (inst_material.c)
void mat_init(void);
void mat_strike_pad(int pad, int vel, int delay);             // PAINT: this box's own swing
void mat_strike_voice(int voice, int vel, int delay);         // the other hand's hit (DV_*): it also ducks the bow
void mat_run(float *acc, int n, bool on);                     // adds the material into acc (on: PAINT or PUMP)
void mat_silence(void);

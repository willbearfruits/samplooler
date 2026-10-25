// AIRDWELLER's kit: eight drums made from nothing (inst_ad.h names them), twelve voices, a voice taken from the
// quietest with a 3 ms fade (a ghost copy plays it out), one KICK and one SUB at a time (a new one takes over from the
// last in 5 ms). Each voice renders about +-1, then SQUEEZE folds it (a sine folder, pushed harder on the transient and
// by velocity, its negative side folded less on the attack for even harmonics), then its level. Pitches and decays are
// set at the hit; the squeeze glides across the block. Audio task only (kit_init: UI task, before the audio starts).
#include "inst_eng.h"
#include <string.h>

#define NV   12                       // voices
#define NGH  2                        // ghosts: stolen voices fading out
#define NMD  8                        // modes a voice may ring
#define SR   ((float)AUDIO_SR)
#define ISR  (1.0f / (float)AUDIO_SR)

typedef struct {
    uint8_t type, on;
    int   delay, t, hold, cnt;
    float amp, vn, lvl;               // the level, the velocity 0..1, last block's peak (for stealing)
    float sqg, sqt, te, kte;          // the squeeze's sustained and transient fold, the transient
    float e[6], k[6];                 // envelopes, multiplied by k each sample
    float ph[4];
    float hz, hz2, a;
    float fade, dfade;                // a ghost fading out
    float z1;
    uint32_t rng;
    svf_t f;
    int   nm;
    float c1[NMD], c2[NMD], y1[NMD], y2[NMD], g[NMD], bi[NMD];
} dv_t;

static dv_t V[NV + NGH];
// the modal voices' modes: ratios over the lowest, gains, T60s over the lowest's
static const float CLANG_R[8] = { 1.000f, 1.504f, 1.836f, 2.416f, 2.749f, 3.231f, 3.982f, 4.833f };   // a plate
static const float CLANG_G[8] = { 1.0f, 0.9f, 0.75f, 0.8f, 0.65f, 0.55f, 0.5f, 0.42f };
static const float CLANG_D[8] = { 1.0f, 0.85f, 0.75f, 0.62f, 0.55f, 0.46f, 0.38f, 0.32f };
static const float HAT_R[8]   = { 1.000f, 1.483f, 1.800f, 2.546f, 2.630f, 3.073f, 3.420f, 3.897f };   // the 808's, and two more
static const float CREAK_R[8] = { 1.000f, 1.593f, 2.135f, 2.653f, 3.155f, 3.650f, 4.061f, 4.802f };
static const float CREAK_G[8] = { 1.0f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.35f, 0.3f };
static float sq_last;
static uint32_t seed = 0x2545f491u;

// ---------------------------------------------------------------- pieces

// a mode: a two-pole resonator at f, T60 t60; g its output gain, b its input gain (an impulse of 1 rings at 1)
static void mode_set(dv_t *v, int k, float f, float t60, float g, float b)
{
    float S, C, r = decay_of(6.9078f / (fmaxf(t60, 0.005f) * SR));
    sincos_turns(clampf(f, 20.0f, 0.42f * SR) * ISR, &S, &C);
    v->c1[k] = 2.0f * r * C; v->c2[k] = -r * r; v->g[k] = g; v->bi[k] = b * S; v->y1[k] = v->y2[k] = 0.0f;
}
static inline void mode_strike(dv_t *v, int k, float a) { v->y1[k] += a * v->bi[k]; }
// sin(pi/2 u) without a branch (sin_turns branches on the sign, which mispredicts half the time on audio): the folder
static inline float fold_sin(float u)
{
    float p = 0.25f * u + 256.0f;
    p -= (float)(int)p;
    float x = 2.0f * p - 1.0f, y = 4.0f * x * (1.0f - fabsf(x));
    y = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}
static inline float logf_k(float k) { return -(1.0f - k) * (1.0f + 0.5f * (1.0f - k)); }   // ln k for a decay close to 1
// the modes, four at a time (four independent recursions overlap; on the host they also share a vector register): x is
// their input (NULL: none), y gets their sum. Every voice with modes has eight (unused ones have no gain).
static IRAM_ATTR __attribute__((noinline)) void modes_run(dv_t *v, const float *restrict x, float *restrict y, int n)
{
    for (int k = 0; k < NMD; k += 4) {
        float A[4], B[4], P[4], Q[4], G[4], I[4];
        for (int j = 0; j < 4; j++) { A[j] = v->c1[k + j]; B[j] = v->c2[k + j]; P[j] = v->y1[k + j]; Q[j] = v->y2[k + j]; G[j] = v->g[k + j]; I[j] = v->bi[k + j]; }
        if (x) {
            for (int i = 0; i < n; i++) {
                float xi = x[i], o = 0.0f;
                for (int j = 0; j < 4; j++) { float t = A[j] * P[j] + B[j] * Q[j] + I[j] * xi; Q[j] = P[j]; P[j] = t; o += G[j] * t; }
                y[i] += o;
            }
        } else {
            for (int i = 0; i < n; i++) {
                float o = 0.0f;
                for (int j = 0; j < 4; j++) { float t = A[j] * P[j] + B[j] * Q[j]; Q[j] = P[j]; P[j] = t; o += G[j] * t; }
                y[i] += o;
            }
        }
        for (int j = 0; j < 4; j++) { v->y1[k + j] = flushf(P[j]); v->y2[k + j] = flushf(Q[j]); }
    }
}

// ---------------------------------------------------------------- the voices, each about +-1
// KICK (rubber): a sine that falls from about 4 kHz to the note, two exponentials in octaves (a 1.6 ms click, an 11 ms
// body) so it lands within 3 % by 40 ms; a hold, then the sub's tail; a puff of differentiated noise on the click.
static void run_kick(dv_t *v, float *y, int n)
{
    float e0 = v->e[0], e1 = v->e[1], e2 = v->e[2], e3 = v->e[3], ph = v->ph[0], hz = v->hz * ISR, z = v->z1;
    const float k0 = v->k[0], k1 = v->k[1], k2 = v->k[2], k3 = v->k[3];
    int t = v->t, hold = v->hold;
    if (e0 + e1 < 1e-5f && e3 < 1e-6f && t >= hold) {                // landed: the sub alone
        for (int i = 0; i < n; i++) { ph += hz; if (ph >= 1.0f) ph -= 1.0f; e2 *= k2; y[i] = sin_turns(ph) * e2; }
        v->e[2] = e2; v->ph[0] = ph; v->t = t + n;
        return;
    }
    for (int i = 0; i < n; i++) {
        ph += hz * fast_exp2(e0 + e1); if (ph >= 1.0f) ph -= 1.0f;
        float a = 1.0f;
        if (t >= hold) { e2 *= k2; a = e2; }
        float nz = rng_bi(&v->rng), c = (nz - z) * e3; z = nz;
        y[i] = sin_turns(ph) * a + 0.3f * c;
        e0 *= k0; e1 *= k1; e3 *= k3; t++;
    }
    v->e[0] = flushf(e0); v->e[1] = flushf(e1); v->e[2] = e2; v->e[3] = flushf(e3); v->ph[0] = ph; v->z1 = z; v->t = t;
}
// SNAP (latex): a 2 ms crack of bright noise, noise through a resonant band pass whipped down from 6 kHz to 1.5 kHz in a
// few ms, a short pitched pop that drops into its note, and a squeak: a sine chirping up from 2.4 to 4.3 kHz, faded in
// over 2 ms.
static void run_snap(dv_t *v, float *y, int n)
{
    float e0 = v->e[0], e1 = v->e[1], e2 = v->e[2], e3 = v->e[3], e4 = v->e[4], e5 = v->e[5];
    const float k0 = v->k[0], k1 = v->k[1], k2 = v->k[2], k3 = v->k[3], k4 = v->k[4], k5 = v->k[5];
    float p0 = v->ph[0], p1 = v->ph[1], hz = v->hz * ISR, sq = 4300.0f * v->a * ISR, sqi = v->hz2;
    int t = v->t;
    for (int i = 0; i < n; i++) {
        if ((t & 7) == 0) { svf_fc(&v->f, 1500.0f * v->a * fast_exp2(2.0f * e0)); sqi = sq * fast_exp2(-0.84f * e4); }
        float nz = rng_bi(&v->rng), bp = svf_bp(&v->f, nz, NULL), hp = nz - v->z1; v->z1 = nz;
        p0 += hz * (1.0f + 1.2f * e2); if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += sqi; if (p1 >= 1.0f) p1 -= 1.0f;
        float att = t < 64 ? (float)t * (1.0f / 64.0f) : 1.0f;
        float ck = t < 64 ? 1.0f - (float)t * (1.0f / 64.0f) : 0.0f;
        y[i] = 3.2f * bp * e1 + 0.3f * hp * ck + 0.4f * sin_turns(p0) * e3 + 0.22f * sin_turns(p1) * e5 * att;
        e0 *= k0; e1 *= k1; e2 *= k2; e3 *= k3; e4 *= k4; e5 *= k5; t++;
    }
    v->e[0] = flushf(e0); v->e[1] = flushf(e1); v->e[2] = flushf(e2); v->e[3] = flushf(e3); v->e[4] = flushf(e4); v->e[5] = flushf(e5);
    v->ph[0] = p0; v->ph[1] = p1; v->hz2 = sqi; v->t = t;
    v->f.ic1 = flushf(v->f.ic1); v->f.ic2 = flushf(v->f.ic2);
}
// the modal voices: an exciter (noise bursts, slips) into the modes, plus what the exciter says straight out
// CLANG (a struck plate): eight inharmonic modes struck at once, harder on the upper ones with velocity, a 1 ms noise
// contact into them and straight out (the clank).
// HAT: eight high modes (the 808's square-wave ratios and two more, high up) rung by a 2 ms noise burst, and noise
// through a band pass.
// CREAK (friction): stick-slip, a jittered train of slips at 24..60 Hz whose rate rises and falls (a groan) through
// eight short metal modes, each slip with a dry tick: a mechanical animal.
static void run_modal(dv_t *v, float *y, int n)
{
    float x[AUDIO_BLOCK], d[AUDIO_BLOCK];
    bool exc = false;
    float e0 = v->e[0];
    const float k0 = v->k[0];
    int t = v->t;
    if (v->type == DV_CLANG || v->type == DV_HAT) {
        if (e0 > 1e-4f) {
            exc = true;
            float z = v->z1;
            for (int i = 0; i < n; i++) {
                float nz = rng_bi(&v->rng);
                x[i] = nz * e0;
                float hp = nz - z; z = nz;
                d[i] = v->type == DV_CLANG ? 0.45f * hp * e0 : 0.0f;
                e0 *= k0;
            }
            v->z1 = z;
        } else e0 = 0.0f;
        if (v->type == DV_HAT) {                                      // the hat's noise: its own band pass, its own decay
            float e1 = v->e[1]; const float k1 = v->k[1];
            for (int i = 0; i < n; i++) { d[i] = 0.9f * svf_bp(&v->f, rng_bi(&v->rng), NULL) * e1; e1 *= k1; }
            v->e[1] = e1; v->f.ic1 = flushf(v->f.ic1); v->f.ic2 = flushf(v->f.ic2);
        }
    } else {                                                          // CREAK
        exc = true;
        float e1 = v->e[1], e2 = v->e[2], e3 = v->e[3], e4 = v->e[4];
        const float k1 = v->k[1], k2 = v->k[2], k3 = v->k[3], k4 = v->k[4];
        int cnt = v->cnt, tick = v->hold;
        for (int i = 0; i < n; i++) {
            x[i] = d[i] = 0.0f;
            if (--cnt <= 0) {
                float life = (1.0f - e0) * e1, rate = 24.0f * fast_exp2(2.4f * (e2 - e3));
                cnt = (int)(SR / rate * (0.5f + rng_uni(&v->rng))) + 1;
                x[i] = life * (0.55f + 0.45f * rng_uni(&v->rng));
                e4 = life; tick = 64;
            }
            if (tick > 0) { tick--; d[i] = 0.25f * rng_bi(&v->rng) * e4; e4 *= k4; }
            e0 *= k0; e1 *= k1; e2 *= k2; e3 *= k3;
        }
        v->cnt = cnt; v->hold = tick; v->e[1] = e1; v->e[2] = e2; v->e[3] = e3; v->e[4] = flushf(e4);
    }
    v->e[0] = e0;
    if (exc || v->type == DV_HAT) for (int i = 0; i < n; i++) y[i] = d[i];
    else for (int i = 0; i < n; i++) y[i] = 0.0f;
    modes_run(v, exc ? x : NULL, y, n);
    v->t = t + n;
}
// GLASS: an FM bell (the modulator at sqrt 2 of the carrier: inharmonic), its index falling from 4 to 0.5, a pure
// partial at 2.756 (a glass bar's), and a wet blip: a sine rising from 700 Hz to 2.6 kHz, a drop of water. The
// modulator is a lossless two-pole oscillator, the partial a struck resonator (modes 0 and 1); the blip runs while it lasts.
static void run_glass(dv_t *v, float *y, int n)
{
    float e0 = v->e[0], e1 = v->e[1], e3 = v->e[3], e4 = v->e[4], p0 = v->ph[0], p3 = v->ph[3], fbi = v->hz2;
    const float k0 = v->k[0], k1 = v->k[1], k3 = v->k[3], k4 = v->k[4], fc = v->hz * ISR, fb = 2600.0f * v->a * ISR;
    float mc = v->c1[0], m1 = v->y1[0], m2 = v->y2[0], pc1 = v->c1[1], pc2 = v->c2[1], q1 = v->y1[1], q2 = v->y2[1];
    int t = v->t;
    if (e4 > 1e-4f) {
        for (int i = 0; i < n; i++) {
            float m = mc * m1 - m2; m2 = m1; m1 = m;
            float q = pc1 * q1 + pc2 * q2; q2 = q1; q1 = q;
            p0 += fc; if (p0 >= 1.0f) p0 -= 1.0f;
            if ((t & 7) == 0) fbi = fb * fast_exp2(-1.9f * e3);
            p3 += fbi; if (p3 >= 1.0f) p3 -= 1.0f;
            float att = t < 32 ? (float)t * (1.0f / 32.0f) : 1.0f;
            y[i] = 0.62f * sin_turns(p0 + 1.0f + (0.5f + 3.5f * e0) * 0.15915494f * m) * e1 + 0.22f * q + 0.45f * sin_turns(p3) * e4 * att;
            e0 *= k0; e1 *= k1; e3 *= k3; e4 *= k4; t++;
        }
    } else {
        e4 = 0.0f;
        for (int i = 0; i < n; i++) {
            float m = mc * m1 - m2; m2 = m1; m1 = m;
            float q = pc1 * q1 + pc2 * q2; q2 = q1; q1 = q;
            p0 += fc; if (p0 >= 1.0f) p0 -= 1.0f;
            y[i] = 0.62f * sin_turns(p0 + 1.0f + (0.5f + 3.5f * e0) * 0.15915494f * m) * e1 + 0.22f * q;
            e0 *= k0; e1 *= k1;
        }
        t += n;
    }
    v->e[0] = flushf(e0); v->e[1] = e1; v->e[3] = flushf(e3); v->e[4] = e4;
    v->y1[0] = m1; v->y2[0] = m2; v->y1[1] = flushf(q1); v->y2[1] = flushf(q2);
    v->ph[0] = p0; v->ph[3] = p3; v->hz2 = fbi; v->t = t;
}
// SUB: a sine an octave over the kick's note falling 1.6 octaves (0.4 s), a 4 ms attack, a long tail, rounded by a soft clip
static void run_sub(dv_t *v, float *y, int n)
{
    float e0 = v->e[0], e1 = v->e[1], ph = v->ph[0], hz = v->hz * ISR;
    const float k1 = v->k[1];
    float e0n = e0 * fast_exp2(1.442695f * (float)n * logf_k(v->k[0]));
    float f = hz * fast_exp2(-1.6f * (1.0f - e0)), df = (hz * fast_exp2(-1.6f * (1.0f - e0n)) - f) / (float)n;
    int t = v->t;
    for (int i = 0; i < n; i++) {
        ph += f; f += df; if (ph >= 1.0f) ph -= 1.0f;
        float a = t < 128 ? (float)t * (1.0f / 128.0f) : (e1 *= k1);
        y[i] = 1.04f * soft_tanh(1.7f * sin_turns(ph)) * a;
        t++;
    }
    v->e[0] = e0n; v->e[1] = e1; v->ph[0] = ph; v->t = t;
}
// WHIP: noise through a band pass swept up from 500 Hz to 6.8 kHz in about 8 ms, then the crack: a doublet and a
// 0.3 ms splinter of noise as the tip turns.
#define CRACK_T 208
static void run_whip(dv_t *v, float *y, int n)
{
    float e0 = v->e[0], e1 = v->e[1], e2 = v->e[2];
    const float k0 = v->k[0], k1 = v->k[1], k2 = v->k[2];
    int t = v->t;
    for (int i = 0; i < n; i++) {
        if ((t & 3) == 0) svf_fc(&v->f, 6800.0f * v->a * fast_exp2(-3.9f * e0));
        float att = t < 32 ? (float)t * (1.0f / 32.0f) : 1.0f;
        float c = t == CRACK_T ? 0.9f : t == CRACK_T + 1 ? -0.9f : 0.0f;
        if (t == CRACK_T) e2 = 1.0f;
        y[i] = 1.5f * svf_bp(&v->f, rng_bi(&v->rng), NULL) * att * e1 + c + 0.5f * rng_bi(&v->rng) * e2;
        e0 *= k0; if (t >= 32) e1 *= k1; e2 *= k2; t++;
    }
    v->e[0] = flushf(e0); v->e[1] = flushf(e1); v->e[2] = flushf(e2); v->t = t;
    v->f.ic1 = flushf(v->f.ic1); v->f.ic2 = flushf(v->f.ic2);
}

// ---------------------------------------------------------------- the squeeze, the level, a ghost's fade
// SQUEEZE: y (about +-1) crossfaded towards a sine folder, sin(pi/2 . x g), g = 1 + SQUEEZE x (the voice's sustained
// fold + its transient fold x the transient), more with velocity; the negative side through less gain while the
// transient lasts (even harmonics). The rubbery voices (KICK, SUB, SNAP, WHIP) are folded all along; the ringing ones
// (CLANG, HAT, GLASS, CREAK) only on their attack: how much of the fold is heard falls with the transient, so the ring
// is left as it is. Both stay within the voice's level. The gain, the asymmetry and the share ramp across the block
// (the transient is worked out at the block's two ends).
static float post(dv_t *v, const float *restrict y, float *restrict acc, int i0, int n, float sq0, float sq1, bool ghost)
{
    const int m = n - i0;
    const float df = ghost ? v->dfade : 0.0f, vg = 0.5f + 0.5f * v->vn, amp = v->amp, im = 1.0f / (float)m;
    const float te0 = v->te, te1 = te0 * fast_exp2(1.442695f * (float)m * logf_k(v->kte));
    const float qa = sq0 + (sq1 - sq0) * (float)i0 / (float)n, gs = v->sqg * vg, gt = v->sqt * vg;
    const bool ringing = v->sqg <= 0.0f;
    float g = 1.0f + qa * (gs + gt * te0), g1 = 1.0f + sq1 * (gs + gt * te1);
    float a = 0.2f * qa * te0, a1 = 0.2f * sq1 * te1;
    float B = fminf(qa * (1.0f / 0.35f), 1.0f) * amp, B1 = fminf(sq1 * (1.0f / 0.35f), 1.0f) * amp;
    if (ringing) { B *= fminf(4.0f * te0, 1.0f); B1 *= fminf(4.0f * te1, 1.0f); }
    const float dg = (g1 - g) * im, da = (a1 - a) * im, dB = (B1 - B) * im;
    float pk = 0.0f, f = v->fade;
    v->te = flushf(te1);
    if (B > 1e-4f * amp || B1 > 1e-4f * amp) {                         // amp (x + share (fold - x))
        float A = amp - B;
        for (int i = i0; i < n; i++) {
            float x = y[i];
            pk = fmaxf(pk, fabsf(x));
            float o = A * x + B * fold_sin(x * g * (1.0f - a + a * copysignf(1.0f, x)));
            if (ghost) { o *= f; f = fmaxf(f - df, 0.0f); }
            acc[i] += o;
            g += dg; a += da; B += dB; A -= dB;
        }
    } else {
        for (int i = i0; i < n; i++) {
            float x = y[i];
            pk = fmaxf(pk, fabsf(x));
            float o = amp * x;
            if (ghost) { o *= f; f = fmaxf(f - df, 0.0f); }
            acc[i] += o;
        }
    }
    v->fade = f;
    return pk;
}

// ---------------------------------------------------------------- hits
static void ghost_of(int j, float ms)
{
    dv_t *v = &V[j];
    if (!v->on || v->delay > 0) { v->on = 0; return; }               // not started: nothing to fade
    int g = NV;
    for (int k = NV; k < NV + NGH; k++) { if (!V[k].on) { g = k; break; } if (V[k].fade < V[g].fade) g = k; }
    V[g] = *v;
    V[g].fade = 1.0f; V[g].dfade = 1.0f / (ms * 0.001f * SR);
    v->on = 0;
}

void kit_init(void) { memset(V, 0, sizeof V); sq_last = 0.0f; }
void kit_silence(void) { for (int j = 0; j < NV + NGH; j++) V[j].on = 0; }

void kit_hit(int voice, int vel, float tune_st, int delay)
{
    if (voice < 0 || voice >= DV_OFF || vel <= 0) return;
    int k = -1;
    if (voice == DV_KICK || voice == DV_SUB)                          // one at a time: the last one fades in 5 ms
        for (int j = 0; j < NV; j++) if (V[j].on && V[j].type == voice) { ghost_of(j, 5.0f); k = j; break; }
    if (k < 0) for (int j = 0; j < NV; j++) if (!V[j].on) { k = j; break; }
    if (k < 0) {                                                      // all busy: the quietest goes, in 3 ms
        float q = 1e9f; k = 0;
        for (int j = 0; j < NV; j++) { float l = V[j].lvl * V[j].amp; if (V[j].delay > 0) l = 1e8f; if (l < q) { q = l; k = j; } }
        ghost_of(k, 3.0f);
    }
    dv_t *v = &V[k];
    memset(v, 0, sizeof *v);
    float vn = clampf((float)vel / 127.0f, 0.0f, 1.0f), tn = fast_exp2(clampf(tune_st, -24.0f, 24.0f) * (1.0f / 12.0f));
    float dc = clampf(eng_drum_decay, 0.25f, 4.0f);
    v->type = (uint8_t)voice; v->on = 1; v->delay = delay; v->vn = vn; v->lvl = 1.0f;
    v->amp = vn * (0.35f + 0.65f * vn);
    v->rng = (seed = seed * 1664525u + 1013904223u) | 1u;
    v->te = 1.0f; v->kte = tau_k(0.02f);
    for (int i = 0; i < 6; i++) v->k[i] = 1.0f;
    switch (voice) {
    case DV_KICK:
        v->hz = clampf(eng_kick_hz * tn, 25.0f, 200.0f);
        v->e[0] = 4.2f; v->k[0] = tau_k(0.0016f);
        v->e[1] = 2.0f; v->k[1] = tau_k(0.011f);
        v->e[2] = 1.0f; v->k[2] = tau_k(0.17f * dc);
        v->e[3] = 1.0f; v->k[3] = tau_k(0.0006f);
        v->hold = (int)(0.02f * fminf(dc, 1.5f) * SR);
        v->amp *= 0.5f; v->sqg = 1.3f; v->sqt = 4.0f; v->kte = tau_k(0.018f);
        break;
    case DV_SNAP:
        v->a = tn; v->hz = 330.0f * tn;
        v->f.k = 1.0f / 3.0f; svf_fc(&v->f, 6000.0f * tn);
        v->e[0] = 1.0f; v->k[0] = tau_k(0.007f);
        v->e[1] = 1.0f; v->k[1] = tau_k(0.03f * dc);
        v->e[2] = 1.0f; v->k[2] = tau_k(0.005f);
        v->e[3] = 1.0f; v->k[3] = tau_k(0.011f * dc);
        v->e[4] = 1.0f; v->k[4] = tau_k(0.015f);
        v->e[5] = 1.0f; v->k[5] = tau_k(0.022f * dc);
        v->amp *= 0.3f; v->sqg = 1.0f; v->sqt = 3.0f; v->kte = tau_k(0.012f);
        break;
    case DV_CLANG: {
        float hz = 415.0f * tn;
        v->nm = 8;
        for (int i = 0; i < 8; i++) {
            mode_set(v, i, hz * CLANG_R[i], 1.5f * dc * CLANG_D[i], CLANG_G[i] * 0.2f, 0.25f);
            mode_strike(v, i, 4.0f * (1.0f - (1.0f - vn) * 0.09f * (float)i));
        }
        v->e[0] = 1.0f; v->k[0] = tau_k(0.0012f);
        v->amp *= 0.3f; v->sqg = 0.0f; v->sqt = 3.0f; v->kte = tau_k(0.012f);
        break;
    }
    case DV_HAT: {
        float hz = 2900.0f * tn;
        v->nm = 8;
        for (int i = 0; i < 8; i++) mode_set(v, i, hz * HAT_R[i], 0.1f * dc * (1.0f - 0.05f * (float)i), 0.18f, 2.0f);
        v->e[0] = 1.0f; v->k[0] = tau_k(0.002f);
        v->e[1] = 1.0f; v->k[1] = tau_k(0.022f * dc);
        v->f.k = 1.0f / 0.9f; svf_fc(&v->f, clampf(8500.0f * tn, 2000.0f, 12800.0f));
        v->amp *= 0.2f; v->sqg = 0.0f; v->sqt = 2.5f; v->kte = tau_k(0.008f);
        break;
    }
    case DV_GLASS: {
        v->a = tn; v->hz = 1568.0f * tn;
        float sm, cm; sincos_turns(fminf(v->hz * 1.4142136f, 0.45f * SR) * ISR, &sm, &cm);
        v->c1[0] = 2.0f * cm; v->y1[0] = sm; v->y2[0] = 0.0f;           // the modulator: sin((n + 1) w)
        mode_set(v, 1, v->hz * 2.756f, 1.243f * dc, 1.0f, 1.0f); mode_strike(v, 1, 1.0f);
        v->e[0] = 1.0f; v->k[0] = tau_k(0.03f);
        v->e[1] = 1.0f; v->k[1] = tau_k(0.32f * dc);
        v->e[3] = 1.0f; v->k[3] = tau_k(0.009f);
        v->e[4] = 1.0f; v->k[4] = tau_k(0.015f);
        v->amp *= 0.26f; v->sqg = 0.0f; v->sqt = 3.0f; v->kte = tau_k(0.015f);
        break;
    }
    case DV_SUB:
        v->hz = clampf(eng_kick_hz * 2.0f * tn, 30.0f, 320.0f);
        v->e[0] = 1.0f; v->k[0] = tau_k(0.4f);
        v->e[1] = 1.0f; v->k[1] = tau_k(0.55f * dc);
        v->amp *= 0.26f; v->sqg = 1.0f; v->sqt = 1.5f; v->kte = tau_k(0.05f);
        break;
    case DV_WHIP:
        v->a = tn;
        v->f.k = 0.25f; svf_fc(&v->f, 500.0f * tn);
        v->e[0] = 1.0f; v->k[0] = tau_k(0.0024f);
        v->e[1] = 1.0f; v->k[1] = tau_k(0.014f * dc);
        v->e[2] = 0.0f; v->k[2] = tau_k(0.0003f);
        v->amp *= 0.3f; v->sqg = 0.8f; v->sqt = 3.0f; v->kte = tau_k(0.01f);
        break;
    case DV_CREAK: {
        float hz = 560.0f * tn;
        v->nm = 8;
        for (int i = 0; i < 8; i++) mode_set(v, i, hz * CREAK_R[i], 0.1f * (1.0f - 0.08f * (float)i), CREAK_G[i] * 0.33f, 1.0f);
        v->e[0] = 1.0f; v->k[0] = tau_k(0.025f);                      // the life: its attack ...
        v->e[1] = 1.0f; v->k[1] = tau_k(0.22f * dc);                  // ... and its decay
        v->e[2] = 1.0f; v->k[2] = tau_k(0.3f);                        // the groan: the slips' rate rises and falls
        v->e[3] = 1.0f; v->k[3] = tau_k(0.06f);
        v->k[4] = tau_k(0.0004f);                                     // each slip's tick
        v->cnt = 1;
        v->amp *= 0.32f; v->sqg = 0.0f; v->sqt = 3.0f; v->kte = tau_k(0.02f);
        break;
    }
    default: v->on = 0; break;
    }
}

// every voice into acc; the squeeze glides from the last block's to this one's
int kit_run(float *acc, int n, float squeeze)
{
    float y[AUDIO_BLOCK];
    int on = 0;
    float sq0 = sq_last, sq1 = clampf(squeeze, 0.0f, 1.0f);
    sq_last = sq1;
    for (int j = 0; j < NV + NGH; j++) {
        dv_t *v = &V[j];
        if (!v->on) continue;
        bool ghost = j >= NV;
        if (v->delay >= n) { v->delay -= n; if (!ghost) on++; continue; }
        int i0 = v->delay; v->delay = 0;                             // each voice writes y[i0..n), post reads only that
        switch (v->type) {
        case DV_KICK:  run_kick(v, y + i0, n - i0); break;
        case DV_SNAP:  run_snap(v, y + i0, n - i0); break;
        case DV_GLASS: run_glass(v, y + i0, n - i0); break;
        case DV_SUB:   run_sub(v, y + i0, n - i0); break;
        case DV_WHIP:  run_whip(v, y + i0, n - i0); break;
        default:       run_modal(v, y + i0, n - i0); break;
        }
        float pk = post(v, y, acc, i0, n, sq0, sq1, ghost);
        v->lvl = pk;
        bool done = v->t > AUDIO_SR / 20 && pk < 3e-5f;
        if (v->type == DV_CREAK && v->e[1] > 0.002f) done = false;     // between two slips it may be quiet
        if (done || (ghost && v->fade <= 0.0f)) v->on = 0;
        else if (!ghost) on++;
    }
    return on;
}

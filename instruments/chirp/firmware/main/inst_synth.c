// CHIRP's voices: eight calls (chirp, whistle, trill, warble, pip, coo, bell, drop), a pool of short grains
// for SWARM and the HUNT ticks, a bed of air, and a small reverb (four delay lines, a Hadamard mix,
// damping in the loop) that far birds are sent into. Calls start on the sample they were scheduled for.
//
// Each call is one sine (with a little self-modulation for brightness, or a sideband pair for the bell)
// whose pitch glides in one or two exponential segments, with vibrato, a trill (fast tremolo), pips
// (short repeats) and a touch of breath as the species asks. Distance: quieter, duller, wetter.
#include "inst_chirp.h"
#include "dsp.h"
#include "spsc.h"
#include <string.h>

volatile synth_set_t S = { .sweep = 1.0f, .bright = 0.35f, .reverb = 0.35f, .room = 2.2f, .dist = 0.6f, .air = 0.2f, .tone = 9000.0f };
volatile float synth_level;

SPSC(trigq, trig_t, 64)
static trigq_t tq;

typedef struct {
    float len, att, r0, r1, g1, r2, g2;     // seconds, seconds, glide ratios and the share of the call each segment takes
    float vib_hz, vib_d, am_hz, am_d;        // vibrato depth (ratio), trill depth (0..1)
    float noise, bright, pip_on, pip_per;    // breath, self-modulation (turns), pips (seconds; 0: one long call)
    float fm_ratio, fm_idx, end, pitch, gain;
} species_t;
static const species_t SP[SP_N] = {
    [SP_CHIRP]   = { 0.075f, 0.004f, 0.72f, 1.28f, 0.85f, 1.0f, 0.0f, 0, 0, 0, 0, 0.02f, 0.10f, 0, 0, 0, 0, 0.05f, 1.0f, 0.95f },
    [SP_WHISTLE] = { 0.28f, 0.030f, 1.06f, 0.94f, 1.00f, 1.0f, 0.0f, 6.5f, 0.012f, 0, 0, 0.03f, 0.02f, 0, 0, 0, 0, 0.30f, 1.0f, 0.80f },
    [SP_TRILL]   = { 0.32f, 0.005f, 1.12f, 0.90f, 1.00f, 1.0f, 0.0f, 0, 0, 26.0f, 0.92f, 0.02f, 0.06f, 0, 0, 0, 0, 0.25f, 1.0f, 0.95f },
    [SP_WARBLE]  = { 0.24f, 0.010f, 0.95f, 1.18f, 0.45f, 0.78f, 0.55f, 14.0f, 0.045f, 0, 0, 0.02f, 0.07f, 0, 0, 0, 0, 0.20f, 1.0f, 0.85f },
    [SP_PIP]     = { 0.17f, 0.002f, 0.98f, 1.04f, 1.00f, 1.0f, 0.0f, 0, 0, 0, 0, 0.0f, 0.05f, 0.022f, 0.058f, 0, 0, 0.6f, 1.0f, 0.85f },
    [SP_COO]     = { 0.44f, 0.040f, 1.00f, 0.86f, 1.00f, 1.0f, 0.0f, 3.0f, 0.006f, 0, 0, 0.28f, 0.01f, 0.17f, 0.24f, 0, 0, 0.5f, 0.5f, 1.10f },
    [SP_BELL]    = { 1.00f, 0.001f, 1.00f, 1.00f, 1.00f, 1.0f, 0.0f, 0, 0, 0, 0, 0.0f, 0.0f, 0, 0, 1.4142f, 0.35f, 0.02f, 0.5f, 0.45f },
    [SP_DROP]    = { 0.11f, 0.001f, 0.55f, 1.60f, 0.35f, 1.0f, 0.0f, 0, 0, 0, 0, 0.0f, 0.01f, 0, 0, 0, 0, 0.03f, 1.0f, 1.00f },
};

#define NV 16
typedef struct {
    bool  on;
    int   delay, t, len, att_n, n1, n2, pip_on, pip_per, steal;
    uint8_t sp;
    float ph, inc, m1, m2, vib_ph, vib_inc, vib_d, am_ph, am_inc, am_d;
    float mph, mratio, idx, idx_k, self, env, att, dk, noise, amp, dry, wet, lp, lpk, hp, n2b;
} voice_t;
static voice_t vo[NV];

#define NG 24
typedef struct { bool on; int delay, t, len; float ph, inc, amp, tick; } grain_t;
static grain_t gr[NG];

#define NPEND 32
static trig_t pend[NPEND];
static int npend;

// reverb
#define NL 4
static const int rlen[NL] = { 1201, 1553, 1867, 2251 };
#define PRE 480                                  // 15 ms before the first reflection
static float *rbuf[NL], *pre;
static int   rpos[NL], ppos;
static float rlp[NL], rg[NL], room_now = -1;
static svf_t tone_f, air_f;
static float tone_now = -1, air_ph, tone_s = 9000.0f, air_s;
static uint32_t seed = 0xc41c4bu;

void synth_init(void)
{
    for (int i = 0; i < NL; i++) rbuf[i] = app_alloc((size_t)rlen[i] * sizeof(float));
    pre = app_alloc(PRE * sizeof(float));
    trigq_clear(&tq);
    svf_set(&tone_f, 9000.0f, 0.6f);
    svf_set(&air_f, 1200.0f, 0.7f);
}

bool synth_trigger(const trig_t *t) { return trigq_push(&tq, t); }

static void start(voice_t *v, const trig_t *t, int delay)
{
    const species_t *s = &SP[t->species < SP_N ? t->species : 0];
    float lenm = t->len > 0.05f ? t->len : 1.0f;
    float sweep = S.sweep, near = clampf(t->near, 0.0f, 1.0f), far = 1.0f - near;
    float r0 = s->r0, r1 = s->r1, r2 = s->r2, g1 = s->g1, g2 = s->g2, len = s->len * lenm;
    int pips_on = (int)(s->pip_on * lenm * (float)AUDIO_SR), pips_per = (int)(s->pip_per * lenm * (float)AUDIO_SR);
    if (t->kind == TR_HELLO) { r0 = 1.0f; r1 = 1.5f; g1 = 0.5f; r2 = 1.0f; g2 = 0; len = 0.22f; pips_on = (int)(0.07f * AUDIO_SR); pips_per = (int)(0.11f * AUDIO_SR); }
    if (t->kind == TR_BYE) { r0 = 1.0f; r1 = 0.5f; g1 = 1.0f; g2 = 0; len = 0.5f; pips_on = pips_per = 0; }
    // the sweep setting scales every glide (0: flat calls, 2: twice as wide)
    float lr0 = logf(r0) * sweep, lr1 = logf(r1) * sweep, lr2 = logf(r2) * sweep;
    memset(v, 0, sizeof *v);
    v->on = true; v->delay = delay; v->sp = t->species;
    v->len = (int)(len * (float)AUDIO_SR) + 1;
    v->att_n = (int)(s->att * (float)AUDIO_SR) + 1;
    v->inc = t->hz * s->pitch * expf(lr0) / (float)AUDIO_SR;
    v->n1 = (int)(g1 * (float)v->len) + 1; v->m1 = expf((lr1 - lr0) / (float)v->n1);
    v->n2 = (int)(g2 * (float)v->len) + 1; v->m2 = expf((lr2 - lr1) / (float)v->n2);
    if (g2 <= 0) v->n2 = 0;
    v->vib_inc = s->vib_hz / (float)AUDIO_SR; v->vib_d = s->vib_d;
    v->am_inc = s->am_hz / (float)AUDIO_SR; v->am_d = s->am_d;
    v->mratio = s->fm_ratio; v->idx = s->fm_idx * (0.6f + S.bright); v->idx_k = expf(-1.0f / (0.25f * len * (float)AUDIO_SR));
    v->self = s->bright * S.bright * (0.4f + 0.6f * near) * (t->kind == TR_PULSE ? 1.6f : 1.0f);
    v->dk = expf(logf(s->end) / (float)(v->len > v->att_n ? v->len - v->att_n : 1));
    v->att = 1.0f / (float)v->att_n;
    v->noise = s->noise;
    v->pip_on = pips_on; v->pip_per = pips_per;
    float dist = S.dist;
    float a = 0.05f + 0.20f * near * sqrtf(near);                // nearer: louder
    if (t->kind == TR_HELLO) a *= 1.3f;
    if (t->kind == TR_BYE) a *= 0.8f;
    if (t->spot) a *= exp2f((float)t->spot / 6.02f);           // the spotlit bird, SPOTLIGHT dB louder
    v->amp = a * s->gain * (1.0f - 0.5f * dist * far);
    v->dry = 1.0f - 0.7f * dist * far;
    v->wet = S.reverb * (0.3f + 0.7f * (dist * far + (t->kind == TR_BYE ? 0.5f : 0.0f)));
    float fc = 1500.0f + 12000.0f * (1.0f - dist * far * 0.9f);  // far: duller (the air takes the highs)
    v->lpk = onepole_hz(fc);
}

static void grain(const trig_t *t, int delay)
{
    int k = 0; float best = 1e9f;
    for (int i = 0; i < NG; i++) { float s = gr[i].on ? (float)(gr[i].len - gr[i].t) : -1.0f; if (s < best) { best = s; k = i; } }
    grain_t *g = &gr[k];
    if (g->on && g->t < g->len - 64) return;                    // all busy: drop this one rather than cut one
    float near = clampf(t->near, 0.0f, 1.0f);
    g->on = true; g->delay = delay; g->t = 0;
    if (t->kind == TR_GEIGER) { g->len = (int)(0.004f * AUDIO_SR); g->tick = 0.6f; g->amp = 0.10f + 0.20f * near; }
    else { g->len = (int)((0.006f + 0.024f * t->len) * AUDIO_SR); g->tick = 0; g->amp = 0.04f + 0.10f * near; }
    if (g->len < 32) g->len = 32;
    g->inc = t->hz / (float)AUDIO_SR;
}

static void voice_for(const trig_t *t, int delay)
{
    if (t->kind == TR_GRAIN || t->kind == TR_GEIGER) { grain(t, delay); return; }
    int k = -1; float q = 1e9f;
    for (int i = 0; i < NV; i++) if (!vo[i].on) { k = i; break; }
    if (k < 0) {                                                  // steal the quietest, with a short fade
        for (int i = 0; i < NV; i++) { float l = vo[i].env * vo[i].amp; if (!vo[i].steal && l < q) { q = l; k = i; } }
        if (k >= 0 && !vo[k].steal) vo[k].steal = 64;
        if (npend < NPEND) { pend[npend] = *t; pend[npend].at = audio_frames() + AUDIO_BLOCK; npend++; }
        return;
    }
    start(&vo[k], t, delay);
}

static inline float run_voice(voice_t *v, float *wet)
{
    if (v->delay > 0) { v->delay--; return 0; }
    float vib = v->vib_d > 0 ? 1.0f + v->vib_d * sin_turns(v->vib_ph) : 1.0f;
    v->vib_ph = wrap1(v->vib_ph + v->vib_inc);
    float inc = v->inc * vib;
    v->ph = wrap1(v->ph + inc);
    float x;
    if (v->mratio > 0) {                                          // the bell: a sideband pair that fades
        v->mph = wrap1(v->mph + inc * v->mratio);
        x = sin_turns(v->ph + v->idx * sin_turns(v->mph));
        v->idx *= v->idx_k;
    } else x = sin_turns(v->ph + v->self * sin_turns(v->ph));
    if (v->am_d > 0) { v->am_ph = wrap1(v->am_ph + v->am_inc); x *= 1.0f - v->am_d * (0.5f + 0.5f * sin_turns(v->am_ph)); }
    if (v->noise > 0) {                                          // breath: noise in a band around the note, not a hiss
        float nz = rng_bi(&seed);
        v->hp += 0.06f * (nz - v->hp);                          // two poles at about 300 Hz, moved up to the note
        v->n2b += 0.06f * (v->hp - v->n2b);
        x += v->noise * 9.0f * v->n2b * sin_turns(v->ph + 0.25f);
    }
    // glide
    if (v->n1 > 0) { v->inc *= v->m1; v->n1--; } else if (v->n2 > 0) { v->inc *= v->m2; v->n2--; }
    // envelope: a linear attack, an exponential fall, a 3 ms ramp at the end; pips gate it
    if (v->t < v->att_n) v->env += v->att; else v->env *= v->dk;
    float e = v->env;
    if (v->pip_per > 0) {
        int p = v->t % v->pip_per;
        if (p < v->pip_on) { float q = (float)p / (float)v->pip_on; e *= 4.0f * q * (1.0f - q); } else e = 0;
    }
    int left = v->len - v->t;
    if (left < 96) e *= (float)left * (1.0f / 96.0f);
    if (v->steal) { e *= (float)v->steal * (1.0f / 64.0f); if (--v->steal == 0) v->on = false; }
    v->t++;
    if (v->t >= v->len) v->on = false;
    float y = x * e * v->amp;
    v->lp += v->lpk * (y - v->lp);
    *wet += v->lp * v->wet;
    return v->lp * v->dry;
}

static inline float run_grain(grain_t *g)
{
    if (g->delay > 0) { g->delay--; return 0; }
    float q = (float)g->t / (float)g->len, w = 4.0f * q * (1.0f - q);
    g->ph = wrap1(g->ph + g->inc);
    float x = sin_turns(g->ph);
    if (g->tick > 0) x = x * 0.6f + g->tick * rng_bi(&seed) * (1.0f - q);
    if (++g->t >= g->len) g->on = false;
    return x * w * g->amp;
}

void synth_audio(const float *in, float *out, int n)
{
    (void)in;
    uint32_t f0 = audio_frames();
    trig_t t;
    // what is due in this block starts on its sample; what is later waits
    while (trigq_pop(&tq, &t)) { if (npend < NPEND) pend[npend++] = t; }
    for (int i = 0; i < npend; ) {
        int32_t d = (int32_t)(pend[i].at - f0);
        if (d < n) { trig_t x = pend[i]; pend[i] = pend[--npend]; voice_for(&x, d < 0 ? 0 : d); continue; }
        if (d > AUDIO_SR) { pend[i] = pend[--npend]; continue; }    // a clock gone wrong: drop it
        i++;
    }
    // block-rate settings
    float room = S.room;
    if (room != room_now) {
        room_now = room;
        for (int i = 0; i < NL; i++) rg[i] = expf(-6.9078f * (float)rlen[i] / (room * (float)AUDIO_SR));
    }
    float tone = S.tone;
    tone_s += 0.25f * (tone - tone_s);
    if (fabsf(tone_s - tone_now) > 1.0f) { tone_now = tone_s; svf_set(&tone_f, tone_s, 0.6f); }
    air_ph = wrap1(air_ph + (float)n * 0.07f / (float)AUDIO_SR);
    svf_set(&air_f, 900.0f + 700.0f * sin_turns(air_ph), 0.8f);
    float air_t = S.air * (0.10f + 0.9f * clampf(S.swarm_air, 0.0f, 1.0f)) * 0.10f, air0 = air_s, dair = (air_t - air0) / (float)n;
    air_s = air_t;                                               // ramped across the block
    bool have_verb = rbuf[NL - 1] && pre;
    float pk = 0;
    for (int i = 0; i < n; i++) {
        float wet = 0, dry = 0;
        for (int k = 0; k < NV; k++) if (vo[k].on) dry += run_voice(&vo[k], &wet);
        for (int k = 0; k < NG; k++) if (gr[k].on) { float g = run_grain(&gr[k]); dry += g; wet += 0.4f * g * S.reverb; }
        float air = air0 + dair * (float)i;
        if (air > 1e-5f) { float a = svf_bp(&air_f, rng_bi(&seed), NULL) * air; dry += a; wet += a; }
        float y = dry;
        if (have_verb) {
            float x = pre[ppos]; pre[ppos] = wet; ppos = ppos + 1 < PRE ? ppos + 1 : 0;
            float o[NL];
            for (int k = 0; k < NL; k++) o[k] = rbuf[k][rpos[k]];
            float a = o[0] + o[1], b = o[0] - o[1], c = o[2] + o[3], d = o[2] - o[3];
            float h[NL] = { 0.5f * (a + c), 0.5f * (b + d), 0.5f * (a - c), 0.5f * (b - d) };
            for (int k = 0; k < NL; k++) {
                rlp[k] += 0.45f * (h[k] - rlp[k]);                     // damping: highs die first
                rlp[k] = flush_denormal(rlp[k]);
                rbuf[k][rpos[k]] = x + rg[k] * rlp[k];
                if (++rpos[k] >= rlen[k]) rpos[k] = 0;
            }
            y += 0.6f * (o[0] + o[1] + o[2] + o[3]) * 0.5f;
        }
        y = svf_lp(&tone_f, y);
        out[i] = y;
        float ay = fabsf(y); if (ay > pk) pk = ay;
    }
    synth_level = pk > synth_level ? pk : synth_level * 0.97f;
}

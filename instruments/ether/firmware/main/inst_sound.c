// ETHER's audio engine: both worlds in one mix.
//   Wi-Fi (SNIFF's)   12 voices for frames: modal plucks, bells and tines (struck two-pole resonators,
//                     a unit impulse plus a little noise), drops, beeps, static; ACK ticks; rain for the
//                     frames over the budget; the ROOM pad (8 detuned sine pairs shaped by CSI); the STORM.
//   Bluetooth (CHIRP's) 12 call voices (a gliding sine: chirp, whistle, trill, warble, pip, coo, bell,
//                     drop), 16 grains (SWARM), and a breath of wind from the crowd.
//   Both              the BALANCE as two bus gains, one tempo echo and one reverb (4 lines, a Hadamard
//                     mix, damping in the loop, a pre-delay), the HUNT beeper on top.
// Every level is ramped per block, filters are set at block rate; nothing allocates. Stolen voices fade.
#include "inst.h"
#include "spsc.h"
#include <string.h>

volatile snd_ctl_t sc;
volatile float snd_out_wifi, snd_out_bt, snd_out_room, snd_out_storm;
volatile int   snd_voices;
volatile uint32_t snd_beeps;

SPSC(strq, strike_t, 128)
SPSC(trigq, trig_t, 64)
static strq_t sq;
static trigq_t tq;
static uint32_t seed = 0xe7e4e7e4u;
static inline float nz(void) { return rng_bi(&seed); }

// ---------------------------------------------------------------- Wi-Fi voices
#define NWV 12
#define FADE_N 64
typedef struct { float c1, c2, y1, y2, g; } mode_t;
typedef struct {
    int     on, delay, quiet;
    uint8_t t;
    mode_t  m[3]; int nm;
    int     exc_n; float exc_lp, exc_k, exc_amp, imp;
    float   ph, f, f_end, glide, env, env_k;
    int     gate;
    svf_t   bp;
    float   amp, wet, fade;
    bool    stealing; strike_t next;
} wvoice_t;
static wvoice_t wv[NWV];

static void mode_set(mode_t *m, float f, float t60, float g)
{
    float w = TWO_PI * clampf(f, 20.0f, 0.45f * (float)AUDIO_SR) / (float)AUDIO_SR;
    float r = expf(-6.9078f / (fmaxf(t60, 0.005f) * (float)AUDIO_SR));
    m->c1 = 2.0f * r * cosf(w); m->c2 = -r * r; m->y1 = m->y2 = 0; m->g = g * sinf(w);
}

static void wstart(wvoice_t *v, const strike_t *s, int delay)
{
    memset(v, 0, sizeof *v);
    v->on = 1; v->delay = delay; v->t = s->timbre; v->amp = s->amp; v->wet = s->wet; v->fade = 1.0f;
    float f = s->f, r = fmaxf(s->ring, 0.01f), b = clampf(s->bright, 0, 1);
    v->exc_k = 0.08f + 0.85f * b; v->imp = 1.0f;               // a unit impulse rings each mode at its gain; the noise is texture
    switch (s->timbre) {
    case T_PLUCK:
        v->nm = 3;
        mode_set(&v->m[0], f, r, 1.0f); mode_set(&v->m[1], 2.0f * f, r * 0.55f, 0.5f * b + 0.1f); mode_set(&v->m[2], 3.0f * f, r * 0.33f, 0.3f * b);
        v->exc_n = 48; v->exc_amp = 0.10f; break;
    case T_BELL:
        v->nm = 3;
        mode_set(&v->m[0], f, r, 1.0f); mode_set(&v->m[1], 2.76f * f, r * 0.6f, 0.55f); mode_set(&v->m[2], 5.40f * f, r * 0.3f, 0.3f * b + 0.05f);
        v->exc_n = 8; v->exc_amp = 0.05f; break;
    case T_KALIMBA:
        v->nm = 2;
        mode_set(&v->m[0], f, r, 1.0f); mode_set(&v->m[1], 5.93f * f, r * 0.09f, 0.5f * b + 0.15f);
        v->exc_n = 12; v->exc_amp = 0.08f; break;
    case T_DROP:
        v->f = f * 0.55f; v->f_end = f * 1.45f; v->glide = onepole_s(0.012f + 0.03f * r);
        v->env = 1.0f; v->env_k = expf(-6.9078f / (r * (float)AUDIO_SR)); break;
    case T_BEEP:
        v->f = v->f_end = f; v->gate = (int)(r * (float)AUDIO_SR); break;
    case T_STATIC:
        svf_set(&v->bp, f, 1.5f + 5.0f * b);
        v->env = 1.0f; v->env_k = expf(-6.9078f / (r * (float)AUDIO_SR)); break;
    }
}

static inline float wrun(wvoice_t *v)
{
    float e = v->imp, y = 0;
    v->imp = 0;
    if (v->exc_n > 0) { v->exc_lp += v->exc_k * (nz() - v->exc_lp); e += v->exc_lp * v->exc_amp * (float)v->exc_n * (1.0f / 48.0f); v->exc_n--; }
    switch (v->t) {
    case T_PLUCK: case T_BELL: case T_KALIMBA:
        for (int k = 0; k < v->nm; k++) {
            mode_t *m = &v->m[k];
            float o = m->c1 * m->y1 + m->c2 * m->y2 + e * m->g;
            m->y2 = m->y1; m->y1 = o; y += o;
        }
        y *= 0.7f;
        break;
    case T_DROP:
        v->f += v->glide * (v->f_end - v->f);
        v->ph = wrap1(v->ph + v->f / (float)AUDIO_SR);
        y = sin_turns(v->ph) * v->env; v->env *= v->env_k;
        break;
    case T_BEEP: {
        float want = v->gate > 0 ? 1.0f : 0.0f;
        v->env += (want - v->env) * 0.01f;                    // 3 ms edges
        if (v->gate > 0) v->gate--;
        v->ph = wrap1(v->ph + v->f / (float)AUDIO_SR);
        y = (sin_turns(v->ph) + 0.18f * sin_turns(3.0f * v->ph)) * v->env * 0.8f;
        break;
    }
    case T_STATIC:
        y = svf_bp(&v->bp, nz(), NULL) * v->env * 3.2f; v->env *= v->env_k;
        break;
    }
    return y * v->amp;
}

static void take_strike(const strike_t *s, uint32_t f0)
{
    int32_t d = (int32_t)(s->at - f0);
    int delay = d < 0 ? 0 : d > AUDIO_SR ? AUDIO_SR : d;
    int best = -1; float bq = 1e9f;
    for (int i = 0; i < NWV; i++) {
        if (!wv[i].on) { wstart(&wv[i], s, delay); return; }
        if (wv[i].stealing) continue;
        float q = wv[i].amp * (wv[i].delay > 0 ? 10.0f : 1.0f);
        if (q < bq) { bq = q; best = i; }
    }
    if (best >= 0) { wv[best].stealing = true; wv[best].next = *s; }
}

// ---------------------------------------------------------------- bird calls (CHIRP's species)
typedef struct {
    float len, att, r0, r1, g1, r2, g2;
    float vib_hz, vib_d, am_hz, am_d;
    float noise, bright, pip_on, pip_per;
    float fm_ratio, fm_idx, end, pitch, gain;
} species_t;
static const species_t SPC[SP_N] = {
    [SP_CHIRP]   = { 0.075f, 0.004f, 0.72f, 1.28f, 0.85f, 1.0f, 0.0f, 0, 0, 0, 0, 0.02f, 0.10f, 0, 0, 0, 0, 0.05f, 1.0f, 0.95f },
    [SP_WHISTLE] = { 0.28f, 0.030f, 1.06f, 0.94f, 1.00f, 1.0f, 0.0f, 6.5f, 0.012f, 0, 0, 0.03f, 0.02f, 0, 0, 0, 0, 0.30f, 1.0f, 0.80f },
    [SP_TRILL]   = { 0.32f, 0.005f, 1.12f, 0.90f, 1.00f, 1.0f, 0.0f, 0, 0, 26.0f, 0.92f, 0.02f, 0.06f, 0, 0, 0, 0, 0.25f, 1.0f, 0.95f },
    [SP_WARBLE]  = { 0.24f, 0.010f, 0.95f, 1.18f, 0.45f, 0.78f, 0.55f, 14.0f, 0.045f, 0, 0, 0.02f, 0.07f, 0, 0, 0, 0, 0.20f, 1.0f, 0.85f },
    [SP_PIP]     = { 0.17f, 0.002f, 0.98f, 1.04f, 1.00f, 1.0f, 0.0f, 0, 0, 0, 0, 0.0f, 0.05f, 0.022f, 0.058f, 0, 0, 0.6f, 1.0f, 0.85f },
    [SP_COO]     = { 0.44f, 0.040f, 1.00f, 0.86f, 1.00f, 1.0f, 0.0f, 3.0f, 0.006f, 0, 0, 0.28f, 0.01f, 0.17f, 0.24f, 0, 0, 0.5f, 0.5f, 1.10f },
    [SP_BELL]    = { 1.00f, 0.001f, 1.00f, 1.00f, 1.00f, 1.0f, 0.0f, 0, 0, 0, 0, 0.0f, 0.0f, 0, 0, 1.4142f, 0.35f, 0.02f, 0.5f, 0.45f },
    [SP_DROP]    = { 0.11f, 0.001f, 0.55f, 1.60f, 0.35f, 1.0f, 0.0f, 0, 0, 0, 0, 0.0f, 0.01f, 0, 0, 0, 0, 0.03f, 1.0f, 1.00f },
};

#define NBV 12
typedef struct {
    bool  on;
    int   delay, t, len, att_n, n1, n2, pip_on, pip_per, steal;
    float ph, inc, m1, m2, vib_ph, vib_inc, vib_d, am_ph, am_inc, am_d;
    float mph, mratio, idx, idx_k, self, env, att, dk, noise, amp, dry, wet, lp, lpk, hp, n2b;
} bvoice_t;
static bvoice_t bv[NBV];
#define NG 16
typedef struct { bool on; int delay, t, len; float ph, inc, amp; } grain_t;
static grain_t gr[NG];
#define NPEND 24
static trig_t pend[NPEND];
static int npend;

static void bstart(bvoice_t *v, const trig_t *t, int delay)
{
    const species_t *s = &SPC[t->species < SP_N ? t->species : 0];
    float lenm = t->len > 0.05f ? t->len : 1.0f, sweep = sc.sweep, near = clampf(t->near, 0.0f, 1.0f), far = 1.0f - near;
    float r0 = s->r0, r1 = s->r1, r2 = s->r2, g1 = s->g1, g2 = s->g2, len = s->len * lenm;
    int pips_on = (int)(s->pip_on * lenm * (float)AUDIO_SR), pips_per = (int)(s->pip_per * lenm * (float)AUDIO_SR);
    if (t->kind == TR_HELLO) { r0 = 1.0f; r1 = 1.5f; g1 = 0.5f; r2 = 1.0f; g2 = 0; len = 0.22f; pips_on = (int)(0.07f * AUDIO_SR); pips_per = (int)(0.11f * AUDIO_SR); }
    if (t->kind == TR_BYE) { r0 = 1.0f; r1 = 0.5f; g1 = 1.0f; g2 = 0; len = 0.5f; pips_on = pips_per = 0; }
    float lr0 = logf(r0) * sweep, lr1 = logf(r1) * sweep, lr2 = logf(r2) * sweep;
    memset(v, 0, sizeof *v);
    v->on = true; v->delay = delay;
    v->len = (int)(len * (float)AUDIO_SR) + 1;
    v->att_n = (int)(s->att * (float)AUDIO_SR) + 1;
    v->inc = t->hz * s->pitch * expf(lr0) / (float)AUDIO_SR;
    v->n1 = (int)(g1 * (float)v->len) + 1; v->m1 = expf((lr1 - lr0) / (float)v->n1);
    v->n2 = (int)(g2 * (float)v->len) + 1; v->m2 = expf((lr2 - lr1) / (float)v->n2);
    if (g2 <= 0) v->n2 = 0;
    v->vib_inc = s->vib_hz / (float)AUDIO_SR; v->vib_d = s->vib_d;
    v->am_inc = s->am_hz / (float)AUDIO_SR; v->am_d = s->am_d;
    v->mratio = s->fm_ratio; v->idx = s->fm_idx * (0.6f + sc.bright); v->idx_k = expf(-1.0f / (0.25f * len * (float)AUDIO_SR));
    v->self = s->bright * sc.bright * (0.4f + 0.6f * near) * (t->kind == TR_PULSE ? 1.6f : 1.0f);
    v->dk = expf(logf(s->end) / (float)(v->len > v->att_n ? v->len - v->att_n : 1));
    v->att = 1.0f / (float)v->att_n;
    v->noise = s->noise;
    v->pip_on = pips_on; v->pip_per = pips_per;
    float a = 0.05f + 0.20f * near * sqrtf(near);
    if (t->kind == TR_HELLO) a *= 1.3f;
    if (t->kind == TR_BYE) a *= 0.8f;
    float dist = clampf(sc.dist, 0, 1);
    v->amp = a * s->gain * (1.0f - 0.5f * dist * far);
    v->dry = 1.0f - 0.7f * dist * far;
    v->wet = 0.3f + 0.7f * (dist * far + (t->kind == TR_BYE ? 0.5f : 0.0f));
    v->lpk = onepole_hz(1500.0f + 12000.0f * (1.0f - far * 0.9f));
}

static void grain(const trig_t *t, int delay)
{
    int k = 0; float best = 1e9f;
    for (int i = 0; i < NG; i++) { float s = gr[i].on ? (float)(gr[i].len - gr[i].t) : -1.0f; if (s < best) { best = s; k = i; } }
    grain_t *g = &gr[k];
    if (g->on && g->t < g->len - 64) return;
    float near = clampf(t->near, 0.0f, 1.0f);
    g->on = true; g->delay = delay; g->t = 0;
    g->len = (int)((0.006f + 0.024f * t->len) * AUDIO_SR); if (g->len < 32) g->len = 32;
    g->amp = 0.04f + 0.10f * near;
    g->inc = t->hz / (float)AUDIO_SR;
}

static void bird_for(const trig_t *t, int delay)
{
    if (t->kind == TR_GRAIN) { grain(t, delay); return; }
    int k = -1; float q = 1e9f;
    for (int i = 0; i < NBV; i++) if (!bv[i].on) { k = i; break; }
    if (k < 0) {
        for (int i = 0; i < NBV; i++) { float l = bv[i].env * bv[i].amp; if (!bv[i].steal && l < q) { q = l; k = i; } }
        if (k >= 0 && !bv[k].steal) bv[k].steal = 64;
        if (npend < NPEND) { pend[npend] = *t; pend[npend].at = audio_frames() + AUDIO_BLOCK; npend++; }
        return;
    }
    bstart(&bv[k], t, delay);
}

static inline float brun(bvoice_t *v, float *wet)
{
    if (v->delay > 0) { v->delay--; return 0; }
    float vib = v->vib_d > 0 ? 1.0f + v->vib_d * sin_turns(v->vib_ph) : 1.0f;
    v->vib_ph = wrap1(v->vib_ph + v->vib_inc);
    float inc = v->inc * vib;
    v->ph = wrap1(v->ph + inc);
    float x;
    if (v->mratio > 0) { v->mph = wrap1(v->mph + inc * v->mratio); x = sin_turns(v->ph + v->idx * sin_turns(v->mph)); v->idx *= v->idx_k; }
    else x = sin_turns(v->ph + v->self * sin_turns(v->ph));
    if (v->am_d > 0) { v->am_ph = wrap1(v->am_ph + v->am_inc); x *= 1.0f - v->am_d * (0.5f + 0.5f * sin_turns(v->am_ph)); }
    if (v->noise > 0) {
        float n = nz();
        v->hp += 0.06f * (n - v->hp); v->n2b += 0.06f * (v->hp - v->n2b);
        x += v->noise * 9.0f * v->n2b * sin_turns(v->ph + 0.25f);
    }
    if (v->n1 > 0) { v->inc *= v->m1; v->n1--; } else if (v->n2 > 0) { v->inc *= v->m2; v->n2--; }
    if (v->t < v->att_n) v->env += v->att; else v->env *= v->dk;
    float e = v->env;
    if (v->pip_per > 0) { int p = v->t % v->pip_per; if (p < v->pip_on) { float q = (float)p / (float)v->pip_on; e *= 4.0f * q * (1.0f - q); } else e = 0; }
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

static inline float grun(grain_t *g)
{
    if (g->delay > 0) { g->delay--; return 0; }
    float q = (float)g->t / (float)g->len, w = 4.0f * q * (1.0f - q);
    g->ph = wrap1(g->ph + g->inc);
    if (++g->t >= g->len) g->on = false;
    return sin_turns(g->ph) * w * g->amp;
}

// ---------------------------------------------------------------- ticks, the room, the storm, HUNT, space
#define TICKQ 48
static uint32_t tick_at[TICKQ]; static float tick_amp[TICKQ]; static int tick_n;
static float tick_env, tick_hx, tick_hy;
static float rain_env; static svf_t rain_bp;
static float ph_a[PAD_N], ph_b[PAD_N], pad_fs[PAD_N], pad_gs[PAD_N], motion_s, wind_lp;
static svf_t wind_bp, air_bp;
static float air_ph;
static float crack_env, crack_hx, crack_hy, brown1, brown2, thunder_env, thunder_fc = 1500.0f;
static svf_t thunder_lp; static uint32_t thunder_seen;
static float beep_ph, beep_env, beep_tone_ph, click_env;
// the reverb: four lines of fixed length (prime), a Hadamard mix, damping, a pre-delay
#define NL 4
static const int rlen[NL] = { 1201, 1553, 1867, 2251 };
#define PRE 480
static float *rbuf[NL], *pre;
static int   rpos[NL], ppos;
static float rlp[NL], rg[NL], size_now = -1;
#define ECHO_LEN 32768
static float *echo_buf; static uint32_t echo_w; static float echo_d = 9600, echo_lp;
static float lv_w, lv_b, lv_pkt, lv_room, lv_storm, lv_birds, lv_hunt, lv_verb, lv_echo, bright_lp, bright_k = 1.0f;
static float pk_w, pk_b, pk_r, pk_s;

void snd_init(void)
{
    for (int i = 0; i < NL; i++) if (!rbuf[i]) rbuf[i] = app_alloc((size_t)rlen[i] * sizeof(float));
    if (!pre) pre = app_alloc(PRE * sizeof(float));
    if (!echo_buf) echo_buf = app_alloc(ECHO_LEN * sizeof(float));
    strq_clear(&sq); trigq_clear(&tq);
    svf_set(&rain_bp, 2600.0f, 3.0f);
    svf_set(&air_bp, 1200.0f, 0.7f);
}
bool snd_strike(const strike_t *s) { return strq_push(&sq, s); }
bool snd_call(const trig_t *t) { return trigq_push(&tq, t); }

void snd_audio(const float *in, float *out, int n)
{
    (void)in;
    uint32_t f0 = audio_frames();
    // what is due in this block starts on its sample
    strike_t s;
    int popped = 0;
    while (popped < 24 && strq_pop(&sq, &s)) {
        popped++;
        if (s.timbre == T_TICK) { if (tick_n < TICKQ) { tick_at[tick_n] = s.at; tick_amp[tick_n] = s.amp; tick_n++; } continue; }
        take_strike(&s, f0);
    }
    trig_t t;
    while (trigq_pop(&tq, &t)) { if (npend < NPEND) pend[npend++] = t; }
    for (int i = 0; i < npend; ) {
        int32_t d = (int32_t)(pend[i].at - f0);
        if (d < n) { trig_t x = pend[i]; pend[i] = pend[--npend]; bird_for(&x, d < 0 ? 0 : d); continue; }
        if (d > AUDIO_SR) { pend[i] = pend[--npend]; continue; }
        i++;
    }

    // block-rate settings, ramped across the block
    const float rk = 1.0f / (float)n;
    float t_w = clampf(sc.wifi, 0, 1), t_b = clampf(sc.bt, 0, 1);
    float d_w = (t_w - lv_w) * rk, d_b = (t_b - lv_b) * rk;
    float d_pkt = (clampf(sc.pkt, 0, 1) - lv_pkt) * rk, d_room = (clampf(sc.room, 0, 1) - lv_room) * rk;
    float d_storm = (clampf(sc.storm, 0, 1) - lv_storm) * rk, d_birds = (clampf(sc.birds, 0, 1) - lv_birds) * rk;
    float d_hunt = ((sc.hunt > 0.5f ? 1.0f : 0.0f) - lv_hunt) * rk * 0.25f;
    float d_verb = (clampf(sc.verb, 0, 1) - lv_verb) * rk, d_echo = (clampf(sc.echo, 0, 1) - lv_echo) * rk;
    float bk = sc.bright > 0.97f ? 1.0f : onepole_hz(1200.0f * exp2f(clampf(sc.bright, 0, 1) * 4.0f));
    bright_k += 0.2f * (bk - bright_k);
    float size = clampf(sc.size, 0, 1);
    if (fabsf(size - size_now) > 0.002f) {
        size_now = size;
        float rt = 0.4f + 6.0f * size * size;
        for (int i = 0; i < NL; i++) rg[i] = expf(-6.9078f * (float)rlen[i] / (rt * (float)AUDIO_SR));
    }
    float echo_t = clampf(sc.echo_s * (float)AUDIO_SR, 64.0f, (float)ECHO_LEN - 2.0f), efb = clampf(sc.efb, 0, 0.92f);
    float pad_gt[PAD_N];
    for (int k = 0; k < PAD_N; k++) { pad_gt[k] = clampf(sc.pad_g[k], 0, 2); if (pad_fs[k] <= 0) pad_fs[k] = clampf(sc.pad_f[k], 20.0f, 8000.0f); }
    float m_t = clampf(sc.motion, 0, 1);
    float crack_p = clampf(sc.crackle, 0, 3000) / (float)AUDIO_SR, rain_p = clampf(sc.rain, 0, 3000) / (float)AUDIO_SR, rumble = clampf(sc.rumble, 0, 1);
    if (sc.thunder != thunder_seen) { thunder_seen = sc.thunder; thunder_env = 1.0f; thunder_fc = 1600.0f; }
    if (thunder_env > 0.001f) { thunder_fc += 0.03f * (90.0f - thunder_fc); svf_set(&thunder_lp, thunder_fc, 0.8f); }
    svf_set(&wind_bp, 700.0f + 1800.0f * motion_s, 0.9f);
    air_ph = wrap1(air_ph + (float)n * 0.07f / (float)AUDIO_SR);
    svf_set(&air_bp, 900.0f + 700.0f * sin_turns(air_ph), 0.8f);
    float air = clampf(sc.air, 0, 1) * 0.012f;
    float hunt_rate = clampf(sc.hunt_rate, 0.2f, 30.0f), hunt_f = clampf(sc.hunt_pitch, 100.0f, 6000.0f);
    bool have_verb = rbuf[NL - 1] && pre;

    float mx_w = 0, mx_b = 0, mx_r = 0, mx_s = 0;
    for (int i = 0; i < n; i++) {
        uint32_t now = f0 + (uint32_t)i;
        lv_w += d_w; lv_b += d_b; lv_pkt += d_pkt; lv_room += d_room; lv_storm += d_storm; lv_birds += d_birds;
        lv_hunt += d_hunt; if (lv_hunt < 0) lv_hunt = 0;
        lv_verb += d_verb; lv_echo += d_echo;
        float send = 0;
        // ---- Wi-Fi: frames, ticks, rain
        float wdry = 0, wsend = 0;
        for (int k = 0; k < NWV; k++) {
            wvoice_t *v = &wv[k];
            if (!v->on) continue;
            if (v->delay > 0) { v->delay--; continue; }
            float y = wrun(v);
            if (v->stealing) {
                v->fade -= 1.0f / (float)FADE_N;
                if (v->fade <= 0) { strike_t nx = v->next; int32_t d = (int32_t)(nx.at - now); wstart(v, &nx, d < 0 ? 0 : d > AUDIO_SR ? AUDIO_SR : d); continue; }
                y *= v->fade;
            }
            wdry += y * (1.0f - v->wet); wsend += y * v->wet;
        }
        for (int k = 0; k < tick_n; ) {
            if ((int32_t)(tick_at[k] - now) <= 0) { tick_env += tick_amp[k]; if (tick_env > 1.2f) tick_env = 1.2f; tick_at[k] = tick_at[--tick_n]; tick_amp[k] = tick_amp[tick_n]; }
            else k++;
        }
        float tn = nz(), th = tn - tick_hx + 0.6f * tick_hy; tick_hx = tn; tick_hy = th;
        wdry += th * tick_env * 0.12f; tick_env *= 0.93f;
        if (rng_uni(&seed) < rain_p) rain_env += 0.35f * rng_uni(&seed);
        wdry += svf_bp(&rain_bp, nz(), NULL) * rain_env * 0.5f; rain_env *= 0.985f;
        float gw = lv_pkt * lv_w;
        bright_lp += bright_k * (wdry * gw - bright_lp);
        float wifi = bright_lp;
        send += wsend * gw + wifi * 0.15f;
        // ---- the room
        motion_s += 0.0002f * (m_t - motion_s);
        float pad = 0;
        if (lv_room * lv_w > 0.0005f) {
            float det = 1.0015f + 0.004f * motion_s;
            for (int k = 0; k < PAD_N; k++) {
                if ((i & 15) == 0) { pad_fs[k] += 0.02f * (clampf(sc.pad_f[k], 20.0f, 8000.0f) - pad_fs[k]); pad_gs[k] += 0.004f * (pad_gt[k] - pad_gs[k]); }
                float f = pad_fs[k] / (float)AUDIO_SR;
                ph_a[k] = wrap1(ph_a[k] + f); ph_b[k] = wrap1(ph_b[k] + f * det);
                pad += pad_gs[k] * (sin_turns(ph_a[k]) + sin_turns(ph_b[k]));
            }
            pad *= 0.045f * (0.15f + 0.85f * motion_s);
            float w = svf_bp(&wind_bp, nz(), NULL);
            wind_lp += 0.3f * (w - wind_lp);
            pad += wind_lp * 0.12f * motion_s * motion_s;
            pad *= lv_room * lv_w;
        }
        send += pad * 0.5f;
        // ---- the storm
        float storm = 0;
        if (lv_storm * lv_w > 0.0005f) {
            if (rng_uni(&seed) < crack_p) { float a = rng_uni(&seed); crack_env += a * a * a + 0.05f; }
            float cn = nz(), chp = cn - crack_hx + 0.3f * crack_hy; crack_hx = cn; crack_hy = chp;
            float crack = chp * crack_env * 0.5f; crack_env *= 0.82f;
            brown1 += 0.025f * (nz() - brown1); brown2 += 0.025f * (brown1 - brown2);
            float thu = 0;
            if (thunder_env > 0.0005f) { thu = svf_lp(&thunder_lp, nz()) * thunder_env * 1.6f; thunder_env *= 0.99993f; }
            storm = (crack + brown2 * 7.0f * rumble + thu) * lv_storm * lv_w;
        }
        send += storm * 0.25f;
        // ---- Bluetooth: calls, grains, the crowd's air
        float bdry = 0, bwet = 0;
        for (int k = 0; k < NBV; k++) if (bv[k].on) bdry += brun(&bv[k], &bwet);
        for (int k = 0; k < NG; k++) if (gr[k].on) { float g = grun(&gr[k]); bdry += g; bwet += 0.4f * g; }
        if (air > 1e-6f) { float a = svf_bp(&air_bp, nz(), NULL) * air; bdry += a; bwet += a; }
        float gb = lv_birds * lv_b;
        float birds = bdry * gb;
        send += bwet * gb;
        // ---- HUNT
        float hunt = 0;
        if (lv_hunt > 0.0005f) {
            beep_ph += hunt_rate / (float)AUDIO_SR;
            if (beep_ph >= 1.0f) { beep_ph -= 1.0f; beep_env = 1.0f; click_env = 1.0f; snd_beeps++; }
            beep_tone_ph = wrap1(beep_tone_ph + hunt_f / (float)AUDIO_SR);
            hunt = (sin_turns(beep_tone_ph) * beep_env * 0.30f + nz() * click_env * 0.15f) * lv_hunt;
            beep_env *= 0.9985f; click_env *= 0.8f;
            send += hunt * 0.15f;
        }
        // ---- space: the echo (both worlds), the reverb (one for everything)
        float mix = wifi + pad * 0.6f + storm + birds + hunt;
        if (echo_buf) {
            echo_d += 0.0004f * (echo_t - echo_d);
            float e = dline_read(&(dline_t){ echo_buf, ECHO_LEN - 1, echo_w }, echo_d);
            echo_lp += 0.35f * (e - echo_lp);
            echo_buf[echo_w & (ECHO_LEN - 1)] = flush_denormal((wifi + birds) * lv_echo + echo_lp * efb);
            echo_w++;
            mix += echo_lp * 0.8f;
            send += echo_lp * 0.3f;
        }
        if (have_verb) {
            float x = pre[ppos]; pre[ppos] = send; ppos = ppos + 1 < PRE ? ppos + 1 : 0;
            float o[NL];
            for (int k = 0; k < NL; k++) o[k] = rbuf[k][rpos[k]];
            float a = o[0] + o[1], b = o[0] - o[1], c = o[2] + o[3], d = o[2] - o[3];
            float h[NL] = { 0.5f * (a + c), 0.5f * (b + d), 0.5f * (a - c), 0.5f * (b - d) };
            for (int k = 0; k < NL; k++) {
                rlp[k] += 0.5f * (h[k] - rlp[k]);
                rlp[k] = flush_denormal(rlp[k]);
                rbuf[k][rpos[k]] = x * 0.6f + rg[k] * rlp[k];
                if (++rpos[k] >= rlen[k]) rpos[k] = 0;
            }
            mix += (o[0] + o[1] + o[2] + o[3]) * 0.4f * lv_verb;
        }
        out[i] = mix;
        float aw = fabsf(wifi), ab = fabsf(birds), ar = fabsf(pad), as = fabsf(storm);
        if (aw > mx_w) mx_w = aw;
        if (ab > mx_b) mx_b = ab;
        if (ar > mx_r) mx_r = ar;
        if (as > mx_s) mx_s = as;
    }
    // Wi-Fi voices that have rung out
    int live = 0;
    for (int k = 0; k < NWV; k++) {
        wvoice_t *v = &wv[k];
        if (!v->on) continue;
        live++;
        if (v->delay > 0 || v->stealing || v->exc_n > 0 || v->gate > 0) { v->quiet = 0; continue; }
        float e = 0;
        if (v->t == T_PLUCK || v->t == T_BELL || v->t == T_KALIMBA) { for (int j = 0; j < v->nm; j++) e += fabsf(v->m[j].y1) + fabsf(v->m[j].y2); }
        else e = v->env;
        if (e * v->amp < 2e-5f) { if (++v->quiet > 2) v->on = 0; } else v->quiet = 0;
    }
    for (int k = 0; k < NBV; k++) live += bv[k].on;
    snd_voices = live;
    echo_lp = flush_denormal(echo_lp); bright_lp = flush_denormal(bright_lp); wind_lp = flush_denormal(wind_lp);
    brown1 = flush_denormal(brown1); brown2 = flush_denormal(brown2);
    pk_w = mx_w > pk_w ? mx_w : pk_w * 0.97f; pk_b = mx_b > pk_b ? mx_b : pk_b * 0.97f;
    pk_r = mx_r > pk_r ? mx_r : pk_r * 0.97f; pk_s = mx_s > pk_s ? mx_s : pk_s * 0.97f;
    snd_out_wifi = pk_w; snd_out_bt = pk_b; snd_out_room = pk_r; snd_out_storm = pk_s;
}

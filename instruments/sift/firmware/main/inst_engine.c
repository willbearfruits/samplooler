// SIFT's audio task: what REC records goes into the corpus; grains play from it; dirt and the space after them.
//
// DECOMPOSE (the default mode). A head moves through the recording at eng_speed. Grains are taken at it one after
// another, and how they are taken is set by one number, D: the knob, the tilt's push and the shake (eng_chaos):
//   D = 0     grains of 120 ms every 60 ms, each read from where the head is when it starts, under a Hann window:
//             at speed 1 they add up to the recording exactly (overlap-add), so it plays whole
//   the tape wears (0 .. 0.15): a grain now and then repeats (a skip), a hole, a slow wow in the pitch
//   syllables come loose (0.1 .. 0.5): the stream stays whole, but each time the head reaches a syllable (eng_cut:
//             the material's own cuts, or where the level jumps) it may be damaged, a chance rising from nothing to
//             nearly all: stammered (its start said again and again while the line waits: STUTTER), swapped with a
//             neighbour (JUMPS), dropped (GAPS), backwards (REVERSE) or out of tune (SCATTER). The words go one by one
//   phonemes (0.45 .. 0.75): the grains shrink from 120 to 20 ms, come from nearby and then from wherever sounds like
//             this moment (the catch the UI publishes round the head's dot on the map), thrown in pitch, holes
//   dust (0.75 .. 1): 20 down to 4 ms from anywhere, at DENSITY, two octaves of pitch, backwards, hard-edged
// The far end is made violent by BITE: it adds drive and crush as D rises. FEEDBACK runs the output through a delay
// (FB TIME) and back in, the drive inside the loop; ROT writes the output back into the corpus where the head reads,
// so the decay stays in the recording (it is played again, and torn again). Takes from the stream at speed 1 add up
// to what is there, so at D = 0 ROT writes back what it read (plus the space and the tone, which build up: a room).
//
// MAP. Two clocks make the grains. The order side's is the flock's grid (link_origin / link_fpb: alone, the board's
// own session): every step a grain, or STACK of them, from the pattern the UI publishes, the pattern's step taken
// from the grid's own count so it stays locked to the bar. The chaos side's is a Poisson process, DENSITY grains a
// second at full chaos. Between the two the grid thins out (a step plays with a chance of 1 - chaos^2 and drifts
// late by up to 0.45 of a step) while the swarm thickens (DENSITY * chaos^1.5). A grain takes its pitch from the
// catch (tuned on the order side, free on the chaos side, blended by chaos, plus SCATTER and DRIFT), plays backwards
// with a chance of REVERSE * chaos, and wanders from its unit's attack by up to the catch's spread * chaos.
//
// Grains read 16-bit samples from PSRAM with linear interpolation under one of five windows. Their level is set when
// they start (the take's gain, the shape's); LEVEL, the follower's gate, 1 / sqrt(how many overlap) and TONE are
// smoothed after the sum, so nothing the UI moves can click. The space is a four-line feedback delay network (Hadamard
// mix, damped, two allpasses in front) in internal RAM.
#include "inst_sift.h"
#include "dsp.h"
#include "link.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "usb.h"
#include "esp_attr.h"                       // voice_start: the compiler clones it, so it is placed in IRAM by name here
#else
#define IRAM_ATTR
int (*sift_sim_usb_in)(int16_t *mono, int n);   // the tests' sound card
static int usb_audio_in_pull(int16_t *mono, int n)
{
    int got = sift_sim_usb_in ? sift_sim_usb_in(mono, n) : 0;
    for (int i = got; i < n; i++) mono[i] = 0;
    return got;
}
#endif

int16_t *corp;
int corp_frames, corp_units;

// ---------------------------------------------------------------- shared with the UI
volatile int      eng_rec_state;
volatile uint32_t eng_wr, eng_take_seq;
volatile int      eng_take_units, eng_rec_full;
volatile float    eng_rec_level;
float             eng_live[LIVE_N];
volatile uint32_t eng_live_w;

volatile float eng_chaos = 0.3f, eng_size = 0.09f, eng_density = 30.0f, eng_reverse, eng_scatter, eng_detune, eng_level = 0.8f;
volatile int   eng_shape, eng_div = 4, eng_stack = 1;
volatile float eng_gate = 1.0f, eng_tone, eng_space, eng_size_mul = 1.0f;
volatile float eng_fb[4];
volatile uint32_t eng_burst_req;
volatile int   eng_mode;
volatile float eng_pitch, eng_speed = 1.0f, eng_stutter = 1.0f, eng_jumps = 1.0f, eng_gaps = 1.0f, eng_bite;
volatile float eng_drive, eng_crush, eng_fb_amt, eng_fb_time = 0.18f, eng_rot;
volatile uint8_t  eng_unit_ok[NU_MAX];
volatile float    eng_unit_gain[NU_MAX];
volatile uint8_t  eng_cut[NU_MAX];
volatile uint16_t eng_cut_at[NU_MAX];
volatile float    eng_head, eng_head_speed = 1.0f;
volatile int16_t eng_vis_unit[NV];
volatile float   eng_vis_env[NV];
volatile float   eng_out_lvl;

SPSC(spawnq, spawn_t, 64)
static spawnq_t spawnq;
bool eng_spawn_pop(spawn_t *s) { return spawnq_pop(&spawnq, s); }

// the catch, double-buffered: the UI fills the one the audio task is not reading, then flips the index
static cands_t cbuf[2];
static volatile int cidx;
static volatile uint32_t cseq;
void eng_cands_publish(const cands_t *c)
{
    int w = cidx ^ 1;
    cbuf[w] = *c;
    __atomic_store_n(&cidx, w, __ATOMIC_RELEASE);
    __atomic_add_fetch(&cseq, 1, __ATOMIC_RELEASE);
}

// recording requests
enum { RQ_NONE, RQ_START, RQ_STOP };
static volatile int rq;
static volatile uint32_t rq_seq, rq_from;
static volatile int rq_fresh, rq_src;
static volatile uint32_t hush_seq;
void eng_rec_start(uint32_t from, bool fresh, int src)
{
    rq_from = from; rq_fresh = fresh ? 1 : 0; rq_src = src; rq = RQ_START;
    __atomic_add_fetch(&rq_seq, 1, __ATOMIC_RELEASE);
}
void eng_rec_stop(void) { rq = RQ_STOP; __atomic_add_fetch(&rq_seq, 1, __ATOMIC_RELEASE); }
void eng_hush(void) { __atomic_add_fetch(&hush_seq, 1, __ATOMIC_RELEASE); }

// ---------------------------------------------------------------- the windows
#define WIN_N 512
static float win[NSHAPE][WIN_N + 2];
static float shape_gain[NSHAPE], shape_mean[NSHAPE];

static void windows_init(void)
{
    float ref = 0;
    for (int s = 0; s < NSHAPE; s++) {
        float e = 0, m = 0;
        for (int i = 0; i <= WIN_N + 1; i++) {
            float t = (float)(i > WIN_N ? WIN_N : i) / (float)WIN_N, w;
            switch (s) {
            case SH_PLUCK: {                                      // 3 % attack, then a fall, closed at the end
                const float a = 0.03f;
                w = t < a ? t / a : expf(-4.5f * (t - a) / (1.0f - a));
                if (t > 0.92f) w *= 0.5f + 0.5f * cosf(3.14159265f * (t - 0.92f) / 0.08f);
                break;
            }
            case SH_SWELL: {                                      // the pluck backwards
                const float a = 0.03f; float u = 1.0f - t;
                w = u < a ? u / a : expf(-4.5f * (u - a) / (1.0f - a));
                if (u > 0.92f) w *= 0.5f + 0.5f * cosf(3.14159265f * (u - 0.92f) / 0.08f);
                break;
            }
            case SH_SLICE:                                        // flat, with 8 % fades
                w = t < 0.08f ? 0.5f - 0.5f * cosf(3.14159265f * t / 0.08f) : t > 0.92f ? 0.5f - 0.5f * cosf(3.14159265f * (1.0f - t) / 0.08f) : 1.0f;
                break;
            case SH_HARD:                                         // flat, 1.5 % fades: it snaps on and off
                w = t < 0.015f ? t / 0.015f : t > 0.985f ? (1.0f - t) / 0.015f : 1.0f;
                break;
            default:                                              // Hann
                w = 0.5f - 0.5f * cosf(6.2831853f * t);
                break;
            }
            win[s][i] = w;
            if (i < WIN_N) { e += w * w; m += w; }
        }
        float rms = sqrtf(e / (float)WIN_N);
        shape_mean[s] = m / (float)WIN_N;
        if (s == SH_SOFT) ref = rms;
        shape_gain[s] = sqrtf(ref / rms);                          // half way to equal loudness: a pluck stays a pluck
    }
    shape_gain[SH_SOFT] = 1.0f;
    win[SH_SOFT][WIN_N] = win[SH_SOFT][WIN_N + 1] = 0.0f;
    win[SH_HARD][WIN_N] = win[SH_HARD][WIN_N + 1] = 0.0f;
}

// ---------------------------------------------------------------- voices
typedef struct {
    float pos, rate, ph, dph, amp, fade, dfade;
    int   delay;                 // samples into this block before it starts
    int16_t unit;
    uint8_t on, shape;
} voice_t;
static voice_t vo[NV];
static uint32_t rng = 0x5eed1234u;

int eng_voices_on(void) { int n = 0; for (int i = 0; i < NV; i++) n += vo[i].on; return n; }

static inline float fast_log2(float x)
{
    union { float f; uint32_t i; } u = { .f = x };
    float e = (float)(int)((u.i >> 23) & 255) - 128.0f;              // the polynomial below adds the 1 back on [1, 2)
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}

static inline float wrapf(float p) { float m = (float)corp_frames; while (p < 0.0f) p += m; while (p >= m) p -= m; return p; }
static inline int32_t circ(uint32_t a, uint32_t b)   // a - b on the ring, -N/2..N/2
{
    int32_t d = (int32_t)a - (int32_t)b, h = corp_frames / 2;
    if (d > h) d -= corp_frames; else if (d < -h) d += corp_frames;
    return d;
}
static inline int unit_of(float p) { int u = (int)(p * (1.0f / (float)UNIT)); return u < 0 ? 0 : u >= corp_units ? corp_units - 1 : u; }

// ---------------------------------------------------------------- the audio task's own state
static cands_t cur;                          // the catch this block
static float chaos_s = 0.3f, gate_s = 1.0f, level_s = 0.8f, tone_s, space_s, speed_s = 1.0f, comp_s = 1.0f;
static svf_t f_lp, f_hp;
static uint32_t rq_seen, hush_seen, burst_seen;
static int rec_src, rec_fresh, rec_ramp;
static float self_buf[AUDIO_BLOCK];
typedef struct { uint32_t at; int32_t step; uint8_t stack; } pend_t;
static pend_t pend[8];
static int npend;

// the space
static const int rv_len[4] = { 1559, 1877, 2203, 2617 }, ap_len[2] = { 223, 557 };
static float *rv_buf[4], *ap_buf[2];
static int rv_w[4], ap_w[2];
static float rv_lp[4], rv_damp;

// dirt: the drive's and the crush's amounts (smoothed), the crush's hold, the feedback's line (PSRAM, int16)
#define FB_N 32768                           // 1.02 s
static int16_t *fbuf;
static int fb_w;
static float fb_s, fb_t_s = 0.18f * AUDIO_SR, fb_hp, fb_hpx, drive_s, crush_s, crush_held;
static int crush_hc;

// one grain, wherever it comes from. start: where it begins in corp (backwards: where its stretch begins); len in frames
static IRAM_ATTR bool voice_start(int off, float start, float len, float semi, bool rev, int shape, float amp, int kind, float dist)
{
    if (!corp || len < 16.0f) return false;
    float rate = fast_exp2(clampf(semi, -36.0f, 36.0f) * (1.0f / 12.0f));
    float reach = len * rate;
    if (rev) start += reach;                                       // the same stretch of sound, backwards
    start = wrapf(start);
    uint32_t s0 = (uint32_t)start;
    if (eng_rec_state != RS_OFF) {                                 // never next to what is being written
        int32_t d = circ(s0, eng_wr);
        if ((d < 0 ? -d : d) < (int32_t)reach + 2 * UNIT) return false;
    }
    // a voice: a free one; else the one nearest its end fades in 2 ms and this grain is dropped
    int vi = -1;
    for (int i = 0; i < NV; i++) if (!vo[i].on) { vi = i; break; }
    if (vi < 0) {
        float best = 0.5f;
        for (int i = 0; i < NV; i++) if (vo[i].dfade == 0.0f && vo[i].ph > best) { best = vo[i].ph; vi = i; }
        if (vi >= 0) vo[vi].dfade = 1.0f / 64.0f;
        return false;
    }
    voice_t *v = &vo[vi];
    v->pos = start;
    v->rate = rev ? -rate : rate;
    v->ph = 0.0f; v->dph = 1.0f / len;
    v->amp = amp * shape_gain[shape];
    v->fade = 1.0f; v->dfade = 0.0f;
    v->delay = off < 0 ? 0 : off >= AUDIO_BLOCK ? AUDIO_BLOCK - 1 : off;
    v->unit = (int16_t)unit_of(start); v->shape = (uint8_t)shape; v->on = 1;
    spawn_t ev = { audio_frames() + (uint32_t)v->delay, s0, v->unit, (uint8_t)kind, (uint8_t)rev,
                   (uint16_t)(len > 65535.0f ? 65535.0f : len), semi, v->amp, dist };
    spawnq_push(&spawnq, &ev);
    return true;
}

// ---------------------------------------------------------------- MAP: a grain from the catch
static void spawn_map(int off, int kind, int32_t step)
{
    if (cur.n <= 0) return;
    float ce = chaos_s;
    int ci;
    if (kind == GK_ORDER && cur.nseq > 0) {
        int32_t k = step % cur.nseq; if (k < 0) k += cur.nseq;
        ci = cur.seq[k];
    } else {
        float r = rng_uni(&rng);
        ci = 0;
        while (ci < cur.n - 1 && cur.c[ci].wcum < r) ci++;
    }
    if (ci < 0 || ci >= cur.n) return;
    const cand_t *c = &cur.c[ci];
    float semi = c->semi_order + (c->semi_free - c->semi_order) * ce;
    semi += rng_bi(&rng) * eng_scatter * ce * ce + rng_bi(&rng) * eng_detune * 0.01f;
    semi = clampf(semi, -36.0f, 36.0f);
    bool rev = kind != GK_ORDER && rng_uni(&rng) < eng_reverse * ce;
    float len = eng_size * eng_size_mul * (float)AUDIO_SR * (1.0f + 0.5f * ce * rng_bi(&rng));
    if (len < 96.0f) len = 96.0f;
    if (len > 1.6f * (float)AUDIO_SR) len = 1.6f * (float)AUDIO_SR;
    float start = (float)c->start + (ce > 0.0f ? rng_bi(&rng) * ce * cur.spread : 0.0f);
    voice_start(off, start, len, semi, rev, eng_shape, c->gain, kind, 0.0f);
}

// the first grid line at or after frame f, and its number since beat 0 of the session
static uint32_t grid_at(uint32_t f, int32_t *step)
{
    uint32_t o; float fr; link_origin(&o, &fr);
    uint32_t fpb = link_fpb(); if (fpb < 1000) fpb = 16000;
    int div = eng_div; if (div < 1) div = 1;
    float st = (float)fpb / (float)div;
    int32_t rel = (int32_t)(f - o), m = rel % (int32_t)fpb;
    int32_t beat = rel / (int32_t)fpb;
    if (m < 0) { m += (int32_t)fpb; beat--; }
    float p = (float)m - fr;
    if (p < 0.0f) { p += (float)fpb; beat--; }
    int k = (int)(p / st);
    float t = (float)k * st - p;                                   // <= 0: line k is at or before f
    if (t < -0.5f) { k++; t += st; }
    if (k >= div) { k -= div; beat++; }
    if (step) *step = beat * div + k;
    return f + (uint32_t)(int32_t)(t + 0.5f);
}

static void map_block(uint32_t f0, int n)
{
    float ce = chaos_s;
    uint32_t f1 = f0 + (uint32_t)n;
    // the order side: grid lines in this block, each drifting late with chaos, kept until their frame comes
    int32_t step;
    uint32_t t = grid_at(f0, &step);
    float st = (float)(link_fpb() < 1000 ? 16000 : link_fpb()) / (float)(eng_div < 1 ? 1 : eng_div);
    while ((int32_t)(t - f1) < 0) {
        if (rng_uni(&rng) >= ce * ce && npend < 8) {
            uint32_t at = t + (uint32_t)(int32_t)(rng_uni(&rng) * 0.45f * st * ce);
            pend[npend++] = (pend_t){ at, step, (uint8_t)clampi(eng_stack, 1, 4) };
        }
        t = grid_at(t + 1, &step);
    }
    for (int i = 0; i < npend;) {
        if ((int32_t)(pend[i].at - f1) < 0) {
            int off = (int32_t)(pend[i].at - f0);
            for (int s = 0; s < pend[i].stack; s++) spawn_map(off, GK_ORDER, pend[i].step + s);
            pend[i] = pend[--npend];
        } else i++;
    }
    // the chaos side: a Poisson count for this block, each at a random place in it
    float rate = eng_density * ce * sqrtf(ce);
    float lam = rate * (float)n / (float)AUDIO_SR;
    if (lam > 0.0005f) {
        float p = fast_exp2(-lam * 1.442695f), s = p, u = rng_uni(&rng);
        int k = 0;
        while (u > s && k < 6) { k++; p *= lam / (float)k; s += p; }
        for (int j = 0; j < k; j++) spawn_map((int)(rng_uni(&rng) * (float)n), GK_CHAOS, 0);
    }
    // how many overlap
    float fpb = (float)(link_fpb() < 1000 ? 16000 : link_fpb());
    float grid_rate = (float)AUDIO_SR / fpb * (float)(eng_div < 1 ? 1 : eng_div) * (float)clampi(eng_stack, 1, 4);
    float overlap = (grid_rate * (1.0f - ce * ce) + eng_density * ce * sqrtf(ce)) * eng_size * eng_size_mul;
    comp_s += (1.0f / sqrtf(overlap > 1.0f ? overlap : 1.0f) - comp_s) * 0.05f;
}

// ---------------------------------------------------------------- DECOMPOSE
static float head, d_wait, wow_ph, last_hpos;
static float freeze;                         // frames the head waits: a stammer holds the line
static int   rep_left;                       // a grain said again (a skip, at the ends of the knob)
static float rep_pos, rep_len;
typedef struct { uint32_t at; float pos, len, semi; uint8_t rev, shape, kind; } dg_t;
static dg_t dq[16];                          // an altered syllable's grains, waiting for their frame
static int ndq;

typedef struct {
    float lg, ov, palter, prep, pjl, pjg, pany, jit, pgap, sig, prev, soft, hard, hopmin, wow;
} dc_t;

// D in, how the grains are taken out. The stream stays a smooth overlap-add up to the middle, so the words are
// broken by what happens to syllables, not by the grains chopping; then the grains shrink to phonemes and dust.
static const float curve_d[4] = { 0.45f, 0.75f, 1.0f, 1.0f };
static const float curve_l[4] = { -3.0589f, -5.6439f, -7.9658f, -7.9658f };  // log2 of 0.12 0.02 0.004 s
static void curves(float D, dc_t *c)
{
    const float *kd = curve_d, *kl = curve_l;
    float scale = clampf(eng_size / 0.09f, 0.25f, 4.0f) * eng_size_mul;
    float l2 = kl[0];
    if (D > kd[0]) { int k = D > kd[1] ? 1 : 0; float t = clampf((D - kd[k]) / (kd[k + 1] - kd[k]), 0.0f, 1.0f); l2 = kl[k] + (kl[k + 1] - kl[k]) * t; }
    c->lg = D <= kd[0] ? 2.0f * (float)(int)(0.12f * (float)AUDIO_SR * scale * 0.5f + 0.5f)   // even: the overlap-add is exact
                       : fast_exp2(l2) * (float)AUDIO_SR * scale;
    c->ov = 0.5f + 0.35f * smoothstep(0.6f, 1.0f, D);
    c->palter = 0.95f * smoothstep(0.1f, 0.5f, D) * (1.0f - smoothstep(0.6f, 0.85f, D));   // a syllable's chance of being damaged
    c->prep = (0.06f * smoothstep(0.02f, 0.15f, D) * (1.0f - smoothstep(0.4f, 0.55f, D))
             + 0.25f * smoothstep(0.5f, 0.75f, D) * (1.0f - 0.6f * smoothstep(0.8f, 1.0f, D))) * eng_stutter;
    float jm = fminf(eng_jumps, 1.0f);
    c->pjl = 0.4f * smoothstep(0.35f, 0.6f, D) * (1.0f - smoothstep(0.7f, 0.9f, D)) * eng_jumps;
    c->pjg = smoothstep(0.45f, 0.75f, D) * (1.0f - smoothstep(0.85f, 1.0f, D)) * jm;
    c->pany = smoothstep(0.75f, 1.0f, D) * (0.5f + 0.5f * jm);
    c->jit = smoothstep(0.4f, 1.0f, D) * 0.04f * (float)AUDIO_SR;
    c->pgap = (0.03f * smoothstep(0.05f, 0.2f, D) + 0.1f * smoothstep(0.35f, 0.65f, D) + 0.2f * smoothstep(0.75f, 1.0f, D)) * eng_gaps;
    c->sig = 0.12f * smoothstep(0.05f, 0.3f, D) + eng_scatter * (0.1f * smoothstep(0.3f, 0.55f, D) + 1.3f * smoothstep(0.55f, 1.0f, D));
    c->prev = eng_reverse * smoothstep(0.45f, 0.85f, D);
    c->soft = 1.0f - smoothstep(0.5f, 0.65f, D);
    c->hard = smoothstep(0.7f, 0.9f, D);
    c->hopmin = smoothstep(0.55f, 1.0f, D) * (float)AUDIO_SR / (2.0f * fmaxf(eng_density, 1.0f));
    c->wow = 0.25f * smoothstep(0.02f, 0.35f, D);
}

// the syllable that starts at the cut at a: where the next one starts (or 0.3 s on)
static float seg_end(float a)
{
    int u = unit_of(a);
    for (int k = 1; k < 24; k++) {
        int w = (u + k) % corp_units;
        if (!eng_unit_ok[w]) return (float)(w * UNIT) + (w * UNIT < (int)a ? (float)corp_frames : 0.0f);
        if (eng_cut[w]) { float b = (float)(w * UNIT + eng_cut_at[w]); return b < a ? b + (float)corp_frames : b; }
    }
    return a + 0.3f * (float)AUDIO_SR;
}
// the start of the syllable k syllables on (back when k < 0) from the one at pos
static float seg_step(float pos, int k)
{
    int u = unit_of(pos), dir = k > 0 ? 1 : -1, left = k > 0 ? k : -k;
    for (int s = 1; s < 120 && left > 0; s++) {
        int w = u + dir * s; w = ((w % corp_units) + corp_units) % corp_units;
        if (!eng_unit_ok[w]) continue;
        if (eng_cut[w] && --left == 0) return (float)(w * UNIT + eng_cut_at[w]);
    }
    return wrapf(pos + (float)dir * (float)left * 0.25f * (float)AUDIO_SR);
}
// a syllable starts between the two places (the head moving forwards from p0 to p1): where
static bool cut_between(float p0, float p1, float *a)
{
    float span = p1 - p0;
    if (span < 0.0f) span += (float)corp_frames;
    if (span <= 0.0f || span > 0.25f * (float)AUDIO_SR) return false;
    int u0 = unit_of(p0), n = unit_of(p0 + span >= (float)corp_frames ? p0 + span - (float)corp_frames : p0 + span) - u0;
    if (n < 0) n += corp_units;
    for (int k = 0; k <= n; k++) {
        int w = (u0 + k) % corp_units;
        if (!eng_cut[w] || !eng_unit_ok[w]) continue;
        float c = (float)(w * UNIT + eng_cut_at[w]), d = c - p0;
        if (d < 0.0f) d += (float)corp_frames;
        if (d > 0.0f && d <= span) { *a = c; return true; }
    }
    return false;
}
static float any_unit(void)
{
    for (int tries = 0; tries < 16; tries++) {
        int u = (int)(rng_uni(&rng) * (float)corp_units);
        if (u < corp_units && eng_unit_ok[u]) return (float)u * UNIT + rng_uni(&rng) * (float)UNIT;
    }
    return head;
}

// keep the head on the recording: past its end (or before its start), on to the next unit that is part of it
static bool head_fix(float dir)
{
    head = wrapf(head);
    int hu = unit_of(head);
    if (eng_unit_ok[hu]) return true;
    for (int k = 1; k < corp_units; k++) {
        int w = hu + (dir >= 0.0f ? k : -k); w = ((w % corp_units) + corp_units) % corp_units;
        if (eng_unit_ok[w]) { head = dir >= 0.0f ? (float)(w * UNIT) : (float)(w * UNIT + UNIT - 1); return true; }
    }
    return false;
}

static void dq_push(uint32_t at, float pos, float len, float semi, bool rev, int shape, int kind)
{
    if (ndq < 16) dq[ndq++] = (dg_t){ at, wrapf(pos), len, semi, (uint8_t)rev, (uint8_t)shape, (uint8_t)kind };
}
static void dq_run(uint32_t f0, int n)
{
    uint32_t f1 = f0 + (uint32_t)n;
    for (int i = 0; i < ndq;) {
        if ((int32_t)(dq[i].at - f1) < 0) {
            const dg_t *g = &dq[i];
            voice_start((int32_t)(g->at - f0), g->pos, g->len, g->semi, g->rev, g->shape, eng_unit_gain[unit_of(g->pos)], g->kind,
                        (float)circ((uint32_t)g->pos, (uint32_t)head));
            dq[i] = dq[--ndq];
        } else i++;
    }
}

// a syllable damaged as it comes: stammered (the line waits), swapped with a neighbour, dropped, backwards, out of
// tune. STUTTER, JUMPS, GAPS, REVERSE and SCATTER weigh which. Returns how long it lasts.
static float alter(uint32_t t0, float a, float D)
{
    const float maxl = 0.6f * (float)AUDIO_SR;
    float seg = seg_end(a) - a;
    if (seg < 0.04f * (float)AUDIO_SR) seg = 0.04f * (float)AUDIO_SR;
    float base = eng_pitch + 0.25f * smoothstep(0.02f, 0.35f, D) * sin_turns(wow_ph);
    float w[5] = { 1.2f * eng_stutter, 0.8f * eng_jumps, 0.5f * eng_gaps, 1.5f * eng_reverse, eng_scatter / 10.0f };
    float sum = w[0] + w[1] + w[2] + w[3] + w[4];
    if (sum <= 0.0f) return 16.0f;
    float r = rng_uni(&rng) * sum;
    int kind = 0;
    while (kind < 4 && r > w[kind]) { r -= w[kind]; kind++; }
    float len = fminf(seg, maxl);
    switch (kind) {
    case 0: {                                                      // stammered: its start, again and again
        float piece = fminf(seg, (0.1f + 0.12f * rng_uni(&rng)) * (float)AUDIO_SR);
        int k = 2 + (int)(rng_uni(&rng) * (1.5f + 3.0f * D));
        float hop = piece * 0.92f;
        for (int i = 0; i < k; i++) dq_push(t0 + (uint32_t)((float)i * hop), a, piece, base, false, SH_SLICE, i ? GK_REPEAT : GK_ORDER);
        freeze += (float)(k - 1) * hop;
        return (float)k * hop;
    }
    case 1: {                                                      // a neighbour in its place
        float b = seg_step(a, rng_uni(&rng) < 0.5f ? -1 - (int)(rng_uni(&rng) * 1.99f) : 1 + (int)(rng_uni(&rng) * 1.99f));
        float l2 = fminf(seg_end(b) - b, maxl);
        if (l2 < 0.04f * (float)AUDIO_SR) l2 = len;
        dq_push(t0, b, l2, base, false, SH_SLICE, GK_CHAOS);
        return len * 0.94f;
    }
    case 2: return len;                                            // gone: a hole the length of it
    case 3: dq_push(t0, a, len, base, true, SH_SLICE, GK_CHAOS); return len * 0.94f;
    default: {
        float st = (2.0f + 5.0f * rng_uni(&rng)) * (rng_uni(&rng) < 0.5f ? -1.0f : 1.0f);
        dq_push(t0, a, len, base + st, false, SH_SLICE, GK_ORDER);
        return len * 0.94f;
    }
    }
}

static float decomp_event(uint32_t f0, int off, const dc_t *c, float D)
{
    float hpos = wrapf(head + speed_s * (float)off);
    // a syllable starting since the last grain: damaged, or not
    if (speed_s > 0.05f && c->palter > 0.0f) {
        float a;
        bool crossed = cut_between(last_hpos, hpos, &a);
        last_hpos = hpos;
        if (crossed && rng_uni(&rng) < c->palter) { rep_left = 0; return alter(f0 + (uint32_t)off, a, D); }
    } else last_hpos = hpos;
    // the stream
    float pos = hpos, len = c->lg;
    int kind = GK_ORDER;
    if (rep_left > 0) { pos = rep_pos; len = rep_len; rep_left--; kind = GK_REPEAT; }
    else {
        float r = rng_uni(&rng);
        if (r < c->pjl) { pos = wrapf(hpos + rng_bi(&rng) * (0.1f + 0.4f * D) * (float)AUDIO_SR); kind = GK_CHAOS; }   // nearby
        else if (r < c->pjl + c->pjg && cur.n > 0) {             // from wherever sounds like this moment
            float q = rng_uni(&rng);
            int ci = 0;
            while (ci < cur.n - 1 && cur.c[ci].wcum < q) ci++;
            pos = (float)cur.c[ci].start;
            kind = GK_CHAOS;
        } else if (r < c->pjl + c->pjg + c->pany) { pos = any_unit(); kind = GK_CHAOS; }
        if (c->jit > 0.0f) pos = wrapf(pos + rng_bi(&rng) * c->jit);
        if (c->prep > 0.0f && rng_uni(&rng) < c->prep) { rep_left = 1 + (int)(rng_uni(&rng) * (1.0f + 3.0f * D)); rep_pos = pos; rep_len = len; }
    }
    float hop = len * (1.0f - c->ov);
    if (hop < c->hopmin) hop = c->hopmin;
    if (hop < 16.0f) hop = 16.0f;
    if (c->pgap > 0.0f && rng_uni(&rng) < c->pgap) return hop;     // a hole
    float semi = eng_pitch + c->wow * sin_turns(wow_ph) + (c->sig > 0.0f ? rng_bi(&rng) * c->sig : 0.0f) + rng_bi(&rng) * eng_detune * 0.01f * D;
    bool rev = c->prev > 0.0f && rng_uni(&rng) < c->prev;
    float q = rng_uni(&rng);
    int shape = q < c->soft ? SH_SOFT : q < 1.0f - c->hard ? SH_SLICE : SH_HARD;
    float amp = eng_unit_gain[unit_of(pos)] * (1.0f + 0.6f * D * D * rng_bi(&rng));
    voice_start(off, pos, len, semi, rev, shape, amp, kind, (float)circ((uint32_t)pos, (uint32_t)hpos));
    return hop;
}

static void decomp_block(uint32_t f0, int n)
{
    float D = chaos_s;
    dc_t c;
    curves(D, &c);
    wow_ph = wrap1(wow_ph + 0.7f * (float)n / (float)AUDIO_SR);
    if (!head_fix(speed_s)) { ndq = 0; return; }                    // nothing recorded
    dq_run(f0, n);
    while (d_wait < (float)n) {
        int off = (int)d_wait;
        d_wait += decomp_event(f0, off < 0 ? 0 : off, &c, D);
    }
    d_wait -= (float)n;
    dq_run(f0, n);
    // a shake's burst: grains from anywhere, short and hard
    uint32_t br = __atomic_load_n(&eng_burst_req, __ATOMIC_ACQUIRE);
    int nb = (int)(br - burst_seen);
    if (nb > 0) {
        if (nb > 6) nb = 6;
        burst_seen += (uint32_t)nb;
        for (int j = 0; j < nb; j++) {
            float p = any_unit();
            voice_start((int)(rng_uni(&rng) * (float)n), p, 0.03f * (float)AUDIO_SR, 12.0f * rng_bi(&rng), rng_uni(&rng) < 0.5f, SH_HARD,
                        eng_unit_gain[unit_of(p)], GK_BURST, (float)circ((uint32_t)p, (uint32_t)head));
        }
    }
    // the head moves on, unless a stammer holds it
    float go = (float)n;
    if (freeze > 0.0f) { float f = fminf(freeze, go); freeze -= f; go -= f; }
    if (freeze > 2.0f * (float)AUDIO_SR) freeze = 2.0f * (float)AUDIO_SR;
    head = wrapf(head + speed_s * go);
    eng_head = head;
    // how many overlap (a stream at speed 1 adds up exactly: Hann at half its length is 1)
    float hop = c.lg * (1.0f - c.ov);
    if (hop < c.hopmin) hop = c.hopmin;
    float mean = c.soft * shape_mean[SH_SOFT] + (1.0f - c.soft - c.hard) * shape_mean[SH_SLICE] + c.hard * shape_mean[SH_HARD];
    float nov = c.lg / fmaxf(hop, 16.0f) * mean;
    comp_s += (1.0f / sqrtf(nov > 1.0f ? nov : 1.0f) - comp_s) * 0.05f;
}

// ---------------------------------------------------------------- the grains
static void voice_run(voice_t *v, float *acc, int n)
{
    const float *w = win[v->shape];
    const int16_t *b = corp;
    const int max = corp_frames;
    const float maxf = (float)max;
    float pos = v->pos, rate = v->rate, wp = v->ph * (float)WIN_N, dwp = v->dph * (float)WIN_N;
    float g = v->amp * v->fade, dg = v->dfade * v->amp;
    int i = v->delay;
    v->delay = 0;
    for (; i < n; i++) {
        int wi = (int)wp;
        float ww = w[wi] + (wp - (float)wi) * (w[wi + 1] - w[wi]);
        int ip = (int)pos;
        float fr = pos - (float)ip;
        int ip1 = ip + 1 < max ? ip + 1 : 0;
        float s0 = (float)b[ip];
        acc[i] += (s0 + fr * ((float)b[ip1] - s0)) * ww * g;
        pos += rate;
        if (pos >= maxf) pos -= maxf; else if (pos < 0.0f) pos += maxf;
        wp += dwp;
        if (wp >= (float)WIN_N) { v->on = 0; break; }
        if (dg > 0.0f) { g -= dg; if (g <= 0.0f) { v->on = 0; break; } }
    }
    v->pos = pos; v->ph = wp * (1.0f / (float)WIN_N);
    v->fade = v->amp > 0.0f ? g / v->amp : 0.0f;
}

static void record(const float *in, int n)
{
    float src[AUDIO_BLOCK];
    if (rec_src == SRC_USB) {
        int16_t raw[AUDIO_BLOCK];
        usb_audio_in_pull(raw, n);
        for (int i = 0; i < n; i++) src[i] = (float)raw[i] * (1.0f / 32768.0f);
    } else if (rec_src == SRC_SELF) for (int i = 0; i < n; i++) src[i] = self_buf[i];
    else if (rec_src == SRC_MIX) for (int i = 0; i < n; i++) src[i] = in[i] + self_buf[i];
    else for (int i = 0; i < n; i++) src[i] = in[i];
    uint32_t wr = eng_wr;
    float pk = 0;
    for (int i = 0; i < n; i++) {
        float g;
        if (eng_rec_state == RS_ON) g = rec_ramp < 64 ? (float)(rec_ramp++) / 64.0f : 1.0f;
        else { g = (float)(64 - rec_ramp) / 64.0f; rec_ramp++; }
        float x = soft_tanh(src[i] * g);
        float a = fabsf(x); if (a > pk) pk = a;
        corp[wr] = (int16_t)(x * 32000.0f);
        wr++;
        if ((wr % UNIT) == 0) eng_take_units++;
        if (wr >= (uint32_t)corp_frames) {
            wr = 0;
            if (rec_fresh) { eng_rec_full = 1; eng_rec_state = RS_OFF; break; }   // TAKES NEW: the corpus is full
        }
        if (eng_rec_state == RS_TAIL && rec_ramp >= 64) {
            uint32_t rest = (UNIT - wr % UNIT) % UNIT;                 // silence to the end of the unit
            if (rest) {
                memset(&corp[wr], 0, rest * sizeof(int16_t));
                wr += rest; if (wr >= (uint32_t)corp_frames) wr = 0;
                eng_take_units++;
            }
            eng_rec_state = RS_OFF;
            break;
        }
    }
    eng_wr = wr;
    if (pk > eng_rec_level) eng_rec_level = pk;
}

void eng_audio(const float *in, float *out, int n)
{
    uint32_t f0 = audio_frames();
    // the catch
    {
        uint32_t s = __atomic_load_n(&cseq, __ATOMIC_ACQUIRE);
        int k = __atomic_load_n(&cidx, __ATOMIC_ACQUIRE);
        cur = cbuf[k];
        if (__atomic_load_n(&cseq, __ATOMIC_ACQUIRE) != s) cur = cbuf[__atomic_load_n(&cidx, __ATOMIC_ACQUIRE)];
    }
    // the follower's ear
    {
        uint32_t w = eng_live_w;
        for (int i = 0; i < n; i++) eng_live[(w + (uint32_t)i) & (LIVE_N - 1)] = in[i];
        __atomic_store_n(&eng_live_w, w + (uint32_t)n, __ATOMIC_RELEASE);
    }
    // requests
    uint32_t hs = __atomic_load_n(&hush_seq, __ATOMIC_ACQUIRE);
    bool hush = hs != hush_seen; hush_seen = hs;
    uint32_t rs = __atomic_load_n(&rq_seq, __ATOMIC_ACQUIRE);
    if (rs != rq_seen) {
        rq_seen = rs;
        int r = rq;
        if (r == RQ_START && corp) {
            eng_wr = rq_from % (uint32_t)corp_frames;
            rec_fresh = rq_fresh; rec_src = rq_src; rec_ramp = 0;
            eng_take_units = 0; eng_rec_full = 0;
            eng_take_seq++;
            eng_rec_state = RS_ON;
            if (rec_fresh) hush = true;
        } else if (r == RQ_STOP && eng_rec_state == RS_ON) { eng_rec_state = RS_TAIL; rec_ramp = 0; }
    }
    if (hush) { for (int i = 0; i < NV; i++) if (vo[i].on && vo[i].dfade == 0.0f) vo[i].dfade = 1.0f / 320.0f; rep_left = 0; ndq = 0; freeze = 0.0f; }
    if (eng_rec_state != RS_OFF && corp) record(in, n);
    eng_rec_level *= 0.995f;

    // the controls, smoothed per block (the decomposition moves fast: the tilt throws it)
    chaos_s += (clampf(eng_chaos, 0.0f, 1.0f) - chaos_s) * (eng_mode == MODE_DECOMP ? 0.2f : 0.08f);
    if (chaos_s < 1e-4f && eng_chaos <= 0.0f) chaos_s = 0.0f;
    speed_s += (clampf(eng_speed, -2.0f, 3.0f) - speed_s) * 0.1f;
    if (fabsf(speed_s - eng_speed) < 1e-4f) speed_s = eng_speed;
    eng_head_speed = speed_s;
    float g0 = gate_s, l0 = level_s, comp0 = comp_s;
    gate_s += (clampf(eng_gate, 0.0f, 1.0f) - gate_s) * 0.15f;
    level_s += (clampf(eng_level, 0.0f, 1.0f) - level_s) * 0.15f;
    tone_s += (clampf(eng_tone, -1.0f, 1.0f) - tone_s) * 0.15f;
    float sp0 = space_s;
    space_s += (clampf(eng_space, 0.0f, 1.0f) - space_s) * 0.1f;

    // the grains' clocks
    if (eng_mode == MODE_DECOMP) decomp_block(f0, n);
    else {
        map_block(f0, n);
        uint32_t br = __atomic_load_n(&eng_burst_req, __ATOMIC_ACQUIRE);
        int nb = (int)(br - burst_seen);
        if (nb > 0) { if (nb > 6) nb = 6; burst_seen += (uint32_t)nb; for (int j = 0; j < nb; j++) spawn_map((int)(rng_uni(&rng) * (float)n), GK_BURST, 0); }
    }

    // the grains
    float acc[AUDIO_BLOCK];
    memset(acc, 0, sizeof(float) * (size_t)n);
    for (int i = 0; i < NV; i++) {
        voice_t *v = &vo[i];
        if (!v->on) { eng_vis_unit[i] = -1; continue; }
        if (eng_rec_state != RS_OFF && v->dfade == 0.0f) {           // the head is about to write where it reads
            int32_t d = circ((uint32_t)v->pos, eng_wr);
            if ((d < 0 ? -d : d) < 2 * UNIT) v->dfade = 1.0f / 64.0f;
        }
        voice_run(v, acc, n);
        eng_vis_unit[i] = v->on ? v->unit : -1;
        if (v->on) { float wp = v->ph * (float)WIN_N; eng_vis_env[i] = win[v->shape][(int)wp] * v->fade; }
    }

    // TONE: a low pass down, a high pass up
    svf_set(&f_lp, 18000.0f * fast_exp2(fminf(tone_s, 0.0f) * 6.0f), 0.7f);
    svf_set(&f_hp, 20.0f * fast_exp2(fmaxf(tone_s, 0.0f) * 7.3f), 0.7f);
    // dirt: the drive and the crush (BITE adds to them as D rises), the feedback's line
    float D = chaos_s;
    float dr0 = drive_s, cr0 = crush_s;
    float dsm = eng_mode == MODE_DECOMP ? smoothstep(0.35f, 1.0f, D) : 0.0f;
    drive_s += (clampf(eng_drive + eng_bite * 0.9f * dsm * dsm, 0.0f, 1.0f) - drive_s) * 0.1f;
    crush_s += (clampf(eng_crush + (eng_mode == MODE_DECOMP ? eng_bite * 0.8f * smoothstep(0.6f, 1.0f, D) : 0.0f), 0.0f, 1.0f) - crush_s) * 0.1f;
    float fb0 = fb_s;
    fb_s += (clampf(eng_fb_amt, 0.0f, 0.95f) - fb_s) * 0.1f;
    float fb_t_want = clampf(eng_fb_time, 0.01f, 1.0f) * (float)AUDIO_SR;
    bool dirt = drive_s > 1e-4f || dr0 > 1e-4f, crush = crush_s > 1e-4f || cr0 > 1e-4f, fbk = fbuf && (fb_s > 1e-4f || fb0 > 1e-4f);
    float dg0 = 1.0f + 30.0f * dr0 * dr0, dg1 = 1.0f + 30.0f * drive_s * drive_s;
    float dn0 = 0.3f / soft_tanh(0.3f * dg0), dn1 = 0.3f / soft_tanh(0.3f * dg1);
    float bits = 16.0f - 13.0f * crush_s;
    float qlev = fast_exp2(bits - 1.0f), qinv = 1.0f / qlev;
    int hold_n = 1 + (int)(crush_s * crush_s * 12.0f);
    // ROT: what the head read is written over by what comes out, at the take's own level
    float rot = eng_mode == MODE_DECOMP && eng_rec_state == RS_OFF ? clampf(eng_rot, 0.0f, 1.0f) * 0.6f : 0.0f;
    float rhead = wrapf(head - speed_s * (float)n);                 // where the head was at the start of this block
    float rgain = 1.0f;
    if (rot > 0.0f) {
        int ru = unit_of(rhead);
        float play = eng_unit_gain[ru] * 0.62f * level_s * comp_s * gate_s;
        rgain = play > 1e-9f ? 1.0f / play : 0.0f;
        if (!eng_unit_ok[ru]) rot = 0.0f;
    }

    const float trim = 0.62f;                                       // a grain peaks near 0.5 (its take's gain)
    float pk = 0;
    float inv = 1.0f / (float)n;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) * inv;
        float g = (g0 + (gate_s - g0) * t) * (l0 + (level_s - l0) * t) * (comp0 + (comp_s - comp0) * t);
        float x = acc[i] * trim * g;
        if (fbk) {                                                  // the loop: back in, a slow glide when FB TIME moves
            fb_t_s += (fb_t_want - fb_t_s) * 0.0015f;
            float rp = (float)fb_w - fb_t_s;
            while (rp < 0.0f) rp += (float)FB_N;
            int ri = (int)rp; float rf = rp - (float)ri;
            float y0 = (float)fbuf[ri & (FB_N - 1)], y1 = (float)fbuf[(ri + 1) & (FB_N - 1)];
            x += (fb0 + (fb_s - fb0) * t) * (y0 + rf * (y1 - y0)) * (1.0f / 16384.0f);
        }
        if (dirt) {
            float dr = dr0 + (drive_s - dr0) * t, dg = dg0 + (dg1 - dg0) * t, dn = dn0 + (dn1 - dn0) * t;
            float d = soft_tanh(x * dg) * dn;
            x += (d - x) * fminf(1.0f, dr * 4.0f);
        }
        if (crush) {
            if (++crush_hc >= hold_n) { crush_hc = 0; crush_held = x; }
            float h = crush_held * qlev;
            float y = (float)(int)(h + (h >= 0.0f ? 0.5f : -0.5f)) * qinv;
            x += (y - x) * fminf(1.0f, (cr0 + (crush_s - cr0) * t) * 5.0f);
        }
        if (fbuf) {                                                 // the loop's line: what comes out of the dirt, high-passed
            float hp = x - fb_hpx + 0.995f * fb_hp; fb_hpx = x; fb_hp = flush_denormal(hp);
            float v = clampf(hp, -1.99f, 1.99f) * 16384.0f;
            fbuf[fb_w] = (int16_t)v;
            fb_w = (fb_w + 1) & (FB_N - 1);
        }
        x = svf_lp(&f_lp, x);
        x = svf_hp(&f_hp, x);
        // the space
        float sp = sp0 + (space_s - sp0) * t;
        float d = x * sp;
        if (rv_buf[0]) {
            for (int k = 0; k < 2; k++) {
                float *ab = ap_buf[k]; int w = ap_w[k];
                float z = ab[w], vv = d - 0.6f * z;
                ab[w] = vv; d = z + 0.6f * vv;
                if (++w >= ap_len[k]) w = 0;
                ap_w[k] = w;
            }
            float r[4];
            for (int k = 0; k < 4; k++) {
                float y = rv_buf[k][rv_w[k]];
                rv_lp[k] += rv_damp * (y - rv_lp[k]);
                r[k] = rv_lp[k];
            }
            float h0 = 0.5f * (r[0] + r[1] + r[2] + r[3]), h1 = 0.5f * (r[0] - r[1] + r[2] - r[3]);
            float h2 = 0.5f * (r[0] + r[1] - r[2] - r[3]), h3 = 0.5f * (r[0] - r[1] - r[2] + r[3]);
            float hh[4] = { h0, h1, h2, h3 };
            for (int k = 0; k < 4; k++) {
                rv_buf[k][rv_w[k]] = flush_denormal(d + eng_fb[k] * hh[k]);
                if (++rv_w[k] >= rv_len[k]) rv_w[k] = 0;
            }
            x += 0.9f * (r[0] - r[1] + r[2] - r[3]) * 0.5f * (0.4f + 0.6f * sp);
        }
        if (rot > 0.0f) {
            int hp = (int)wrapf(rhead + speed_s * (float)i);
            float v = (1.0f - rot) * (float)corp[hp] + rot * x * rgain;
            corp[hp] = (int16_t)clampf(v, -32000.0f, 32000.0f);
        }
        out[i] = x;
        self_buf[i] = x;
        float a = fabsf(x); if (a > pk) pk = a;
    }
    for (int k = 0; k < 4; k++) rv_lp[k] = flush_denormal(rv_lp[k]);
    eng_out_lvl = fmaxf(pk, eng_out_lvl * 0.97f);
}

void eng_init(void)
{
    windows_init();
    static const int secs[] = { CORP_SECS, 12, 6 };
    for (int k = 0; k < 3 && !corp; k++) {
        int units = secs[k] * AUDIO_SR / UNIT;
        corp = app_alloc((size_t)units * UNIT * sizeof(int16_t));
        if (corp) { corp_units = units; corp_frames = units * UNIT; }
    }
    if (!corp) app_log("sift: no memory for the corpus");
    else app_log("sift: corpus %d s (%d units) in PSRAM", corp_frames / AUDIO_SR, corp_units);
    fbuf = app_alloc(FB_N * sizeof(int16_t));
    if (!fbuf) app_log("sift: no memory for the feedback line");
    for (int k = 0; k < 4; k++) rv_buf[k] = app_alloc_fast((size_t)rv_len[k] * sizeof(float));
    for (int k = 0; k < 2; k++) ap_buf[k] = app_alloc_fast((size_t)ap_len[k] * sizeof(float));
    for (int k = 0; k < 4; k++) if (!rv_buf[k]) rv_buf[0] = NULL;
    for (int k = 0; k < 2; k++) if (!ap_buf[k]) rv_buf[0] = NULL;
    rv_damp = onepole_hz(5200.0f);
    svf_set(&f_lp, 18000.0f, 0.7f); svf_set(&f_hp, 20.0f, 0.7f);
    for (int i = 0; i < NV; i++) eng_vis_unit[i] = -1;
}

// SIFT's audio task: what REC records goes into the corpus; grains play from it; the space after them.
//
// Two clocks make the grains. The order side's is the flock's grid (link_origin / link_fpb: alone, the board's
// own session): every step a grain, or STACK of them, from the pattern the UI publishes, the pattern's step taken
// from the grid's own count so it stays locked to the bar. The chaos side's is a Poisson process, DENSITY grains a
// second at full chaos. Between the two the grid thins out (a step plays with a chance of 1 - chaos^2 and drifts
// late by up to 0.45 of a step) while the swarm thickens (DENSITY * chaos^1.5), so one knob crosses from a pattern
// to a cloud. A grain takes its pitch from the catch (tuned on the order side, free on the chaos side, the two
// blended by chaos, plus SCATTER and DETUNE), plays backwards with a chance of REVERSE * chaos, and wanders from its
// unit's attack by up to the catch's spread * chaos.
//
// Grains read 16-bit samples from PSRAM with linear interpolation under one of four windows. Their level is set
// when they start (the take's gain, the shape's, 1 / sqrt(how many overlap)); LEVEL, the follower's gate and TONE
// are smoothed after the sum, so nothing the UI moves can click. The space is a four-line feedback delay network
// (Hadamard mix, damped, two allpasses in front) in internal RAM.
#include "inst_sift.h"
#include "dsp.h"
#include "link.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "usb.h"
#else
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
static float shape_gain[NSHAPE];

static void windows_init(void)
{
    float ref = 0;
    for (int s = 0; s < NSHAPE; s++) {
        float e = 0;
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
            default:                                              // Hann
                w = 0.5f - 0.5f * cosf(6.2831853f * t);
                break;
            }
            win[s][i] = w;
            if (i < WIN_N) e += w * w;
        }
        float rms = sqrtf(e / (float)WIN_N);
        if (s == SH_SOFT) ref = rms;
        shape_gain[s] = sqrtf(ref / rms);                          // half way to equal loudness: a pluck stays a pluck
    }
    win[SH_SOFT][WIN_N] = win[SH_SOFT][WIN_N + 1] = 0.0f;
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
    float e = (float)(int)((u.i >> 23) & 255) - 127.0f;
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}

static inline uint32_t wrapf(float p) { int32_t i = (int32_t)p; while (i < 0) i += corp_frames; while (i >= corp_frames) i -= corp_frames; return (uint32_t)i; }
static inline int32_t circ(uint32_t a, uint32_t b)   // a - b on the ring, -N/2..N/2
{
    int32_t d = (int32_t)a - (int32_t)b, h = corp_frames / 2;
    if (d > h) d -= corp_frames; else if (d < -h) d += corp_frames;
    return d;
}

// ---------------------------------------------------------------- the audio task's own state
static cands_t cur;                          // the catch this block
static float chaos_s = 0.3f, gate_s = 1.0f, level_s = 0.8f, tone_s, space_s;
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

static void spawn(int off, int kind, int32_t step)
{
    if (cur.n <= 0 || !corp) return;
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
    // pitch: tuned (order) towards free (chaos), thrown by SCATTER, drifting by DETUNE
    float semi = c->semi_order + (c->semi_free - c->semi_order) * ce;
    semi += rng_bi(&rng) * eng_scatter * ce * ce + rng_bi(&rng) * eng_detune * 0.01f;
    semi = clampf(semi, -36.0f, 36.0f);
    float rate = fast_exp2(semi * (1.0f / 12.0f));
    bool rev = kind != GK_ORDER && rng_uni(&rng) < eng_reverse * ce;
    float len = eng_size * eng_size_mul * (float)AUDIO_SR * (1.0f + 0.5f * ce * rng_bi(&rng));
    if (len < 96.0f) len = 96.0f;
    if (len > 1.6f * (float)AUDIO_SR) len = 1.6f * (float)AUDIO_SR;
    float start = (float)c->start + (ce > 0.0f ? rng_bi(&rng) * ce * cur.spread : 0.0f);
    float reach = len * rate;
    if (rev) start += reach;                                       // the same stretch of sound, backwards
    uint32_t s0 = wrapf(start);
    if (eng_rec_state != RS_OFF) {                                 // never next to what is being written
        int32_t d = circ(s0, eng_wr);
        if ((d < 0 ? -d : d) < (int32_t)reach + 2 * UNIT) return;
    }
    // a voice: a free one; else the one nearest its end fades in 2 ms and this grain is dropped
    int vi = -1;
    for (int i = 0; i < NV; i++) if (!vo[i].on) { vi = i; break; }
    if (vi < 0) {
        float best = 0.5f;
        for (int i = 0; i < NV; i++) if (vo[i].dfade == 0.0f && vo[i].ph > best) { best = vo[i].ph; vi = i; }
        if (vi < 0) return;
        vo[vi].dfade = 1.0f / 64.0f;                               // the old grain goes; the new one waits for a voice
        return;
    }
    voice_t *v = &vo[vi];
    float fl = (float)(int32_t)start; if (fl > start) fl -= 1.0f;
    v->pos = (float)s0 + (start - fl);
    if (v->pos >= (float)corp_frames) v->pos -= (float)corp_frames;
    v->rate = rev ? -rate : rate;
    v->ph = 0.0f; v->dph = 1.0f / len;
    v->amp = c->gain * shape_gain[eng_shape];
    v->fade = 1.0f; v->dfade = 0.0f;
    v->delay = off < 0 ? 0 : off >= AUDIO_BLOCK ? AUDIO_BLOCK - 1 : off;
    v->unit = c->unit; v->shape = (uint8_t)eng_shape; v->on = 1;
    spawn_t ev = { audio_frames() + (uint32_t)v->delay, c->unit, (uint8_t)kind, (uint8_t)rev, semi, v->amp };
    spawnq_push(&spawnq, &ev);
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

// one grain through this block: the window in table steps, the read head with its wrap on the side it moves to
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
    // what goes in
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
    if (hush) for (int i = 0; i < NV; i++) if (vo[i].on && vo[i].dfade == 0.0f) vo[i].dfade = 1.0f / 320.0f;
    if (eng_rec_state != RS_OFF && corp) record(in, n);
    eng_rec_level *= 0.995f;

    // the controls, smoothed per block
    chaos_s += (clampf(eng_chaos, 0.0f, 1.0f) - chaos_s) * 0.08f;
    float ce = chaos_s;
    float g0 = gate_s, l0 = level_s;
    gate_s += (clampf(eng_gate, 0.0f, 1.0f) - gate_s) * 0.15f;
    level_s += (clampf(eng_level, 0.0f, 1.0f) - level_s) * 0.15f;
    tone_s += (clampf(eng_tone, -1.0f, 1.0f) - tone_s) * 0.15f;
    float sp0 = space_s;
    space_s += (clampf(eng_space, 0.0f, 1.0f) - space_s) * 0.1f;

    // the order side: grid lines in this block, each drifting late with chaos, kept until their frame comes
    uint32_t f1 = f0 + (uint32_t)n;
    {
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
                for (int s = 0; s < pend[i].stack; s++) spawn(off, GK_ORDER, pend[i].step + s);
                pend[i] = pend[--npend];
            } else i++;
        }
    }
    // the chaos side: a Poisson count for this block, each at a random place in it
    {
        float rate = eng_density * ce * sqrtf(ce);
        float lam = rate * (float)n / (float)AUDIO_SR;
        if (lam > 0.0005f) {
            float p = fast_exp2(-lam * 1.442695f), s = p, u = rng_uni(&rng);
            int k = 0;
            while (u > s && k < 6) { k++; p *= lam / (float)k; s += p; }
            for (int j = 0; j < k; j++) spawn((int)(rng_uni(&rng) * (float)n), GK_CHAOS, 0);
        }
        uint32_t br = __atomic_load_n(&eng_burst_req, __ATOMIC_ACQUIRE);
        int nb = (int)(br - burst_seen);
        if (nb > 0) { if (nb > 6) nb = 6; burst_seen += (uint32_t)nb; for (int j = 0; j < nb; j++) spawn((int)(rng_uni(&rng) * (float)n), GK_BURST, 0); }
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

    // how many overlap: their level comes down by its square root (taken per block, so a change glides)
    float fpb = (float)(link_fpb() < 1000 ? 16000 : link_fpb());
    float grid_rate = (float)AUDIO_SR / fpb * (float)(eng_div < 1 ? 1 : eng_div) * (float)clampi(eng_stack, 1, 4);
    float overlap = (grid_rate * (1.0f - ce * ce) + eng_density * ce * sqrtf(ce)) * eng_size * eng_size_mul;
    static float comp_s = 1.0f;
    float comp0 = comp_s;
    comp_s += (1.0f / sqrtf(overlap > 1.0f ? overlap : 1.0f) - comp_s) * 0.05f;

    // TONE: a low pass down, a high pass up
    svf_set(&f_lp, 18000.0f * fast_exp2(fminf(tone_s, 0.0f) * 6.0f), 0.7f);
    svf_set(&f_hp, 20.0f * fast_exp2(fmaxf(tone_s, 0.0f) * 7.3f), 0.7f);

    const float trim = 0.62f;                                       // a grain peaks near 0.5 (its take's gain)
    float pk = 0;
    float inv = 1.0f / (float)n;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) * inv;
        float g = (g0 + (gate_s - g0) * t) * (l0 + (level_s - l0) * t) * (comp0 + (comp_s - comp0) * t);
        float x = acc[i] * trim * g;
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
    for (int k = 0; k < 4; k++) rv_buf[k] = app_alloc_fast((size_t)rv_len[k] * sizeof(float));
    for (int k = 0; k < 2; k++) ap_buf[k] = app_alloc_fast((size_t)ap_len[k] * sizeof(float));
    for (int k = 0; k < 4; k++) if (!rv_buf[k]) rv_buf[0] = NULL;
    for (int k = 0; k < 2; k++) if (!ap_buf[k]) rv_buf[0] = NULL;
    rv_damp = onepole_hz(5200.0f);
    svf_set(&f_lp, 18000.0f, 0.7f); svf_set(&f_hp, 20.0f, 0.7f);
    for (int i = 0; i < NV; i++) eng_vis_unit[i] = -1;
}

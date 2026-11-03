// NYLON's grains, audio task.
//
// CLOUD: the last three seconds of the guitar, always kept (int16, PSRAM), and grains from anywhere in REACH of
// them: GRAIN SIZE long, DENSITY a second, some an octave up (SHIMMER), some an octave down (DEPTH), some
// backwards. FREEZE stops the writing: the cloud is what was there, and you play over it.
//
// BLOOM: the note you just played, held. Each note (CATCH) is caught 40 ms after its attack for 0.3 s, into one
// of three short buffers in turn. The UI side finds the note's period in it (ear_period); then the grains start on
// whole periods of the note, at output times a whole number of two periods apart, Hann windows half overlapped:
// every grain in phase with the others (pitch-synchronous), so the held note is the note, steady, and a string
// tuned to it is driven at its own frequency. Where no period is found (a chord, a noise), grains from random
// places, four overlapping: the same chord, still, a little chorused. It swells in over SWELL while the string dies, then holds as long as HOLD (the
// neck) says, and with HARMONIC the octave and the twelfth come in as it ages, as feedback does on an amp. With a
// speaker (or a stick-on exciter) on the guitar's top, what it plays rings the strings themselves.
#include "inst_nylon.h"
#include "dsp.h"
#include "spsc.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

#define RG_N   96000                         // 3 s
#define CAP_N  9600                          // 0.3 s a note
#define NCAP   3
#define CAP_SKIP (AUDIO_SR * 40 / 1000)      // the attack left out
#define NV     24
#define WIN_N  512

SPSC(grainq, grain_ev_t, 32)
static grainq_t gq;
bool eng_grain_pop(grain_ev_t *g) { return grainq_pop(&gq, g); }
volatile uint32_t eng_grains;

static int16_t *rg;                          // the ring
static int rg_w, rg_fill;
static int16_t *cap[NCAP];
static float win[WIN_N + 2];
static uint32_t seed = 0x13579bdfu;

typedef struct {
    const int16_t *src; int src_n; bool wrap;
    float pos, rate, ph, dph, amp;
    int delay;
    bool on;
} gvoice_t;
static gvoice_t cv[NV], bv[NV];

static inline int16_t s16(float x) { x *= 16384.0f; return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x); }

void grain_init(void)
{
    for (int i = 0; i <= WIN_N + 1; i++) { float t = (float)(i > WIN_N ? WIN_N : i) / (float)WIN_N; win[i] = 0.5f - 0.5f * cosf(6.2831853f * t); }
    rg = app_alloc(RG_N * sizeof(int16_t));
    for (int k = 0; k < NCAP; k++) cap[k] = app_alloc(CAP_N * sizeof(int16_t));
    if (!rg || !cap[NCAP - 1]) app_log("nylon: no memory for the grains");
}

IRAM_ATTR void ring_write(const float *in, int n, bool frozen)
{
    if (!rg || frozen) return;
    for (int i = 0; i < n; i++) { rg[rg_w] = s16(in[i]); if (++rg_w >= RG_N) rg_w = 0; }
    if (rg_fill < RG_N) rg_fill += n;
}

static IRAM_ATTR gvoice_t *voice_free(gvoice_t *pool)
{
    for (int i = 0; i < NV; i++) if (!pool[i].on) return &pool[i];
    return NULL;
}

static IRAM_ATTR void grain_run(gvoice_t *v, float *acc, int n)
{
    const int16_t *b = v->src;
    const int max = v->src_n;
    const float maxf = (float)max;
    float pos = v->pos, rate = v->rate, wp = v->ph * (float)WIN_N, dwp = v->dph * (float)WIN_N, g = v->amp * (1.0f / 16384.0f);
    int i = v->delay;
    v->delay = 0;
    for (; i < n; i++) {
        int wi = (int)wp;
        float ww = win[wi] + (wp - (float)wi) * (win[wi + 1] - win[wi]);
        int ip = (int)pos;
        float fr = pos - (float)ip;
        int ip1 = ip + 1;
        if (ip1 >= max) ip1 = v->wrap ? 0 : max - 1;
        float s0 = (float)b[ip];
        acc[i] += (s0 + fr * ((float)b[ip1] - s0)) * ww * g;
        pos += rate;
        if (v->wrap) { if (pos >= maxf) pos -= maxf; else if (pos < 0.0f) pos += maxf; }
        else if (pos < 0.0f || pos > maxf - 1.0f) { v->on = false; break; }
        wp += dwp;
        if (wp >= (float)WIN_N) { v->on = false; break; }
    }
    v->pos = pos; v->ph = wp * (1.0f / (float)WIN_N);
}

// ---------------------------------------------------------------- CLOUD
static float c_acc, c_lvl;
IRAM_ATTR void cloud_run(float *wet, int n, float feed)
{
    memset(wet, 0, (size_t)n * sizeof(float));
    if (!rg) return;
    float size = clampf(E.c_size, 0.01f, 0.6f), dens = clampf(E.c_dens, 0.5f, 120.0f), reach = clampf(E.c_reach, 0.05f, 2.8f);
    // new grains, while the cloud is fed (left, it rings out with what it has)
    if (feed > 0.01f) c_acc += dens * (float)n / (float)AUDIO_SR * 2.0f * rng_uni(&seed);
    float comp = 0.9f / sqrtf(fmaxf(1.0f, dens * size));
    while (c_acc >= 1.0f) {
        c_acc -= 1.0f;
        gvoice_t *v = voice_free(cv);
        if (!v) break;
        float len = size * (float)AUDIO_SR * fast_exp2(0.45f * rng_bi(&seed));
        float r = rng_uni(&seed), rate = 1.0f;
        if (r < E.c_up) rate = 2.0f;
        else if (r < E.c_up + E.c_down) rate = 0.5f;
        rate *= 1.0f + 0.004f * rng_bi(&seed);                  // a few cents apart: a chorus, not a copy
        bool rev = rng_uni(&seed) < E.c_rev;
        float reachs = len * rate + 64.0f;
        float back = 0.02f * (float)AUDIO_SR + reachs + rng_uni(&seed) * reach * (float)AUDIO_SR;
        if (back + reachs > (float)(rg_fill < RG_N ? rg_fill : RG_N) - 64.0f) continue;   // not that much heard yet
        float start = (float)rg_w - back;
        if (rev) start += reachs - 64.0f;                        // read the same stretch the other way
        while (start < 0.0f) start += (float)RG_N;
        while (start >= (float)RG_N) start -= (float)RG_N;
        *v = (gvoice_t){ rg, RG_N, true, start, rev ? -rate : rate, 0.0f, 1.0f / len, comp * feed, (int)(rng_uni(&seed) * (float)n), true };
        eng_grains++;
        grain_ev_t ev = { back / (float)AUDIO_SR, comp, rate, (uint8_t)rev, 0 };
        grainq_push(&gq, &ev);
    }
    for (int i = 0; i < NV; i++) if (cv[i].on) grain_run(&cv[i], wet, n);
    float pk = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(wet[i]); if (a > pk) pk = a; }
    c_lvl = fmaxf(pk, c_lvl * 0.97f);
}

// ---------------------------------------------------------------- BLOOM
static int cap_cur = -1, cap_fill = -1, cap_next, cap_pos, cap_skip;
static float cap_acc;
static float b_env, b_age, b_acc, b_swell_g, b_peak, b_nxt, b_k;
volatile float eng_bloom_period[NCAP];
volatile int eng_bloom_cur = -1;
const int16_t *bloom_buf(int k) { return k >= 0 && k < NCAP ? cap[k] : NULL; }
static bool  dropping;                       // a slap: the held note fades and is let go
static uint32_t b_seq;

void bloom_clear(void) { cap_cur = -1; cap_fill = -1; b_env = 0.0f; for (int i = 0; i < NV; i++) bv[i].on = false; eng_bloom_lvl = 0.0f; }

void bloom_onset(uint32_t frame, float level)
{
    (void)frame;
    if (!cap[NCAP - 1]) return;
    int ct = E.b_catch;
    if (ct == CATCH_HOLD && cap_cur >= 0) return;
    if (ct == CATCH_LOUD && level < 2.0f * b_peak * 0.35f && cap_cur >= 0) return;
    cap_fill = cap_next; cap_next = (cap_next + 1) % NCAP;
    cap_pos = 0; cap_skip = CAP_SKIP; cap_acc = 0.0f;
    b_peak = fmaxf(level, b_peak * 0.5f);
}

// a grain of the held note: at random (start < 0), or from start for len samples, in phase with the rest
static IRAM_ATTR void bloom_spawn(int off, float rate, float w, float start, float len)
{
    if (cap_cur < 0 || w < 0.02f) return;
    gvoice_t *v = voice_free(bv);
    if (!v) return;
    float amp;
    if (start < 0.0f) {
        len = (rate > 1.5f ? 0.07f : 0.11f) * (float)AUDIO_SR;
        float reach = len * rate;
        if (reach > (float)CAP_N - 8.0f) { len = ((float)CAP_N - 8.0f) / rate; reach = len * rate; }
        start = rng_uni(&seed) * ((float)CAP_N - 4.0f - reach);
        amp = 0.67f * w;                                         // four overlapping at random: the note at its own level
        rate *= 1.0f + 0.002f * rng_bi(&seed);
    } else amp = w;                                              // half overlapped, in phase: Hann adds to one
    *v = (gvoice_t){ cap[cap_cur], CAP_N, false, start, rate, 0.0f, 1.0f / len, amp, off, true };
    if ((b_seq++ & 3) == 0) { grain_ev_t ev = { 0.0f, w, rate, 0, 1 }; grainq_push(&gq, &ev); }
}

// the grains in phase: start on period k (each rate as far in as fits), the fraction of a sample the spawn was late
// made up in the source
static IRAM_ATTR void bloom_sync(int off, float late, float P, float len, float rate, float w)
{
    if (w < 0.02f) return;
    float room = (float)CAP_N - 6.0f - len * rate;
    if (room < 0.0f) return;
    float kmax = (float)(int)(room / P);
    float k = b_k > kmax ? kmax : b_k;
    bloom_spawn(off, rate, w, k * P + late * rate, len);
}

IRAM_ATTR void bloom_run(const float *in, float *wet, int n, float feed)
{
    memset(wet, 0, (size_t)n * sizeof(float));
    if (!cap[NCAP - 1]) return;
    // catching a note: skip the attack, then 0.3 s of it (unless the UI side says the note was a slap)
    static uint32_t cancel_seen;
    if (eng_bloom_cancel != cancel_seen) { cancel_seen = eng_bloom_cancel; cap_fill = -1; }
    if (cap_fill >= 0) {
        int i = 0;
        if (cap_skip > 0) { int s = cap_skip < n ? cap_skip : n; cap_skip -= s; i = s; }
        for (; i < n && cap_pos < CAP_N; i++) { float x = in[i] * feed; cap[cap_fill][cap_pos++] = s16(x); cap_acc += x * x; }
        if (cap_pos >= CAP_N) {
            float rms = sqrtf(cap_acc / (float)CAP_N);
            if (rms > 2e-4f) {
                eng_bloom_period[cap_fill] = 0.0f;                  // the UI side measures it
                cap_cur = cap_fill; b_age = 0.0f; dropping = false; b_k = -1.0f;
                eng_bloom_cur = cap_cur;
                eng_bloom_caps++;
            }
            cap_fill = -1;
        }
    }
    const float bdt = (float)n / (float)AUDIO_SR;
    // the envelope: swells in, holds, fades as HOLD says; the neck down chokes it
    float hold = clampf(E.b_hold, 0.0f, 1.0f);
    float swell = clampf(E.b_swell, 0.05f, 10.0f);
    bool left = feed < 0.01f;                                    // another sound now: the held note goes, over 0.3 s
    bool choke = E.b_choke || dropping;
    float target = cap_cur >= 0 && !choke && !left ? 1.0f : 0.0f;
    if (cap_cur >= 0 && b_age > swell && hold < 0.97f && !left && !choke) {   // past the swell it fades unless held
        float tau = 0.25f * exp2f(hold * 6.0f);
        target = expf(-(b_age - swell) / tau);
    }
    float k = target > b_env ? 1.0f - expf(-bdt / (swell * 0.35f)) : choke ? 1.0f - expf(-bdt / 0.1f) : left ? 1.0f - expf(-bdt / 0.1f) : 1.0f - expf(-bdt / 0.08f);
    b_env += k * (target - b_env);
    b_env = flush_denormal(b_env);
    if (choke && b_env < 1e-3f) { if (cap_cur >= 0) { cap_cur = -1; b_env = 0.0f; } dropping = false; }   // choked: let go, as a damped string
    b_age += bdt;
    // the voices of it: the note, its octave and twelfth as it ages (HARMONIC), the octave under (SUB)
    float h = clampf(E.b_harm, 0.0f, 1.0f) * smoothstep(0.4f, 3.0f, b_age), sub = clampf(E.b_sub, 0.0f, 1.0f);
    float w1 = (1.0f - 0.65f * h) * b_env, w2 = h * b_env, w3 = 0.55f * h * h * b_env, w0 = sub * b_env;
    float P = cap_cur >= 0 ? eng_bloom_period[cap_cur] : 0.0f;
    if (b_env > 1e-4f && P > 16.0f) {
        // in phase: a grain every hop (a whole number of two periods, about 25 ms), each 2 hops long
        float two = 2.0f * P, hop = two * fmaxf(1.0f, (float)(int)(800.0f / two + 0.5f)), len = 2.0f * hop;
        if (b_k < 0.0f) b_k = (float)(int)(0.35f * ((float)CAP_N - len) / P);   // a third in: past what is left of the attack
        while (b_nxt < (float)n) {
            int off = (int)b_nxt;
            float late = b_nxt - (float)off;
            b_k += (float)((int)(rng_uni(&seed) * 3.0f) - 1);    // the source wanders a period either way: alive, not a loop
            float km = (float)(int)(((float)CAP_N - 6.0f - len) / P);
            if (b_k < 0.0f) b_k = 1.0f; else if (b_k > km) b_k = km - 1.0f;
            bloom_sync(off, late, P, len, 1.0f, w1);
            bloom_sync(off, late, P, len, 2.0f, w2);
            bloom_sync(off, late, P, len, 3.0f, w3);
            bloom_sync(off, late, P, len, 0.5f, w0);
            b_nxt += hop;
        }
        b_nxt -= (float)n;
    } else if (b_env > 1e-4f) {
        b_acc += bdt * 40.0f;                                    // a grain every 25 ms: four overlap
        while (b_acc >= 1.0f) {
            b_acc -= 1.0f;
            int off = (int)(rng_uni(&seed) * (float)n);
            bloom_spawn(off, 1.0f, w1, -1.0f, 0.0f);
            bloom_spawn(off, 2.0f, w2, -1.0f, 0.0f);
            bloom_spawn(off, 3.0f, w3, -1.0f, 0.0f);
            bloom_spawn(off, 0.5f, w0, -1.0f, 0.0f);
        }
        b_nxt = 0.0f;
    }
    for (int i = 0; i < NV; i++) if (bv[i].on) grain_run(&bv[i], wet, n);
    if (cap_cur >= 0) {
        float pk = 0;
        for (int i = 0; i < n; i++) { float a = fabsf(wet[i]); if (a > pk) pk = a; }
        b_swell_g = fmaxf(pk, b_swell_g * 0.97f);
    }
    eng_bloom_lvl = b_env;
    eng_bloom_age = cap_cur >= 0 ? b_age : 0.0f;
}

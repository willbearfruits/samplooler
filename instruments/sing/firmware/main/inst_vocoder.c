// SING's vocoder (see sing.h). A channel vocoder in two passes per 64-frame block:
//
//   1. the modulator (the voice) through each band's band pass: its mean level in the block, made an envelope
//      (attack about a block, release_ms), and its top (above 5 kHz) against its body (below 1.5 kHz), which says
//      whether the block is an s, t or sh
//   2. the carrier through the same bands: each band scaled so its envelope becomes the voice's in that band,
//      the gain ramped across the block (no steps). The carrier's own spectrum is flattened band by band (its
//      level in each band from the block before), so a saw, a buzz or noise all speak as clearly; its overall
//      level is kept, so a carrier that fades out fades the vocoder out.
//
// Bands: log-spaced 100 Hz .. 7 kHz, 8..20 of them. The count glides: the band centres move together and the
// band being added (or taken away) fades in (out) above the top, so a new count never clicks. SHARP sets the
// bands' Q against their spacing (wide and smooth .. narrow and ringing); the level is kept across it.
// The carrier: the chord as detuned pairs of polyBLEP oscillators (saw blending to a narrow pulse with BRIGHT),
// each gliding to its new note on a chord change and fading in or out when the chord has more or fewer notes;
// the tuned voice's note as a pair of its own; or noise. BRIGHT also opens a gentle low pass on the output.
// The voice's s and t sounds above 5 kHz go on top, let through only in blocks the top/body ratio calls sibilant.
#include "sing.h"
#include <string.h>

#define NB_MAX   20
#define F_LO     100.0f
#define F_HI     7000.0f
#define NCH      8                       // chord oscillators: 4 notes x a detuned pair
#define NNO      2                       // the note's pair
#define GMAX     30.0f                   // a band's gain never goes past this
#define BLOCK_S  ((float)AUDIO_BLOCK / (float)AUDIO_SR)

// ---------------------------------------------------------------- params (UI task -> audio task, seqlock)
static voc_params_t p_ui, p_au = {
    .carrier = VOC_CHORD, .chord = { 48, 52, 55, 0 }, .chord_n = 3, .note = 0, .octave = 0,
    .bands = 16, .sharp = 0.5f, .release_ms = 60, .sibilance = 0.5f, .bright = 0.5f, .detune = 8,
};
static volatile uint32_t p_seq;
static uint32_t p_seen;

void vocoder_set(const voc_params_t *p)
{
    __atomic_add_fetch(&p_seq, 1, __ATOMIC_ACQ_REL);
    p_ui = *p;
    __atomic_add_fetch(&p_seq, 1, __ATOMIC_ACQ_REL);
}
static void pull(void)
{
    uint32_t s = __atomic_load_n(&p_seq, __ATOMIC_ACQUIRE);
    if ((s & 1u) || s == p_seen) return;
    voc_params_t t = p_ui;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&p_seq, __ATOMIC_RELAXED) != s) return;   // written meanwhile: next block
    p_au = t; p_seen = s;
}

// ---------------------------------------------------------------- state (audio task)
typedef struct { float a1, a2, a3, k; } svfc_t;
static svfc_t cf[NB_MAX], cfh[NB_MAX];  // the bands at 32 kHz (the carrier, the top of the voice), and at 16 kHz
static float  hcomp[NB_MAX];            // the halving's loss at a band's centre, made up
static bool   qhalf[NB_MAX / 4];        // a group of four whose voice side runs at 16 kHz (all its bands under 3.5 kHz)
#define HALF_MAX_HZ 3500.0f
static float  mz1[NB_MAX], mz2[NB_MAX], cz1[NB_MAX], cz2[NB_MAX];   // band pass states: modulator, carrier
static float  env_m[NB_MAX], nom_c[NB_MAX], g_prev[NB_MAX], wband[NB_MAX];   // nom_c: the carrier's level in each band at full level
static int    nb_act;
static float  bands_s = 16, sharp_s = 0.5f, bands_done = -1, sharp_done = -1, comp = 1;

typedef struct { float ph, ph2, note, dt, g; bool on; } osc_t;
static osc_t  och[NCH], ono[NNO];
static float  set_c, set_n, set_z;      // the chord set's, the note pair's and the noise's levels (ramped)
static float  bright_s = 0.5f, rel_k, lp_z, lp_k = 0.5f;
static int    learn_tick;
static float  agc = 0.6f, agc_in = 1e-6f, agc_out = 1e-6f;   // the output's level kept at the voice's (slowly, while it sings)
static uint32_t seed = 0x6a09e667u;
// sibilance
static biquad_t sib_hp;
static float  sib_lp, sib_g;
// for the screen
static volatile float view_lv[NB_MAX];
static volatile int   view_n;

static float car[AUDIO_BLOCK], hpbuf[AUDIO_BLOCK];

// four band passes (Simper's state variable filter, band output at unity) side by side, states in registers
#define SVF4_LOAD(Z1, Z2) SVF4_LOADC(cf, Z1, Z2)
#define SVF4_LOADC(C, Z1, Z2) \
    const svfc_t c0 = C[b], c1 = C[b + 1], c2 = C[b + 2], c3 = C[b + 3]; \
    float p0 = Z1[b], p1 = Z1[b + 1], p2 = Z1[b + 2], p3 = Z1[b + 3]; \
    float r0 = Z2[b], r1 = Z2[b + 1], r2 = Z2[b + 2], r3 = Z2[b + 3];
#define SVF4_ONE(c, p, r, x, y) { float v3 = (x) - r, v1 = c.a1 * p + c.a2 * v3, v2 = r + c.a2 * p + c.a3 * v3; \
                                  p = 2.0f * v1 - p; r = 2.0f * v2 - r; y = c.k * v1; }
#define SVF4_STEP(x) SVF4_ONE(c0, p0, r0, x, y0) SVF4_ONE(c1, p1, r1, x, y1) SVF4_ONE(c2, p2, r2, x, y2) SVF4_ONE(c3, p3, r3, x, y3)
#define SVF4_STORE(Z1, Z2) \
    Z1[b] = flush_denormal(p0); Z1[b + 1] = flush_denormal(p1); Z1[b + 2] = flush_denormal(p2); Z1[b + 3] = flush_denormal(p3); \
    Z2[b] = flush_denormal(r0); Z2[b + 1] = flush_denormal(r1); Z2[b + 2] = flush_denormal(r2); Z2[b + 3] = flush_denormal(r3);

static void svf_coef(svfc_t *c, float fc, float q, float sr)
{
    fc = clampf(fc, 20.0f, 0.42f * sr);
    float g = tanf(3.14159265f * fc / sr);
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k)); c->a2 = g * c->a1; c->a3 = g * c->a2;
}

// band centres, weights and Q from the (gliding) band count and sharpness; libm, so only when they move
static void design(void)
{
    if (fabsf(bands_s - bands_done) < 0.002f && fabsf(sharp_s - sharp_done) < 0.002f) return;
    bands_done = bands_s; sharp_done = sharp_s;
    int full = (int)bands_s;
    float frac = bands_s - (float)full;
    nb_act = full + (frac > 0.002f ? 1 : 0);
    if (nb_act > NB_MAX) nb_act = NB_MAX;
    float span = bands_s - 1.0f, oct = log2f(F_HI / F_LO) / span;   // octaves between bands
    float q0 = 1.0f / (exp2f(0.5f * oct) - exp2f(-0.5f * oct));     // a Q that tiles the spectrum
    float q = q0 * exp2f(3.0f * sharp_s - 1.0f);                    // 0.5 .. 4 times that
    comp = sqrtf(q / q0) * 0.85f;                                   // a narrower band hears less of the voice: keep the level
    for (int b = 0; b < nb_act; b++) {
        float fc = F_LO * exp2f(oct * (float)b);
        svf_coef(&cf[b], fc, q, (float)AUDIO_SR);
        svf_coef(&cfh[b], fc, q, 0.5f * (float)AUDIO_SR);
        hcomp[b] = 1.0f / cosf(3.14159265f * fminf(fc, HALF_MAX_HZ) / (float)AUDIO_SR);
        wband[b] = b < full ? 1.0f : frac;
    }
    for (int q4 = 0; q4 < NB_MAX / 4; q4++) {
        int top = 4 * q4 + 3 < nb_act ? 4 * q4 + 3 : nb_act - 1;
        bool h = top >= 4 * q4 && F_LO * exp2f(oct * (float)top) < HALF_MAX_HZ;
        if (h != qhalf[q4]) for (int k = 0; k < 4; k++) mz1[4 * q4 + k] = mz2[4 * q4 + k] = 0;   // its states belong to the other rate
        qhalf[q4] = h;
    }
    for (int b = nb_act; b < NB_MAX; b++) {                          // the rest of the last group of four: silent and still
        cf[b] = cfh[b] = (svfc_t){ 0 }; hcomp[b] = 1; wband[b] = 0; mz1[b] = mz2[b] = cz1[b] = cz2[b] = 0; env_m[b] = g_prev[b] = 0;
    }
}

void vocoder_init(void)
{
    for (int i = 0; i < NCH; i++) { och[i].ph = rng_uni(&seed); och[i].ph2 = och[i].ph; och[i].note = 60; }
    for (int i = 0; i < NNO; i++) { ono[i].ph = rng_uni(&seed); ono[i].ph2 = ono[i].ph; ono[i].note = 60; }
    biquad_set(&sib_hp, BQ_HP, 5000.0f, 0.707f, 0);
    bands_done = -1;
    design();
}

// one oscillator's note and gain for this block: glide to the note, or jump there when silent
static void osc_block(osc_t *o, float note, bool on, float glide)
{
    if (on && o->g < 0.001f && !o->on) o->note = note;                 // waking: start on the note
    if (on) o->note += (note - o->note) * glide;
    o->on = on;
    o->dt = 440.0f * fast_exp2((o->note - 69.0f) * (1.0f / 12.0f)) / (float)AUDIO_SR;
    if (o->dt > 0.45f) o->dt = 0.45f;
}

void vocoder_process(const float *mod, float *out, int n)
{
    pull();
    const voc_params_t *P = &p_au;
    // the controls, glided per block
    bands_s += (clampf(P->bands, 8.0f, 20.0f) - bands_s) * 0.06f;      // about 70 ms
    if (fabsf(clampf(P->bands, 8.0f, 20.0f) - bands_s) < 0.003f) bands_s = clampf(P->bands, 8.0f, 20.0f);
    sharp_s += (clampf(P->sharp, 0.0f, 1.0f) - sharp_s) * 0.06f;
    design();
    float bright_t = clampf(P->bright, 0.0f, 1.0f);
    bright_s += (bright_t - bright_s) * 0.06f;
    float lp_t = 1.0f - expf(-TWO_PI * fminf(1800.0f * exp2f(3.0f * bright_s), 14000.0f) / (float)AUDIO_SR);
    float lp_k0 = lp_k, dlp = (lp_t - lp_k0) / (float)n; lp_k = lp_t;
    float rel = clampf(P->release_ms, 5.0f, 300.0f) * 0.001f;
    rel_k = 1.0f - expf(-BLOCK_S / rel);

    // the carrier's notes: each pair glides to its note (about 25 ms), sets fade in and out (about 20 ms)
    int car_t = P->carrier < VOC_CHORD || P->carrier > VOC_NOISE ? VOC_CHORD : P->carrier;
    bool want_c = car_t == VOC_CHORD || car_t == VOC_CHORD_NOTE, want_z = car_t == VOC_NOISE;
    bool want_n = (car_t == VOC_NOTE || car_t == VOC_CHORD_NOTE) && P->note > 1.0f;
    float oct = 12.0f * (float)clampi(P->octave, -2, 2);
    float det = clampf(P->detune, 0.0f, 30.0f) * 0.01f * 0.5f;        // half the cents, in semitones, each side
    const float glide = 0.077f, ng = 0.12f;
    int cn = clampi(P->chord_n, 0, 4);
    for (int k = 0; k < 4; k++) {
        bool on = k < cn && P->chord[k] > 0;
        float nt = (float)(on ? P->chord[k] : 60) + oct;
        osc_block(&och[2 * k], nt - det, on, glide);
        osc_block(&och[2 * k + 1], nt + det, on, glide);
    }
    float nn = P->note + oct;
    osc_block(&ono[0], nn - det, want_n, ng);
    osc_block(&ono[1], nn + det, want_n, ng);
    float sc0 = set_c, sn0 = set_n, sz0 = set_z;
    const float fade = 0.1f;                                            // per block: 20 ms end to end
    set_c += clampf((want_c ? 1.0f : 0.0f) - set_c, -fade, fade);
    set_n += clampf((want_n ? 1.0f : 0.0f) - set_n, -fade, fade);
    set_z += clampf((want_z ? 1.0f : 0.0f) - set_z, -fade, fade);
    float og0[NCH], og1[NCH];
    for (int k = 0; k < NCH; k++) { og0[k] = och[k].g; og1[k] = och[k].g + clampf((och[k].on ? 1.0f : 0.0f) - och[k].g, -fade, fade); och[k].g = og1[k]; }
    float on0[NNO], on1[NNO];
    for (int k = 0; k < NNO; k++) { on0[k] = ono[k].g; on1[k] = ono[k].g + clampf((ono[k].on ? 1.0f : 0.0f) - ono[k].g, -fade, fade); ono[k].g = on1[k]; }
    float lvl0 = clampf(sc0 + sn0 + sz0, 0.0f, 1.0f), lvl1 = clampf(set_c + set_n + set_z, 0.0f, 1.0f);   // the carrier's level

    // pass 0: the carrier block, one oscillator at a time: a detuned pair of band-limited saws per note. (The bands
    // flatten the carrier's spectrum anyway, so its brightness is set after them, by BRIGHT's low pass.)
    float inv = 1.0f / (float)n;
    for (int i = 0; i < n; i++) car[i] = 0;
    for (int k = 0; k < NCH + NNO; k++) {
        osc_t *o = k < NCH ? &och[k] : &ono[k - NCH];
        float ga = k < NCH ? og0[k] * sc0 : on0[k - NCH] * sn0, gb = k < NCH ? og1[k] * set_c : on1[k - NCH] * set_n;
        float ph = o->ph, dt = o->dt;
        if (ga <= 0.0f && gb <= 0.0f) { ph += dt * (float)n; ph -= (float)(int)ph; o->ph = ph; continue; }
        float g = ga, dg = (gb - ga) * inv;
        for (int i = 0; i < n; i++) {
            car[i] += saw_blep(ph, dt) * g;
            g += dg;
            ph += dt; if (ph >= 1.0f) ph -= 1.0f;
        }
        o->ph = ph;
    }
    if (sz0 > 0.0f || set_z > 0.0f) {
        float g = sz0, dg = (set_z - sz0) * inv;
        for (int i = 0; i < n; i++) { car[i] += rng_bi(&seed) * g; g += dg; }
    }

    // pass 1: the voice in each band, and its top against its body
    float e_hf = 0, e_lf = 0;
    for (int i = 0; i < n; i++) {
        float x = mod[i];
        float h = biquad_run(&sib_hp, x);
        hpbuf[i] = h; e_hf += h * h;
        sib_lp += 0.25f * (x - sib_lp); e_lf += sib_lp * sib_lp;       // a one-pole near 1.5 kHz
    }
    sib_hp.z1 = flush_denormal(sib_hp.z1); sib_hp.z2 = flush_denormal(sib_hp.z2); sib_lp = flush_denormal(sib_lp);
    // the bands four at a time: four independent filters in one loop keep the FPU busy (on the S3 as on a PC)
    int nq = (nb_act + 3) / 4;
    if (nq < 0) nq = 0;
    if (nq > NB_MAX / 4) nq = NB_MAX / 4;
    // The voice under 3.5 kHz is heard at 16 kHz (pairs averaged): its levels are all that is wanted of it
    float macc[NB_MAX], half[AUDIO_BLOCK / 2];
    int nh = n / 2;
    for (int j = 0; j < nh; j++) half[j] = 0.5f * (mod[2 * j] + mod[2 * j + 1]);
    for (int q = 0; q < nq; q++) {
        int b = 4 * q;
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        if (qhalf[q]) {
            SVF4_LOADC(cfh, mz1, mz2)
            for (int j = 0; j < nh; j++) {
                float x = half[j], y0, y1, y2, y3;
                SVF4_STEP(x)
                s0 += fabsf(y0); s1 += fabsf(y1); s2 += fabsf(y2); s3 += fabsf(y3);
            }
            SVF4_STORE(mz1, mz2)
            s0 *= 2.0f * hcomp[b]; s1 *= 2.0f * hcomp[b + 1]; s2 *= 2.0f * hcomp[b + 2]; s3 *= 2.0f * hcomp[b + 3];
        } else {
            SVF4_LOADC(cf, mz1, mz2)
            for (int i = 0; i < n; i++) {
                float x = mod[i], y0, y1, y2, y3;
                SVF4_STEP(x)
                s0 += fabsf(y0); s1 += fabsf(y1); s2 += fabsf(y2); s3 += fabsf(y3);
            }
            SVF4_STORE(mz1, mz2)
        }
        macc[b] = s0; macc[b + 1] = s1; macc[b + 2] = s2; macc[b + 3] = s3;
    }
    float mmax = 0, cmax = 0;
    float target_g[NB_MAX];
    for (int b = 0; b < nb_act; b++) {
        float lv = macc[b] * inv * 1.5708f;                             // mean |x| to the amplitude of a sine
        float e = env_m[b];
        e += (lv > e ? 0.65f : rel_k) * (lv - e);                       // attack about a block, release as set
        env_m[b] = flush_denormal(e);
        if (e > mmax) mmax = e;
        if (nom_c[b] > cmax) cmax = nom_c[b];
    }
    // each band's gain: the voice's level there over the carrier's level there at full strength. The carrier is
    // flattened band by band, and its own fade (sets coming and going) passes straight through to the output.
    float floor_c = fmaxf(cmax * 0.05f, 1e-6f);
    for (int b = nb_act < 0 ? 0 : nb_act; b < 4 * nq; b++) target_g[b] = 0;
    for (int b = 0; b < nb_act; b++) {
        float g = cmax > 1e-5f ? env_m[b] / fmaxf(nom_c[b], floor_c) : 0.0f;
        target_g[b] = fminf(g, GMAX) * wband[b] * comp;
    }
    float lvl_avg = 0.5f * (lvl0 + lvl1);
    // sibilance: only in blocks whose top outweighs the body
    float r = e_hf / (e_lf + 1e-9f);
    float sg0 = sib_g, sg1 = clampf(P->sibilance, 0.0f, 1.0f) * smoothstep(0.12f, 0.6f, r) * 1.6f;
    sib_g += (sg1 - sib_g) * (sg1 > sib_g ? 0.7f : 0.25f);
    sg1 = sib_g;

    // pass 2: the carrier through the same bands, each shaped by the voice
    for (int i = 0; i < n; i++) out[i] = 0;
    float cacc[NB_MAX];
    learn_tick ^= 1;
    bool learn = learn_tick && lvl_avg > 0.05f;                        // the carrier's spectrum, every other block
    for (int q = 0; q < nq; q++) {
        int b = 4 * q;
        SVF4_LOAD(cz1, cz2)
        float g0 = g_prev[b], g1 = g_prev[b + 1], g2 = g_prev[b + 2], g3 = g_prev[b + 3];
        float d0 = (target_g[b] - g0) * inv, d1 = (target_g[b + 1] - g1) * inv, d2 = (target_g[b + 2] - g2) * inv, d3 = (target_g[b + 3] - g3) * inv;
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        if (learn) for (int i = 0; i < n; i++) {
            float x = car[i], y0, y1, y2, y3;
            SVF4_STEP(x)
            s0 += fabsf(y0); s1 += fabsf(y1); s2 += fabsf(y2); s3 += fabsf(y3);
            out[i] += y0 * g0 + y1 * g1 + y2 * g2 + y3 * g3;
            g0 += d0; g1 += d1; g2 += d2; g3 += d3;
        } else for (int i = 0; i < n; i++) {
            float x = car[i], y0, y1, y2, y3;
            SVF4_STEP(x)
            out[i] += y0 * g0 + y1 * g1 + y2 * g2 + y3 * g3;
            g0 += d0; g1 += d1; g2 += d2; g3 += d3;
        }
        SVF4_STORE(cz1, cz2)
        cacc[b] = s0; cacc[b + 1] = s1; cacc[b + 2] = s2; cacc[b + 3] = s3;
    }
    for (int b = 0; b < nb_act; b++) {
        g_prev[b] = target_g[b];
        if (learn) {                                                    // learn the carrier's spectrum while it sounds
            float lv = cacc[b] * inv * 1.5708f / lvl_avg;
            nom_c[b] += 0.5f * (lv - nom_c[b]);
            nom_c[b] = flush_denormal(nom_c[b]);
        }
        view_lv[b] = clampf(sqrtf(env_m[b] * 6.0f), 0.0f, 1.0f);
    }
    view_n = nb_act;
    // BRIGHT's low pass, the level kept at the voice's, then the s and t on top
    float ein = 0, eout = 0;
    for (int i = 0; i < n; i++) { float k = lp_k0 + dlp * (float)i; lp_z += k * (out[i] - lp_z); out[i] = lp_z; eout += lp_z * lp_z; ein += mod[i] * mod[i]; }
    float a0 = agc;
    if (ein > 1e-6f * (float)n) {                                     // only while the voice is there (about -60 dBFS)
        agc_in += 0.05f * (ein - agc_in); agc_out += 0.05f * (eout - agc_out);
        float want = clampf(sqrtf(agc_in / (agc_out + 1e-12f)), 0.25f, 4.0f);
        agc += (want - agc) * 0.02f;                                   // about 100 ms
    }
    for (int i = 0; i < n; i++) {
        float t = (float)i * inv;
        out[i] = out[i] * (a0 + (agc - a0) * t) + hpbuf[i] * (sg0 + (sg1 - sg0) * t);
    }
    lp_z = flush_denormal(lp_z);
}

int vocoder_bands(float *lv, int max)
{
    int n = view_n < max ? view_n : max;
    for (int b = 0; b < n; b++) lv[b] = view_lv[b];
    return n;
}

// AIRDWELLER's material (PAINT and PUMP): a modal bank per note of the chord, in two banks that ring on the same modes:
// eight modes the strikes and the scrapes ring, and the lowest four, which the bow sustains (so the other hand's hits
// can duck the drone and leave the strike alone). Each mode is a decaying rotation (s, c turned by the mode's angle each
// sample): its pitch can move every block without a click, and its amplitude is there to read for the screen. A mode's
// output gain lives in its state (the state is the mode times its gain): a new gain is reached by a growth or decay of
// the rotation over the block (at most x1.43 or x0.7 a block, the rest follows), so the sum needs no multiply and the
// gain no ramp. Modes run four at a time (their recursions overlap); a bank with nothing in it and nothing coming is
// skipped; a note's coefficients are worked out again only when its pitch, the material or RING move.
//
// The four materials are tables of eight modes (ratio, T60 over RING, gain); MORPH (0..3) slides between neighbours:
// ratios and T60s in log2, gains in line. METAL is a plate (inharmonic, the lowest mode rings RING, the upper ones
// shorter), GLASS sparse and high and long, RUBBER nearly harmonic, dark and short, its pitch squashed up a little by
// each strike, LATEX two modes that ring and squeak up after each strike. BRIGHT tilts the upper modes (at the output,
// so the hand shapes a ring while it rings). The note glides (50 ms), BEND glides (30 ms), the chord's notes glide to
// their new ratios, a note that leaves fades in 20 ms.
//
// Excitation: a strike is an impulse into every mode at its sample, weighted between a dark and a bright tilt and by
// the contact's spectrum (a 0.5..3.5 ms raised cosine for the heavy ones: the upper modes hardly move). A scrape (BACK,
// a CREAK) and CREAK are trains of jittered slips, each a bright impulse. The bow keeps each mode of its bank at an
// amplitude (BOW, tilted brighter as it grows): each block the mode's decay is set so it reaches that amplitude within
// about 50 ms (a self-sustained mode, as a bowed plate's), and the target never falls faster than the mode's own decay,
// so a lifted bow lets the material ring out. Noise pulsed at each note's own rate (the slips of a bow) feeds the bank
// as well: it starts the modes and keeps them rough. The bow's bank rings 4 s at most. DUCK: the other hand's strikes
// push the bow's bank down (10 ms in, 10 ms held, 100 ms back).
#include "inst_eng.h"
#include <string.h>

#define NCV 4                         // the chord's notes
#define NM  8                         // modes per note (the strikes')
#define NB  4                         // the bow's: the lowest four
#define NPS 8                         // strikes waiting for their block
#define NEV 12                        // impulses in one block
#define SR  ((float)AUDIO_SR)
#define ISR (1.0f / (float)AUDIO_SR)
#define MAT_OUT  0.45f                // the material's level: a heavy strike at full velocity peaks near -12 dBFS
#define BOW_A    0.3f                 // the bow's amplitude per mode at BOW 1 (a heavy strike rings near 1): the drone sits under a strike
#define BOW_IN   0.3f                 // the bow's noise

static const float M_R[NMAT][NM] = {
    { 1.000f, 1.505f, 1.811f, 2.598f, 2.734f, 3.483f, 4.297f, 5.120f },   // METAL: a plate, two close modes that beat
    { 1.000f, 2.320f, 2.756f, 4.250f, 5.404f, 6.630f, 8.933f, 11.30f },   // GLASS: sparse and high
    { 1.000f, 2.010f, 2.980f, 4.050f, 5.020f, 6.100f, 7.000f, 8.200f },   // RUBBER: nearly harmonic
    { 1.000f, 1.480f, 2.950f, 3.900f, 4.700f, 5.900f, 7.100f, 8.300f },   // LATEX: two that ring, the rest a whisper
};
static const float M_T[NMAT][NM] = {                                       // T60 over RING
    { 1.00f, 0.82f, 0.74f, 0.60f, 0.56f, 0.45f, 0.36f, 0.30f },
    { 1.40f, 1.30f, 1.30f, 1.10f, 1.00f, 0.90f, 0.80f, 0.70f },
    { 0.22f, 0.16f, 0.12f, 0.10f, 0.08f, 0.07f, 0.06f, 0.05f },
    { 0.80f, 0.70f, 0.20f, 0.15f, 0.12f, 0.10f, 0.08f, 0.07f },
};
static const float M_G[NMAT][NM] = {
    { 1.00f, 0.85f, 0.70f, 0.75f, 0.60f, 0.50f, 0.45f, 0.40f },
    { 1.00f, 0.35f, 0.55f, 0.25f, 0.35f, 0.18f, 0.22f, 0.10f },
    { 1.00f, 0.55f, 0.32f, 0.20f, 0.12f, 0.08f, 0.05f, 0.03f },
    { 1.00f, 0.45f, 0.06f, 0.04f, 0.03f, 0.02f, 0.015f, 0.01f },
};
static const float M_SQUASH[NMAT] = { 0.0f, 0.0f, 1.0f, 0.15f };          // a strike pushes the pitch up, it falls back
static const float M_BLIP[NMAT]   = { 0.0f, 0.0f, 0.0f, 1.0f };           // a strike, then the pitch squeaks up and back
static const float CH_LG[4][NCV] = {                                       // the chord's notes, log2 over the root
    { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.5849625f, 0.0f, 0.0f },
    { 0.0f, 0.5849625f, 1.0f, 0.0f }, { 0.0f, 0.3219281f, 0.5849625f, 0.8073549f },
};
static const int CH_N[4] = { 1, 2, 3, 4 };
static const float SHARE[4] = { 1.0f, 0.8408964f, 0.7598357f, 0.7071068f };   // 1 / n^(1/4): more notes, each a little quieter

// what a strike is: how hard, how bright (0 the dark tilt, 1 the bright), the contact (ms, 0: an impulse), a scrape (s)
typedef struct { float amp, bright, contact_ms, scrape_s; } stroke_t;
static const stroke_t PAD_ST[NPAD] = {
    { 0.50f, 0.90f, 0.0f, 0.0f },     // FRONT: light and high
    { 1.00f, 0.10f, 1.5f, 0.0f },     // LEFT: heavy and low
    { 0.75f, 0.45f, 0.5f, 0.0f },     // RIGHT: the middle
    { 0.55f, 0.80f, 0.0f, 0.18f },    // BACK: a scrape
};
static const stroke_t DV_ST[NDV] = {
    { 1.00f, 0.00f, 2.5f, 0.0f },     // KICK: heavy, low, long
    { 0.70f, 0.90f, 0.0f, 0.0f },     // SNAP: bright
    { 0.80f, 0.60f, 0.2f, 0.0f },     // CLANG
    { 0.35f, 1.00f, 0.0f, 0.0f },     // HAT: light, high, short
    { 0.50f, 0.85f, 0.0f, 0.0f },     // GLASS
    { 0.90f, 0.00f, 3.5f, 0.0f },     // SUB: heavy and low
    { 0.65f, 1.00f, 0.0f, 0.0f },     // WHIP: bright
    { 0.50f, 0.70f, 0.0f, 0.25f },    // CREAK: a scrape
    { 0.0f, 0.0f, 0.0f, 0.0f },       // OFF
};

typedef struct {
    float s[NM], c[NM], bs[NB], bc[NB];        // the strikes' bank and the bow's
    float rC[NM], rS[NM], bC[NB], bS[NB];      // their rotations x decays
    float cq[NM], sq[NM], fq[NM], rs_[NM], rb[NB], tb[NB];   // each mode's cos, sin, Hz, decay; the bow's own decay and T60
    float gs[NM], gbw[NB], gb[NB], bt[NB];     // the gain in each bank's states; the bow's noise gain, its amplitude
    float rlg, g;                              // its ratio in the chord (log2), its share (fading)
    float se, be;                              // what rings in each bank (energy)
    float bph;                                 // the bow's slip phase
    float key_lg, key_m, key_r;                // what its coefficients were worked out for
    bool  live;
} cv_t;
typedef struct { int off; float lo, hi, len_s; } ev_t;             // an impulse: its sample, dark and bright parts, contact

static cv_t  CV[NCV];
static ev_t  ps[NPS], ev[NEV];
static int   nps, nev;
static float g_lo[NM], g_hi[NM], g_norm[NMAT], lr[NMAT][NM], lt[NMAT][NM];
static float hz_lg = 7.78f, bend_s, ring_s = 2.0f, bright_s = 0.5f, morph_s, bow_s, creak_s, lvl_s = 1.0f, on_s, duck_s;
static float duck_env, duck_g = 1.0f, out_g, hz_last = -1.0f, lg_last;
static int   duck_att, chord_now = -1;
static float squash, bl_fast, bl_slow, scrape, press, press_to;
static int   press_cnt, slip_cnt = 1;
static float mat_pk;
static uint32_t rng = 0x6d2b79f5u;
static bool  first = true;

// ln x for 0.25 < x < 2 (2 atanh((x - 1) / (x + 1))): the bow's regulation
static inline float ln_near1(float x) { float z = (x - 1.0f) / (x + 1.0f), z2 = z * z; return 2.0f * z * (1.0f + z2 * (0.33333333f + z2 * (0.2f + z2 * 0.14285714f))); }
// a glide that arrives: within a hair of the target it is the target (so what depends on it can stop changing)
static inline float glide(float v, float to, float k) { v += k * (to - v); return fabsf(to - v) < 1e-6f ? to : v; }
// a raised-cosine contact of L seconds, its spectrum at f (1 at 0 Hz): how much a heavy strike reaches a mode
static inline float contact(float f, float L)
{
    if (L <= 0.0f) return 1.0f;
    float z = f * L;
    if (z > 3.5f) return 0.0f;
    if (fabsf(z - 1.0f) < 1e-3f) return 0.5f;
    if (z < 1e-4f) return 1.0f;
    return fabsf(sin_turns(0.5f * z) / (3.14159265f * z * (1.0f - z * z)));
}

// ---------------------------------------------------------------- init (UI task)
void mat_init(void)
{
    memset(CV, 0, sizeof CV);
    nps = nev = 0;
    for (int k = 0; k < NM; k++) { g_lo[k] = exp2f(-0.7f * (float)k); g_hi[k] = exp2f(-0.06f * (float)k); }
    for (int m = 0; m < NMAT; m++) {
        float e = 0.0f;
        for (int k = 0; k < NM; k++) { lr[m][k] = log2f(M_R[m][k]); lt[m][k] = log2f(M_T[m][k]); e += M_G[m][k] * M_G[m][k]; }
        g_norm[m] = 1.0f / sqrtf(e);                                   // every material as loud, struck
    }
    for (int v = 0; v < NCV; v++) CV[v].key_lg = -100.0f;
    first = true;
}
void mat_silence(void)
{
    for (int v = 0; v < NCV; v++) {
        cv_t *cv = &CV[v];
        memset(cv->s, 0, sizeof cv->s); memset(cv->c, 0, sizeof cv->c); memset(cv->bs, 0, sizeof cv->bs); memset(cv->bc, 0, sizeof cv->bc);
        memset(cv->bt, 0, sizeof cv->bt);
        cv->se = cv->be = 0.0f; cv->live = false;
    }
    nps = 0; scrape = 0.0f;
}

// ---------------------------------------------------------------- strikes (audio task: from fire())
static void strike(const stroke_t *st, int vel, int delay)
{
    if (st->amp <= 0.0f || vel <= 0) return;
    float vn = clampf((float)vel / 127.0f, 0.0f, 1.0f), a = st->amp * vn * (0.35f + 0.65f * vn);
    float b = clampf(st->bright * (0.6f + 0.4f * vn), 0.0f, 1.0f);
    int k = nps < NPS ? nps++ : 0;                                    // full (eight waiting): one of them goes
    ps[k] = (ev_t){ delay, a * (1.0f - b), a * b, st->contact_ms * 0.001f };
    if (st->scrape_s > 0.0f && a > scrape) scrape = a;
    float sq = st->amp * vn;
    if (sq > squash) squash = sq;
    if (sq > bl_slow) bl_slow = sq;
    if (sq > bl_fast) bl_fast = sq;
}
void mat_strike_pad(int pad, int vel, int delay) { if (pad >= 0 && pad < NPAD) strike(&PAD_ST[pad], vel, delay); }
void mat_strike_voice(int voice, int vel, int delay)
{
    if (voice < 0 || voice >= NDV) return;
    strike(&DV_ST[voice], vel, delay);
    duck_att = 10;                                                    // 10 ms down, 10 ms held, then back
}

// this block's impulses: the strikes that fall in it and the slips (CREAK's bursts while the pressure is over what it
// leaves; a scrape's train), sorted by their sample
static void events(int n)
{
    nev = 0;
    for (int j = 0; j < nps;) {
        if (ps[j].off < n) { if (nev < NEV) ev[nev++] = ps[j]; ps[j] = ps[--nps]; }
        else { ps[j].off -= n; j++; }
    }
    if (--press_cnt <= 0) { press_to = rng_uni(&rng); press_cnt = 4 + (int)(rng_next(&rng) % 24u); }
    press += 0.15f * (press_to - press);
    float gate = smoothstep(1.0f - creak_s, 1.15f - creak_s, press);
    float lv = fmaxf(creak_s * gate, scrape);
    if (lv > 0.002f) {
        int i = slip_cnt - 1;
        while (i < n) {
            float rate = 22.0f + 58.0f * lv * rng_uni(&rng);
            if (nev < NEV) ev[nev++] = (ev_t){ i, 0.0f, 0.45f * lv * (0.4f + 0.6f * rng_uni(&rng)), 0.0f };
            i += (int)(SR / rate * (0.5f + rng_uni(&rng))) + 1;
        }
        slip_cnt = i - n + 1;
    } else if (slip_cnt > 1) slip_cnt = slip_cnt > n ? slip_cnt - n : 1;
    for (int a = 1; a < nev; a++) { ev_t e = ev[a]; int b = a - 1; while (b >= 0 && ev[b].off > e.off) { ev[b + 1] = ev[b]; b--; } ev[b + 1] = e; }
}

// ---------------------------------------------------------------- the banks
// nm rotations (a multiple of 4), four at a time. s, c: the states (gain included); rc, rs: rotation x decay x the
// gain's change; x: an input with per-mode gains ix (NULL: none); w[e]: impulse e's weight per mode, added to c after
// sample ev[e].off; out += the sum.
static IRAM_ATTR __attribute__((noinline)) void rot_run(float *restrict s, float *restrict c, const float *restrict rc, const float *restrict rs,
                                                        int nm, const float *restrict x, const float *restrict ix,
                                                        float (*restrict w)[NM], int ne, float *restrict out, int n)
{
    for (int k = 0; k < nm; k += 4) {
        float S[4], C[4], A[4], B[4], U[4];
        for (int j = 0; j < 4; j++) { S[j] = s[k + j]; C[j] = c[k + j]; A[j] = rc[k + j]; B[j] = rs[k + j]; U[j] = x ? ix[k + j] : 0.0f; }
        int i = 0;
        for (int e = 0; e <= ne; e++) {
            int end = e < ne ? ev[e].off + 1 : n;
            if (end > n) end = n;
            if (x) {
                for (; i < end; i++) {
                    float xi = x[i], o = 0.0f;
                    for (int j = 0; j < 4; j++) { float t = A[j] * S[j] + B[j] * C[j]; C[j] = A[j] * C[j] - B[j] * S[j] + U[j] * xi; S[j] = t; o += t; }
                    out[i] += o;
                }
            } else {
                for (; i < end; i++) {
                    float o = 0.0f;
                    for (int j = 0; j < 4; j++) { float t = A[j] * S[j] + B[j] * C[j]; C[j] = A[j] * C[j] - B[j] * S[j]; S[j] = t; o += t; }
                    out[i] += o;
                }
            }
            if (e < ne) for (int j = 0; j < 4; j++) C[j] += w[e][k + j];
        }
        for (int j = 0; j < 4; j++) { s[k + j] = flushf(S[j]); c[k + j] = flushf(C[j]); }
    }
}
// the growth per sample that takes a gain from g0 towards g1 in one block (at most x1.43 or x0.7); *g0 moves with it
static inline float gain_step(float *g0, float g1, float inv_n)
{
    if (fabsf(g1 - *g0) <= 1e-6f * *g0) return 1.0f;                   // there
    float x = clampf(fmaxf(g1, 1e-9f) / fmaxf(*g0, 1e-9f), 0.7f, 1.43f), y = ln_near1(x) * inv_n;
    *g0 = fmaxf(*g0, 1e-9f) * x;
    return 1.0f + y * (1.0f + 0.5f * y);
}
static float bank_e(const float *s, const float *c, int nm) { float e = 0.0f; for (int k = 0; k < nm; k++) e += s[k] * s[k] + c[k] * c[k]; return e; }   // gain included

// ---------------------------------------------------------------- a block
void mat_run(float *acc, int n, bool on)
{
    on_s = glide(on_s, on ? 1.0f : 0.0f, kblock(0.02f, n));
    if (!on) { nps = 0; scrape = 0.0f; duck_att = 0; }
    if (!on && on_s < 1e-4f) {                                        // DRUM: nothing rings here
        if (CV[0].live || CV[1].live || CV[2].live || CV[3].live) mat_silence();
        on_s = 0.0f; out_g = 0.0f; mat_pk = 0.0f;
        eng_mat_lvl = 0.0f;
        for (int k = 0; k < NM; k++) eng_mode_amp[k] = 0.0f;
        return;
    }
    // the controls, smoothed at block rate
    float hz_now = clampf(eng_mat_hz, 20.0f, 4000.0f);
    if (hz_now != hz_last) { hz_last = hz_now; lg_last = log2_f(hz_now); }
    float lg_t = lg_last;
    if (first) { hz_lg = lg_t; morph_s = clampf(eng_mat_morph, 0.0f, 3.0f); ring_s = clampf(eng_mat_ring, 0.05f, 12.0f); first = false; }
    hz_lg = glide(hz_lg, lg_t, kblock(0.05f, n));
    bend_s = glide(bend_s, clampf(eng_bend, -36.0f, 36.0f), kblock(0.03f, n));
    ring_s = glide(ring_s, clampf(eng_mat_ring, 0.05f, 12.0f), kblock(0.05f, n));
    bright_s = glide(bright_s, clampf(eng_mat_bright, 0.0f, 1.0f), kblock(0.03f, n));
    morph_s = glide(morph_s, clampf(eng_mat_morph, 0.0f, (float)(NMAT - 1)), kblock(0.04f, n));
    creak_s = glide(creak_s, clampf(eng_creak, 0.0f, 1.0f), kblock(0.03f, n));
    duck_s = glide(duck_s, clampf(eng_duck, 0.0f, 1.0f), kblock(0.03f, n));
    float bow_t = clampf(eng_bow, 0.0f, 1.0f);
    if (eng_role == ROLE_PUMP) bow_t = 0.25f + 0.75f * bow_t;         // PUMP always bows a little: REST LEVEL is how loud
    if (!on) bow_t = 0.0f;
    float bow0 = bow_s;
    bow_s = glide(bow_s, bow_t, kblock(0.03f, n));
    float lv0 = out_g;
    lvl_s = glide(lvl_s, clampf(eng_mat_level, 0.0f, 1.0f), kblock(0.03f, n));
    out_g = lvl_s * on_s;
    // the duck: 10 ms down, held, 100 ms back
    float dg0 = duck_g;
    if (duck_att > 0) { duck_att--; duck_env += kblock(0.003f, n) * (1.0f - duck_env); }
    else duck_env = flushf(duck_env - kblock(0.1f, n) * duck_env);
    duck_g = 1.0f - duck_s * duck_env;
    // the strike's pitch: RUBBER's squash (up, falling back), LATEX's squeak (rising, then back)
    squash = flushf(squash * (1.0f - kblock(0.05f, n)));
    bl_fast = flushf(bl_fast * (1.0f - kblock(0.015f, n)));
    bl_slow = flushf(bl_slow * (1.0f - kblock(0.12f, n)));
    scrape = flushf(scrape * (1.0f - kblock(0.06f, n)));
    if (squash < 1e-5f) squash = 0.0f;
    if (bl_slow < 1e-5f) bl_slow = bl_fast = 0.0f;
    // the material now: the two neighbours and where between them
    int m0 = (int)morph_s; if (m0 > NMAT - 2) m0 = NMAT - 2;
    float mt = morph_s - (float)m0;
    float sqa = lerpf(M_SQUASH[m0], M_SQUASH[m0 + 1], mt), bla = lerpf(M_BLIP[m0], M_BLIP[m0 + 1], mt);
    float pmod = sqa * 0.07f * squash + bla * 0.4f * clampf(bl_slow - bl_fast, 0.0f, 1.0f);   // octaves
    float nrm = lerpf(g_norm[m0], g_norm[m0 + 1], mt);
    float g_k[NM];
    for (int k = 0; k < NM; k++) g_k[k] = lerpf(M_G[m0][k], M_G[m0 + 1][k], mt) * nrm * fast_exp2(-(float)k * (1.0f - bright_s) * 0.55f);
    // the chord
    int ch = clampi(eng_mat_chord, 0, 3), nch = CH_N[ch];
    if (chord_now < 0) for (int v = 0; v < NCV; v++) { CV[v].rlg = CH_LG[ch][v]; CV[v].g = v < nch ? 1.0f : 0.0f; }
    chord_now = ch;
    float share = SHARE[nch - 1];
    // the inputs
    events(n);
    float noise[AUDIO_BLOCK], xb[AUDIO_BLOCK], mix[AUDIO_BLOCK], bowmix[AUDIO_BLOCK], w[NEV][NM];
    bool bowing = bow_s > 1e-4f || bow0 > 1e-4f;
    if (bowing) for (int i = 0; i < n; i++) noise[i] = rng_bi(&rng);
    for (int i = 0; i < n; i++) mix[i] = bowmix[i] = 0.0f;
    const float kreg = kblock(0.05f, n), inv_n = 1.0f / (float)n;
    for (int v = 0; v < NCV; v++) {
        cv_t *cv = &CV[v];
        bool act = v < nch;
        if (cv->g < 0.001f && act) cv->rlg = CH_LG[ch][v];               // a note coming in starts on its own pitch
        cv->rlg = glide(cv->rlg, CH_LG[ch][v], kblock(0.03f, n));
        cv->g = glide(cv->g, act ? share : 0.0f, kblock(0.02f, n));
        if (!act && cv->g < 0.001f) {                                   // gone: silent, and nothing to run
            if (cv->live) {
                memset(cv->s, 0, sizeof cv->s); memset(cv->c, 0, sizeof cv->c); memset(cv->bs, 0, sizeof cv->bs); memset(cv->bc, 0, sizeof cv->bc);
                memset(cv->bt, 0, sizeof cv->bt); cv->se = cv->be = 0.0f; cv->live = false;
            }
            cv->g = 0.0f;
            continue;
        }
        if (!cv->live && !nev && !bowing) continue;
        cv->live = true;
        // its modes: worked out again when its pitch, the material or RING moved
        float lg = hz_lg + bend_s * (1.0f / 12.0f) + cv->rlg + pmod;
        if (lg != cv->key_lg || morph_s != cv->key_m || ring_s != cv->key_r) {
            cv->key_lg = lg; cv->key_m = morph_s; cv->key_r = ring_s;
            for (int k = 0; k < NM; k++) {
                float f = fast_exp2(lg + lerpf(lr[m0][k], lr[m0 + 1][k], mt));
                float S, C; sincos_turns(fminf(f * ISR, 0.45f), &S, &C);
                float q = 1.0f / sqrt_f(S * S + C * C);
                float t60 = fmaxf(ring_s * fast_exp2(lerpf(lt[m0][k], lt[m0 + 1][k], mt)), 0.02f), r = decay_of(6.9078f / (t60 * SR));
                cv->fq[k] = f; cv->cq[k] = C * q; cv->sq[k] = S * q; cv->rs_[k] = r;
                if (k < NB) { cv->tb[k] = fminf(t60, 4.0f); cv->rb[k] = decay_of(6.9078f / (cv->tb[k] * SR)); }
            }
        }
        // each mode's output gain now, reached in the states
        float go1[NM];
        for (int k = 0; k < NM; k++) {
            float f = cv->fq[k], hf = f > 11000.0f ? clampf((14000.0f - f) * (1.0f / 3000.0f), 0.0f, 1.0f) : 1.0f;
            go1[k] = g_k[k] * hf * cv->g * MAT_OUT;
        }
        if (nev || cv->se > 1e-14f) {
            if (cv->se <= 1e-14f) { memset(cv->s, 0, sizeof cv->s); memset(cv->c, 0, sizeof cv->c); for (int k = 0; k < NM; k++) cv->gs[k] = go1[k]; }
            for (int k = 0; k < NM; k++) {
                float rg = gain_step(&cv->gs[k], go1[k], inv_n), r = cv->rs_[k] * rg;
                cv->rC[k] = r * cv->cq[k]; cv->rS[k] = r * cv->sq[k];
            }
            // the strikes and slips: each mode's share of each impulse
            for (int e = 0; e < nev; e++)
                for (int k = 0; k < NM; k++) w[e][k] = (ev[e].lo * g_lo[k] + ev[e].hi * g_hi[k]) * contact(cv->fq[k], ev[e].len_s) * cv->gs[k];
            rot_run(cv->s, cv->c, cv->rC, cv->rS, NM, NULL, NULL, w, nev, mix, n);
        }
        // the bow: an amplitude to keep each of its modes at, held up no faster than the mode's own decay
        if (bowing || cv->be > 1e-14f) {
            const float bw = bow_s;
            if (cv->be <= 1e-14f) { memset(cv->bs, 0, sizeof cv->bs); memset(cv->bc, 0, sizeof cv->bc); for (int k = 0; k < NB; k++) cv->gbw[k] = go1[k]; }
            for (int k = 0; k < NB; k++) {
                float tilt = fast_exp2(-(float)k * (0.55f - 0.35f * bw));
                float tg = fmaxf(bw * BOW_A * tilt, cv->bt[k] * fast_exp2(-9.966f * (float)n / (cv->tb[k] * SR)));
                cv->bt[k] = tg > 1e-6f ? tg : 0.0f;
                float re = cv->rb[k], g0 = fmaxf(cv->gbw[k], 1e-9f);
                if (tg > 1e-6f) {
                    float a = sqrt_f(cv->bs[k] * cv->bs[k] + cv->bc[k] * cv->bc[k]) / g0;
                    float x = clampf((a + kreg * (tg - a)) / fmaxf(a, 1e-9f), 0.3f, 1.6f), y = ln_near1(x) * inv_n;
                    re = 1.0f + y * (1.0f + y * (0.5f + y * (1.0f / 6.0f)));   // the decay (or growth) per sample that gets it there
                }
                re *= gain_step(&cv->gbw[k], go1[k], inv_n);
                cv->bC[k] = re * cv->cq[k]; cv->bS[k] = re * cv->sq[k];
                cv->gb[k] = sqrt_f(fmaxf(1.0f - cv->rb[k] * cv->rb[k], 0.0f)) * tilt * BOW_IN * cv->gbw[k];
            }
            // noise, pulsed at the note's own rate (the slips), the bow's level ramped across the block
            const float *bx = NULL;
            if (bowing) {
                float f0 = cv->fq[0] * ISR, ph = cv->bph, b = bow0, db = (bow_s - bow0) * inv_n;
                for (int i = 0; i < n; i++) {
                    ph += f0; if (ph >= 1.0f) ph -= 1.0f;
                    float slip = ph < 0.2f ? 1.0f - 5.0f * ph : 0.0f;
                    xb[i] = b * noise[i] * (0.45f + 1.1f * slip);
                    b += db;
                }
                cv->bph = ph; bx = xb;
            }
            rot_run(cv->bs, cv->bc, cv->bC, cv->bS, NB, bx, cv->gb, NULL, 0, bowmix, n);
        }
        cv->se = bank_e(cv->s, cv->c, NM); cv->be = bank_e(cv->bs, cv->bc, NB);
        if (cv->se + cv->be < 1e-14f && !bowing) cv->live = false;
        if (v == 0)                                                     // the screen: the first note's modes
            for (int k = 0; k < NM; k++) {
                float a = cv->se > 0.0f ? sqrt_f(cv->s[k] * cv->s[k] + cv->c[k] * cv->c[k]) / fmaxf(cv->gs[k], 1e-9f) : 0.0f;
                if (k < NB && cv->be > 0.0f) a += duck_g * sqrt_f(cv->bs[k] * cv->bs[k] + cv->bc[k] * cv->bc[k]) / fmaxf(cv->gbw[k], 1e-9f);
                eng_mode_amp[k] = clampf(a * g_k[k] * 2.5f, 0.0f, 1.5f);
            }
    }
    // out: the strikes plus the ducked bow, at the material's level (both ramped across the block)
    float g = lv0, dg = (out_g - lv0) * inv_n, d = dg0, dd = (duck_g - dg0) * inv_n, pk = 0.0f;
    for (int i = 0; i < n; i++) {
        float o = (mix[i] + d * bowmix[i]) * g;
        acc[i] += o;
        float a = fabsf(o); if (a > pk) pk = a;
        g += dg; d += dd;
    }
    mat_pk = pk > mat_pk ? pk : mat_pk * 0.97f;
    eng_mat_lvl = clampf(mat_pk * 4.0f, 0.0f, 1.5f);
}

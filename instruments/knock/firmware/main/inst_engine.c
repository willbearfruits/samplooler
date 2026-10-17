// KNOCK's sound engine. See inst_engine.h for the path of a knock. Audio task only, except where noted.
#include "inst_engine.h"
#include "dsp.h"
#include <string.h>

// ---------------------------------------------------------------- tables
const char *const vt_label[NVTYPE] = { "BODY", "KICK", "SNARE", "HAT", "OPEN", "TOM", "CLAP", "RIM", "BELL", "SHAKE", "CONGA", "ZAP" };
const int vt_gm[NVTYPE] = { 0, 36, 38, 42, 46, 45, 39, 37, 56, 70, 63, 81 };
const int side_gm[NSIDE] = { 36, 38, 42, 45, 39 };
// BY SIDE: face, left, right, top, bottom as degrees (semitones) of the scale
const int8_t scale_deg[][NSIDE] = {
    { 0, 2, 4, 7, 9 },       // PENTA
    { 0, 3, 5, 7, 10 },      // MINOR
    { 0, 4, 7, 11, 14 },     // MAJ 7
    { 0, 3, 7, 10, 12 },     // MIN 7
    { 0, 7, 12, 19, -5 },    // FIFTHS
    { 0, 1, 4, 5, 7 },       // HIJAZ
    { 0, 2, 4, 6, 8 },       // WHOLE
    { 0, 0, 0, 0, 0 },       // UNISON
};

// modal bodies: mode ratios, decays (s) and gains
#define NMODE 6
typedef struct { int n; float r[NMODE], d[NMODE], g[NMODE]; float trim; } body_t;
static const body_t bodies[NBODY] = {
    [B_OFF]      = { 0, { 0 }, { 0 }, { 0 } },
    [B_WOOD]     = { 4, { 1, 2.756f, 5.404f, 8.933f }, { 0.16f, 0.07f, 0.04f, 0.022f }, { 1, 0.5f, 0.28f, 0.15f }, 0.90f },
    [B_MARIMBA]  = { 3, { 1, 3.99f, 9.9f }, { 0.75f, 0.2f, 0.07f }, { 1, 0.32f, 0.12f }, 0.50f },
    [B_BELL]     = { 6, { 0.5f, 1, 1.183f, 1.506f, 2.0f, 2.514f }, { 3.2f, 2.4f, 1.7f, 1.3f, 1.0f, 0.6f }, { 0.55f, 1, 0.6f, 0.45f, 0.4f, 0.22f }, 0.22f },
    [B_GLASS]    = { 4, { 1, 2.32f, 4.25f, 6.63f }, { 1.7f, 1.0f, 0.6f, 0.35f }, { 1, 0.6f, 0.4f, 0.25f }, 0.30f },
    [B_TABLA]    = { 5, { 1, 2, 3, 4, 5 }, { 0.5f, 0.35f, 0.25f, 0.18f, 0.12f }, { 1, 0.7f, 0.5f, 0.3f, 0.2f }, 0.45f },
    [B_STEEL]    = { 4, { 1, 2, 3.01f, 4.02f }, { 1.4f, 0.9f, 0.5f, 0.3f }, { 1, 0.6f, 0.35f, 0.2f }, 0.35f },
    [B_MEMBRANE] = { 6, { 1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f }, { 0.38f, 0.26f, 0.2f, 0.18f, 0.15f, 0.12f }, { 1, 0.6f, 0.45f, 0.4f, 0.3f, 0.25f }, 0.45f },
    [B_METAL]    = { 6, { 1, 1.41f, 2.24f, 2.92f, 3.83f, 4.69f }, { 1.8f, 1.4f, 1.0f, 0.8f, 0.6f, 0.4f }, { 1, 0.8f, 0.6f, 0.5f, 0.4f, 0.3f }, 0.20f },
};

// ---------------------------------------------------------------- shared state
hit_t eng_hits[MAXHITS];
volatile int      eng_nhits;
volatile uint32_t eng_loop_len, eng_loop_start;
volatile int      eng_loop_run = 1;
volatile float eng_tune, eng_mutate, eng_filter, eng_roll_hz, eng_stutter, eng_knock_avg = 0.1f;
volatile int   eng_roll_side, eng_roll_vel = 90;
volatile uint8_t eng_pinned[NSNIP];
volatile float eng_env, eng_thr, eng_body_lvl[NSIDE], eng_out_lvl;

SPSC(onsetq, onset_t, 16)
SPSC(trigq, trig_t, 32)
SPSC(playq, played_t, 64)
static onsetq_t onsets;
static trigq_t  trigs;
static playq_t  played;

bool eng_onset_pop(onset_t *o) { return onsetq_pop(&onsets, o); }
void eng_trigger(const trig_t *t) { trigq_push(&trigs, t); }
bool eng_played_pop(played_t *p) { return playq_pop(&played, p); }

static uint32_t seed = 0x9e3779b9u;
static uint32_t frame0;                     // audio_frames() at this block

// ---------------------------------------------------------------- snippets
static int16_t *snips;                      // NSNIP x SNIP_LEN
static volatile int snip_fill[NSNIP];       // samples captured so far
static int snip_next, cap_slot = -1, cap_pos;
#define HIST 4096
static float hist[HIST];
static uint32_t hist_w;

const int16_t *eng_snip(int s) { return snips && s >= 0 && s < NSNIP ? snips + (size_t)s * SNIP_LEN : NULL; }
static inline int16_t to16(float x) { x *= 16384.0f; return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x); }

// ---------------------------------------------------------------- voices
#define NV 12
typedef struct {
    uint8_t type, side;
    bool on;
    int delay, t;
    float amp, ph, ph2, f0, f1;
    float ae, kae, pe, kpe, xe;              // amp env, pitch env, an extra env
    float carry;                             // the stolen voice's last sample, decaying away
    float last;
    svf_t fa, fb;
} voice_t;
static voice_t vo[NV];

static float dec_mul(void) { return pv[P_DECAY]; }

static void voice_start(int type, int side, float amp, int delay)
{
    // a free voice, else the quietest; a voice in use hands its last sample over as a fading carry
    int k = -1; float q = 1e9f;
    for (int i = 0; i < NV; i++) {
        if (!vo[i].on) { k = i; break; }
        float l = vo[i].ae * vo[i].amp;
        if (l < q) { q = l; k = i; }
    }
    voice_t *v = &vo[k];
    float carry = v->on ? v->last + v->carry : 0.0f;
    if (type == V_HAT) for (int i = 0; i < NV; i++) if (vo[i].on && vo[i].type == V_OHAT) vo[i].kae = 0.992f;   // closed hat chokes the open one
    memset(v, 0, sizeof *v);
    v->carry = carry;
    v->type = (uint8_t)type; v->side = (uint8_t)side; v->on = true; v->delay = delay; v->amp = amp; v->ae = 1.0f; v->pe = 1.0f; v->xe = 1.0f;
    float tn = exp2f((eng_tune + (float)(type == V_TOM || type == V_CONGA || type == V_ZAP || type == V_COWBELL
                                       ? scale_deg[(int)pv[P_SCALE]][side] : 0)) / 12.0f);
    float dc = dec_mul(), pk = pv[P_PUNCHV] * 0.01f;
    switch (type) {
    case V_KICK:    v->f0 = 46.0f * tn; v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.30f * dc); v->f1 = 2.0f + 3.0f * pk; break;
    case V_SNARE:   v->f0 = 185.0f * tn; svf_set(&v->fa, 1600.0f * tn, 0.7f); v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.15f * dc); break;
    case V_HAT:     svf_set(&v->fa, 7200.0f * tn, 0.8f); v->kae = 1.0f - onepole_s(0.04f * dc); break;
    case V_OHAT:    svf_set(&v->fa, 6500.0f * tn, 0.9f); v->kae = 1.0f - onepole_s(0.32f * dc); break;
    case V_TOM:     v->f0 = 105.0f * tn; v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.38f * dc); break;
    case V_CLAP:    svf_set(&v->fa, 1100.0f * tn, 1.5f); v->kpe = 1.0f - onepole_s(0.004f); v->kae = 1.0f - onepole_s(0.14f * dc); break;
    case V_RIM:     v->f0 = 520.0f * tn; svf_set(&v->fa, 1800.0f * tn, 2.0f); v->kae = 1.0f - onepole_s(0.028f * dc); v->kpe = 1.0f - onepole_s(0.006f); break;
    case V_COWBELL: v->f0 = 540.0f * tn; v->f1 = 800.0f * tn; svf_set(&v->fa, 2400.0f * tn, 1.2f); v->kpe = 1.0f - onepole_s(0.02f); v->kae = 1.0f - onepole_s(0.22f * dc); break;
    case V_SHAKER:  svf_set(&v->fa, 5500.0f * tn, 1.2f); v->kae = 1.0f - onepole_s(0.07f * dc); v->xe = 0.0f; break;
    case V_CONGA:   v->f0 = 330.0f * tn; v->kpe = 1.0f - onepole_s(0.008f); v->kae = 1.0f - onepole_s(0.2f * dc); break;
    case V_ZAP:     v->f0 = 85.0f * tn; v->f1 = 2600.0f * tn; v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.17f * dc); break;
    }
}

static inline float voice_run(voice_t *v)
{
    if (v->delay > 0) { v->delay--; float c = v->carry; v->carry *= 0.93f; return c; }
    float y = 0, pk = pv[P_PUNCHV] * 0.01f;
    const float isr = 1.0f / (float)AUDIO_SR;
    switch (v->type) {
    case V_KICK:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + v->f1 * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        if (v->t < 48) y += (0.2f + 0.4f * pk) * rng_bi(&seed) * (1.0f - (float)v->t / 48.0f);
        break;
    case V_SNARE:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + 0.4f * v->pe) * isr);
        y = 0.45f * sin_turns(v->ph) * v->pe + 0.75f * svf_hp(&v->fa, rng_bi(&seed)) * v->ae;
        break;
    case V_HAT: case V_OHAT:
        y = 0.75f * svf_hp(&v->fa, rng_bi(&seed)) * v->ae;
        break;
    case V_TOM: case V_CONGA:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + (v->type == V_TOM ? 0.45f : 0.25f) * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        if (v->type == V_CONGA && v->t < 40) y += 0.25f * rng_bi(&seed);
        break;
    case V_CLAP: {                                   // three claps 11 ms apart, then the tail
        if (v->t == 352 || v->t == 704) v->pe = 1.0f;
        float e = v->t > 704 && v->ae > v->pe ? v->ae : v->pe;
        y = 1.5f * svf_bp(&v->fa, rng_bi(&seed), NULL) * e;
        break;
    }
    case V_RIM:
        v->ph = wrap1(v->ph + v->f0 * isr);
        y = 0.6f * sin_turns(v->ph) * v->ae + 1.2f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->pe;
        break;
    case V_COWBELL: {
        float d0 = v->f0 * isr, d1 = v->f1 * isr;
        v->ph = wrap1(v->ph + d0); v->ph2 = wrap1(v->ph2 + d1);
        float s = 0.5f * (square_blep(v->ph, d0) + square_blep(v->ph2, d1));
        y = 0.8f * svf_bp(&v->fa, s, NULL) * (0.4f * v->pe + 0.6f) * v->ae;
        break;
    }
    case V_SHAKER:
        v->xe += (1.0f - v->xe) * 0.03f;             // a 1 ms swell: grains, not a click
        y = 1.1f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->ae * v->xe;
        break;
    case V_ZAP:
        v->ph = wrap1(v->ph + (v->f0 + v->f1 * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        break;
    }
    v->ae *= v->kae; v->pe *= v->kpe;
    v->t++;
    y *= v->amp;
    v->last = y;
    float c = v->carry; v->carry = flush_denormal(v->carry * 0.93f);
    if (v->ae < 1e-4f && v->t > 64) { v->on = false; return y + c; }
    return y + c;
}

// ---------------------------------------------------------------- bodies: one bank of modes per side
typedef struct { float c1, c2, g, y1, y2; } mode_t;
typedef struct {
    mode_t m[NMODE]; int nm;
    int snip, pos, len, delay;               // exciter: a snippet, or a synthetic one when snip < 0
    float gain, lp, lvl;
    float note, note_done;                   // the bank's pitch, gliding; what its coefficients were made for
} bank_t;
static bank_t bank[NSIDE];
static int   body_cur = -1, body_fade;       // a new body: the old one fades for 128 samples first
static float last_ring = -1;
static float live_gate, exc_lp_live;

// Each bank glides to its note (ROOT, TUNE and tilt, the side's degree) over ~25 ms, so a new root or scale
// slides the ringing modes instead of jumping them. Coefficients are remade only while a bank moves.
static void bank_tune(bool force)
{
    const body_t *b = &bodies[body_cur < 0 ? 0 : body_cur];
    bool by_side = pv[P_BPITCH] >= 1.0f;
    for (int s = 0; s < NSIDE; s++) {
        bank_t *k = &bank[s];
        float want = pv[P_ROOT] + eng_tune + (by_side ? (float)scale_deg[(int)pv[P_SCALE]][s] : 0.0f);
        k->note = k->note <= 0.0f ? want : k->note + 0.15f * (want - k->note);   // the first time it is set outright
        if (!force && fabsf(k->note - k->note_done) < 0.004f) continue;
        k->note_done = k->note;
        k->nm = b->n;
        float f0 = midi_hz(k->note);
        for (int i = 0; i < b->n; i++) {
            float f = clampf(f0 * b->r[i], 30.0f, 0.42f * (float)AUDIO_SR);
            float w = TWO_PI * f / (float)AUDIO_SR;
            float t60 = b->d[i] * pv[P_RING] * (f > 4000.0f ? 4000.0f / f : 1.0f);
            float r = expf(-6.9078f / (t60 * (float)AUDIO_SR));
            k->m[i].c1 = 2.0f * r * cosf(w); k->m[i].c2 = -r * r;
            k->m[i].g = b->g[i] * sinf(w) * 0.018f * b->trim;
        }
    }
}

static void body_excite(int side, int snip, float gain, int delay)
{
    bool by_side = pv[P_BPITCH] >= 1.0f;
    bank_t *k = &bank[by_side ? side : 0];
    k->snip = snip; k->pos = 0; k->delay = delay;
    k->len = (int)(pv[P_TOUCH] * 0.001f * (float)AUDIO_SR);
    if (k->len > SNIP_LEN) k->len = SNIP_LEN;
    if (k->len < 96) k->len = 96;
    k->gain = gain;
}

// ---------------------------------------------------------------- the bus
static svf_t f_lp, f_hp;
static float flt_s;
static smooth_t drive_s, vlev_s, blev_s, room_s, echo_s;
static float comp_env, comp_g = 1.0f, comp_s;
#define ECHO_N 32768
static float *echo_buf; static uint32_t echo_w;
static float echo_lp, echo_t_s = 0.25f, wow_ph;
#define CN 4
#define CLEN 1024
static float comb[CN][CLEN], comb_lp[CN];
static uint32_t comb_w;
static const float comb_len[CN] = { 810.0f, 862.0f, 927.0f, 984.0f };
#define AP_LEN 512
static float ap[2][AP_LEN]; static uint32_t ap_w;
static const int ap_len[2] = { 403, 320 };
static float rsize_s = 0.5f;
#define STUT_N 32768
static float *stut_buf; static uint32_t stut_w;
static bool  stut_on; static uint32_t stut_start; static int stut_len, stut_pos; static float stut_x;
static float roll_ph; static bool roll_was;

// ---------------------------------------------------------------- detection
static float env_f, env_s, floor_e = 0.01f, out_env, a_f, r_f, a_s, r_s, prev_x;
static float bleed_k = 0.5f;                // how much of its own output the mic hears (learned)
static int   refract, bright_n;
static float bright_hf, bright_all, onset_lvl;
static uint32_t onset_frame;
static int16_t onset_snip;

void eng_init(void)
{
    snips = app_alloc((size_t)NSNIP * SNIP_LEN * sizeof(int16_t));
    echo_buf = app_alloc(ECHO_N * sizeof(float));
    stut_buf = app_alloc(STUT_N * sizeof(float));
    a_f = onepole_s(0.0003f); r_f = onepole_s(0.010f); a_s = onepole_s(0.030f); r_s = onepole_s(0.200f);
    refract = AUDIO_SR / 4;
    smooth_init(&drive_s, 0, 0.02f); smooth_init(&vlev_s, 0.8f, 0.02f); smooth_init(&blev_s, 0.5f, 0.02f);
    smooth_init(&room_s, 0, 0.03f); smooth_init(&echo_s, 0, 0.03f);
    for (int s = 0; s < NSIDE; s++) bank[s].snip = -2;
    if (!snips || !echo_buf || !stut_buf) app_log("knock: short of memory (snippets %p, echo %p, stutter %p)", (void *)snips, (void *)echo_buf, (void *)stut_buf);
}

void eng_changed(void) { }      // everything is picked up per block; safe before eng_init()

uint32_t eng_pass(uint32_t frame) { uint32_t l = eng_loop_len; return l ? (frame - eng_loop_start) / l : 0; }

int eng_beats(void)
{
    int b = (int)pv[P_BEATS];
    uint32_t len = eng_loop_len;
    if (b > 0 || !len) return b > 0 ? b : 4;
    static const int pref[] = { 4, 2, 8, 3, 6, 1, 12, 16 };
    float sec = (float)len / (float)AUDIO_SR, best = 1e9f; int bb = 4;
    for (int i = 0; i < 8; i++) {
        float bpm = (float)pref[i] * 60.0f / sec;
        if (bpm >= 75.0f && bpm <= 165.0f) return pref[i];
        float d = fabsf(bpm - 110.0f);
        if (d < best) { best = d; bb = pref[i]; }
    }
    return bb;
}
float eng_bpm(void) { uint32_t len = eng_loop_len; return len ? (float)eng_beats() * 60.0f * (float)AUDIO_SR / (float)len : 0.0f; }

uint32_t eng_qoff(const hit_t *h)
{
    static const int div[] = { 0, 1, 2, 3, 4, 6, 8 };       // GRID: OFF, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32
    uint32_t len = eng_loop_len;
    int d = div[(int)pv[P_GRID]];
    if (!len || !d) return h->off;
    int steps = eng_beats() * d;
    float g = (float)len / (float)steps, off = (float)h->off;
    float k = (float)(int)(off / g + 0.5f);
    float target = k * g;
    if (((int)k & 1) && d >= 2) target += pv[P_SWING] * 0.01f * g * 0.5f;
    float q = off + (target - off) * pv[P_SNAP] * 0.01f;
    while (q >= (float)len) q -= (float)len;
    if (q < 0) q += (float)len;
    return (uint32_t)q;
}

static void trigger(int side, int vel, int src, int snip, int delay)
{
    if (side < 0 || side >= NSIDE) return;
    float a = (float)vel / 127.0f;
    if (src == SRC_LOOP) a *= pv[P_LLEVEL] * 0.01f;
    int vt = (int)pv[P_FACE + side];
    if (vt != V_NONE) voice_start(vt, side, a * a * 0.42f + 0.02f * a, delay);
    // the body: live ONE-pitch hits already rang it from the mic itself
    bool one = pv[P_BPITCH] < 1.0f;
    if (body_cur > B_OFF && !(src == SRC_LIVE && one)) body_excite(side, snip, a, delay);
    played_t p = { frame0 + (uint32_t)delay, (uint8_t)side, (uint8_t)vel, (uint8_t)src, (int16_t)snip };
    playq_push(&played, &p);
}

static void loop_block(int n)
{
    uint32_t len = eng_loop_len;
    if (!len || !eng_loop_run) return;
    uint32_t rel = frame0 - eng_loop_start, ph = rel % len;
    int nh = eng_nhits;
    float mu = eng_mutate;
    for (int i = 0; i < nh; i++) {
        hit_t *h = &eng_hits[i];
        if (h->dead) continue;
        uint32_t q = eng_qoff(h), d = (q + len - ph) % len;
        if (d >= (uint32_t)n) continue;
        uint32_t pass = (rel + d) / len;
        if ((uint16_t)pass == h->born) continue;                       // it was knocked in this pass: it already sounded
        int side = h->side, vel = h->vel;
        if (mu > 0.001f) {
            float r = rng_uni(&seed);
            if (r < mu * 0.35f) continue;                     // dropped
            if (r < mu * 0.7f) side = (int)(rng_next(&seed) % NSIDE);
            vel = clampi((int)((float)vel * (1.0f + mu * 0.5f * rng_bi(&seed))), 10, 127);
        }
        trigger(side, vel, SRC_LOOP, h->snip, (int)d);
    }
}

static void roll_block(int n)
{
    float hz = eng_roll_hz;
    if (hz <= 0.0f) { roll_was = false; return; }
    if (!roll_was) { roll_ph = 1.0f; roll_was = true; }
    float step = hz / (float)AUDIO_SR;
    for (int i = 0; i < n; i++) {
        if (roll_ph >= 1.0f) { roll_ph -= 1.0f; trigger(eng_roll_side, eng_roll_vel, SRC_ROLL, -1, i); }
        roll_ph += step;
    }
}

void eng_audio(const float *in, float *out, int n)
{
    frame0 = audio_frames();
    // parameters per block
    int want_body = (int)pv[P_BODY];
    if (want_body != body_cur && body_fade == 0) body_fade = 128;
    if (body_cur >= 0) { bool f = pv[P_RING] != last_ring; last_ring = pv[P_RING]; bank_tune(f); }
    float sens = pv[P_SENS] * 0.01f;
    float base = 0.003f * powf(40.0f, 1.0f - sens);              // 100 %: 0.003, 50 %: 0.019, 0 %: 0.12
    float touch_rel = 1.0f / (pv[P_TOUCH] * 0.001f * (float)AUDIO_SR);
    float exc_k = onepole_hz(400.0f * exp2f(pv[P_BRIGHT] * 0.05f));
    float bgain = 1.0f / fmaxf(eng_knock_avg, 0.01f);
    // the DJ filter, per block
    float fl = clampf(eng_filter, -1.0f, 1.0f);
    flt_s += 0.2f * (fl - flt_s);
    svf_set(&f_lp, 18000.0f * exp2f(fminf(flt_s, 0.0f) * 7.0f), 0.9f);
    svf_set(&f_hp, 18.0f * exp2f(fmaxf(flt_s, 0.0f) * 9.0f), 0.9f);
    comp_s += 0.12f * (pv[P_COMP] * 0.01f - comp_s);
    float comp_amt = comp_s;
    float etime = pv[P_ETIME], efb = pv[P_EFB] * 0.01f;
    float rsz = 0.45f + 0.55f * pv[P_RSIZE] * 0.01f, rfb = 0.7f + 0.25f * pv[P_RSIZE] * 0.01f;

    // what the UI asked for
    trig_t t;
    while (trigq_pop(&trigs, &t)) trigger(t.side, t.vel, t.src, t.snip, 0);
    loop_block(n);
    roll_block(n);

    float pk = 0, comp_pk = 0, bl[NSIDE] = { 0 };
    float gscale = 1.0f, gstep = 0.0f;
    {                                                         // the compressor's gain, worked out per block
        float thr = 0.25f, ratio = 1.0f + 5.0f * comp_amt;
        float want = comp_env > thr ? powf(thr / comp_env, 1.0f - 1.0f / ratio) : 1.0f;
        gscale = comp_g; gstep = (want - comp_g) / (float)n; comp_g = want;
    }
    float makeup = 1.0f + 0.9f * comp_amt;
    for (int i = 0; i < n; i++) {
        float x = in[i];
        hist[hist_w++ & (HIST - 1)] = x;
        // ---- detection
        follow(&env_f, x, a_f, r_f);
        float slow_before = env_s;
        follow(&env_s, x, a_s, r_s);
        if (env_s < floor_e) floor_e += 0.01f * (env_s - floor_e); else floor_e += 0.000005f * (env_s - floor_e);
        // the box's own sound must not count as knocks: what the mic should be hearing of it comes off both
        // envelopes before the jump test, with a little margin on the threshold for the guess
        float bleed = bleed_k * out_env;
        float thr = base + 4.0f * floor_e + 0.3f * bleed;
        if (cap_slot >= 0) {                                    // capturing a snippet
            snips[(size_t)cap_slot * SNIP_LEN + cap_pos] = to16(x);
            snip_fill[cap_slot] = ++cap_pos;
            if (cap_pos >= SNIP_LEN) cap_slot = -1;
        }
        if (bright_n > 0) {                                     // the first 3 ms of a knock: how sharp is it
            float d = x - prev_x;
            bright_hf += d * d; bright_all += x * x;
            if (env_f > onset_lvl) onset_lvl = env_f;
            if (--bright_n == 0) {
                onset_t o = { onset_frame, clampf(0.5f * bright_hf / (bright_all + 1e-9f), 0.0f, 1.0f), onset_lvl, onset_snip };
                onsetq_push(&onsets, &o);
            }
        } else if (refract > 0) refract--;
        else if (env_f - bleed > thr && env_f - bleed > 2.5f * (slow_before - bleed) + 0.005f) {
            onset_frame = frame0 + (uint32_t)i; bright_n = 96; bright_hf = bright_all = 0; onset_lvl = env_f;
            refract = (int)(0.06f * (float)AUDIO_SR);
            onset_snip = -1;
            if (snips) {                                        // a snippet, starting a little before the onset
                int s = snip_next;
                for (int k = 0; k < NSNIP && eng_pinned[s]; k++) s = (s + 1) % NSNIP;
                if (!eng_pinned[s]) {
                    snip_next = (s + 1) % NSNIP;
                    int16_t *p = snips + (size_t)s * SNIP_LEN;
                    for (int k = 0; k < SNIP_PRE; k++) p[k] = to16(hist[(hist_w - SNIP_PRE + k) & (HIST - 1)]);
                    cap_slot = s; cap_pos = SNIP_PRE; snip_fill[s] = SNIP_PRE; onset_snip = (int16_t)s;
                }
            }
        }
        prev_x = x;
        // ---- the live gate for ONE-pitch bodies: opens on knocks, lets rubs and scrapes through
        float gt = clampf((env_f - bleed - 0.6f * thr) / (0.8f * thr + 1e-6f), 0.0f, 1.0f);
        if (gt > live_gate) live_gate = gt; else live_gate -= touch_rel;
        if (live_gate < 0) live_gate = 0;

        // ---- voices
        float yv = 0;
        for (int v = 0; v < NV; v++) if (vo[v].on) yv += voice_run(&vo[v]);
        // ---- bodies
        float yb = 0;
        if (body_cur > B_OFF) {
            bool one = pv[P_BPITCH] < 1.0f;
            for (int s = 0; s < NSIDE; s++) {
                bank_t *k = &bank[s];
                float e = 0;
                if (s == 0 && one && live_gate > 0) { exc_lp_live += exc_k * (x * bgain - exc_lp_live); e += exc_lp_live * live_gate; }
                if (k->snip >= -1 && k->pos < k->len) {
                    if (k->delay > 0) k->delay--;
                    else {
                        float sx;
                        if (k->snip >= 0) {
                            int f = snip_fill[k->snip];
                            sx = k->pos < f ? (float)snips[(size_t)k->snip * SNIP_LEN + k->pos] * (1.0f / 16384.0f) * bgain : 0.0f;
                            if (k->pos < f) k->pos++;
                            else if (f >= SNIP_LEN) k->pos = k->len;
                        } else {                                // no snippet: a click and a short burst of noise
                            sx = (k->pos == 0 ? 3.0f : 0.0f) + rng_bi(&seed) * (k->pos < 96 ? 1.0f - (float)k->pos / 96.0f : 0.0f);
                            k->pos++;
                        }
                        float w = k->len - k->pos < 64 ? (float)(k->len - k->pos) / 64.0f : 1.0f;
                        k->lp += exc_k * (sx - k->lp);
                        e += k->lp * w * k->gain;
                    }
                }
                float ys = 0;
                for (int m = 0; m < k->nm; m++) {
                    mode_t *md = &k->m[m];
                    float y = md->c1 * md->y1 + md->c2 * md->y2 + md->g * e;
                    md->y2 = md->y1; md->y1 = flush_denormal(y);
                    ys += y;
                }
                if (body_fade > 0) { for (int m = 0; m < k->nm; m++) { k->m[m].y1 *= 0.96f; k->m[m].y2 *= 0.96f; } }
                yb += ys;
                float a = fabsf(ys); if (a > bl[s]) bl[s] = a;
            }
            if (body_fade > 0 && --body_fade == 0) { body_cur = want_body; for (int s = 0; s < NSIDE; s++) memset(bank[s].m, 0, sizeof bank[s].m); bank_tune(true); }
        } else if (body_cur != want_body) { body_cur = want_body; body_fade = 0; for (int s = 0; s < NSIDE; s++) memset(bank[s].m, 0, sizeof bank[s].m); bank_tune(true); }
        // ---- the bus
        yb = 0.8f * soft_tanh(yb * 1.25f);                     // a hard knock rounds off instead of spiking
        float y = smooth_to(&vlev_s, pv[P_VLEVEL] * 0.01f) * yv + smooth_to(&blev_s, pv[P_BLEVEL] * 0.01f) * yb;
        y = svf_hp(&f_hp, svf_lp(&f_lp, y));
        float dr = smooth_to(&drive_s, pv[P_DRIVE] * 0.01f);
        if (dr > 0.002f) { float k = 1.0f + 7.0f * dr; y = (1.0f - dr) * y + dr * soft_tanh(y * k) * (1.0f + 2.0f * dr) / k; }
        float a = fabsf(y);
        comp_env += (a > comp_env ? 0.006f : 0.0004f) * (a - comp_env);
        y *= gscale * makeup; gscale += gstep;
        if (a > comp_pk) comp_pk = a;
        // tape echo: a send, repeats through a darkening, saturating loop, a little wow
        float es = smooth_to(&echo_s, pv[P_ECHO] * 0.01f);
        if (echo_buf) {
            echo_t_s += 0.0004f * (etime - echo_t_s);
            wow_ph = wrap1(wow_ph + 0.6f / (float)AUDIO_SR);
            float dly = echo_t_s * (float)AUDIO_SR + 6.0f * sin_turns(wow_ph);
            if (dly > (float)ECHO_N - 4.0f) dly = (float)ECHO_N - 4.0f;
            int di = (int)dly; float fr = dly - (float)di;
            float e0 = echo_buf[(echo_w - (uint32_t)di) & (ECHO_N - 1)], e1 = echo_buf[(echo_w - (uint32_t)di - 1) & (ECHO_N - 1)];
            float ret = e0 + (e1 - e0) * fr;
            echo_lp += 0.45f * (ret - echo_lp);
            echo_buf[echo_w & (ECHO_N - 1)] = flush_denormal(soft_tanh(y * es + echo_lp * efb));
            echo_w++;
            y += echo_lp * 0.9f;
        }
        // room: four damped combs, two all-passes
        float rm = smooth_to(&room_s, pv[P_ROOM] * 0.01f);
        rsize_s += 0.0005f * (rsz - rsize_s);
        float rin = y * 0.25f, rout = 0;
        for (int c = 0; c < CN; c++) {
            float dl = comb_len[c] * rsize_s;
            int di = (int)dl; float fr = dl - (float)di;
            float c0 = comb[c][(comb_w - (uint32_t)di) & (CLEN - 1)], c1 = comb[c][(comb_w - (uint32_t)di - 1) & (CLEN - 1)];
            float o = c0 + (c1 - c0) * fr;
            comb_lp[c] += 0.4f * (o - comb_lp[c]);
            comb[c][comb_w & (CLEN - 1)] = flush_denormal(rin + comb_lp[c] * rfb);
            rout += o;
        }
        comb_w++;
        for (int k = 0; k < 2; k++) {
            float d = ap[k][(ap_w - (uint32_t)ap_len[k]) & (AP_LEN - 1)];
            float v = rout + 0.5f * d;
            ap[k][ap_w & (AP_LEN - 1)] = flush_denormal(v);
            rout = d - 0.5f * v;
        }
        ap_w++;
        y += rm * 0.35f * rout;
        // stutter: the last slice of the bus, repeated while the stick is held down
        if (stut_buf) {
            if (!stut_on) { stut_buf[stut_w & (STUT_N - 1)] = y; stut_w++; }   // held still while it repeats: the ring
            float want = eng_stutter;                                            // would overwrite the slice otherwise
            if (want > 0.0f && !stut_on) { stut_on = true; stut_len = (int)(want * (float)AUDIO_SR); if (stut_len > STUT_N - 64) stut_len = STUT_N - 64; stut_start = stut_w - (uint32_t)stut_len; stut_pos = 0; }
            if (stut_on) {
                float r = stut_buf[(stut_start + (uint32_t)stut_pos) & (STUT_N - 1)];
                float w = stut_pos < 48 ? (float)stut_pos / 48.0f : stut_len - stut_pos < 48 ? (float)(stut_len - stut_pos) / 48.0f : 1.0f;
                if (++stut_pos >= stut_len) { stut_pos = 0; if (want > 0.0f) { stut_len = (int)(want * (float)AUDIO_SR); if (stut_len > STUT_N - 64) stut_len = STUT_N - 64; } }
                float tgt = want > 0.0f ? 1.0f : 0.0f;
                stut_x += (tgt - stut_x) * 0.01f;
                y = y * (1.0f - stut_x) + r * w * stut_x;
                if (want <= 0.0f && stut_x < 0.001f) { stut_on = false; stut_x = 0; }
            }
        }
        out[i] = y;
        float ay = fabsf(y); if (ay > pk) pk = ay;
    }
    (void)comp_pk;
    out_env = pk > out_env ? pk : out_env * 0.96f;
    // How loud is the box's own sound in the mic? With a speaker fitted, the mic follows the output; on
    // headphones it does not. The ratio of the two while the box plays is learned: down fast, up slowly
    // (knocks raise the mic too), and it sets how far the threshold climbs over the box's own drums.
    if (out_env > 0.03f) {
        float r = env_s / out_env;
        bleed_k += (r < bleed_k ? 0.05f : 0.002f) * (r - bleed_k);
        bleed_k = clampf(bleed_k, 0.01f, 2.0f);
    }
    eng_env = env_f; eng_thr = base + 4.0f * floor_e + 1.3f * bleed_k * out_env;
    eng_out_lvl = pk;
    for (int s = 0; s < NSIDE; s++) eng_body_lvl[s] = bl[s];
}

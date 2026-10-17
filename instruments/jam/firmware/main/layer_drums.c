// DRUMS: KNOCK as a layer of the jam. Knock on the box: the mic hears when and how hard, the
// accelerometer's 1 kHz stream says which side was hit (face, left, right, top, bottom edge), and each side
// is its own drum (VOICE) over the knock itself ringing a set of tuned resonators (BODY). The KIT picks the
// five drums and the body; tonal drums and BY SIDE bodies play the song's chord, so they follow the key and
// the progression. REC is an event loop in the song's loop: the first loop of the jam (on any layer) sets
// its length; hold REC to add, tap to mute the drums' loop. GRID, SWING and MUTATE act at playback and never
// change what was recorded. The stick: up rolls the last drum, down repeats the last slice of the drums,
// left/right a filter on them. Tilt tunes; leaning mutates. The kicks of the loop go to song.kicks for the bass.
//
// Ported from ../knock (inst_engine.c + instrument.c); the numbers measured there hold here.
#include "jam.h"
#include "ui.h"
#include "spsc.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- params
enum { DP_KIT, DP_TUNE, DP_DECAY, DP_SENSE, DP_SIDES, DP_GRID, DP_STRENGTH, DP_SWING, DP_MUTATE, DP_UNDO,
       DP_BODY, DP_PITCH, DP_RING, DP_DRIVE, DP_PUNCH, DP_SEND, DP_N };
enum { SD_FACE, SD_LEFT, SD_RIGHT, SD_TOP, SD_BOTTOM, NSIDE };
enum { V_NONE, V_KICK, V_SNARE, V_HAT, V_OHAT, V_TOM, V_CLAP, V_RIM, V_COWBELL, V_SHAKER, V_CONGA, V_ZAP, NVTYPE };
enum { B_OFF, B_WOOD, B_MARIMBA, B_BELL, B_GLASS, B_TABLA, B_STEEL, B_MEMBRANE, B_METAL, NBODY };
enum { SRC_LIVE, SRC_LOOP, SRC_MIDI, SRC_ROLL };

typedef struct {
    const char *name;
    uint8_t v[NSIDE];          // the drum on each side
    uint8_t body, by_side;     // the body, and whether its pitch follows the side (chord tones)
    int8_t  octave;            // the body's octave (scale_note's o)
    float   body_lvl, decay, ring, touch_ms, click;
} kit_t;
static const kit_t kits[] = {
    { "STREET",    { V_KICK, V_SNARE, V_HAT, V_TOM, V_CLAP }, B_WOOD, 0, 4, 0.35f, 1.0f, 1.0f, 10, 0.5f },
    { "808",       { V_KICK, V_CLAP, V_HAT, V_COWBELL, V_OHAT }, B_OFF, 0, 4, 0, 1.8f, 1.0f, 10, 0.4f },
    { "BOOM BAP",  { V_KICK, V_SNARE, V_HAT, V_RIM, V_SHAKER }, B_OFF, 0, 4, 0, 0.8f, 1.0f, 10, 0.7f },
    { "TECHNO",    { V_KICK, V_CLAP, V_OHAT, V_HAT, V_RIM }, B_OFF, 0, 4, 0, 1.2f, 1.0f, 10, 0.8f },
    { "HAND DRUM", { V_CONGA, V_CONGA, V_RIM, V_CONGA, V_SHAKER }, B_MEMBRANE, 0, 3, 0.8f, 1.0f, 1.2f, 20, 0.3f },
    { "MARIMBA",   { V_NONE, V_NONE, V_NONE, V_NONE, V_NONE }, B_MARIMBA, 1, 4, 0.95f, 1.0f, 1.2f, 10, 0.5f },
    { "BELLS",     { V_NONE, V_NONE, V_NONE, V_NONE, V_NONE }, B_BELL, 1, 4, 0.75f, 1.0f, 1.5f, 10, 0.5f },
    { "GLASS",     { V_NONE, V_NONE, V_NONE, V_NONE, V_NONE }, B_GLASS, 1, 5, 0.85f, 1.0f, 1.4f, 10, 0.5f },
    { "TABLA",     { V_NONE, V_NONE, V_SHAKER, V_NONE, V_NONE }, B_TABLA, 1, 4, 0.9f, 1.0f, 1.0f, 14, 0.5f },
    { "STEEL PAN", { V_NONE, V_NONE, V_NONE, V_NONE, V_NONE }, B_STEEL, 1, 4, 0.9f, 1.0f, 1.2f, 10, 0.5f },
    { "SCRAP",     { V_KICK, V_NONE, V_NONE, V_NONE, V_RIM }, B_METAL, 0, 3, 0.85f, 1.0f, 2.0f, 30, 0.5f },
    { "ZAPS",      { V_ZAP, V_ZAP, V_ZAP, V_ZAP, V_ZAP }, B_OFF, 0, 4, 0, 1.0f, 1.0f, 10, 0.5f },
    { "COWBELLS",  { V_KICK, V_COWBELL, V_COWBELL, V_COWBELL, V_CLAP }, B_OFF, 0, 4, 0, 1.0f, 1.0f, 10, 0.5f },
    { "SCRATCH",   { V_NONE, V_NONE, V_NONE, V_NONE, V_NONE }, B_GLASS, 0, 3, 0.95f, 1.0f, 2.5f, 30, 0.5f },
};
#define NKITS ((int)(sizeof kits / sizeof kits[0]))
static const char *const kit_l[NKITS] = { "STREET", "808", "BOOM BAP", "TECHNO", "HAND DRUM", "MARIMBA", "BELLS", "GLASS",
                                          "TABLA", "STEEL PAN", "SCRAP", "ZAPS", "COWBELLS", "SCRATCH" };
static const char *const sides_l[] = { "SIDE", "TONE", "ONE DRUM" };
static const char *const grid_l[] = { "OFF", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
static const char *const pitch_l[] = { "KIT", "ONE", "BY SIDE" };
static const char *const pages[2] = { "DRUMS", "DRUM KIT" };
static const param_t params[DP_N] = {
    [DP_KIT]      = { "KIT", "dkit", 0, PK_ENUM, 0, 0, NKITS - 1, 1, 0, "", kit_l, "FIVE DRUMS, WHAT THEY RING" },
    [DP_TUNE]     = { "TUNE", "dtun", 0, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "THE DRUMS UP OR DOWN" },
    [DP_DECAY]    = { "DECAY", "ddcy", 0, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "SHORTER OR LONGER DRUMS" },
    [DP_SENSE]    = { "SENSE", "dsns", 0, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "HOW SOFT A KNOCK COUNTS" },
    [DP_SIDES]    = { "SIDES", "dsid", 0, PK_ENUM, 0, 0, 2, 1, 0, "", sides_l, "HOW A KNOCK PICKS ITS DRUM" },
    [DP_GRID]     = { "GRID", "dgrd", 0, PK_ENUM, 0, 0, 6, 1, 4, "", grid_l, "WHAT HITS ARE PULLED TO" },
    [DP_STRENGTH] = { "STRENGTH", "dstr", 0, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "HOW HARD THEY ARE PULLED" },
    [DP_SWING]    = { "SWING", "dswg", 0, PK_NUM, PF_INT, 0, 75, 5, 0, "%", NULL, "LATE OFFBEATS" },
    [DP_MUTATE]   = { "MUTATE", "dmut", 0, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HITS DROP OUT OR CHANGE" },
    [DP_UNDO]     = { "UNDO LAST", "dund", 0, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "TAKE AWAY THE LAST LAYER" },
    [DP_BODY]     = { "BODY", "dbdy", 1, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "HOW LOUD THE RINGING IS" },
    [DP_PITCH]    = { "BODY PITCH", "dpit", 1, PK_ENUM, 0, 0, 2, 1, 0, "", pitch_l, "ONE NOTE, OR CHORD BY SIDE" },
    [DP_RING]     = { "RING", "drng", 1, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "HOW LONG IT RINGS" },
    [DP_DRIVE]    = { "DRIVE", "ddrv", 1, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "DIRT" },
    [DP_PUNCH]    = { "PUNCH", "dpch", 1, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "SQUEEZE: FATTER, LOUDER" },
    [DP_SEND]     = { "FX SEND", "dsnd", 1, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "INTO THE ECHO AND REVERB" },
};
#define P(i) LP(L_DRUMS, i)

// ---------------------------------------------------------------- tables
static const char *const vt_label[NVTYPE] = { "BODY", "KICK", "SNARE", "HAT", "OPEN", "TOM", "CLAP", "RIM", "BELL", "SHAKE", "CONGA", "ZAP" };
static const int vt_gm[NVTYPE] = { 0, 36, 38, 42, 46, 45, 39, 37, 56, 70, 63, 81 };
static const int side_gm[NSIDE] = { 36, 38, 42, 45, 39 };
static const int side_degree[NSIDE] = { 0, 2, 4, 6, 7 };          // chord tones by side: root, 3rd, 5th, 7th, octave
#define NMODE 6
typedef struct { int n; float r[NMODE], d[NMODE], g[NMODE]; float trim; } body_t;
static const body_t bodies[NBODY] = {
    [B_OFF]      = { 0, { 0 }, { 0 }, { 0 }, 0 },
    [B_WOOD]     = { 4, { 1, 2.756f, 5.404f, 8.933f }, { 0.16f, 0.07f, 0.04f, 0.022f }, { 1, 0.5f, 0.28f, 0.15f }, 0.9f },
    [B_MARIMBA]  = { 3, { 1, 3.99f, 9.9f }, { 0.75f, 0.2f, 0.07f }, { 1, 0.32f, 0.12f }, 0.5f },
    [B_BELL]     = { 6, { 0.5f, 1, 1.183f, 1.506f, 2.0f, 2.514f }, { 3.2f, 2.4f, 1.7f, 1.3f, 1.0f, 0.6f }, { 0.55f, 1, 0.6f, 0.45f, 0.4f, 0.22f }, 0.22f },
    [B_GLASS]    = { 4, { 1, 2.32f, 4.25f, 6.63f }, { 1.7f, 1.0f, 0.6f, 0.35f }, { 1, 0.6f, 0.4f, 0.25f }, 0.3f },
    [B_TABLA]    = { 5, { 1, 2, 3, 4, 5 }, { 0.5f, 0.35f, 0.25f, 0.18f, 0.12f }, { 1, 0.7f, 0.5f, 0.3f, 0.2f }, 0.45f },
    [B_STEEL]    = { 4, { 1, 2, 3.01f, 4.02f }, { 1.4f, 0.9f, 0.5f, 0.3f }, { 1, 0.6f, 0.35f, 0.2f }, 0.35f },
    [B_MEMBRANE] = { 6, { 1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f }, { 0.38f, 0.26f, 0.2f, 0.18f, 0.15f, 0.12f }, { 1, 0.6f, 0.45f, 0.4f, 0.3f, 0.25f }, 0.45f },
    [B_METAL]    = { 6, { 1, 1.41f, 2.24f, 2.92f, 3.83f, 4.69f }, { 1.8f, 1.4f, 1.0f, 0.8f, 0.6f, 0.4f }, { 1, 0.8f, 0.6f, 0.5f, 0.4f, 0.3f }, 0.2f },
};

static const kit_t *kit(void) { int k = (int)P(DP_KIT); return &kits[k < 0 ? 0 : k >= NKITS ? NKITS - 1 : k]; }
static bool by_side(void) { int p = (int)P(DP_PITCH); return p == 0 ? kit()->by_side != 0 : p == 2; }
// the note a side plays: tonal drums and BY SIDE bodies take the chord's tones (in the song's scale)
static int side_note(int side, int octave) { return scale_note(song.degree + side_degree[side], octave); }

// ---------------------------------------------------------------- shared state (UI <-> audio)
#define SNIP_LEN 512           // 16 ms of mic per knock (int16, PSRAM), 16 samples before the onset
#define SNIP_PRE 16
#define NSNIP    160           // 128 can be pinned by the loop, the rest rotate
#define MAXHITS  128
typedef struct { uint32_t frame; float bright, level; int16_t snip; } onset_t;
typedef struct { uint8_t side, vel, src; int16_t snip; } trig_t;
typedef struct { uint32_t frame; uint8_t side, vel, src; } played_t;
typedef struct { uint32_t off; uint8_t side, vel, layer; volatile uint8_t dead; int16_t snip; uint16_t born; } hit_t;
SPSC(d_onsetq, onset_t, 16)
SPSC(d_trigq, trig_t, 32)
SPSC(d_playq, played_t, 64)
static d_onsetq_t onsets;
static d_trigq_t  trigs;
static d_playq_t  played;
static hit_t hits[MAXHITS];
static volatile int nhits;
static volatile int loop_on = 1;            // the drums' loop plays (a tap on REC mutes it)
static volatile float tune_now, mutate_now, filter_now, roll_hz, stutter_s, knock_avg = 0.1f;
static volatile int   roll_side, roll_vel = 90;
static volatile uint8_t pinned[NSNIP];
static volatile float env_now, thr_now, body_lvl[NSIDE];
static volatile bool  live_now;

// ---------------------------------------------------------------- audio: voices
static uint32_t seed = 0x9e3779b9u, frame0;
#define NV 10
typedef struct {
    uint8_t type, side; bool on; int delay, t;
    float amp, ph, ph2, f0, f1, ae, kae, pe, kpe, xe, carry, last;
    svf_t fa;
} voice_t;
static voice_t vo[NV];

static void voice_start(int type, int side, float amp, int delay)
{
    int k = -1; float q = 1e9f;
    for (int i = 0; i < NV; i++) {
        if (!vo[i].on) { k = i; break; }
        float l = vo[i].ae * vo[i].amp;
        if (l < q) { q = l; k = i; }
    }
    voice_t *v = &vo[k];
    float carry = v->on ? v->last + v->carry : 0.0f;             // a stolen voice fades out, never cut
    if (type == V_HAT) for (int i = 0; i < NV; i++) if (vo[i].on && vo[i].type == V_OHAT) vo[i].kae = 0.992f;
    memset(v, 0, sizeof *v);
    v->carry = carry;
    v->type = (uint8_t)type; v->side = (uint8_t)side; v->on = true; v->delay = delay; v->amp = amp; v->ae = v->pe = v->xe = 1.0f;
    bool tonal = type == V_TOM || type == V_CONGA || type == V_ZAP || type == V_COWBELL;
    // tonal drums: the side's chord tone, relative to C4 (their base tunings are for C)
    float tn = exp2f((tune_now + (tonal ? (float)(side_note(side, 4) - 60) : 0.0f)) / 12.0f);
    float dc = P(DP_DECAY) * kit()->decay, pk = kit()->click;
    switch (type) {
    case V_KICK:    v->f0 = 46.0f * exp2f(tune_now / 12.0f); v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.30f * dc); v->f1 = 2.0f + 3.0f * pk; break;
    case V_SNARE:   v->f0 = 185.0f * tn; svf_set(&v->fa, 1600.0f * tn, 0.7f); v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.15f * dc); break;
    case V_HAT:     svf_set(&v->fa, 7200.0f * tn, 0.8f); v->kae = 1.0f - onepole_s(0.04f * dc); break;
    case V_OHAT:    svf_set(&v->fa, 6500.0f * tn, 0.9f); v->kae = 1.0f - onepole_s(0.32f * dc); break;
    case V_TOM:     v->f0 = 130.8f * tn; v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.38f * dc); break;
    case V_CLAP:    svf_set(&v->fa, 1100.0f * tn, 1.5f); v->kpe = 1.0f - onepole_s(0.004f); v->kae = 1.0f - onepole_s(0.14f * dc); break;
    case V_RIM:     v->f0 = 520.0f * tn; svf_set(&v->fa, 1800.0f * tn, 2.0f); v->kae = 1.0f - onepole_s(0.028f * dc); v->kpe = 1.0f - onepole_s(0.006f); break;
    case V_COWBELL: v->f0 = 523.3f * tn; v->f1 = 784.0f * tn; svf_set(&v->fa, 2400.0f * tn, 1.2f); v->kpe = 1.0f - onepole_s(0.02f); v->kae = 1.0f - onepole_s(0.22f * dc); break;
    case V_SHAKER:  svf_set(&v->fa, 5500.0f * tn, 1.2f); v->kae = 1.0f - onepole_s(0.07f * dc); v->xe = 0.0f; break;
    case V_CONGA:   v->f0 = 261.6f * tn; v->kpe = 1.0f - onepole_s(0.008f); v->kae = 1.0f - onepole_s(0.2f * dc); break;
    case V_ZAP:     v->f0 = 85.0f * tn; v->f1 = 2600.0f * tn; v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.17f * dc); break;
    }
}

static inline float voice_run(voice_t *v)
{
    if (v->delay > 0) { v->delay--; float c = v->carry; v->carry *= 0.93f; return c; }
    float y = 0, pk = kit()->click;
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
    case V_HAT: case V_OHAT: y = 0.75f * svf_hp(&v->fa, rng_bi(&seed)) * v->ae; break;
    case V_TOM: case V_CONGA:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + (v->type == V_TOM ? 0.45f : 0.25f) * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        if (v->type == V_CONGA && v->t < 40) y += 0.25f * rng_bi(&seed);
        break;
    case V_CLAP: {
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
        y = 0.8f * svf_bp(&v->fa, 0.5f * (square_blep(v->ph, d0) + square_blep(v->ph2, d1)), NULL) * (0.4f * v->pe + 0.6f) * v->ae;
        break;
    }
    case V_SHAKER: v->xe += (1.0f - v->xe) * 0.03f; y = 1.1f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->ae * v->xe; break;
    case V_ZAP: v->ph = wrap1(v->ph + (v->f0 + v->f1 * v->pe) * isr); y = sin_turns(v->ph) * v->ae; break;
    }
    v->ae *= v->kae; v->pe *= v->kpe; v->t++;
    y *= v->amp; v->last = y;
    float c = v->carry; v->carry = flush_denormal(v->carry * 0.93f);
    if (v->ae < 1e-4f && v->t > 64) v->on = false;
    return y + c;
}

// ---------------------------------------------------------------- audio: bodies, one bank of modes per side
typedef struct { float c1, c2, g, y1, y2; } mode_t;
typedef struct { mode_t m[NMODE]; int nm, snip, pos, len, delay; float gain, lp, note, note_done; } bank_t;
static bank_t bank[NSIDE];
static int   body_cur = -1, body_fade;
static float last_ring = -1, live_gate, exc_lp_live;

// each bank glides to its note (the chord's root, or with BY SIDE the side's chord tone; TUNE and tilt)
static void bank_tune(bool force)
{
    const body_t *b = &bodies[body_cur < 0 ? 0 : body_cur];
    bool bs = by_side();
    int oct = kit()->octave;
    for (int s = 0; s < NSIDE; s++) {
        bank_t *k = &bank[s];
        float want = (float)(bs ? side_note(s, oct) : scale_note(song.degree, oct)) + tune_now;
        k->note = k->note <= 0.0f ? want : k->note + 0.25f * (want - k->note);
        if (!force && fabsf(k->note - k->note_done) < 0.004f) continue;
        k->note_done = k->note; k->nm = b->n;
        float f0 = midi_hz(k->note);
        for (int i = 0; i < b->n; i++) {
            float f = clampf(f0 * b->r[i], 30.0f, 0.42f * (float)AUDIO_SR), w = TWO_PI * f / (float)AUDIO_SR;
            float t60 = b->d[i] * P(DP_RING) * kit()->ring * (f > 4000.0f ? 4000.0f / f : 1.0f);
            float r = expf(-6.9078f / (t60 * (float)AUDIO_SR));
            k->m[i].c1 = 2.0f * r * cosf(w); k->m[i].c2 = -r * r;
            k->m[i].g = b->g[i] * sinf(w) * 0.018f * b->trim;
        }
    }
}

static void body_excite(int side, int snip, float gain, int delay)
{
    bank_t *k = &bank[by_side() ? side : 0];
    k->snip = snip; k->pos = 0; k->delay = delay; k->gain = gain;
    k->len = (int)(kit()->touch_ms * 0.001f * (float)AUDIO_SR);
    if (k->len > SNIP_LEN) k->len = SNIP_LEN;
    if (k->len < 96) k->len = 96;
}

// ---------------------------------------------------------------- audio: detection, snippets, the bus
static int16_t *snips;
static volatile int snip_fill[NSNIP];
static int snip_next, cap_slot = -1, cap_pos;
#define HIST 1024
static float mic_hist[HIST];
static uint32_t mic_w;
static float env_f, env_s, floor_e = 0.01f, a_f, r_f, a_s, r_s, prev_x, bleed_k = 0.5f;
static int   refract, bright_n;
static float bright_hf, bright_all, onset_lvl;
static uint32_t onset_frame;
static int16_t onset_snip;
static svf_t f_lp, f_hp;
static float flt_s, comp_env, comp_g = 1.0f, comp_s;
static smooth_t drive_s, body_s, send_s;
#define STUT_N 16384           // 0.5 s of the drums for the stick's repeat (int16, PSRAM)
static int16_t *stut_buf;
static uint32_t stut_w;
static bool  stut_on; static uint32_t stut_start; static int stut_len, stut_pos; static float stut_x, roll_ph; static bool roll_was;

static inline int16_t to16(float x) { x *= 16384.0f; return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x); }

static void trigger(int side, int vel, int src, int snip, int delay)
{
    if (side < 0 || side >= NSIDE) return;
    float a = (float)vel / 127.0f;
    int vt = kit()->v[side];
    if (vt != V_NONE) voice_start(vt, side, a * a * 0.42f + 0.02f * a, delay);
    bool one = !by_side();
    if (body_cur > B_OFF && !(src == SRC_LIVE && one)) body_excite(side, snip, a, delay);
    played_t p = { frame0 + (uint32_t)delay, (uint8_t)side, (uint8_t)vel, (uint8_t)src };
    d_playq_push(&played, &p);
}

static uint16_t pass_of(uint32_t frame) { uint32_t l = song.loop_len; return l ? (uint16_t)((frame - song.loop_start) / l) : 0; }

// where a hit plays in the loop after GRID, STRENGTH and SWING (the song's beats)
static uint32_t qoff(const hit_t *h)
{
    static const int div[] = { 0, 1, 2, 3, 4, 6, 8 };
    uint32_t len = song.loop_len;
    int d = div[(int)P(DP_GRID)];
    if (!len || !d || song.beats < 1) return h->off;
    float g = (float)len / (float)(song.beats * d), off = (float)h->off;
    float k = (float)(int)(off / g + 0.5f), target = k * g;
    if (((int)k & 1) && d >= 2) target += P(DP_SWING) * 0.01f * g * 0.5f;
    float q = off + (target - off) * P(DP_STRENGTH) * 0.01f;
    while (q >= (float)len) q -= (float)len;
    if (q < 0) q += (float)len;
    return (uint32_t)q;
}

static void loop_block(int n)
{
    uint32_t len = song.loop_len;
    if (!len || !loop_on) return;
    uint32_t rel = frame0 - song.loop_start, ph = rel % len;
    int nh = nhits;
    float mu = mutate_now;
    for (int i = 0; i < nh; i++) {
        hit_t *h = &hits[i];
        if (h->dead) continue;
        uint32_t d = (qoff(h) + len - ph) % len;
        if (d >= (uint32_t)n) continue;
        if ((uint16_t)((rel + d) / len) == h->born) continue;       // knocked in this pass: it already sounded
        int side = h->side, vel = h->vel;
        if (mu > 0.001f) {
            float r = rng_uni(&seed);
            if (r < mu * 0.35f) continue;
            if (r < mu * 0.7f) side = (int)(rng_next(&seed) % NSIDE);
            vel = clampi((int)((float)vel * (1.0f + mu * 0.5f * rng_bi(&seed))), 10, 127);
        }
        trigger(side, vel, SRC_LOOP, h->snip, (int)d);
    }
}

static void roll_block(int n)
{
    float hz = roll_hz;
    if (hz <= 0.0f) { roll_was = false; return; }
    if (!roll_was) { roll_ph = 1.0f; roll_was = true; }
    float step = hz / (float)AUDIO_SR;
    for (int i = 0; i < n; i++) {
        if (roll_ph >= 1.0f) { roll_ph -= 1.0f; trigger(roll_side, roll_vel, SRC_ROLL, -1, i); }
        roll_ph += step;
    }
}

static void d_init(void)
{
    snips = app_alloc((size_t)NSNIP * SNIP_LEN * sizeof(int16_t));
    stut_buf = app_alloc(STUT_N * sizeof(int16_t));
    a_f = onepole_s(0.0003f); r_f = onepole_s(0.010f); a_s = onepole_s(0.030f); r_s = onepole_s(0.200f);
    refract = AUDIO_SR / 4;
    smooth_init(&drive_s, 0, 0.02f); smooth_init(&body_s, 1.0f, 0.02f); smooth_init(&send_s, 0.15f, 0.03f);
    for (int s = 0; s < NSIDE; s++) bank[s].snip = -2;
    if (!snips || !stut_buf) app_log("drums: short of memory");
}

static void d_audio(const float *mic, float *dry, float *send, int n, bool live)
{
    frame0 = audio_frames();
    live_now = live;
    int want_body = kit()->body;
    if (want_body != body_cur && body_fade == 0 && body_cur > B_OFF) body_fade = 128;
    if (body_cur >= 0) { bool f = P(DP_RING) != last_ring; last_ring = P(DP_RING); bank_tune(f); }
    float base = 0.003f * powf(40.0f, 1.0f - P(DP_SENSE) * 0.01f);
    float touch_rel = 1.0f / (kit()->touch_ms * 0.001f * (float)AUDIO_SR);
    float exc_k = onepole_hz(2500.0f);
    float bgain = 1.0f / fmaxf(knock_avg, 0.01f);
    float out_env = host_out_env;
    flt_s += 0.2f * (clampf(filter_now, -1.0f, 1.0f) - flt_s);
    svf_set(&f_lp, 18000.0f * exp2f(fminf(flt_s, 0.0f) * 7.0f), 0.9f);
    svf_set(&f_hp, 18.0f * exp2f(fmaxf(flt_s, 0.0f) * 9.0f), 0.9f);
    comp_s += 0.12f * (P(DP_PUNCH) * 0.01f - comp_s);
    float ca = comp_s, makeup = 1.0f + 0.9f * ca;
    float thr_c = 0.25f, ratio = 1.0f + 5.0f * ca;
    float want_g = comp_env > thr_c ? powf(thr_c / comp_env, 1.0f - 1.0f / ratio) : 1.0f;
    float gscale = comp_g, gstep = (want_g - comp_g) / (float)n; comp_g = want_g;

    trig_t t;
    while (d_trigq_pop(&trigs, &t)) trigger(t.side, t.vel, t.src, t.snip, 0);
    loop_block(n);
    if (live) roll_block(n); else roll_was = false;

    float bl[NSIDE] = { 0 };
    float blevel = kit()->body_lvl;
    for (int i = 0; i < n; i++) {
        float x = mic[i], e0 = 0;
        // ---- listening (only while live: the mic is the live layer's)
        if (live) {
            mic_hist[mic_w++ & (HIST - 1)] = x;
            follow(&env_f, x, a_f, r_f);
            float slow_before = env_s;
            follow(&env_s, x, a_s, r_s);
            if (env_s < floor_e) floor_e += 0.01f * (env_s - floor_e); else floor_e += 0.000005f * (env_s - floor_e);
            float bleed = bleed_k * out_env;                     // what the mic should be hearing of the speaker
            float thr = base + 4.0f * floor_e + 0.3f * bleed;
            if (cap_slot >= 0) {
                snips[(size_t)cap_slot * SNIP_LEN + cap_pos] = to16(x);
                snip_fill[cap_slot] = ++cap_pos;
                if (cap_pos >= SNIP_LEN) cap_slot = -1;
            }
            if (bright_n > 0) {
                float d = x - prev_x;
                bright_hf += d * d; bright_all += x * x;
                if (env_f > onset_lvl) onset_lvl = env_f;
                if (--bright_n == 0) {
                    onset_t o = { onset_frame, clampf(0.5f * bright_hf / (bright_all + 1e-9f), 0.0f, 1.0f), onset_lvl, onset_snip };
                    d_onsetq_push(&onsets, &o);
                }
            } else if (refract > 0) refract--;
            else if (env_f - bleed > thr && env_f - bleed > 2.5f * (slow_before - bleed) + 0.005f) {
                onset_frame = frame0 + (uint32_t)i; bright_n = 96; bright_hf = bright_all = 0; onset_lvl = env_f;
                refract = (int)(0.06f * (float)AUDIO_SR);
                onset_snip = -1;
                if (snips) {
                    int s = snip_next;
                    for (int k = 0; k < NSNIP && pinned[s]; k++) s = (s + 1) % NSNIP;
                    if (!pinned[s]) {
                        snip_next = (s + 1) % NSNIP;
                        int16_t *p = snips + (size_t)s * SNIP_LEN;
                        for (int k = 0; k < SNIP_PRE; k++) p[k] = to16(mic_hist[(mic_w - SNIP_PRE + k) & (HIST - 1)]);
                        cap_slot = s; cap_pos = SNIP_PRE; snip_fill[s] = SNIP_PRE; onset_snip = (int16_t)s;
                    }
                }
            }
            prev_x = x;
            float gt = clampf((env_f - bleed - 0.6f * thr) / (0.8f * thr + 1e-6f), 0.0f, 1.0f);
            if (gt > live_gate) live_gate = gt; else live_gate -= touch_rel;
            if (live_gate < 0) live_gate = 0;
            e0 = live_gate;
        } else live_gate = 0;
        // ---- voices
        float yv = 0;
        for (int v = 0; v < NV; v++) if (vo[v].on) yv += voice_run(&vo[v]);
        // ---- bodies
        float yb = 0;
        if (body_cur > B_OFF) {
            bool one = !by_side();
            for (int s = 0; s < NSIDE; s++) {
                bank_t *k = &bank[s];
                if (!k->nm) continue;
                float e = 0;
                if (s == 0 && one && e0 > 0) { exc_lp_live += exc_k * (x * bgain - exc_lp_live); e += exc_lp_live * e0; }
                if (k->snip >= -1 && k->pos < k->len) {
                    if (k->delay > 0) k->delay--;
                    else {
                        float sx;
                        if (k->snip >= 0 && snips) {
                            int f = snip_fill[k->snip];
                            sx = k->pos < f ? (float)snips[(size_t)k->snip * SNIP_LEN + k->pos] * (1.0f / 16384.0f) * bgain : 0.0f;
                            if (k->pos < f) k->pos++; else if (f >= SNIP_LEN) k->pos = k->len;
                        } else { sx = (k->pos == 0 ? 3.0f : 0.0f) + rng_bi(&seed) * (k->pos < 96 ? 1.0f - (float)k->pos / 96.0f : 0.0f); k->pos++; }
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
                if (body_fade > 0) for (int m = 0; m < k->nm; m++) { k->m[m].y1 *= 0.96f; k->m[m].y2 *= 0.96f; }
                yb += ys;
                float a = fabsf(ys); if (a > bl[s]) bl[s] = a;
            }
            if (body_fade > 0 && --body_fade == 0) body_cur = -2;
        }
        if (body_cur != want_body && body_fade == 0) {               // a new kit's body: from silence
            body_cur = want_body;
            for (int s = 0; s < NSIDE; s++) { memset(bank[s].m, 0, sizeof bank[s].m); bank[s].nm = 0; }
            bank_tune(true);
        }
        yb = 0.8f * soft_tanh(yb * 1.25f);
        float y = yv + smooth_to(&body_s, P(DP_BODY) * 0.01f * blevel * 1.6f) * yb;
        // ---- the drums' own bus: the stick's filter, drive, punch
        y = svf_hp(&f_hp, svf_lp(&f_lp, y));
        float dr = smooth_to(&drive_s, P(DP_DRIVE) * 0.01f);
        if (dr > 0.002f) { float kk = 1.0f + 7.0f * dr; y = (1.0f - dr) * y + dr * soft_tanh(y * kk) * (1.0f + 2.0f * dr) / kk; }
        float a = fabsf(y);
        comp_env += (a > comp_env ? 0.006f : 0.0004f) * (a - comp_env);
        y *= gscale * makeup; gscale += gstep;
        // ---- the stick's repeat: the last slice of the drums, again and again while held down
        if (stut_buf) {
            if (!stut_on) { stut_buf[stut_w & (STUT_N - 1)] = to16(y * 0.5f); stut_w++; }   // held still while it repeats
            float want = stutter_s;
            if (want > 0.0f && !stut_on) { stut_on = true; stut_len = (int)(want * (float)AUDIO_SR); if (stut_len > STUT_N - 64) stut_len = STUT_N - 64; stut_start = stut_w - (uint32_t)stut_len; stut_pos = 0; }
            if (stut_on) {
                float r = (float)stut_buf[(stut_start + (uint32_t)stut_pos) & (STUT_N - 1)] * (2.0f / 16384.0f);
                float w = stut_pos < 48 ? (float)stut_pos / 48.0f : stut_len - stut_pos < 48 ? (float)(stut_len - stut_pos) / 48.0f : 1.0f;
                if (++stut_pos >= stut_len) { stut_pos = 0; if (want > 0.0f) { stut_len = (int)(want * (float)AUDIO_SR); if (stut_len > STUT_N - 64) stut_len = STUT_N - 64; } }
                stut_x += ((want > 0.0f ? 1.0f : 0.0f) - stut_x) * 0.01f;
                y = y * (1.0f - stut_x) + r * w * stut_x;
                if (want <= 0.0f && stut_x < 0.001f) { stut_on = false; stut_x = 0; }
            }
        }
        dry[i] += y;
        send[i] += y * smooth_to(&send_s, P(DP_SEND) * 0.01f);
    }
    // how loud the mix is in the mic, learned while live (down fast, up slowly: knocks raise it too)
    if (live && out_env > 0.03f) {
        float r = env_s / out_env;
        bleed_k += (r < bleed_k ? 0.05f : 0.002f) * (r - bleed_k);
        bleed_k = clampf(bleed_k, 0.01f, 2.0f);
    }
    env_now = env_f; thr_now = base + 4.0f * floor_e + 1.3f * bleed_k * out_env;
    for (int s = 0; s < NSIDE; s++) body_lvl[s] = bl[s];
}

// ---------------------------------------------------------------- UI: which side was knocked
#define WAIT_MS 10             // the push is over within a few ms; the audio path's buffering adds a few more
#define AHIST 256
static struct { uint32_t frame; vec3_t a; } acc_hist[AHIST];
static int acc_w;
static onset_t pending[4];
static int npending, last_side, last_vel = 100;
static uint32_t now_frames;

static int classify(const onset_t *o, bool use_side)
{
    int mode = (int)P(DP_SIDES);
    if (mode == 2) return SD_FACE;
    if (use_side && mode == 0) {
        vec3_t g = { 0, 0, 0 }; int ng = 0;
        const uint32_t ms = AUDIO_SR / 1000;
        for (int k = 0; k < AHIST; k++) {
            int32_t dt = (int32_t)(acc_hist[k].frame - o->frame);
            if (dt >= -(int32_t)(80 * ms) && dt < -(int32_t)(20 * ms)) { g.x += acc_hist[k].a.x; g.y += acc_hist[k].a.y; g.z += acc_hist[k].a.z; ng++; }
        }
        if (ng) { g.x /= (float)ng; g.y /= (float)ng; g.z /= (float)ng; }
        int idx[64], ni = 0;
        for (int k = 0; k < AHIST && ni < 64; k++) {
            int j = (acc_w + k) % AHIST;
            int32_t dt = (int32_t)(acc_hist[j].frame - o->frame);
            if (dt >= -(int32_t)(20 * ms) && dt <= (int32_t)(WAIT_MS * ms)) idx[ni++] = j;
        }
        float best = 0; int bi = -1;
        for (int k = 0; k < ni; k++) {
            vec3_t d = { acc_hist[idx[k]].a.x - g.x, acc_hist[idx[k]].a.y - g.y, acc_hist[idx[k]].a.z - g.z };
            float m = d.x * d.x + d.y * d.y + d.z * d.z;
            if (m > best) { best = m; bi = k; }
        }
        if (ng && bi >= 0 && best > 0.35f * 0.35f)
            for (int k = 0; k <= bi; k++) {                          // the blow itself, before the box rings back
                vec3_t d = { acc_hist[idx[k]].a.x - g.x, acc_hist[idx[k]].a.y - g.y, acc_hist[idx[k]].a.z - g.z };
                if (d.x * d.x + d.y * d.y + d.z * d.z < 0.25f * best) continue;
                float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z);
                if (az >= ax && az >= ay) return SD_FACE;
                if (ax >= ay) return d.x > 0 ? SD_LEFT : SD_RIGHT;   // pushed right: hit on the left
                return d.y > 0 ? SD_TOP : SD_BOTTOM;                 // pushed down the screen: hit on the top edge
            }
    }
    return o->bright < 0.25f ? SD_FACE : o->bright < 0.55f ? SD_LEFT : SD_RIGHT;
}

// ---------------------------------------------------------------- UI: the loop
enum { LS_EMPTY, LS_REC, LS_PLAY, LS_DUB };
static int lstate, layer_n, nprov;
static bool rec_pending, first_take;
static uint32_t rec_start;
typedef struct { uint32_t frame; uint8_t side, vel; int16_t snip; } prov_t;
static prov_t prov[16];

static void add_hit(uint32_t frame, int side, int vel, int16_t snip)
{
    int n = nhits;
    if (n >= MAXHITS) { toast("DRUM LOOP FULL", 900); return; }
    hit_t h = { 0 };
    uint32_t len = song.loop_len;
    if (lstate == LS_REC && !len) {                                 // the jam's first loop, being made
        if ((int32_t)(frame - rec_start) < 0) return;
        h.off = frame - rec_start; h.born = 0;
    } else if ((lstate == LS_REC || lstate == LS_DUB) && len) {
        h.off = (frame - song.loop_start) % len; h.born = pass_of(frame);
    } else return;
    h.side = (uint8_t)side; h.vel = (uint8_t)vel; h.layer = (uint8_t)layer_n; h.snip = snip;
    if (snip >= 0) pinned[snip] = 1;
    hits[n] = h;
    __atomic_store_n(&nhits, n + 1, __ATOMIC_RELEASE);
}
static void record_hit(uint32_t frame, int side, int vel, int16_t snip)
{
    if (rec_pending) { if (nprov < 16) prov[nprov++] = (prov_t){ frame, (uint8_t)side, (uint8_t)vel, snip }; return; }
    if (lstate == LS_REC || lstate == LS_DUB) add_hit(frame, side, vel, snip);
}

static void d_clear(void)
{
    int n = nhits;
    for (int i = 0; i < n; i++) { hits[i].dead = 1; if (hits[i].snip >= 0) pinned[hits[i].snip] = 0; }
    __atomic_store_n(&nhits, 0, __ATOMIC_RELEASE);
    lstate = LS_EMPTY; layer_n = 0; rec_pending = false; nprov = 0; loop_on = 1; first_take = false;
}

static void undo(void)
{
    if (layer_n <= 1) { d_clear(); toast("DRUM LOOP CLEARED", 900); return; }
    int n = nhits;
    for (int i = 0; i < n; i++) if (hits[i].layer == layer_n && !hits[i].dead) { hits[i].dead = 1; if (hits[i].snip >= 0) pinned[hits[i].snip] = 0; }
    layer_n--;
    toast("LAST DRUM LAYER GONE", 900);
}

static void rec_logic(const ctl_t *c)
{
    if (c->rec_press) { rec_pending = true; nprov = 0; rec_start = c->frames; }
    if (rec_pending && c->rec && c->rec_ms >= 300) {               // a hold, not a tap
        rec_pending = false;
        first_take = !song.loop_len;
        lstate = first_take ? LS_REC : LS_DUB;
        layer_n++;
        loop_on = 1;
        for (int i = 0; i < nprov; i++) add_hit(prov[i].frame, prov[i].side, prov[i].vel, prov[i].snip);
        nprov = 0;
    }
    // the first loop: auto-close at the longest a loop can be
    if (lstate == LS_REC && first_take && c->rec && c->frames - rec_start >= (uint32_t)(JAM_LOOP_MAX_S * AUDIO_SR)) goto close;
    if (!c->rec_release) return;
    if (rec_pending) {                                               // a tap: mute or unmute the drums' loop
        rec_pending = false; nprov = 0;
        if (nhits) { loop_on = !loop_on; toast(loop_on ? "DRUMS LOOP ON" : "DRUMS LOOP OFF", 800); coach_done(LEARN_INST + 1); }
        return;
    }
    if (lstate == LS_DUB) { lstate = LS_PLAY; return; }
    if (lstate != LS_REC) return;
close:
    if (first_take && !song.loop_len) {
        uint32_t held = c->frames - rec_start;
        int n = nhits;
        // pressed REC and knocked at once: the knock is the downbeat (the release lagged as much)
        uint32_t first = held;
        for (int i = 0; i < n; i++) if (hits[i].off < first) first = hits[i].off;
        uint32_t shift = first < (uint32_t)(0.12f * AUDIO_SR) ? first : 0;
        if (held < AUDIO_SR / 4 || n == 0) { d_clear(); if (held >= AUDIO_SR / 4) toast("NO KNOCKS: NO LOOP", 1200); return; }
        if (shift) for (int i = 0; i < n; i++) hits[i].off -= shift;
        host_first_loop(rec_start + shift, held);
    }
    lstate = LS_PLAY; first_take = false;
}

// the loop's kicks as sixteenths, for the bass
static void publish_kicks(void)
{
    uint32_t len = song.loop_len;
    uint64_t k = 0;
    if (len && song.beats && loop_on) {
        int n = nhits, steps = song.beats * 4;
        if (steps > 64) steps = 64;
        for (int i = 0; i < n; i++) {
            const hit_t *h = &hits[i];
            if (h->dead || kit()->v[h->side] != V_KICK) continue;
            int s = (int)((float)qoff(h) / (float)len * (float)steps + 0.5f) % steps;
            k |= 1ull << s;
        }
    }
    static uint64_t last;
    if (k != last) { last = k; song.kicks = k; }                     // only when it changes
}

// ---------------------------------------------------------------- UI: per tick
typedef struct { uint8_t side, vel, src; } impact_t;
static impact_t impacts[16];
static int nimpacts, roll_div, stut_div;
static float flash[NSIDE];
#define TRACE 128
static uint8_t trace[TRACE];
static int8_t  trace_hit[TRACE];
static int     trace_w;

static void sounded(const played_t *p)
{
    int vt = kit()->v[p->side];
    int note = vt != V_NONE ? vt_gm[vt] : side_gm[p->side];
    host_midi_note(L_DRUMS, note, p->vel);
    host_midi_note(L_DRUMS, note, 0);
    flash[p->side] = 1.0f;
    if (nimpacts < 16) impacts[nimpacts++] = (impact_t){ p->side, p->vel, p->src };
    trace_hit[(trace_w + TRACE - 1) % TRACE] = (int8_t)p->side;
    if (p->src == SRC_ROLL) record_hit(p->frame, p->side, p->vel, -1);
}

static void d_tick(const ctl_t *c, bool live)
{
    now_frames = c->frames;
    if (live) {
        // tilt: the bottom edge down tunes up (7 semitones at 30 degrees); a sideways lean mutates the loop
        float tl = c->has_imu ? clampf(c->lean_y / 30.0f, -1.0f, 1.0f) * 7.0f : 0.0f;
        tune_now = P(DP_TUNE) + tl;
        float mu = P(DP_MUTATE) * 0.01f + (c->has_imu ? clampf((fabsf(c->lean_x) - 15.0f) / 30.0f, 0.0f, 1.0f) * 0.6f : 0.0f);
        mutate_now = clampf(mu, 0.0f, 1.0f);
        filter_now = c->has_joy ? c->jx : 0.0f;
        float jy = c->has_joy ? c->jy : 0.0f, beat = 60.0f / song.bpm;
        roll_div = jy > 0.3f ? (jy > 0.9f ? 8 : jy > 0.6f ? 4 : 2) : 0;
        stut_div = jy < -0.3f ? (jy < -0.9f ? 8 : jy < -0.6f ? 4 : 2) : 0;
        roll_side = last_side; roll_vel = clampi((int)((float)last_vel * 0.85f), 20, 127);
        roll_hz = roll_div ? (float)roll_div / beat : 0.0f;
        stutter_s = stut_div ? beat / (float)stut_div : 0.0f;
        for (int i = 0; i < c->nburst; i++) {                        // the accelerometer, stamped in audio frames
            acc_hist[acc_w].frame = c->frames - (uint32_t)(c->nburst - 1 - i) * (AUDIO_SR / 1000);
            acc_hist[acc_w].a = c->burst[i];
            acc_w = (acc_w + 1) % AHIST;
        }
        rec_logic(c);
    } else {
        // not live: it keeps its tune and mutate, the stick lets go, a recording in progress is closed
        roll_hz = 0; stutter_s = 0; filter_now = 0; roll_div = stut_div = 0;
        if (rec_pending) { rec_pending = false; nprov = 0; }
        if (lstate == LS_REC || lstate == LS_DUB) { ctl_t r = *c; r.rec = false; r.rec_release = true; rec_logic(&r); }
    }
    onset_t o;
    while (npending < 4 && d_onsetq_pop(&onsets, &o)) pending[npending++] = o;
    for (int i = 0; i < npending; ) {
        int32_t age = (int32_t)(c->frames - pending[i].frame);
        bool use_side = c->has_imu && P(DP_SIDES) < 1.0f;
        if (use_side && age < WAIT_MS * AUDIO_SR / 1000) { i++; continue; }
        int side = classify(&pending[i], use_side);
        float v = clampf(pending[i].level / (1.6f * knock_avg), 0.0f, 1.0f);
        int vel = clampi((int)(v * 127.0f + 0.5f), 12, 127);
        knock_avg += 0.15f * (pending[i].level - knock_avg);
        trig_t t = { (uint8_t)side, (uint8_t)vel, SRC_LIVE, pending[i].snip };
        d_trigq_push(&trigs, &t);
        record_hit(pending[i].frame, side, vel, pending[i].snip);
        last_side = side; last_vel = vel;
        ui_awake();
        coach_done(LEARN_INST + 0);                                    // the jam's first hint: knock on the box
        pending[i] = pending[--npending];
    }
    played_t p;
    while (d_playq_pop(&played, &p)) sounded(&p);
    if (c->ticks % 10 == 0) publish_kicks();
}

static void d_changed(int p) { (void)p; }
static void d_midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) != 0x90 || d2 == 0) return;
    int side = -1;
    for (int s = 0; s < NSIDE && side < 0; s++) { int vt = kit()->v[s]; if (vt != V_NONE && vt_gm[vt] == d1) side = s; }
    for (int s = 0; s < NSIDE && side < 0; s++) if (side_gm[s] == d1) side = s;
    if (side < 0) side = d1 % NSIDE;
    trig_t t = { (uint8_t)side, d2, SRC_MIDI, -1 };
    d_trigq_push(&trigs, &t);
}
static void d_action(int p) { if (p == DP_UNDO) { if (nhits) undo(); else toast("NO DRUM LOOP YET", 900); } }

// ---------------------------------------------------------------- the scene
// The box in three quarters view: the face (the screen side), the top and the right side showing. It moves
// on springs: knocked on the left it jumps right, on the top it dips, on the face it squashes.
static float bx, by, bvx, bvy, sq, sqv;
#define BOX_X 52.0f
#define BOX_Y 31.0f
static parts_t parts;
static uint32_t pseed = 0x2468aceu;

static void fill_quad(const int *X, const int *Y, int level)
{
    int y0 = Y[0], y1 = Y[0];
    for (int i = 1; i < 4; i++) { if (Y[i] < y0) y0 = Y[i]; if (Y[i] > y1) y1 = Y[i]; }
    for (int y = y0; y <= y1; y++) {
        float xa = 1e9f, xb = -1e9f;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4, ya = Y[i], yb = Y[j];
            if (ya == yb) { if (y == ya) { xa = fminf(xa, (float)(X[i] < X[j] ? X[i] : X[j])); xb = fmaxf(xb, (float)(X[i] > X[j] ? X[i] : X[j])); } continue; }
            if ((y < ya && y < yb) || (y > ya && y > yb)) continue;
            float x = (float)X[i] + (float)(X[j] - X[i]) * (float)(y - ya) / (float)(yb - ya);
            if (x < xa) xa = x;
            if (x > xb) xb = x;
        }
        for (int x = (int)ceilf(xa); x <= (int)floorf(xb); x++) {
            bool on = level >= 4 || (level == 3 && ((x + y) & 3)) || (level == 2 && !((x + y) & 1)) || (level == 1 && !(x & 1) && !(y & 1));
            if (on) oled_pixel(x, y, true);
        }
    }
}

typedef struct { int fx0, fy0, fx1, fy1, dx, dy; } boxgeo_t;
static boxgeo_t box_geo(float cx, float cy, float w, float h, float d)
{
    boxgeo_t g;
    g.fx0 = (int)lroundf(cx - w * 0.5f); g.fx1 = (int)lroundf(cx + w * 0.5f);
    g.fy0 = (int)lroundf(cy - h * 0.5f); g.fy1 = (int)lroundf(cy + h * 0.5f);
    g.dx = (int)lroundf(d); g.dy = -(int)lroundf(d * 0.66f);
    return g;
}

static void draw_box(const boxgeo_t *g, const float *fl)
{
    int X[4], Y[4], lv[NSIDE];
    for (int s = 0; s < NSIDE; s++) lv[s] = fl ? (fl[s] > 0.75f ? 4 : fl[s] > 0.5f ? 3 : fl[s] > 0.25f ? 2 : fl[s] > 0.05f ? 1 : 0) : 0;
    if (lv[SD_FACE]) { X[0] = g->fx0; Y[0] = g->fy0; X[1] = g->fx1; Y[1] = g->fy0; X[2] = g->fx1; Y[2] = g->fy1; X[3] = g->fx0; Y[3] = g->fy1; fill_quad(X, Y, lv[SD_FACE]); }
    if (lv[SD_TOP]) { X[0] = g->fx0; Y[0] = g->fy0; X[1] = g->fx1; Y[1] = g->fy0; X[2] = g->fx1 + g->dx; Y[2] = g->fy0 + g->dy; X[3] = g->fx0 + g->dx; Y[3] = g->fy0 + g->dy; fill_quad(X, Y, lv[SD_TOP]); }
    if (lv[SD_RIGHT]) { X[0] = g->fx1; Y[0] = g->fy0; X[1] = g->fx1 + g->dx; Y[1] = g->fy0 + g->dy; X[2] = g->fx1 + g->dx; Y[2] = g->fy1 + g->dy; X[3] = g->fx1; Y[3] = g->fy1; fill_quad(X, Y, lv[SD_RIGHT]); }
    oled_frame(g->fx0, g->fy0, g->fx1 - g->fx0 + 1, g->fy1 - g->fy0 + 1);
    oled_line(g->fx0, g->fy0, g->fx0 + g->dx, g->fy0 + g->dy, true);
    oled_line(g->fx1, g->fy0, g->fx1 + g->dx, g->fy0 + g->dy, true);
    oled_line(g->fx1, g->fy1, g->fx1 + g->dx, g->fy1 + g->dy, true);
    oled_line(g->fx0 + g->dx, g->fy0 + g->dy, g->fx1 + g->dx, g->fy0 + g->dy, true);
    oled_line(g->fx1 + g->dx, g->fy0 + g->dy, g->fx1 + g->dx, g->fy1 + g->dy, true);
    if (lv[SD_LEFT]) { int x = g->fx0 - 3, cy = (g->fy0 + g->fy1) / 2; oled_line(x, cy - 4, x, cy + 4, true); if (lv[SD_LEFT] > 2) oled_line(x - 2, cy - 2, x - 2, cy + 2, true); }
    if (lv[SD_BOTTOM]) { int y = g->fy1 + 3, cx = (g->fx0 + g->fx1) / 2; oled_line(cx - 6, y, cx + 6, y, true); if (lv[SD_BOTTOM] > 2) oled_line(cx - 3, y + 2, cx + 3, y + 2, true); }
}

static const char *side_label(int s, char *b, int n)
{
    int vt = kit()->v[s];
    if (vt != V_NONE) return vt_label[vt];
    if (kit()->body == B_OFF) return "-";
    ui_note_name(by_side() ? side_note(s, kit()->octave) : scale_note(song.degree, kit()->octave), b, n);
    return b;
}

static void label(int x, int y, const char *s, float fl, int align)
{
    int w = oled_ftext_w(F_TINY, s, 1);
    int x0 = align < 0 ? x - w : align == 0 ? x - w / 2 : x;
    oled_rect(x0 - 1, y - 1, w + 2, 7, false);
    oled_ftext_ex(F_TINY, x0, y, s, 1);
    if (fl > 0.4f) oled_invert(x0 - 1, y - 1, w + 2, 7);
}

static void physics(void)
{
    for (int i = 0; i < nimpacts; i++) {
        float k = 0.4f + 0.6f * (float)impacts[i].vel / 127.0f;
        if (impacts[i].src == SRC_LOOP) k *= 0.6f;
        float ix = 0, iy = 0, px = BOX_X + bx, py = BOX_Y + by, vx = 0, vy = 0;
        switch (impacts[i].side) {
        case SD_LEFT: ix = 1; px -= 19; vx = -60; break;
        case SD_RIGHT: ix = -1; px += 23; vx = 60; break;
        case SD_TOP: iy = 1; px += 4; py -= 10; vy = -50; break;
        case SD_BOTTOM: iy = -1; py += 8; vy = 50; break;
        default: sqv -= 3.0f * k; break;
        }
        bvx += ix * 110.0f * k; bvy += iy * 80.0f * k;
        int nsp = impacts[i].src == SRC_LOOP ? 2 : 6;
        for (int k2 = 0; k2 < nsp; k2++)
            parts_emit(&parts, px, py, vx + 40.0f * rng_bi(&pseed), vy + 40.0f * rng_bi(&pseed) - (impacts[i].side == SD_FACE ? 20.0f : 0.0f), 0.35f + 0.2f * rng_uni(&pseed), PART_STREAK, 1);
        if (impacts[i].src != SRC_LOOP) parts_emit(&parts, px, py, 0, 0, 0.3f, PART_RING, (uint8_t)(1 + impacts[i].vel / 50));
    }
    nimpacts = 0;
    for (int s = 0; s < 4; s++) {
        const float dt = 0.01f, k = 260.0f, c = 14.0f;
        bvx += (-k * bx - c * bvx) * dt; bvy += (-k * by - c * bvy) * dt; sqv += (-k * sq - c * sqv) * dt;
        bx += bvx * dt; by += bvy * dt; sq += sqv * dt;
    }
    bx = clampf(bx, -7, 7); by = clampf(by, -5, 5); sq = clampf(sq, -0.35f, 0.3f);
    parts_step(&parts, 0.04f, 120.0f, 2.0f);
    for (int s = 0; s < NSIDE; s++) flash[s] *= 0.78f;
}

static void draw_ring(int cx, int cy, int r)
{
    uint32_t len = song.loop_len;
    for (int a = 0; a < 360; a += 12) { float rad = (float)a * 0.0174533f; oled_pixel(cx + (int)lroundf((float)r * cosf(rad)), cy + (int)lroundf((float)r * sinf(rad)), true); }
    char b[12];
    if (lstate == LS_REC && !len) {                                 // making the first loop: an arc grows
        float t = (float)(now_frames - rec_start) / (float)AUDIO_SR;
        ui_arc(cx, cy, r, -90.0f, -90.0f + fminf(t, 8.0f) / 8.0f * 360.0f);
        ui_arc(cx, cy, r - 1, -90.0f, -90.0f + fminf(t, 8.0f) / 8.0f * 360.0f);
        snprintf(b, sizeof b, "%.1f", (double)t);
        oled_ftext_c(F_TINY, cx, cy - 2, b, 1);
        return;
    }
    if (!len) return;
    for (int k = 0; k < song.beats; k++) {
        float rad = ((float)k / (float)song.beats * 360.0f - 90.0f) * 0.0174533f;
        oled_line(cx + (int)lroundf((float)(r + 2) * cosf(rad)), cy + (int)lroundf((float)(r + 2) * sinf(rad)),
                  cx + (int)lroundf((float)(r + 3) * cosf(rad)), cy + (int)lroundf((float)(r + 3) * sinf(rad)), true);
    }
    float ph = (float)((now_frames - song.loop_start) % len) / (float)len;
    int n = nhits;
    for (int i = 0; i < n; i++) {
        const hit_t *h = &hits[i];
        if (h->dead) continue;
        float f = (float)qoff(h) / (float)len, rad = (f * 360.0f - 90.0f) * 0.0174533f, rr = (float)(r - 3) - (float)h->side * 1.6f;
        int x = cx + (int)lroundf(rr * cosf(rad)), y = cy + (int)lroundf(rr * sinf(rad));
        float since = ph - f; if (since < 0) since += 1.0f;
        if (since < 0.05f && loop_on) oled_disc(x, y, 2, true);
        else if (h->vel > 90) oled_rect(x, y, 2, 2, true);
        else oled_pixel(x, y, true);
    }
    float rad = (ph * 360.0f - 90.0f) * 0.0174533f;
    oled_line(cx, cy, cx + (int)lroundf((float)(r + 1) * cosf(rad)), cy + (int)lroundf((float)(r + 1) * sinf(rad)), true);
    snprintf(b, sizeof b, "%d", (int)(song.bpm + 0.5f));
    int w = oled_ftext_w(F_TINY, b, 1);
    oled_rect(cx - w / 2 - 1, cy - 3, w + 2, 7, false);
    oled_ftext_c(F_TINY, cx, cy - 2, b, 1);
}

static void draw_meter(int x, int y, int h)
{
    float fe = clampf((20.0f * log10f(env_now + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
    float ft = clampf((20.0f * log10f(thr_now + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
    oled_rframe(x, y, 7, h, 1);
    int f = (int)(fe * (float)(h - 4));
    if (f > 0) oled_rect(x + 2, y + h - 2 - f, 3, f, true);
    int ty = y + h - 2 - (int)(ft * (float)(h - 4));
    oled_line(x - 2, ty, x + 8, ty, true);
    oled_ftext_c(F_TINY, x + 3, y + h + 3, "MIC", 0);
}

static void draw_lane(int y0)
{
    uint32_t len = song.loop_len;
    if (len && nhits) {
        int x0 = 2, w = OLED_W - 4;
        for (int k = 0; k <= song.beats; k++) oled_rect(x0 + k * (w - 1) / (song.beats ? song.beats : 1), y0, 1, k % 4 == 0 ? 2 : 1, true);
        int n = nhits;
        for (int i = 0; i < n; i++) {
            const hit_t *h = &hits[i];
            if (h->dead) continue;
            int x = x0 + (int)((float)qoff(h) / (float)len * (float)(w - 2));
            oled_rect(x, y0 + 3 + h->side * 3, h->vel > 90 ? 3 : 2, 2, true);
        }
        int px = x0 + (int)((float)((now_frames - song.loop_start) % len) / (float)len * (float)(w - 2));
        oled_invert(px, y0 + 2, 1, 14);
        return;
    }
    int ty = y0 + 15 - (int)(clampf((20.0f * log10f(thr_now + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f) * 14.0f);
    oled_hline_pat(0, ty, OLED_W, 0x11, true);
    for (int i = 0; i < TRACE; i++) {
        int k = (trace_w + i) % TRACE, h = trace[k];
        if (h) oled_line(i, y0 + 15, i, y0 + 15 - h, true);
        if (trace_hit[k] >= 0) oled_rect(i - 1, y0 + 1, 3, 2, true);
    }
}

static void d_draw(const view_t *v)
{
    float e = clampf((20.0f * log10f(env_now + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
    trace[trace_w] = (uint8_t)(e * 14.0f);
    trace_w = (trace_w + 1) % TRACE;
    trace_hit[trace_w] = -1;
    physics();
    boxgeo_t g = box_geo(BOX_X + bx, BOX_Y + by, 34.0f * (1.0f + sq), 13.0f * (1.0f + sq * 0.6f), 9.0f);
    draw_box(&g, flash);
    char b[8], hb[20];
    int cy = (g.fy0 + g.fy1) / 2;
    label(g.fx0 - 6, cy - 2, side_label(SD_LEFT, b, sizeof b), flash[SD_LEFT], -1);
    label(g.fx1 + g.dx + 3, cy - 4, side_label(SD_RIGHT, b, sizeof b), flash[SD_RIGHT], 1);
    label((g.fx0 + g.fx1) / 2 + g.dx / 2, g.fy0 + g.dy / 2 - 2, side_label(SD_TOP, b, sizeof b), flash[SD_TOP], 0);
    label((g.fx0 + g.fx1) / 2, g.fy1 + 2, side_label(SD_BOTTOM, b, sizeof b), flash[SD_BOTTOM], 0);
    label((g.fx0 + g.fx1) / 2, cy - 2, side_label(SD_FACE, b, sizeof b), flash[SD_FACE], 0);
    parts_draw(&parts);
    if (song.loop_len || lstate == LS_REC) draw_ring(115, 30, 11);
    else draw_meter(116, 17, 20);
    // the loop's state, top left; what the stick is doing, bottom left
    if (lstate == LS_REC) oled_pill(F_TINY, 0, 17, "REC", true);
    else if (lstate == LS_DUB) oled_pill(F_TINY, 0, 17, "ADD", true);
    else if (nhits && !loop_on) oled_pill(F_TINY, 0, 17, "LOOP OFF", false);
    static const char *const div_l[] = { "", "", "1/8", "", "1/16", "", "", "", "1/32" };
    if (roll_div) { snprintf(hb, sizeof hb, "ROLL %s", div_l[roll_div]); oled_pill(F_TINY, 0, 39, hb, true); }
    else if (stut_div) { snprintf(hb, sizeof hb, "REPEAT %s", div_l[stut_div]); oled_pill(F_TINY, 0, 39, hb, true); }
    else if (fabsf(filter_now) > 0.05f) { snprintf(hb, sizeof hb, "%s %d", filter_now < 0 ? "LP" : "HP", (int)(fabsf(filter_now) * 100.0f)); oled_pill(F_TINY, 0, 39, hb, false); }
    if (!v->cover) { oled_hline_pat(0, 47, OLED_W, 0x11, true); draw_lane(48); }
}

static void d_icon(int x, int y, bool lit)
{
    (void)lit;
    oled_frame(x, y + 3, 8, 6);                                  // a small box, its top and side, a knock mark
    oled_line(x, y + 3, x + 3, y, true); oled_line(x + 7, y + 3, x + 10, y, true); oled_line(x + 3, y, x + 10, y, true);
    oled_line(x + 10, y, x + 10, y + 5, true); oled_line(x + 7, y + 8, x + 10, y + 5, true);
}

static int d_state(void)
{
    if (lstate == LS_REC || lstate == LS_DUB) return LOOP_REC;
    if (!nhits) return LOOP_NONE;
    return loop_on ? LOOP_ON : LOOP_OFF;
}

const layer_t layer_drums = {
    .name = "DRUMS", .letter = 'd', .pages = pages, .npages = 2, .params = params, .nparams = DP_N,
    .events = true, .init = d_init, .audio = d_audio, .tick = d_tick, .draw = d_draw, .icon = d_icon,
    .changed = d_changed, .clear = d_clear, .midi_in = d_midi_in, .action = d_action, .state = d_state,
};

// BASS: the jam's bass. One voice (legato, last note wins), six sounds, played three ways while the layer is
// live (MODE):
//   KNOCK   a knock on the box plays the note the tilt points at, on a ladder of the song's scale around the
//           chord's root (flat = the root); how hard sets how loud
//   HUM     sing or hum: it follows your pitch, folded down into the bass range and snapped to the scale
//   FOLLOW  it plays by itself: a bassline from the song's chord on the song's grid (ROOT, ROOT+5, OCTAVES,
//           WALK, ARP, ACID), on the drums' kicks with LOCK KICK. Once it has played a second while live it
//           keeps going when another layer is live (the band's bass player); tap REC mutes it
// In every mode leaning picks the chord for the whole jam (song.degree, on a ladder: iii ii IV I V vi vii).
// The stick: X = the filter, up = an octave up, down = accent (more envelope and drive).
// REC: hold = record (or add) what it plays to its loop on the song's sixteenths (the first loop on any layer
// sets the song's loop through host_first_loop); tap = mute / unmute. Loop notes come first; FOLLOW fills the
// steps the loop leaves empty.
#include "jam.h"
#include "ui.h"
#include "spsc.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- params
enum { B_MODE, B_PAT, B_RATE, B_KICK, B_FILL, B_OCT, B_LEAN, B_RIFF,
       B_SOUND, B_CUT, B_RES, B_ENV, B_DEC, B_GLIDE, B_DRIVE, B_FX, B_N };
enum { M_KNOCK, M_HUM, M_FOLLOW };
enum { PT_ROOT, PT_FIFTH, PT_OCT, PT_WALK, PT_ARP, PT_ACID };
enum { S_SUB, S_ACID, S_808, S_FINGER, S_REESE, S_FM, S_N };
#define P(i) LP(L_BASS, i)

static const char *const pages[2] = { "BASS", "BASS TONE" };
static const char *const mode_l[] = { "KNOCK", "HUM", "FOLLOW" };
static const char *const pat_l[] = { "ROOT", "ROOT+5", "OCTAVES", "WALK", "ARP", "ACID" };
static const char *const rate_l[] = { "1/4", "1/8", "1/16" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const sound_l[] = { "SUB", "ACID", "808", "FINGER", "REESE", "FM" };
static const param_t params[B_N] = {
    [B_MODE]  = { "MODE", "bmod", 0, PK_ENUM, 0, 0, 2, 1, M_FOLLOW, "", mode_l, "KNOCK, HUM, OR IT PLAYS" },
    [B_PAT]   = { "PATTERN", "bpat", 0, PK_ENUM, 0, 0, 5, 1, PT_FIFTH, "", pat_l, "WHAT FOLLOW PLAYS" },
    [B_RATE]  = { "RATE", "brat", 0, PK_ENUM, 0, 0, 2, 1, 1, "", rate_l, "HOW OFTEN FOLLOW PLAYS" },
    [B_KICK]  = { "LOCK KICK", "bkik", 0, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "ON: PLAY WITH THE KICKS" },
    [B_FILL]  = { "FILL", "bfil", 0, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "EXTRA NOTES IN BETWEEN" },
    [B_OCT]   = { "OCTAVE", "boct", 0, PK_NUM, PF_INT | PF_SIGNED, -1, 1, 1, 0, "", NULL, "THE WHOLE BASS UP OR DOWN" },
    [B_LEAN]  = { "LEAN CHORD", "blch", 0, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "LEAN TO CHANGE THE CHORD" },
    [B_RIFF]  = { "RIFF", "brif", 0, PK_NUM, PF_INT, 1, 16, 1, 1, "", NULL, "THE ACID RIFF AND FILLS" },
    [B_SOUND] = { "SOUND", "bsnd", 1, PK_ENUM, 0, 0, S_N - 1, 1, S_FINGER, "", sound_l, "THE BASS ITSELF" },
    [B_CUT]   = { "CUTOFF", "bcut", 1, PK_NUM, PF_LOG, 60, 6000, 1.08f, 900, "Hz", NULL, "THE FILTER: DARK TO BRIGHT" },
    [B_RES]   = { "RESONANCE", "bres", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "THE FILTER'S PEAK" },
    [B_ENV]   = { "ENVELOPE", "benv", 1, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "HOW FAR EACH NOTE OPENS IT" },
    [B_DEC]   = { "DECAY", "bdec", 1, PK_NUM, PF_LOG, 0.05f, 2.0f, 1.12f, 0.35f, "s", NULL, "HOW LONG A NOTE RINGS" },
    [B_GLIDE] = { "GLIDE", "bgld", 1, PK_NUM, 0, 0, 0.5f, 0.01f, 0.06f, "s", NULL, "SLIDE ON JOINED NOTES" },
    [B_DRIVE] = { "DRIVE", "bdrv", 1, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "GRIT" },
    [B_FX]    = { "ECHO SEND", "bfx ", 1, PK_NUM, PF_INT, 0, 100, 5, 10, "%", NULL, "INTO THE SHARED ECHO" },
};

// ---------------------------------------------------------------- between the tasks
typedef struct { uint8_t kind, note, vel, legato; } cmd_t;               // UI -> audio: live notes
enum { C_ON, C_OFF };
enum { SRC_LIVE, SRC_FOLLOW, SRC_LOOP };
typedef struct { uint32_t frame; uint8_t note, vel, on, src, flags; } ev_t;   // audio -> UI: what sounded
SPSC(bcmdq, cmd_t, 32)
SPSC(bevq, ev_t, 64)
static bcmdq_t cmds;
static bevq_t evs;

// The loop: one note per sixteenth of the song's loop. The UI writes (note, len, flags, then vel last; vel 0 =
// empty), the audio task reads.
#define MAXSTEP 128
enum { F_SLIDE = 1, F_ACCENT = 2 };
static volatile uint8_t st_vel[MAXSTEP], st_note[MAXSTEP], st_len[MAXSTEP], st_flag[MAXSTEP];
static volatile int nsteps;              // steps in the loop (song.beats * 4, at most MAXSTEP); 0: no loop
static volatile int muted;               // tap REC: loop and FOLLOW silent
static volatile int follow_on;           // the FOLLOW bass player is on
static volatile int a_live, a_mode;      // the UI's view of live and the mode, for the audio task
static volatile int knock_note = 40;     // the note a knock plays now
static volatile int oct_add;             // the stick held up: +12
static volatile float stick_x, accent_amt;
static volatile int model_req;           // the sound to switch to (ducked)
// audio -> UI
static volatile float v_level, v_pitch, v_mic_env, v_thr;
static volatile int   v_note = -1;

// ---------------------------------------------------------------- the voice
#define KS_N 2048
static float *ks;                         // FINGER's string (PSRAM)
#define YR 2048
static float *yring;                      // HUM: the mic at 8 kHz (PSRAM)
static volatile uint32_t yring_w;
static struct {
    float pitch, target;                  // MIDI
    int   gate;                           // samples left with the gate on; -1: until C_OFF
    float amp; int stage;                 // 0 idle, 1 attack, 2 decay / sustain, 3 release
    float fenv, penv;                     // filter envelope, 808 pitch envelope
    float vel, acc;                       // 0..1, accent 0..1
    float ph[3], mph;
    float s1a, s1b, s2a, s2b;             // two SVF stages (ic1, ic2 each)
    int   ks_w; float ks_lp; int exc_n; float exc_lp;
    biquad_t body;
    int   model; float duck; int duck_dir;
    uint32_t seed;
} V;
static float out_env, bleed_k = 0.3f;
// KNOCK's detector (the KNOCK instrument's, simplified)
static float env_f, env_s, floor_e = 0.01f, a_f, r_f, a_s, r_s;
static int   refract;
// HUM's 8 kHz feed
static float ylp1, ylp2; static int ydec;

static inline float tan_pi(float x)      // tan(pi x) for x in 0..0.28 (fc up to 9 kHz), no libm
{
    float t = 3.14159265f * x, t2 = t * t;
    return t * (1.0f + t2 * (0.33333333f + t2 * (0.13333333f + t2 * 0.05396825f)));
}
static inline float midi_to_hz(float m) { return 440.0f * fast_exp2((m - 69.0f) * (1.0f / 12.0f)); }

static void voice_on(int note, int vel, bool legato, bool accent)
{
    int n = note + oct_add;
    if (n < 18) n = 18;
    if (n > 72) n = 72;
    bool tie = legato && V.stage > 0 && V.stage < 3;
    V.target = (float)n;
    if (!tie) V.pitch = V.target;                        // a new note jumps; joined notes glide
    V.vel = (float)vel / 127.0f;
    V.acc = accent ? 1.0f : 0.0f;
    if (!tie) {
        V.stage = 1;                                      // attack from wherever the level is: no click
        V.fenv = 1.0f; V.penv = 1.0f;
        if (V.model == S_FINGER) { V.exc_n = (int)((float)AUDIO_SR / midi_to_hz(V.pitch)); }
    } else V.fenv = V.fenv < 0.5f ? 0.5f : V.fenv;       // a slide nudges the filter, no new pluck
    V.gate = -1;
}
static void voice_off(void) { if (V.stage == 1 || V.stage == 2) V.stage = 3; }

static void report(uint32_t frame, int note, int vel, bool on, int src, int flags)
{
    ev_t e = { frame, (uint8_t)note, (uint8_t)vel, (uint8_t)on, (uint8_t)src, (uint8_t)flags };
    bevq_push(&evs, &e);
}

static void init(void)
{
    ks = app_alloc(KS_N * sizeof(float));
    yring = app_alloc(YR * sizeof(float));
    bcmdq_clear(&cmds); bevq_clear(&evs);
    a_f = onepole_s(0.00015f); r_f = onepole_s(0.005f); a_s = onepole_s(0.015f); r_s = onepole_s(0.100f);   // updated at 16 kHz: 0.3, 10, 30, 200 ms
    biquad_set(&V.body, BQ_PEAK, 95.0f, 1.2f, 5.0f);
    V.pitch = V.target = 40; V.seed = 0x2468aceu; V.model = model_req = S_FINGER; V.duck = 1.0f;
    if (!ks || !yring) app_log("bass: short of memory (string %p, hum %p)", (void *)ks, (void *)yring);
}

// ---------------------------------------------------------------- FOLLOW: the bass player
static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

// Decide step ka (sixteenths since loop position 0, or since boot): a note, or -1. Sets vel, gate (steps), flags.
static int follow_step(uint32_t ka, int *vel, int *len, int *flags)
{
    int s = (int)(ka % 16u);                       // the sixteenth in its bar
    int bar = (int)(ka / 16u);
    int rate = (int)P(B_RATE), every = rate == 0 ? 4 : rate == 1 ? 2 : 1;
    int pat = (int)P(B_PAT);
    int root = song.chord_n > 0 ? song.chord[0] : 36, third = song.chord_n > 1 ? song.chord[1] : root + 4;
    int fifth = song.chord_n > 2 ? song.chord[2] : root + 7;
    root += 12 * (int)P(B_OCT); third += 12 * (int)P(B_OCT); fifth += 12 * (int)P(B_OCT);
    uint32_t riff = (uint32_t)P(B_RIFF) * 2654435761u;
    *flags = 0; *vel = s % 4 == 0 ? 104 : 88;
    if (pat == PT_ACID) {                          // a seeded sixteenth riff, the same every bar, moved to the chord
        uint32_t h = hash32(riff ^ (uint32_t)s * 0x9e3779b9u);
        if ((h & 0xff) < 77 && s != 0) return -1;                 // 30 % rests (never the downbeat)
        static const int8_t iv[8] = { 0, 0, 12, 0, 7, 12, 10, 3 };
        int n = root + iv[(h >> 8) & 7];
        if (iv[(h >> 8) & 7] == 7) n = fifth;
        if (iv[(h >> 8) & 7] == 3) n = third;
        if (((h >> 12) & 0xff) < 77) { *flags |= F_ACCENT; *vel = 124; }
        if (((h >> 20) & 0xff) < 64) *flags |= F_SLIDE;
        *len = 1;
        return n;
    }
    if (pat == PT_WALK) {                          // quarters: up the chord, then down again on odd bars
        if (s % 4) return -1;
        int six = scale_note(song.degree + 5, 2) + 12 * (int)P(B_OCT);
        while (six < root) six += 12;
        int up[4] = { root, third, fifth, six }, dn[4] = { root + 12, six, fifth, third };
        *len = 3;
        return (bar & 1) ? dn[s / 4] : up[s / 4];
    }
    // ROOT, ROOT+5, OCTAVES, ARP: on the kicks (LOCK KICK, when the drums have some) or on the grid; FILL adds
    bool main_step, grid = s % every == 0;
    if (P(B_KICK) >= 1.0f && song.kicks) {
        int nk = nsteps > 0 ? nsteps : 16;
        main_step = (song.kicks >> ((ka % (uint32_t)nk) & 63u)) & 1u;
    } else main_step = grid;
    if (!main_step) {
        if (!grid) return -1;
        uint32_t h = hash32(riff ^ (uint32_t)(s + 16 * (bar & 1)) * 0x85ebca6bu);
        if ((float)(h & 0xffff) / 65536.0f >= P(B_FILL) * 0.01f) return -1;
        *vel = 72;
    }
    *len = every > 1 ? every - 1 : 1;
    int k = (int)(ka / (uint32_t)every);
    switch (pat) {
    case PT_ROOT: return root;
    case PT_FIFTH: { int f5 = fifth - 12 >= 28 ? fifth - 12 : fifth; return (k & 1) ? f5 : root; }
    case PT_OCT: return (k & 1) ? root + 12 : root;
    default: { int a[4] = { root, third, fifth, root + 12 }; return a[k & 3]; }
    }
}

// ---------------------------------------------------------------- audio
// Per block: the live notes from the UI, the steps the block crosses (the loop's, else FOLLOW's), a knock in the
// mic (KNOCK). Then the voice, rendered in segments between those triggers; the controls are smoothed per block
// and the filter's coefficients worked out at each segment's ends and interpolated per sample (no libm, no
// division per sample).
typedef struct { int at, note, vel, gate, flags, src; } trig_t;
typedef struct { float cut, res, drv, fx, ea, ac; } bctl_t;
static bctl_t c0, c1;                       // the controls at the block's start and end
static int    nblk = AUDIO_BLOCK;
static float  sus_lvl, dk, fk, pk808, gk, ks_g, blk_pk;
static volatile int hum_voiced;             // UI: a voice is being followed (the bleed is not learned meanwhile)
static volatile float v_bleed;

typedef struct { float a1, a2, a3, b1, b2, b3, comp; } fco_t;
static void fco(fco_t *c, float cut, float rs, int model)
{
    cut = clampf(cut, 20.0f, 9000.0f);
    float g = tan_pi(cut / (float)AUDIO_SR);
    float k1 = 2.0f - 1.85f * rs * (model == S_ACID ? 1.0f : 0.7f), k2 = 1.41f;
    c->a1 = 1.0f / (1.0f + g * (g + k1)); c->a2 = g * c->a1; c->a3 = g * c->a2;
    c->b1 = 1.0f / (1.0f + g * (g + k2)); c->b2 = g * c->b1; c->b3 = g * c->b2;
    c->comp = 1.0f + 0.6f * rs;                 // keep the level as the peak grows
}
static inline float at_(float x0, float x1, int i) { return x0 + (x1 - x0) * (float)i / (float)nblk; }

static void render(float *dry, float *send, int a, int b, uint32_t f0)
{
    int n = b - a;
    if (n <= 0) return;
    float depth = V.model == S_ACID ? 5.0f : 3.0f;
    float cutA = at_(c0.cut, c1.cut, a), cutB = at_(c0.cut, c1.cut, b);
    float resA = at_(c0.res, c1.res, a), resB = at_(c0.res, c1.res, b);
    float eaA = at_(c0.ea, c1.ea, a), eaB = at_(c0.ea, c1.ea, b);
    float acA = fmaxf(at_(c0.ac, c1.ac, a), V.acc), acB = fmaxf(at_(c0.ac, c1.ac, b), V.acc);
    float feA = V.fenv, feB = V.fenv * powf(fk, (float)n);
    fco_t A, B;
    fco(&A, cutA * fast_exp2((eaA * depth + 1.5f * acA) * feA), resA, V.model);
    fco(&B, cutB * fast_exp2((eaB * depth + 1.5f * acB) * feB), resB, V.model);
    float dA = at_(c0.drv, c1.drv, a) + 0.5f * acA, dB = at_(c0.drv, c1.drv, b) + 0.5f * acB;
    float gA = 1.0f + 3.0f * dA, gB = 1.0f + 3.0f * dB, iA = 1.0f / (1.0f + 1.2f * dA), iB = 1.0f / (1.0f + 1.2f * dB);
    float lvA = (1.0f + 0.25f * acA) * 0.42f, lvB = (1.0f + 0.25f * acB) * 0.42f;
    float fxA = at_(c0.fx, c1.fx, a), fxB = at_(c0.fx, c1.fx, b);
    float inv = 1.0f / (float)n;
    const float att = 1.0f / (0.003f * (float)AUDIO_SR), rel = 1.0f - 0.99896f;   // 3 ms attack, 30 ms release
    const float duck_step = 1.0f / (0.008f * (float)AUDIO_SR);
    bool glide = fabsf(V.target - V.pitch) > 0.001f;
    bool two = V.model == S_ACID || V.model == S_REESE;         // 24 dB for the saws, 12 dB for the rest
    float f = midi_to_hz(V.pitch), dt = f / (float)AUDIO_SR;
    float vel = 0.55f + 0.45f * V.vel;
    // everything interpolated across the segment moves by a step per sample
    float a1 = A.a1, a2 = A.a2, a3 = A.a3, b1 = A.b1, b2 = A.b2, b3 = A.b3, cm = A.comp;
    float da1 = (B.a1 - A.a1) * inv, da2 = (B.a2 - A.a2) * inv, da3 = (B.a3 - A.a3) * inv;
    float db1 = (B.b1 - A.b1) * inv, db2 = (B.b2 - A.b2) * inv, db3 = (B.b3 - A.b3) * inv, dcm = (B.comp - A.comp) * inv;
    float gd = gA, dgd = (gB - gA) * inv, iv = iA * vel * lvA, div_ = (iB * lvB - iA * lvA) * vel * inv;
    float fx = fxA, dfx = (fxB - fxA) * inv, ea = eaA, dea = (eaB - eaA) * inv;
    for (int i = 0; i < n; i++) {
        if (V.gate > 0 && --V.gate == 0) { voice_off(); report(f0 + (uint32_t)(a + i), (int)V.target, 0, false, SRC_FOLLOW, 0); }
        switch (V.stage) {
        case 1: V.amp += att; if (V.amp >= 1.0f) { V.amp = 1.0f; V.stage = 2; } break;
        case 2: V.amp += dk * (sus_lvl - V.amp); break;
        case 3: V.amp -= rel * V.amp; if (V.amp < 1e-4f) { V.amp = 0; V.stage = 0; } break;
        }
        V.fenv *= fk;
        if (glide) { V.pitch += gk * (V.target - V.pitch); f = midi_to_hz(V.pitch); dt = f / (float)AUDIO_SR; }
        float y = 0;
        switch (V.model) {
        case S_SUB:
            V.ph[0] = wrap1(V.ph[0] + dt);
            y = 0.8f * sin_turns(V.ph[0]) + 0.25f * sin_turns(2.0f * V.ph[0]) + 0.08f * sin_turns(3.0f * V.ph[0]);
            break;
        case S_ACID:
            V.ph[0] = wrap1(V.ph[0] + dt);
            y = 0.7f * saw_blep(V.ph[0], dt);
            break;
        case S_808:
            V.penv *= pk808;
            V.ph[0] = wrap1(V.ph[0] + dt * (1.0f + 1.6f * V.penv));
            { float s0 = 1.6f * sin_turns(V.ph[0]); y = s0 * (1.0f - 0.148f * s0 * s0); }   // a little round at the top
            break;
        case S_FINGER: {
            if (!ks) break;
            float d = (float)AUDIO_SR / f - 1.0f;
            d = clampf(d, 4.0f, (float)(KS_N - 4));
            int di = (int)d; float fr = d - (float)di;
            float x0 = ks[(V.ks_w - di) & (KS_N - 1)], x1 = ks[(V.ks_w - di - 1) & (KS_N - 1)];
            float r = x0 + (x1 - x0) * fr;
            V.ks_lp += 0.5f * (r - V.ks_lp);
            float in = V.ks_lp * ks_g;
            if (V.exc_n > 0) {                             // the pluck: one period of soft noise
                V.exc_lp += 0.35f * (rng_bi(&V.seed) - V.exc_lp);
                in += (0.9f + 0.5f * V.vel) * V.exc_lp;
                V.exc_n--;
            }
            ks[V.ks_w & (KS_N - 1)] = flush_denormal(in);
            V.ks_w++;
            y = 1.4f * biquad_run(&V.body, r);
            break;
        }
        case S_REESE: {                                     // two saws a few cents apart, beating, over a sine
            float d1 = dt * 1.0058f, d2 = dt * 0.9942f;
            V.ph[1] = wrap1(V.ph[1] + d1); V.ph[2] = wrap1(V.ph[2] + d2); V.ph[0] = wrap1(V.ph[0] + dt);
            y = 0.45f * (2.0f * (V.ph[1] + V.ph[2]) - 2.0f - poly_blep(V.ph[1], d1) - poly_blep(V.ph[2], d2)) + 0.35f * sin_turns(V.ph[0]);
            break;
        }
        case S_FM: {
            V.mph = wrap1(V.mph + dt);
            float x = 2.0f * V.mph - 1.0f, m = 4.0f * x * (1.0f - fabsf(x));   // the modulator: a plain parabola sine (-sin)
            float idx = -(0.15f + (0.9f * ea + 0.5f * V.acc) * V.fenv) * m;
            V.ph[0] = wrap1(V.ph[0] + dt);
            y = 0.85f * sin_turns(V.ph[0] + idx);
            break;
        }
        }
        // the low pass: SVF (12 dB), a second one for the saws (24 dB)
        float v3 = y - V.s1b, v1 = a1 * V.s1a + a2 * v3, v2 = V.s1b + a2 * V.s1a + a3 * v3;
        V.s1a = 2.0f * v1 - V.s1a; V.s1b = 2.0f * v2 - V.s1b;
        y = v2 * cm;
        if (two) {
            float w3 = y - V.s2b, w1 = b1 * V.s2a + b2 * w3, w2 = V.s2b + b2 * V.s2a + b3 * w3;
            V.s2a = 2.0f * w1 - V.s2a; V.s2b = 2.0f * w2 - V.s2b;
            y = w2;
        }
        // drive: a cubic soft clip (no division), then the level
        float u = clampf(y * gd, -1.5f, 1.5f);
        y = (u - 0.148148f * u * u * u) * iv * V.amp;
        if (V.duck_dir < 0) {                               // a new sound: down 8 ms, switch, up 8 ms
            V.duck -= duck_step;
            if (V.duck <= 0) { V.duck = 0; V.model = model_req; V.duck_dir = 1; if (ks) memset(ks, 0, KS_N * sizeof(float)); V.s1a = V.s1b = V.s2a = V.s2b = 0; }
        } else if (V.duck_dir > 0) { V.duck += duck_step; if (V.duck >= 1.0f) { V.duck = 1.0f; V.duck_dir = 0; } }
        y *= V.duck;
        dry[a + i] += y;
        send[a + i] += y * fx;
        float ay = fabsf(y); if (ay > blk_pk) blk_pk = ay;
        a1 += da1; a2 += da2; a3 += da3; cm += dcm;
        if (two) { b1 += db1; b2 += db2; b3 += db3; }
        gd += dgd; iv += div_; fx += dfx; ea += dea;
    }
    V.s1a = flush_denormal(V.s1a); V.s1b = flush_denormal(V.s1b); V.s2a = flush_denormal(V.s2a); V.s2b = flush_denormal(V.s2b);
}

static void apply(const trig_t *t, uint32_t f0)
{
    voice_on(t->note, t->vel, (t->flags & F_SLIDE) != 0, (t->flags & F_ACCENT) != 0);
    V.gate = t->gate;
    report(f0 + (uint32_t)t->at, t->note + oct_add, t->vel, true, t->src, t->flags);
}

static void audio(const float *mic, float *dry, float *send, int n, bool live)
{
    uint32_t f0 = audio_frames();
    a_live = live;
    nblk = n;
    // live notes from the UI (HUM, MIDI), at the block's start
    cmd_t c;
    while (bcmdq_pop(&cmds, &c)) {
        if (c.kind == C_ON) { voice_on(c.note, c.vel, c.legato, false); report(f0, c.note + oct_add, c.vel, true, SRC_LIVE, c.legato ? F_SLIDE : 0); }
        else { voice_off(); report(f0, (int)V.target, 0, false, SRC_LIVE, 0); }
    }
    float dec = P(B_DEC);
    // the steps this block crosses: loop notes first, FOLLOW fills the rest
    trig_t tr[10]; int ntr = 0;
    float fps16 = song_frames_per_beat() * 0.25f;
    bool fol = !muted && P(B_MODE) == M_FOLLOW && (follow_on || (live && a_mode == M_FOLLOW));
    bool loop = !muted && nsteps > 0 && song.loop_len;
    if (fol || loop) {
        float b0 = song_beat(f0) * 4.0f, b1 = song_beat(f0 + (uint32_t)n) * 4.0f;
        float total = song.loop_len && song.beats ? (float)(song.beats * 4) : 0.0f;
        float span = b1 - b0;
        if (span < 0) span += total > 0 ? total : 0;
        if (span > 0 && span < 8.0f) {
            for (float k = floorf(b0) + 1.0f; k <= b0 + span + 1e-6f && ntr < 8; k += 1.0f) {
                int at = (int)((k - b0) / span * (float)n);
                if (at >= n) at = n - 1;
                float kk = total > 0 && k >= total ? k - total : k;
                uint32_t ka = (uint32_t)(kk + 0.5f);
                int st = nsteps > 0 ? (int)(ka % (uint32_t)nsteps) : -1;
                int note = -1, vel = 0, len = 1, fl = 0, src = SRC_LOOP;
                if (loop && st >= 0 && st_vel[st]) { note = st_note[st]; vel = st_vel[st]; len = st_len[st]; fl = st_flag[st]; }
                else if (fol) { note = follow_step(ka, &vel, &len, &fl); src = SRC_FOLLOW; }
                if (note < 0) continue;
                if (len < 1) len = 1;
                int gate = (fl & F_SLIDE) ? (int)((float)len * fps16 * 1.05f) : (int)((float)len * fps16 * 0.6f) + 64;
                tr[ntr++] = (trig_t){ at, note, vel, gate, fl, src };
            }
        }
    }
    // the mic, while it is this layer's: a knock (KNOCK) plays the tilted note now; HUM's 8 kHz feed
    bool knock = live && a_mode == M_KNOCK, hum = live && a_mode == M_HUM && yring;
    if (knock || hum) {
        uint32_t yw = yring_w;
        for (int i = 0; i < n; i++) {
            float x = mic ? mic[i] : 0.0f;
            if (hum) { ylp1 += 0.33f * (x - ylp1); ylp2 += 0.33f * (ylp1 - ylp2); if (++ydec >= 4) { ydec = 0; yring[yw & (YR - 1)] = ylp2; yw++; } }
            if (i & 1) continue;                                 // the envelopes at 16 kHz
            x = fmaxf(fabsf(x), fabsf(mic ? mic[i + 1] : 0.0f));
            follow(&env_f, x, a_f, r_f);
            float slow_before = env_s;
            follow(&env_s, x, a_s, r_s);
            if (env_s < floor_e) floor_e += 0.01f * (env_s - floor_e); else floor_e += 0.000005f * (env_s - floor_e);
            float bleed = bleed_k * out_env, thr = 0.02f + 4.0f * floor_e + 0.4f * bleed;
            if (refract > 0) refract--;
            else if (knock && env_f - bleed > thr && env_f - bleed > 2.5f * (slow_before - bleed) + 0.005f && ntr < 10) {
                int vel = clampi((int)((env_f - bleed) / 0.3f * 127.0f), 30, 127);
                trig_t t = { i, knock_note, vel, (int)(fmaxf(dec, 0.12f) * (float)AUDIO_SR), vel > 110 ? F_ACCENT : 0, SRC_LIVE };
                int j = ntr++;                                   // in time order among the grid's
                while (j > 0 && tr[j - 1].at > i) { tr[j] = tr[j - 1]; j--; }
                tr[j] = t;
                refract = (int)(0.035f * (float)AUDIO_SR);         // 70 ms at the half rate
            }
        }
        if (hum) yring_w = yw;
    }
    // the controls for this block, smoothed per block (about 20 ms) and interpolated across it
    if (model_req != V.model && V.duck_dir == 0) V.duck_dir = -1;
    c0 = c1;
    const float kb = 0.095f;
    c1.cut += kb * (clampf(P(B_CUT) * fast_exp2(2.0f * stick_x), 30.0f, 9000.0f) - c1.cut);
    c1.res += kb * (P(B_RES) * 0.01f - c1.res);
    c1.drv += kb * (P(B_DRIVE) * 0.01f - c1.drv);
    c1.fx  += kb * (P(B_FX) * 0.01f - c1.fx);
    c1.ea  += kb * (P(B_ENV) * 0.01f - c1.ea);
    c1.ac  += 0.07f * (accent_amt - c1.ac);
    if (c0.cut <= 0) c0 = c1;                                   // the very first block
    sus_lvl = V.model == S_808 ? 0.0f : V.model == S_FINGER ? 1.0f : V.model == S_FM ? 0.55f : V.model == S_ACID ? 0.7f : 0.85f;
    dk = onepole_s((V.model == S_808 ? 2.0f : 1.0f) * dec / 3.0f);
    fk = 1.0f - onepole_s((V.model == S_ACID ? 0.6f * dec : 0.15f) / 3.0f);
    pk808 = 1.0f - onepole_s(0.012f);
    gk = P(B_GLIDE) > 0.002f ? onepole_s(P(B_GLIDE) / 3.0f) : 1.0f;
    if (V.model == S_FINGER) ks_g = fast_exp2(-9.966f / (fmaxf(dec * 2.5f, 0.08f) * midi_to_hz(V.pitch)));   // -60 dB in DECAY x 2.5 s
    // the voice, in segments between the triggers
    blk_pk = 0;
    int pos = 0;
    for (int k = 0; k < ntr; k++) { render(dry, send, pos, tr[k].at, f0); apply(&tr[k], f0); pos = tr[k].at; }
    render(dry, send, pos, n, f0);
    out_env = blk_pk > out_env ? blk_pk : out_env * 0.96f;
    if ((knock || hum) && !hum_voiced && out_env > 0.03f) {    // how much of the bass the mic hears (learned, as KNOCK does)
        float r = env_s / out_env;
        bleed_k += (r < bleed_k ? 0.05f : 0.002f) * (r - bleed_k);
        bleed_k = clampf(bleed_k, 0.01f, 2.0f);
    }
    v_level = V.stage ? V.amp : 0.0f;
    v_pitch = V.pitch;
    v_mic_env = env_s;
    v_bleed = bleed_k * out_env;
    v_thr = 0.02f + 4.0f * floor_e + v_bleed;
}

// ---------------------------------------------------------------- UI: HUM's pitch tracker (HUFF's YIN)
#define YW 256
#define TMAX 100                                            // 80 Hz at 8 kHz
#define TMIN 9                                              // 890 Hz
static float yin_buf[YW + TMAX], yd[TMAX + 1];
static float yin(float *conf)
{
    uint32_t w = yring_w;
    for (int i = 0; i < YW + TMAX; i++) yin_buf[i] = yring[(w - (uint32_t)(YW + TMAX) + (uint32_t)i) & (YR - 1)];
    float run = 0;
    yd[0] = 1.0f;
    for (int tau = 1; tau <= TMAX; tau++) {
        float s = 0;
        for (int j = 0; j < YW; j++) { float df = yin_buf[j] - yin_buf[j + tau]; s += df * df; }
        run += s;
        yd[tau] = run > 1e-12f ? s * (float)tau / run : 1.0f;
    }
    int t = -1;
    for (int tau = TMIN; tau < TMAX; tau++) if (yd[tau] < 0.15f) { while (tau + 1 < TMAX && yd[tau + 1] < yd[tau]) tau++; t = tau; break; }
    if (t < 0) {
        int best = TMIN; for (int tau = TMIN; tau < TMAX; tau++) if (yd[tau] < yd[best]) best = tau;
        if (yd[best] > 0.3f) { *conf = 0; return 0; }
        t = best;
    }
    for (int dv = 3; dv >= 2; dv--) {                       // an octave or a twelfth too low: check the fraction
        int h = (t + dv / 2) / dv;
        if (h - 1 < TMIN) continue;
        int hb = h;
        for (int k = h - 1; k <= h + 1; k++) if (yd[k] < yd[hb]) hb = k;
        if (yd[hb] < 0.3f) { t = hb; break; }
    }
    *conf = 1.0f - yd[t];
    float a = yd[t - 1], b = yd[t], c = t + 1 <= TMAX ? yd[t + 1] : yd[t], den = a - 2.0f * b + c;
    float sh = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
    return (float)AUDIO_SR / 4.0f / ((float)t + clampf(sh, -1.0f, 1.0f));
}

// the scale note nearest to m (MIDI, fractional)
static int snap_scale(float m)
{
    int best = (int)lroundf(m); float bd = 99;
    for (int d = -35; d <= 35; d++) {
        int nt = scale_note(d, 2);
        float e = fabsf((float)nt - m);
        if (e < bd) { bd = e; best = nt; }
    }
    return best;
}

// ---------------------------------------------------------------- UI
static const int8_t lean_deg[7] = { 2, 1, 3, 0, 4, 5, 6 };   // left to right: iii ii IV I V vi vii
static int  lean_pos = 3, ladder = 3;
static bool was_live, chord_ovr_on;                       // chord_ovr_on: leaning has set the chord (held until LEAN CHORD goes off)
static int  live_ms, hum_note = -1, hum_quiet_ms, hum_cand = -1, ytick;
static float hum_m, hum_env0;                                // what was sung (MIDI); the mic's level when the note began
// recording
static bool rec_on, rec_first;
static uint32_t rec_start;
static ev_t pend[160]; static int npend;
static uint8_t bk_vel[MAXSTEP], bk_note[MAXSTEP], bk_len[MAXSTEP], bk_flag[MAXSTEP];
static int open_step = -1; static uint32_t open_frame;
static uint32_t my_loop_len, my_loop_start;
// MIDI keys held (last note wins)
static uint8_t held[8]; static int nheld;
// the scene
static float pump;
static int   midi_cur = -1;                                 // the note MIDI has on
static parts_t ps;
static uint32_t pseed = 0x51ed27u;
static int note_shown = -1, flash_ms;

static int quantize(uint32_t frame)
{
    if (!nsteps) return -1;
    int s = (int)floorf(song_beat(frame) * 4.0f + 0.5f);
    return ((s % nsteps) + nsteps) % nsteps;
}

static void write_step(int st, int note, int vel, int len, int flags)
{
    if (st < 0 || st >= MAXSTEP) return;
    st_vel[st] = 0;
    st_note[st] = (uint8_t)note; st_len[st] = (uint8_t)clampi(len, 1, 16); st_flag[st] = (uint8_t)flags;
    __atomic_store_n(&st_vel[st], (uint8_t)clampi(vel, 1, 127), __ATOMIC_RELEASE);
}

static void clear_steps(void) { for (int i = 0; i < MAXSTEP; i++) st_vel[i] = 0; }
static void clear(void) { clear_steps(); follow_on = 0; npend = 0; rec_on = false; }

static void record(const ev_t *e)
{
    if (!rec_on || e->src == SRC_LOOP) return;
    if (rec_first) { if (npend < (int)(sizeof pend / sizeof pend[0])) pend[npend++] = *e; return; }
    if (e->on) {
        int st = quantize(e->frame);
        int len = e->src == SRC_FOLLOW ? 1 : 2;
        write_step(st, e->note - oct_add, e->vel, len, e->flags);
        open_step = st; open_frame = e->frame;
    } else if (open_step >= 0) {
        float fps16 = song_frames_per_beat() * 0.25f;
        int len = (int)((float)(e->frame - open_frame) / fps16 + 0.5f);
        if (len >= 1) st_len[open_step] = (uint8_t)clampi(len, 1, 16);
        open_step = -1;
    }
}

static void first_loop_done(uint32_t end)
{
    if (!song.loop_len) host_first_loop(rec_start, end - rec_start);
    nsteps = song.loop_len && song.beats ? clampi(song.beats * 4, 1, MAXSTEP) : 0;
    my_loop_len = song.loop_len; my_loop_start = song.loop_start;
    clear_steps();
    open_step = -1;
    for (int i = 0; i < npend; i++) {
        ev_t *e = &pend[i];
        if (!nsteps) break;
        float beat = (float)(e->frame - song.loop_start) * (float)song.beats / (float)song.loop_len;
        int st = ((int)floorf(beat * 4.0f + 0.5f) % nsteps + nsteps) % nsteps;
        if (e->on) { write_step(st, e->note - oct_add, e->vel, e->src == SRC_FOLLOW ? 1 : 2, e->flags); open_step = st; open_frame = e->frame; }
        else if (open_step >= 0) {
            float fps16 = song_frames_per_beat() * 0.25f;
            int len = (int)((float)(e->frame - open_frame) / fps16 + 0.5f);
            if (len >= 1) st_len[open_step] = (uint8_t)clampi(len, 1, 16);
            open_step = -1;
        }
    }
    npend = 0;
}

static void tick(const ctl_t *c, bool live)
{
    int mode = (int)P(B_MODE);
    a_mode = mode;
    if (chord_ovr_on && P(B_LEAN) < 1.0f) { host_chord_override(-1); chord_ovr_on = false; }   // the progression has it again
    if (P(B_SOUND) != (float)model_req) model_req = (int)P(B_SOUND);
    // the song's loop: steps follow its length; a new loop (or none) forgets ours
    if (song.loop_len != my_loop_len || song.loop_start != my_loop_start) {
        if (!(rec_on && rec_first)) {
            if (my_loop_len) clear_steps();
            my_loop_len = song.loop_len; my_loop_start = song.loop_start;
            nsteps = song.loop_len && song.beats ? clampi(song.beats * 4, 1, MAXSTEP) : 0;
        }
    }
    if (live) {
        live_ms = was_live ? live_ms + 10 : 0;
        if (!was_live) {                                     // arriving: the lean as it is now picks nothing
            float p = c->lean_x / 11.0f;
            lean_pos = clampi((int)lroundf(p) + 3, 0, 6);
        }
        // the chord, by leaning (only when it moves to a new rung)
        if (c->has_imu && P(B_LEAN) >= 1.0f) {
            float p = c->lean_x / 11.0f + 3.0f;
            int np = lean_pos;
            if (p > (float)lean_pos + 0.65f) np = lean_pos + 1;
            if (p < (float)lean_pos - 0.65f) np = lean_pos - 1;
            np = clampi(np, 0, 6);
            if (np != lean_pos) { lean_pos = np; host_chord_override(lean_deg[np]); chord_ovr_on = true; coach_done(LEARN_INST + 2); }
        }
        // KNOCK: the tilt picks a rung of the ladder (flat = the chord's root)
        if (c->has_imu) {
            float p = c->lean_y / 7.0f + 3.0f;              // flat: rung 3, the root; bottom edge down: higher
            if (p > (float)ladder + 0.65f) ladder++;
            if (p < (float)ladder - 0.65f) ladder--;
            ladder = clampi(ladder, 0, 9);
        } else ladder = 3;
        knock_note = scale_note(song.degree - 3 + ladder, 2) + 12 * (int)P(B_OCT);
        // the stick
        if (c->has_joy) {
            stick_x = c->jx;
            oct_add = c->jy > 0.6f ? 12 : 0;
            accent_amt = c->jy < -0.3f ? clampf((-c->jy - 0.3f) / 0.6f, 0, 1) : 0;
        }
        // FOLLOW: the bass player joins after a second here with the menu closed
        if (mode == M_FOLLOW && !c->menu_open && live_ms >= 1000 && !muted) follow_on = 1;
        if (mode != M_FOLLOW) follow_on = 0;
        // HUM: YIN every other tick, a note once two readings agree, legato while it holds, off after 0.12 s
        if (mode == M_HUM && yring) {
            if (++ytick % 2 == 0) {
                float conf, f = yin(&conf);
                bool voiced = conf > 0.75f && f > 70.0f && v_mic_env > v_thr * 0.5f + 0.004f;
                if (voiced) {
                    float m = 69.0f + 12.0f * log2f(f / 440.0f);
                    int cand = (int)lroundf(m);
                    // the bass itself in the mic: a reading on (an octave of) the note it plays is not a voice
                    float dm = fmodf(fabsf(m - v_pitch) + 0.5f, 12.0f) - 0.5f;
                    if (hum_note >= 0 && v_level > 0.05f && fabsf(dm) < 0.6f && v_mic_env < 0.45f * hum_env0) voiced = false;
                    if (voiced && hum_cand >= 0 && abs(cand - hum_cand) <= 1) {
                        hum_m = m;
                        float bm = m + 12.0f * (float)P(B_OCT);
                        while (bm > 52.0f + 12.0f * (float)P(B_OCT)) bm -= 12.0f;
                        while (bm < 28.0f + 12.0f * (float)P(B_OCT)) bm += 12.0f;
                        int nn = snap_scale(bm);
                        if (hum_note >= 0 && nn != hum_note && fabsf(bm - (float)hum_note) < fabsf(bm - (float)nn) + 0.35f) nn = hum_note;
                        if (nn != hum_note) {
                            hum_env0 = v_mic_env;
                            int vel = clampi((int)(v_mic_env / 0.15f * 127.0f), 40, 127);
                            cmd_t cm = { C_ON, (uint8_t)nn, (uint8_t)vel, (uint8_t)(hum_note >= 0) };
                            bcmdq_push(&cmds, &cm);
                            hum_note = nn;
                            coach_done(LEARN_INST + 1);
                        }
                        hum_quiet_ms = 0;
                    }
                    hum_cand = voiced ? cand : -1;
                } else hum_cand = -1;
            }
            hum_voiced = hum_note >= 0;
            if (hum_note >= 0 && hum_cand < 0 && (hum_quiet_ms += 10) >= 120) {
                cmd_t cm = { C_OFF, 0, 0, 0 }; bcmdq_push(&cmds, &cm); hum_note = -1;
            }
        } else if (hum_note >= 0) { cmd_t cm = { C_OFF, 0, 0, 0 }; bcmdq_push(&cmds, &cm); hum_note = -1; hum_voiced = 0; }
        // REC: hold = record (or add), tap = mute
        if (c->rec_press) {
            rec_on = true; rec_start = c->frames; rec_first = song.loop_len == 0; npend = 0; open_step = -1;
            for (int i = 0; i < MAXSTEP; i++) { bk_vel[i] = st_vel[i]; bk_note[i] = st_note[i]; bk_len[i] = st_len[i]; bk_flag[i] = st_flag[i]; }
        }
        if (c->rec_release && rec_on) {
            rec_on = false;
            if (c->rec_ms < 300) {                           // a tap: nothing was meant to be recorded
                if (!rec_first) for (int i = MAXSTEP - 1; i >= 0; i--) { st_note[i] = bk_note[i]; st_len[i] = bk_len[i]; st_flag[i] = bk_flag[i]; st_vel[i] = bk_vel[i]; }
                npend = 0;
                muted = !muted;
                if (muted) follow_on = 0;
                toast(muted ? "BASS MUTED" : "BASS ON", 900);
            } else {
                if (rec_first) first_loop_done(c->frames);
                coach_done(LEARN_INST + 3);
            }
        }
    } else {
        if (was_live && hum_note >= 0) { cmd_t cm = { C_OFF, 0, 0, 0 }; bcmdq_push(&cmds, &cm); hum_note = -1; }
        hum_voiced = 0;
        if (rec_on) { rec_on = false; if (rec_first && npend) first_loop_done(c->frames); }
        live_ms = 0;
    }
    was_live = live;
    // what sounded: MIDI, the loop, the scene
    ev_t e;
    while (bevq_pop(&evs, &e)) {
        record(&e);
        if (midi_cur >= 0 && (e.on || midi_cur != e.note || !e.on)) { host_midi_note(L_BASS, midi_cur, 0); midi_cur = -1; }
        if (e.on) { host_midi_note(L_BASS, e.note, e.vel); midi_cur = e.note; }
        if (e.on) {
            note_shown = e.note;
            float v = (float)e.vel / 127.0f;
            pump = fmaxf(pump, 0.5f + 0.5f * v);
            if (live) {
                parts_emit(&ps, 20, 31, 0, 0, 0.45f, PART_RING, (e.flags & F_ACCENT) ? 4 : 2);
                if ((e.flags & F_ACCENT) || e.vel > 112) parts_burst(&ps, 20, 31, 6, 40.0f, 0.6f, PART_DOT, &pseed);
            }
            flash_ms = 120;
        }
    }
    if (flash_ms > 0) flash_ms -= 10;
}

// ---------------------------------------------------------------- MIDI in (while live): the keys play it
static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t k = st & 0xF0;
    if (k == 0x90 && d2 > 0) {
        bool legato = nheld > 0;
        if (nheld < 8) held[nheld++] = d1;
        cmd_t cm = { C_ON, d1, d2, (uint8_t)legato }; bcmdq_push(&cmds, &cm);
    } else if (k == 0x80 || (k == 0x90 && d2 == 0)) {
        int j = -1;
        for (int i = 0; i < nheld; i++) if (held[i] == d1) j = i;
        if (j < 0) return;
        bool top = j == nheld - 1;
        for (int i = j; i < nheld - 1; i++) held[i] = held[i + 1];
        nheld--;
        if (!top) return;
        if (nheld > 0) { cmd_t cm = { C_ON, held[nheld - 1], 100, 1 }; bcmdq_push(&cmds, &cm); }
        else { cmd_t cm = { C_OFF, 0, 0, 0 }; bcmdq_push(&cmds, &cm); }
    } else if (k == 0xB0 && d1 == 123) { nheld = 0; cmd_t cm = { C_OFF, 0, 0, 0 }; bcmdq_push(&cmds, &cm); }
}

static void changed(int p) { (void)p; }        // everything is read per block or per tick: safe before init

static int state(void)
{
    if (rec_on) return LOOP_REC;
    bool any = follow_on;
    for (int i = 0; i < nsteps && !any; i++) if (st_vel[i]) any = true;
    if (!any) return muted ? LOOP_OFF : LOOP_NONE;
    return muted ? LOOP_OFF : LOOP_ON;
}

// ---------------------------------------------------------------- the scene
static const char *const romans[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char *const romans_lc[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };

static void chord_text(int degree, char *num, int nn, char *name, int nm)
{
    int third = song.chord_n > 1 ? song.chord[1] - song.chord[0] : 4;
    int fifth = song.chord_n > 2 ? song.chord[2] - song.chord[0] : 7;
    bool minor = third == 3;
    snprintf(num, (size_t)nn, "%s%s", minor ? romans_lc[degree % 7] : romans[degree % 7], fifth == 6 ? "-" : "");
    char rn[8]; ui_note_name(song.chord_n ? song.chord[0] : 36, rn, sizeof rn);
    int l = (int)strlen(rn); while (l > 0 && (rn[l - 1] >= '0' && rn[l - 1] <= '9')) rn[--l] = 0;
    snprintf(name, (size_t)nm, "%s%s", rn, fifth == 6 ? " DIM" : minor ? " MIN" : "");
}

static void cone(int cx, int cy, float pmp, float level, uint32_t frame)
{
    int j = level > 0.05f && (frame & 1) ? 1 : 0;           // the cone shivers while a note sounds
    float s = 1.0f + 0.16f * pmp;
    oled_circle(cx, cy, 13, true);                           // the frame
    oled_circle(cx, cy, 12, true);
    oled_circle(cx, cy, (int)lroundf(10.0f * s) - j, true);  // the surround
    oled_circle(cx, cy, (int)lroundf(6.5f * s), true);       // the cone
    oled_disc(cx, cy, (int)lroundf(3.0f * s), true);         // the dust cap
    for (int k = 0; k < 4; k++) {                            // screws
        float a = 0.785398f + (float)k * 1.570796f;
        oled_pixel(cx + (int)lroundf(12.6f * cosf(a)), cy + (int)lroundf(12.6f * sinf(a)), false);
    }
}

static void draw(const view_t *v)
{
    int mode = (int)P(B_MODE);
    float dt = 0.04f;
    pump *= 0.80f;
    parts_step(&ps, dt, 120.0f, 1.0f);
    float lvl = v_level;
    // ---- the woofer, left
    cone(20, 31, pump, lvl, v->frame);
    parts_draw(&ps);
    if (muted) { oled_rrect(4, 27, 33, 9, 2, false); oled_ftext_c(F_TINY, 20, 29, "MUTED", 1); oled_rframe(4, 27, 33, 9, 2); }
    // ---- KNOCK: the note ladder the tilt walks (flat = the root)
    int tx = 40;
    if (mode == M_KNOCK) {
        for (int k = 0; k < 10; k++) {
            int y = 44 - k * 3;
            if (k == ladder) oled_rect(37, y - 1, 5, 3, true);
            else oled_rect(38 + (k == 3 ? 0 : 1), y, k == 3 ? 3 : 2, 1, true);
        }
        tx = 45;
    }
    // ---- the mode, the note, the chord
    int px = oled_pill(F_SMALL, tx, 17, mode_l[mode], mode == M_FOLLOW && (follow_on || a_live));
    const char *sub = mode == M_FOLLOW ? pat_l[(int)P(B_PAT)] : mode == M_KNOCK ? "TILT" : "SING LOW";
    oled_ftext_ex(F_TINY, tx + px + 3, 19, sub, 1);
    char nm[8] = "-";
    int shown = note_shown >= 0 ? note_shown : -1;
    if (mode == M_KNOCK && shown < 0) shown = knock_note;
    if (shown >= 0) ui_note_name(shown, nm, sizeof nm);
    oled_ftext_ex(F_MED, tx, 28, nm, 2);
    if (mode == M_HUM && hum_note >= 0) {                    // what you sang, and where it went
        char sg[12], b[20]; ui_note_name((int)lroundf(hum_m), sg, sizeof sg);
        snprintf(b, sizeof b, "YOU %s " S_DOWN, sg);
        oled_ftext_ex(F_TINY, tx + 30, 30, b, 1);
    }
    if (flash_ms > 0) oled_invert(tx - 1, 27, oled_ftext_w(F_MED, nm, 2) + 2, 11);
    char num[8], cn[12];
    chord_text(song.degree, num, sizeof num, cn, sizeof cn);
    oled_ftext_r(F_MED, OLED_W - 2, 28, num, 1);
    oled_ftext_r(F_TINY, OLED_W - 2, 38, cn, 1);
    // ---- the chord ladder leaning walks: iii ii IV I V vi vii
    if (P(B_LEAN) >= 1.0f) {
        int x0 = tx, cur = -1;
        for (int k = 0; k < 7; k++) if (lean_deg[k] == song.degree) cur = k;
        for (int k = 0; k < 7; k++) {
            int x = x0 + k * 7;
            if (k == cur) oled_rrect(x, 40, 5, 5, 1, true);
            else if (k == 3) oled_rframe(x, 40, 5, 5, 1);
            else oled_rect(x + 2, 42, 1, 1, true);
        }
    }
    // ---- the loop: its notes on a lane, the playhead (the bottom rows: the core may cover them)
    if (!v->cover) {
        oled_hline_pat(0, 47, OLED_W, 0x11, true);
        int ns = nsteps;
        if (ns > 0) {
            for (int s = 0; s < ns; s++) {
                if (!st_vel[s]) continue;
                int x = 2 + s * 124 / ns, w = st_len[s] * 124 / ns; if (w < 1) w = 1;
                int y = 61 - clampi(((int)st_note[s] - 26) * 11 / 30, 0, 11);
                oled_rect(x, y, w, (st_flag[s] & F_ACCENT) ? 2 : 1, true);
            }
            float b = song_beat(audio_frames()) * 4.0f;
            int x = 2 + (int)(b * 124.0f / (float)ns) % 124;
            oled_rect(x, 49, 1, 14, true);
            if (rec_on && ((v->frame >> 3) & 1)) oled_pill(F_TINY, 1, 49, "REC", true);
        } else if (rec_on) {
            oled_pill(F_SMALL, 2, 51, "REC", true);
            char b[16]; snprintf(b, sizeof b, "%.1fS", (double)((float)(audio_frames() - rec_start) / (float)AUDIO_SR));
            oled_ftext_ex(F_SMALL, 30, 53, b, 1);
        } else oled_ftext_c(F_TINY, 64, 55, mode == M_FOLLOW ? "LEAN: CHORD   HOLD REC: LOOP" : mode == M_KNOCK ? "KNOCK THE BOX   TILT: NOTE" : "HUM A LOW NOTE", 1);
    }
}

static void icon(int x, int y, bool lit)
{
    (void)lit;
    oled_circle(x + 6, y + 4, 4, true);
    oled_disc(x + 6, y + 4, 1, true);
    oled_pixel(x + 1, y + 8, true); oled_pixel(x + 11, y + 8, true);
}

const layer_t layer_bass = {
    .name = "BASS", .letter = 'b', .pages = pages, .npages = 2, .params = params, .nparams = B_N,
    .events = true, .init = init, .audio = audio, .tick = tick, .draw = draw, .icon = icon,
    .changed = changed, .clear = clear, .midi_in = midi_in, .state = state,
};

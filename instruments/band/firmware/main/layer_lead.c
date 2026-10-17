// LEAD: the band's lead synth. One voice (it slides when notes join), eight sounds, played three ways while the
// layer is live (MODE):
//   HUM    sing or hum: it follows your pitch, folded into the lead's range (G3..F#5) and pulled to the song's
//          scale (SNAP: at 100 % it lands on the notes, less lets your slides and wobbles through)
//   TILT   the lean picks a note on a ladder of the song's scale around the chord (flat = the chord's root);
//          a knock on the box plays it, how hard = how loud
//   ARP    it plays by itself: the song's chord broken up on the song's grid (UP, DOWN, UP-DOWN, RANDOM, OCTAVES
//          and two riffs) at RATE; tipping the bottom edge down widens it by octaves (up: an octave lower),
//          leaning sideways thins it out (left) or fills it in (right). Once it has played a second while live it
//          keeps going when another layer is live (the band's keyboard player); tap REC mutes it
// The stick: X = the filter, up = an octave up, down = bends the note down (two semitones at the bottom).
// In HUM and TILT the sideways lean opens and closes the filter.
// REC: hold = record (or add) what it plays to its loop, on twelfths of a beat (sixteenths and triplets; a sung
// or knocked note lands on the nearest sixteenth); tap = mute / unmute. Loop notes come first; ARP fills the steps
// the loop leaves empty. A recorded note is kept as steps of the scale above the chord's root (and a semitone
// when it was off the scale), so a line recorded over one chord moves with the chord when it changes (the bass's
// lean, a linked board's live chord) and stays in key; over the chords it was recorded on it plays as played.
#include "band.h"
#include "ui.h"
#include "spsc.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"           // the audio path in IRAM (IRAM_ATTR), its tables in DRAM; the screen and HUM stay in flash
#else
#define IRAM_ATTR
#define DRAM_ATTR
#endif
// The voice's loop at -O2: -O3 copies it once per sound and filter (loop unswitching), 12 KB of IRAM for a few
// percent of speed that the budget does not need (the host's cost check runs at -O2 too).
#define LEAN_CODE __attribute__((optimize("O2")))

// ---------------------------------------------------------------- params
enum { LD_MODE, LD_PAT, LD_RATE, LD_RANGE, LD_GATE, LD_OCT, LD_SNAP, LD_SENSE,
       LD_SOUND, LD_COLOR, LD_CUT, LD_RES, LD_ENV, LD_ATK, LD_DEC, LD_GLIDE, LD_VIB, LD_DRIVE, LD_FX, LD_N };
enum { M_HUM, M_TILT, M_ARP };
enum { PT_UP, PT_DOWN, PT_UPDOWN, PT_RANDOM, PT_OCTAVES, PT_RIFF1, PT_RIFF2 };
enum { S_SAW, S_SQUARE, S_SYNC, S_SUPER, S_BELL, S_CHIP, S_WHISTLE, S_BRASS, S_N };
#define P(i) LP(L_LEAD, i)

static const char *const pages[2] = { "LEAD", "LEAD SOUND" };
static const char *const mode_l[] = { "HUM", "TILT", "ARP" };
static const char *const pat_l[] = { "UP", "DOWN", "UP-DOWN", "RANDOM", "OCTAVES", "RIFF 1", "RIFF 2" };
static const char *const rate_l[] = { "1/8", "1/16", "1/8T" };
static const char *const sound_l[] = { "SAW", "SQUARE", "SYNC", "SUPERSAW", "FM BELL", "CHIP", "WHISTLE", "BRASS" };
static const param_t params[LD_N] = {
    [LD_MODE]  = { "MODE", "lmod", 0, PK_ENUM, 0, 0, 2, 1, M_ARP, "", mode_l, "HUM IT, TILT IT, OR IT PLAYS" },
    [LD_PAT]   = { "PATTERN", "larp", 0, PK_ENUM, 0, 0, 6, 1, PT_UP, "", pat_l, "HOW ARP PLAYS THE CHORD" },
    [LD_RATE]  = { "RATE", "lrat", 0, PK_ENUM, 0, 0, 2, 1, 0, "", rate_l, "HOW FAST ARP PLAYS" },
    [LD_RANGE] = { "RANGE", "lrng", 0, PK_NUM, PF_INT, 1, 3, 1, 1, "oct", NULL, "OCTAVES ARP CLIMBS" },
    [LD_GATE]  = { "GATE", "lgat", 0, PK_NUM, PF_INT, 10, 100, 5, 55, "%", NULL, "HOW LONG EACH ARP NOTE IS" },
    [LD_OCT]   = { "OCTAVE", "loct", 0, PK_NUM, PF_INT | PF_SIGNED, -2, 2, 1, 0, "", NULL, "THE WHOLE LEAD UP OR DOWN" },
    [LD_SNAP]  = { "SNAP", "lsnp", 0, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "HUM: PULLED TO THE SCALE" },
    [LD_SENSE] = { "SENSE", "lsns", 0, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "TILT: THE SOFTEST KNOCK" },
    [LD_SOUND] = { "SOUND", "lsnd", 1, PK_ENUM, 0, 0, S_N - 1, 1, S_SAW, "", sound_l, "THE LEAD ITSELF" },
    [LD_COLOR] = { "COLOUR", "ltim", 1, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "EACH SOUND'S OWN CHARACTER" },
    [LD_CUT]   = { "CUTOFF", "lflt", 1, PK_NUM, PF_LOG, 150, 9000, 1.08f, 1600, "Hz", NULL, "THE FILTER: DARK TO BRIGHT" },
    [LD_RES]   = { "RESONANCE", "lres", 1, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "THE FILTER'S PEAK" },
    [LD_ENV]   = { "ENVELOPE", "lenv", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW FAR EACH NOTE OPENS IT" },
    [LD_ATK]   = { "ATTACK", "latk", 1, PK_NUM, PF_LOG, 0.002f, 1.0f, 1.15f, 0.004f, "s", NULL, "A SOFT OR SHARP START" },
    [LD_DEC]   = { "DECAY", "ldec", 1, PK_NUM, PF_LOG, 0.05f, 3.0f, 1.12f, 0.35f, "s", NULL, "HOW FAST A NOTE SETTLES" },
    [LD_GLIDE] = { "GLIDE", "lgld", 1, PK_NUM, 0, 0, 0.5f, 0.01f, 0.04f, "s", NULL, "SLIDE ON JOINED NOTES" },
    [LD_VIB]   = { "VIBRATO", "lvib", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "WOBBLE ON HELD NOTES" },
    [LD_DRIVE] = { "DRIVE", "ldrv", 1, PK_NUM, PF_INT, 0, 100, 5, 10, "%", NULL, "GRIT" },
    [LD_FX]    = { "ECHO SEND", "lfx ", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "INTO THE SHARED ECHO" },
};

// each sound: how far it settles, how far the envelope opens the filter, its poles (0: no filter), its level,
// how long its release is (x DECAY), a filter swell (brass)
typedef struct { float sus, fdepth, lvl, rel, fswell; uint8_t poles; } snd_t;
static const DRAM_ATTR snd_t snds[S_N] = {
    [S_SAW]     = { 0.55f, 3.2f, 0.40f, 0.25f, 0.0f, 2 },
    [S_SQUARE]  = { 0.65f, 2.6f, 0.40f, 0.25f, 0.0f, 2 },
    [S_SYNC]    = { 0.70f, 1.8f, 0.48f, 0.25f, 0.0f, 1 },
    [S_SUPER]   = { 0.80f, 2.2f, 0.38f, 0.35f, 0.0f, 2 },
    [S_BELL]    = { 0.00f, 0.6f, 0.46f, 1.00f, 0.0f, 1 },
    [S_CHIP]    = { 0.85f, 0.0f, 0.40f, 0.15f, 0.0f, 0 },
    [S_WHISTLE] = { 0.90f, 0.4f, 0.48f, 0.30f, 0.0f, 1 },
    [S_BRASS]   = { 0.80f, 2.4f, 0.48f, 0.30f, 0.06f, 2 },
};

// ---------------------------------------------------------------- between the tasks
typedef struct { uint8_t kind, note, vel, legato; int16_t cents; } cmd_t;   // UI -> audio: live notes, HUM's bend
enum { C_ON, C_OFF, C_BEND };
enum { SRC_LIVE, SRC_ARP, SRC_LOOP };
typedef struct { uint32_t frame; uint8_t note, base, vel, on, src, flags; } ev_t;   // audio -> UI: what sounded
SPSC(lcmdq, cmd_t, 32)
SPSC(levq, ev_t, 64)
static lcmdq_t cmds;
static levq_t evs;

// The loop: a note per twelfth of a beat of the song's loop (sixteenths are every third step, eighth triplets
// every fourth). The UI writes (rel, chroma, len, flags, then vel last; vel 0 = empty), the audio task reads.
#define FINE 12                          // steps per beat
#define MAXSTEP 288                      // 24 beats: the longest loop (8 s) at 180 BPM
enum { F_ACCENT = 1, F_LEGATO = 2 };
static volatile uint8_t st_vel[MAXSTEP], st_len[MAXSTEP], st_flag[MAXSTEP], st_chroma[MAXSTEP];
static volatile int8_t  st_rel[MAXSTEP];
static volatile int nsteps;              // steps in the loop (song.beats * FINE); 0: no loop
static volatile int muted;               // tap REC: loop and ARP silent
static volatile int arp_on;              // ARP plays by itself
static volatile int a_live, a_mode;
static volatile int knock_note = 60;     // the note a knock plays now
static volatile int oct_add;             // the stick held up: +12
static volatile float stick_x, bend_st, lean_flt;   // the stick's filter, the stick's bend (semitones), the lean's filter
static volatile float arp_dens;          // -1 (thin) .. 1 (fill)
static volatile int   arp_oct;           // octaves the lean adds to ARP's range (-1 = an octave lower)
static volatile int model_req = S_SAW;
static volatile int arp_idx = -1, arp_n;  // for the scene: the note of ARP's list now, and how many there are
// audio -> UI
static volatile float v_level, v_pitch, v_mic_env, v_thr;

// ---------------------------------------------------------------- the voice
#define YR 2048
static float *yring;                      // HUM: the mic at 8 kHz (PSRAM)
static volatile uint32_t yring_w;
static struct {
    float pitch, target;                  // MIDI
    float bend, bend_t;                   // HUM's pull towards the sung pitch (semitones), slewed
    float fall;                           // BRASS: the note starts a little flat
    int   gate;                           // samples left with the gate on; -1: until C_OFF
    float amp; int stage;                 // 0 idle, 1 attack, 2 decay / sustain, 3 release
    float fenv, swell;                    // filter envelope; BRASS's swell (rises, then fenv decays)
    float vel, acc;
    float ph[5], mph, ph_sl;              // oscillators, FM's modulator, SYNC's slave
    float pwm_ph, vib_ph; int held;       // PWM's LFO, the vibrato's, samples since the note began
    float pw;                             // SQUARE's and CHIP's pulse width, slewed (a step in it would click)
    float nz;                             // WHISTLE's breath
    float s1a, s1b, s2a, s2b;             // two SVF stages
    int   model; float duck; int duck_dir;
    uint32_t seed;
} V;
static float out_env, bleed_k = 0.3f;
static float env_f, env_s, floor_e = 0.01f, a_f, r_f, a_s, r_s;   // TILT's knock detector
static int   refract;
static float ylp1, ylp2; static int ydec;

static inline float tan_pi(float x)      // tan(pi x) for x in 0..0.28 (fc up to 9 kHz), no libm
{
    float t = 3.14159265f * x, t2 = t * t;
    return t * (1.0f + t2 * (0.33333333f + t2 * (0.13333333f + t2 * 0.05396825f)));
}
static inline float midi_to_hz(float m) { return 440.0f * fast_exp2((m - 69.0f) * (1.0f / 12.0f)); }

static IRAM_ATTR __attribute__((noinline)) void voice_on(int note, int vel, bool legato, bool accent)
{
    int n = note + oct_add;
    n = clampi(n, 24, 108);
    bool tie = legato && V.stage > 0 && V.stage < 3;
    V.target = (float)n;
    if (!tie) { V.pitch = V.target; V.held = 0; }         // a new note jumps; joined notes glide
    V.vel = (float)vel / 127.0f;
    V.acc = accent ? 1.0f : 0.0f;
    if (!tie) {
        V.stage = 1;                                      // attack from wherever the level is: no click
        V.fenv = 1.0f; V.swell = snds[V.model].fswell > 0 ? 0.0f : 1.0f;
        V.fall = V.model == S_BRASS ? -0.35f : 0.0f;
    } else V.fenv = V.fenv < 0.6f ? 0.6f : V.fenv;       // a slide nudges the filter, no new attack
    V.gate = -1;
}
static IRAM_ATTR void voice_off(void) { if (V.stage == 1 || V.stage == 2) V.stage = 3; }

static IRAM_ATTR __attribute__((noinline)) void report(uint32_t frame, int note, int base, int vel, bool on, int src, int flags)
{
    ev_t e = { frame, (uint8_t)clampi(note, 0, 127), (uint8_t)clampi(base, 0, 127), (uint8_t)vel, (uint8_t)on, (uint8_t)src, (uint8_t)flags };
    levq_push(&evs, &e);
}

// What only the UI needs lives in PSRAM: HUM's YIN buffers, the first take's notes until the loop exists, the
// loop as it was when REC went down (a tap puts it back), the scene's roll, HUM's trace, the particles.
static float *yin_buf, *yd;
#define NPEND 192
typedef struct { uint8_t vel, len, flag, chroma; int8_t rel; } bk_t;
static ev_t *pend; static bk_t *bk;
typedef struct { float t0, t1; uint8_t note, vel, src; bool open; } roll_t;
#define NROLL 32
#define NTRACE 128
static roll_t *roll; static float *trace; static parts_t *ps;
static void init(void)
{
    yring = app_alloc(YR * sizeof(float));
    yin_buf = app_alloc((256 + 100) * sizeof(float)); yd = app_alloc(101 * sizeof(float));
    pend = app_alloc(NPEND * sizeof(ev_t)); bk = app_alloc(MAXSTEP * sizeof(bk_t));
    roll = app_alloc(NROLL * sizeof(roll_t)); trace = app_alloc(NTRACE * sizeof(float)); ps = app_alloc(sizeof(parts_t));
    if (!yin_buf || !yd || !pend || !bk || !roll || !trace || !ps) app_log("lead: short of memory");
    lcmdq_clear(&cmds); levq_clear(&evs);
    a_f = onepole_s(0.00015f); r_f = onepole_s(0.005f); a_s = onepole_s(0.015f); r_s = onepole_s(0.100f);   // at 16 kHz: 0.3, 10, 30, 200 ms
    V.pitch = V.target = 60; V.pw = 0.5f; V.seed = 0x13572468u; V.model = model_req = (int)P(LD_SOUND); V.duck = 1.0f;
    if (!yring) app_log("lead: short of memory for HUM");
}

// ---------------------------------------------------------------- ARP: the band's keyboard player
static IRAM_ATTR uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
static const DRAM_ATTR int fine_per[3] = { 6, 3, 4 };     // fine steps per ARP step: 1/8, 1/16, 1/8T

// ARP's notes now: the chord's tones from middle C up, over RANGE octaves (plus the lean's), and the top root
static IRAM_ATTR __attribute__((noinline)) int arp_list(int *L)
{
    int nc = song.chord_n > 0 ? song.chord_n : 3;
    if (nc > 4) nc = 4;
    int r = clampi((int)P(LD_RANGE) + (arp_oct > 0 ? arp_oct : 0), 1, 4);
    int base = 24 + 12 * (int)P(LD_OCT) + (arp_oct < 0 ? -12 : 0);
    int n = 0;
    for (int o = 0; o < r; o++) for (int k = 0; k < nc; k++) L[n++] = (song.chord_n ? song.chord[k] : 36 + 4 * k) + base + 12 * o;
    L[n++] = (song.chord_n ? song.chord[0] : 36) + base + 12 * r;
    return n;
}

// ARP's step k (ARP steps since loop position 0, or since beat 0): a note, or -1
static IRAM_ATTR __attribute__((noinline)) int arp_step(uint32_t k, int *vel, int *idx)
{
    int L[24], N = arp_list(L);
    int pat = (int)P(LD_PAT), i;
    switch (pat) {
    default:
    case PT_UP: i = (int)(k % (uint32_t)N); break;
    case PT_DOWN: i = N - 1 - (int)(k % (uint32_t)N); break;
    case PT_UPDOWN: { int per = N > 1 ? 2 * N - 2 : 1; int j = (int)(k % (uint32_t)per); i = j < N ? j : per - j; break; }
    case PT_RANDOM: i = (int)(hash32(k * 0x9e3779b9u + 77u) % (uint32_t)N); break;
    case PT_OCTAVES: {                                   // each chord tone, then its octave
        int nc = song.chord_n > 0 ? song.chord_n : 3, j = (int)(k % (uint32_t)(2 * nc));
        i = clampi(j / 2 + ((j & 1) ? nc : 0), 0, N - 1);
        break;
    }
    case PT_RIFF1: { static const DRAM_ATTR int8_t r1[8] = { 0, 2, 1, 2, 0, 2, 1, 3 }; i = r1[k % 8u] % N; break; }      // broken chords
    case PT_RIFF2: { static const DRAM_ATTR int8_t r2[8] = { 0, 1, 2, 3, 4, 3, 2, 1 }; i = r2[k % 8u] % N; break; }      // up and back
    }
    *idx = i;
    *vel = k % (uint32_t)(FINE / fine_per[(int)P(LD_RATE)]) == 0 ? 112 : 92;
    return L[i];
}

// ---------------------------------------------------------------- audio
typedef struct { int at, note, base, vel, gate, flags, src; } trig_t;
typedef struct { float cut, res, drv, fx, ea, col; } lctl_t;
static lctl_t c0, c1;
static int    nblk = AUDIO_BLOCK;
static float  sus_lvl, dk, fk, rel_k, att_k, gk, blk_pk, vib_depth;
static volatile int hum_voiced;

typedef struct { float a1, a2, a3, b1, b2, b3, comp; } fco_t;
static IRAM_ATTR void fco(fco_t *c, float cut, float rs)
{
    cut = clampf(cut, 30.0f, 9000.0f);
    float g = tan_pi(cut / (float)AUDIO_SR);
    float k = 2.0f - 1.85f * rs, k2 = 1.41f;          // the resonant stage, then a plain one (24 dB)
    c->a1 = 1.0f / (1.0f + g * (g + k)); c->a2 = g * c->a1; c->a3 = g * c->a2;
    c->b1 = 1.0f / (1.0f + g * (g + k2)); c->b2 = g * c->b1; c->b3 = g * c->b2;
    c->comp = 1.0f + 0.6f * rs;
}
static inline float at_(float x0, float x1, int i) { return x0 + (x1 - x0) * (float)i / (float)nblk; }
static const DRAM_ATTR float super_sp[5] = { 0.0f, -0.011f, 0.011f, -0.0255f, 0.0255f };   // SUPERSAW's spread at full COLOUR

// a pulse of width w (0..1) from two saws, band-limited
static inline float pulse(float ph, float w, float dt)
{
    float p2 = ph + 1.0f - w; if (p2 >= 1.0f) p2 -= 1.0f;
    return saw_blep(ph, dt) - saw_blep(p2, dt);
}

static IRAM_ATTR __attribute__((noinline)) LEAN_CODE void render(float *dry, float *send, int a, int b, uint32_t f0)
{
    int n = b - a;
    if (n <= 0) return;
    const snd_t *S = &snds[V.model];
    float keyA = fast_exp2((V.pitch - 69.0f) * (1.0f / 24.0f));      // the filter follows the note half way
    float feA = V.fenv * V.swell, feB = V.fenv * powf(fk, (float)n) * fminf(1.0f, V.swell + (float)n * (S->fswell > 0 ? 1.0f / (S->fswell * (float)AUDIO_SR) : 1.0f));
    float cutA = at_(c0.cut, c1.cut, a) * keyA, cutB = at_(c0.cut, c1.cut, b) * keyA;
    float resA = at_(c0.res, c1.res, a), resB = at_(c0.res, c1.res, b);
    float eaA = at_(c0.ea, c1.ea, a), eaB = at_(c0.ea, c1.ea, b);
    fco_t A, B;
    fco(&A, cutA * fast_exp2(eaA * S->fdepth * feA + 0.8f * V.acc * feA), resA);
    fco(&B, cutB * fast_exp2(eaB * S->fdepth * feB + 0.8f * V.acc * feB), resB);
    float inv = 1.0f / (float)n;
    float a1 = A.a1, a2 = A.a2, a3 = A.a3, b1 = A.b1, b2 = A.b2, b3 = A.b3, cm = A.comp;
    float da1 = (B.a1 - A.a1) * inv, da2 = (B.a2 - A.a2) * inv, da3 = (B.a3 - A.a3) * inv, dcm = (B.comp - A.comp) * inv;
    float db1 = (B.b1 - A.b1) * inv, db2 = (B.b2 - A.b2) * inv, db3 = (B.b3 - A.b3) * inv;
    float drA = at_(c0.drv, c1.drv, a), drB = at_(c0.drv, c1.drv, b);
    float gd = 1.0f + 3.0f * drA, dgd = 3.0f * (drB - drA) * inv;
    float lvA = S->lvl / (1.0f + 1.2f * drA), lvB = S->lvl / (1.0f + 1.2f * drB);
    float vel = 0.6f + 0.4f * V.vel;
    float iv = lvA * vel, div_ = (lvB - lvA) * vel * inv;
    float fx = at_(c0.fx, c1.fx, a), dfx = (at_(c0.fx, c1.fx, b) - fx) * inv;
    float col = at_(c0.col, c1.col, a), dcol = (at_(c0.col, c1.col, b) - col) * inv;
    float ea = eaA, dea = (eaB - eaA) * inv;
    const float duck_step = 1.0f / (0.008f * (float)AUDIO_SR);
    const float vib_inc = 5.4f / (float)AUDIO_SR, vib_on = 0.22f * (float)AUDIO_SR, vib_ramp = 1.0f / (0.45f * (float)AUDIO_SR);
    const float swell_inc = S->fswell > 0 ? 1.0f / (S->fswell * (float)AUDIO_SR) : 1.0f;
    int poles = S->poles;
    float dt = 0.0f, sdt[5] = { 0 };
    const float gk8 = 1.0f - powf(1.0f - gk, 8.0f);
    for (int i = 0; i < n; i++) {
        if (V.gate > 0 && --V.gate == 0) { voice_off(); report(f0 + (uint32_t)(a + i), (int)V.target, (int)V.target - oct_add, 0, false, SRC_ARP, 0); }
        switch (V.stage) {
        case 1: V.amp += att_k; if (V.amp >= 1.0f) { V.amp = 1.0f; V.stage = 2; } break;
        case 2: V.amp += dk * (sus_lvl - V.amp); break;
        case 3: V.amp -= rel_k * V.amp; if (V.amp < 1e-4f) { V.amp = 0; V.stage = 0; } break;
        }
        if (V.stage == 0) {                                // silent: only the filter states settle, a new sound is taken at once
            V.s1a *= 0.99f; V.s1b *= 0.99f; V.s2a *= 0.99f; V.s2b *= 0.99f;
            if (V.duck_dir) { V.model = model_req; V.duck = 1.0f; V.duck_dir = 0; }
            a1 += da1; a2 += da2; a3 += da3; b1 += db1; b2 += db2; b3 += db3; cm += dcm; gd += dgd; iv += div_; fx += dfx; col += dcol; ea += dea;
            continue;
        }
        V.fenv *= fk;
        if (V.swell < 1.0f) { V.swell += swell_inc; if (V.swell > 1.0f) V.swell = 1.0f; }
        if ((i & 7) == 0 || dt <= 0.0f) {                  // the pitch at control rate (every 8 samples): glide, HUM's bend,
            V.pitch += gk8 * (V.target - V.pitch);         // BRASS's fall, the stick's bend, the vibrato
            V.bend += 0.0316f * (V.bend_t - V.bend);
            V.fall *= 0.99362f;
            V.vib_ph = wrap1(V.vib_ph + 8.0f * vib_inc);
            float ramp = V.held > (int)vib_on ? fminf(1.0f, ((float)V.held - vib_on) * vib_ramp) : 0.0f;
            V.held += 8;
            float p = V.pitch + V.bend + V.fall - bend_st + vib_depth * ramp * sin_turns(V.vib_ph);
            dt = midi_to_hz(p) * (1.0f / (float)AUDIO_SR);
            if (dt > 0.45f) dt = 0.45f;
            if (V.model == S_SUPER) for (int k = 0; k < 5; k++) sdt[k] = dt * (1.0f + super_sp[k] * (0.15f + 0.85f * col));
        }
        float y;
        switch (V.model) {
        default:
        case S_SAW: {                                      // a saw and a second one a little apart (COLOUR)
            V.ph[0] += dt; if (V.ph[0] >= 1.0f) V.ph[0] -= 1.0f;
            float d2 = dt * (1.0f + 0.0045f * col);
            V.ph[1] += d2; if (V.ph[1] >= 1.0f) V.ph[1] -= 1.0f;
            y = 0.7f * saw_blep(V.ph[0], dt) + 0.5f * col * saw_blep(V.ph[1], d2);
            break;
        }
        case S_SQUARE: {                                   // a pulse, narrower with COLOUR, swept slowly
            V.ph[0] += dt; if (V.ph[0] >= 1.0f) V.ph[0] -= 1.0f;
            V.pwm_ph += 0.7f / (float)AUDIO_SR; if (V.pwm_ph >= 1.0f) V.pwm_ph -= 1.0f;
            float w = 0.5f - 0.36f * col + 0.1f * col * sin_turns(V.pwm_ph);
            V.pw += 0.003f * (w - V.pw);
            y = 0.62f * pulse(V.ph[0], V.pw, dt);
            break;
        }
        case S_SYNC: {                                     // a slave saw reset by the note: the envelope sweeps it
            float ratio = 1.0f + (1.0f + 3.5f * col) * (0.35f + 1.2f * V.fenv * ea);
            float ds = dt * ratio; if (ds > 0.45f) ds = 0.45f;
            V.ph[0] += dt;
            V.ph_sl += ds;
            float corr = 0.0f;
            if (V.ph[0] >= 1.0f) {                         // the master wraps: reset the slave, half a BLEP for the jump
                V.ph[0] -= 1.0f;
                float u = V.ph[0] / dt, before = V.ph_sl - u * ds;
                if (before >= 1.0f) before -= 1.0f;
                V.ph_sl = u * ds;
                float h = -2.0f * before;
                corr = 0.5f * h * (u + u - u * u - 1.0f);
            } else if (V.ph_sl >= 1.0f) V.ph_sl -= 1.0f;
            y = 0.62f * (saw_blep(V.ph_sl, ds) + corr);
            break;
        }
        case S_SUPER: {                                    // five saws spread by COLOUR
            float s = 0;
            for (int k = 0; k < 5; k++) {
                float ph = V.ph[k] + sdt[k];
                if (ph >= 1.0f) ph -= 1.0f;
                V.ph[k] = ph;
                s += saw_blep(ph, sdt[k]);
            }
            y = 0.3f * s;
            break;
        }
        case S_BELL: {                                     // two-operator FM, the modulator at 3.5x: a struck bell
            V.mph += dt * 3.5f; if (V.mph >= 1.0f) V.mph -= (float)(int)V.mph;
            float idx = (0.4f + 2.2f * col) * (0.25f + 0.75f * V.fenv);
            V.ph[0] += dt; if (V.ph[0] >= 1.0f) V.ph[0] -= 1.0f;
            y = 0.8f * sin_turns(V.ph[0] + idx * 0.16f * sin_turns(V.mph));
            break;
        }
        case S_CHIP: {                                     // a game console's pulse: 50, 25 or 12.5 % (COLOUR)
            V.ph[0] += dt; if (V.ph[0] >= 1.0f) V.ph[0] -= 1.0f;
            float w = col < 0.34f ? 0.5f : col < 0.67f ? 0.25f : 0.125f;
            V.pw += 0.003f * (w - V.pw);
            y = 0.5f * pulse(V.ph[0], V.pw, dt);
            break;
        }
        case S_WHISTLE: {                                  // a sine with breath on it (COLOUR)
            V.ph[0] += dt; if (V.ph[0] >= 1.0f) V.ph[0] -= 1.0f;
            float s0 = sin_turns(V.ph[0]);
            V.nz += 0.12f * (rng_bi(&V.seed) - V.nz);
            y = 0.75f * s0 + (0.05f + 0.6f * col) * V.nz * (0.6f + 0.4f * s0);
            break;
        }
        case S_BRASS: {                                    // two saws a few cents apart, the filter swells open
            float d1 = dt * (1.0f + 0.0018f + 0.0035f * col), d2 = dt * (1.0f - 0.0018f - 0.0035f * col);
            V.ph[0] += d1; if (V.ph[0] >= 1.0f) V.ph[0] -= 1.0f; V.ph[1] += d2; if (V.ph[1] >= 1.0f) V.ph[1] -= 1.0f;
            y = 0.45f * (saw_blep(V.ph[0], d1) + saw_blep(V.ph[1], d2));
            break;
        }
        }
        // the low pass: one or two SVF stages (none for CHIP)
        if (poles > 0) {
            float v3 = y - V.s1b, v1 = a1 * V.s1a + a2 * v3, v2 = V.s1b + a2 * V.s1a + a3 * v3;
            V.s1a = 2.0f * v1 - V.s1a; V.s1b = 2.0f * v2 - V.s1b;
            y = v2 * cm;
            if (poles > 1) {
                float w3 = y - V.s2b, w1 = b1 * V.s2a + b2 * w3, w2 = V.s2b + b2 * V.s2a + b3 * w3;
                V.s2a = 2.0f * w1 - V.s2a; V.s2b = 2.0f * w2 - V.s2b;
                y = w2;
            }
        }
        float u = clampf(y * gd, -1.5f, 1.5f);
        y = (u - 0.148148f * u * u * u) * iv * V.amp;
        if (V.duck_dir < 0) {                              // a new sound: down 8 ms, switch, up 8 ms
            V.duck -= duck_step;
            if (V.duck <= 0) { V.duck = 0; V.model = model_req; V.duck_dir = 1; V.s1a = V.s1b = V.s2a = V.s2b = 0; }
        } else if (V.duck_dir > 0) { V.duck += duck_step; if (V.duck >= 1.0f) { V.duck = 1.0f; V.duck_dir = 0; } }
        y *= V.duck;
        dry[a + i] += y;
        send[a + i] += y * fx;
        float ay = fabsf(y); if (ay > blk_pk) blk_pk = ay;
        a1 += da1; a2 += da2; a3 += da3; b1 += db1; b2 += db2; b3 += db3; cm += dcm; gd += dgd; iv += div_; fx += dfx; col += dcol; ea += dea;
    }
    V.s1a = flush_denormal(V.s1a); V.s1b = flush_denormal(V.s1b); V.s2a = flush_denormal(V.s2a); V.s2b = flush_denormal(V.s2b);
    V.nz = flush_denormal(V.nz);
}

static IRAM_ATTR __attribute__((noinline)) void apply(const trig_t *t, uint32_t f0)
{
    voice_on(t->note, t->vel, (t->flags & F_LEGATO) != 0, (t->flags & F_ACCENT) != 0);
    V.gate = t->gate;
    report(f0 + (uint32_t)t->at, t->note + oct_add, t->base, t->vel, true, t->src, t->flags);
}

// a loop step's note, from the chord now
static inline IRAM_ATTR int step_note(int st) { return scale_note(song.degree + st_rel[st], -1) + st_chroma[st]; }

static IRAM_ATTR LEAN_CODE void audio(const float *mic, float *dry, float *send, int n, bool live)
{
    uint32_t f0 = audio_frames();
    a_live = live;
    nblk = n;
    cmd_t c;
    while (lcmdq_pop(&cmds, &c)) {
        if (c.kind == C_ON) { voice_on(c.note, c.vel, c.legato, false); report(f0, c.note + oct_add, c.note, c.vel, true, SRC_LIVE, c.legato ? F_LEGATO : 0); }
        else if (c.kind == C_OFF) { if (V.stage == 1 || V.stage == 2) report(f0, (int)V.target, (int)V.target - oct_add, 0, false, SRC_LIVE, 0); voice_off(); }
        else V.bend_t = (float)c.cents * 0.01f;
    }
    float dec = P(LD_DEC);
    // the fine steps this block crosses: loop notes first, ARP fills the rest
    trig_t tr[10]; int ntr = 0;
    float fpf = song_frames_per_beat() * (1.0f / (float)FINE);
    bool arp = !muted && P(LD_MODE) == M_ARP && (arp_on || (live && a_mode == M_ARP));
    bool loop = !muted && nsteps > 0 && song.loop_len;
    if (arp || loop) {
        float b0 = song_beat(f0) * (float)FINE, b1 = song_beat(f0 + (uint32_t)n) * (float)FINE;
        float total = song.loop_len && song.beats ? (float)(song.beats * FINE) : (float)SONG_WRAP_BEATS * (float)FINE;
        float span = b1 - b0;
        if (span < 0) span += total;
        int rate = (int)P(LD_RATE), per = fine_per[rate];
        float gate_frac = P(LD_GATE) * 0.01f;
        if (span > 0 && span < 8.0f) {
            for (float k = floorf(b0) + 1.0f; k <= b0 + span + 1e-6f && ntr < 8; k += 1.0f) {
                int at = (int)((k - b0) / span * (float)n);
                if (at >= n) at = n - 1;
                float kk = k >= total ? k - total : k;
                uint32_t g = (uint32_t)(kk + 0.5f);
                int st = nsteps > 0 ? (int)(g % (uint32_t)nsteps) : -1;
                int note = -1, vel = 0, fl = 0, src = SRC_LOOP, gate = 0;
                if (loop && st >= 0 && st_vel[st]) {
                    note = step_note(st); vel = st_vel[st]; fl = st_flag[st];
                    gate = (fl & F_LEGATO) ? (int)((float)st_len[st] * fpf * 1.04f) : (int)((float)st_len[st] * fpf * 0.92f) + 32;
                } else if (arp) {
                    uint32_t ak = g / (uint32_t)per;
                    int idx = 0;
                    bool on_step = g % (uint32_t)per == 0;
                    float d = arp_dens;
                    if (on_step) {
                        uint32_t h = hash32(ak * 0x85ebca6bu + 991u);
                        if (d < 0 && (float)(h & 0xffff) / 65536.0f < -d * 0.75f && ak % 4u) continue;   // thinner (never the bar's first)
                        note = arp_step(ak, &vel, &idx);
                        if (rate == 1 && d > 0 && (ak & 1u) && (float)((h >> 16) & 0xffff) / 65536.0f < d * 0.6f) note += 12;   // 1/16 filled: jumps an octave
                        arp_idx = idx;
                        gate = (int)((float)per * fpf * gate_frac);
                        if (gate_frac >= 0.95f) { fl |= F_LEGATO; gate = (int)((float)per * fpf * 1.04f); }
                    } else if (d > 0 && rate != 1 && (int)(g % (uint32_t)per) == per / 2) {   // filled: a passing note between
                        uint32_t h = hash32(ak * 0x27d4eb2du + 13u);
                        if ((float)(h & 0xffff) / 65536.0f >= d) continue;
                        note = arp_step(ak + 1u, &vel, &idx);
                        vel = 76;
                        gate = (int)((float)(per / 2) * fpf * gate_frac);
                    } else continue;
                    if (vel >= 110) fl |= F_ACCENT;
                    src = SRC_ARP;
                }
                if (note < 0) continue;
                if (gate < 64) gate = 64;
                tr[ntr++] = (trig_t){ at, note, note, vel, gate, fl, src };
            }
        }
    }
    // the mic, while it is this layer's: a knock (TILT) plays the tilted note now; HUM's 8 kHz feed
    bool knock = live && a_mode == M_TILT, hum = live && a_mode == M_HUM && yring;
    if (knock || hum) {
        uint32_t yw = yring_w;
        float sense = 1.6f - 1.2f * P(LD_SENSE) * 0.01f;
        float speaker = host_out_env;
        for (int i = 0; i < n; i++) {
            float x = mic ? mic[i] : 0.0f;
            if (hum) { ylp1 += 0.33f * (x - ylp1); ylp2 += 0.33f * (ylp1 - ylp2); if (++ydec >= 4) { ydec = 0; yring[yw & (YR - 1)] = ylp2; yw++; } }
            if (i & 1) continue;                                 // the envelopes at 16 kHz
            x = fmaxf(fabsf(x), fabsf(mic ? mic[i + 1] : 0.0f));
            follow(&env_f, x, a_f, r_f);
            float slow_before = env_s;
            follow(&env_s, x, a_s, r_s);
            if (env_s < floor_e) floor_e += 0.01f * (env_s - floor_e); else floor_e += 0.000005f * (env_s - floor_e);
            float bleed = bleed_k * speaker, thr = (0.02f + 4.0f * floor_e) * sense + 0.4f * bleed;
            if (refract > 0) refract--;
            else if (knock && env_f - bleed > thr && env_f - bleed > 2.5f * (slow_before - bleed) + 0.005f && ntr < 10) {
                int vel = clampi((int)((env_f - bleed) / 0.3f * 127.0f), 30, 127);
                trig_t t = { i, knock_note, knock_note, vel, (int)(fmaxf(dec, 0.15f) * (float)AUDIO_SR), vel > 110 ? F_ACCENT : 0, SRC_LIVE };
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
    c1.cut += kb * (clampf(P(LD_CUT) * fast_exp2(2.0f * (stick_x + lean_flt)), 60.0f, 9000.0f) - c1.cut);
    c1.res += kb * (P(LD_RES) * 0.01f - c1.res);
    c1.drv += kb * (P(LD_DRIVE) * 0.01f - c1.drv);
    c1.fx  += kb * (P(LD_FX) * 0.01f - c1.fx);
    c1.ea  += kb * (P(LD_ENV) * 0.01f - c1.ea);
    c1.col += kb * (P(LD_COLOR) * 0.01f - c1.col);
    if (c0.cut <= 0) c0 = c1;                                   // the very first block
    const snd_t *S = &snds[V.model];
    sus_lvl = S->sus;
    att_k = 1.0f / (fmaxf(P(LD_ATK), 0.0015f) * (float)AUDIO_SR);
    dk = onepole_s((V.model == S_BELL ? 2.5f : 1.0f) * dec / 3.0f);
    fk = 1.0f - onepole_s(0.4f * dec / 3.0f + 0.02f);
    rel_k = onepole_s(fmaxf(0.04f, S->rel * dec) / 3.0f);
    gk = P(LD_GLIDE) > 0.002f ? onepole_s(P(LD_GLIDE) / 3.0f) : 1.0f;
    vib_depth = P(LD_VIB) * 0.006f * (V.model == S_WHISTLE ? 1.4f : 1.0f);   // up to 0.6 semitone
    // the voice, in segments between the triggers
    blk_pk = 0;
    int pos = 0;
    for (int k = 0; k < ntr; k++) { render(dry, send, pos, tr[k].at, f0); apply(&tr[k], f0); pos = tr[k].at; }
    render(dry, send, pos, n, f0);
    out_env = blk_pk > out_env ? blk_pk : out_env * 0.96f;
    if ((knock || hum) && !hum_voiced && host_out_env > 0.03f) {   // how much of the band the mic hears (learned, as KNOCK does)
        float r = env_s / host_out_env;
        bleed_k += (r < bleed_k ? 0.05f : 0.002f) * (r - bleed_k);
        bleed_k = clampf(bleed_k, 0.01f, 2.0f);
    }
    v_level = V.stage ? V.amp : 0.0f;
    v_pitch = V.pitch + V.bend;
    v_mic_env = env_s;
    v_thr = (0.02f + 4.0f * floor_e) + bleed_k * host_out_env;
}

// ---------------------------------------------------------------- UI: HUM's pitch tracker (the bass's YIN)
#define YW 256
#define TMAX 100                                            // 80 Hz at 8 kHz
#define TMIN 9                                              // 890 Hz
static float yin(float *conf)
{
    if (!yin_buf || !yd) { *conf = 0; return 0; }
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

static int snap_scale(float m)
{
    int best = (int)lroundf(m); float bd = 99;
    for (int d = -10; d <= 80; d++) {
        int nt = scale_note(d, -1);
        float e = fabsf((float)nt - m);
        if (e < bd) { bd = e; best = nt; }
        if ((float)nt > m + 12.0f) break;
    }
    return best;
}
// a note as steps of the scale above the chord's root (and the semitones left over)
static void note_to_rel(int note, int *rel, int *chroma)
{
    int d = -10;
    for (int k = -10; k <= 85; k++) { if (scale_note(k, -1) <= note) d = k; else break; }
    *rel = clampi(d - song.degree, -120, 120);
    *chroma = clampi(note - scale_note(d, -1), 0, 2);
}

// ---------------------------------------------------------------- UI
static int  ladder = 5;
static bool was_live;
static int  live_ms, hum_note = -1, hum_quiet_ms, hum_cand = -1, ytick, last_cents;
static float hum_m, hum_env0;
static bool rec_on, rec_first, retake;
static uint32_t rec_start;
static int npend;
static int open_step = -1; static uint32_t open_frame;
static uint32_t my_loop_len, my_loop_start;
static uint8_t held[8]; static int nheld;
static int midi_cur = -1;
// the scene
static int roll_w, trace_w;
static uint32_t pseed = 0x7a11e5u;
static int note_shown = -1, flash_ms;
static float view_lo = 58, view_hi = 74;               // the roll's pitch window, eased

static float now_s(void) { return (float)audio_frames() * (1.0f / (float)AUDIO_SR); }

// the step a note falls on: the nearest multiple of grid twelfths (3: a sixteenth) of the song's beat
static int quantize(uint32_t frame, int grid)
{
    if (!nsteps) return -1;
    float b = song_beat(frame) * (float)FINE;
    int s = grid * (int)floorf(b / (float)grid + 0.5f);
    return ((s % nsteps) + nsteps) % nsteps;
}
// sung, knocked and played notes land on sixteenths; ARP's on its own grid (eighths, sixteenths, triplets)
static int ev_grid(const ev_t *e) { return e->src == SRC_ARP ? fine_per[(int)P(LD_RATE)] : 3; }

static void write_step(int st, int note, int vel, int len, int flags)
{
    if (st < 0 || st >= MAXSTEP) return;
    int rel, chroma; note_to_rel(note, &rel, &chroma);
    st_vel[st] = 0;
    st_rel[st] = (int8_t)rel; st_chroma[st] = (uint8_t)chroma; st_len[st] = (uint8_t)clampi(len, 1, 255); st_flag[st] = (uint8_t)flags;
    __atomic_store_n(&st_vel[st], (uint8_t)clampi(vel, 1, 127), __ATOMIC_RELEASE);
}

static void clear_steps(void) { for (int i = 0; i < MAXSTEP; i++) st_vel[i] = 0; }
static void clear(void) { clear_steps(); arp_on = 0; npend = 0; rec_on = false; muted = 0; }

static int ev_len(uint32_t from, uint32_t to)
{
    float fpf = song_frames_per_beat() * (1.0f / (float)FINE);
    return (int)((float)(int32_t)(to - from) / fpf + 0.5f);
}

static void record(const ev_t *e)
{
    if (!rec_on || e->src == SRC_LOOP) return;
    if (rec_first) { if (pend && npend < NPEND) pend[npend++] = *e; return; }
    if (e->on) {
        int st = quantize(e->frame, ev_grid(e));
        int len = e->src == SRC_ARP ? fine_per[(int)P(LD_RATE)] : 3;
        write_step(st, e->base, e->vel, len, e->flags);
        open_step = st; open_frame = e->frame;
    } else if (open_step >= 0) {
        int len = ev_len(open_frame, e->frame);
        if (len >= 1) st_len[open_step] = (uint8_t)clampi(len, 1, 255);
        open_step = -1;
    }
}

static void first_loop_done(uint32_t end)
{
    if (!song.loop_len) {                                    // the loop starts at the first note: the silence before it is cut
        uint32_t s0 = rec_start;
        for (int i = 0; i < npend && pend; i++) if (pend[i].on) { s0 = pend[i].frame; break; }
        if ((int32_t)(s0 - rec_start) < 0 || (int32_t)(end - s0) < AUDIO_SR / 4) s0 = rec_start;
        host_first_loop(s0, end - s0);
    }
    nsteps = song.loop_len && song.beats ? clampi(song.beats * FINE, 1, MAXSTEP) : 0;
    my_loop_len = song.loop_len; my_loop_start = song.loop_start;
    clear_steps();
    open_step = -1;
    for (int i = 0; i < npend && pend; i++) {
        ev_t *e = &pend[i];
        if (!nsteps) break;
        if (e->on) {                                        // placed by its frame on the song's grid (signed: it may be before loop_start)
            int st = quantize(e->frame, ev_grid(e));
            write_step(st, e->base, e->vel, e->src == SRC_ARP ? fine_per[(int)P(LD_RATE)] : 3, e->flags);
            open_step = st; open_frame = e->frame;
        } else if (open_step >= 0) {
            int len = ev_len(open_frame, e->frame);
            if (len >= 1) st_len[open_step] = (uint8_t)clampi(len, 1, 255);
            open_step = -1;
        }
    }
    npend = 0;
}

static void push_cmd(int kind, int note, int vel, bool legato, int cents)
{
    cmd_t cm = { (uint8_t)kind, (uint8_t)clampi(note, 0, 127), (uint8_t)vel, (uint8_t)legato, (int16_t)cents };
    lcmdq_push(&cmds, &cm);
}

static void hum_tick(void)
{
    if (++ytick % 2) return;
    float conf, f = yin(&conf);
    bool voiced = conf > 0.75f && f > 70.0f && v_mic_env > v_thr * 0.5f + 0.004f;
    float m = 0;
    if (voiced) {
        m = 69.0f + 12.0f * log2f(f / 440.0f);
        // the lead itself in the mic: a reading on (an octave of) the note it plays is not a voice
        float dm = fmodf(fabsf(m - v_pitch) + 0.5f, 12.0f) - 0.5f;
        if (hum_note >= 0 && v_level > 0.05f && fabsf(dm) < 0.6f && v_mic_env < 0.45f * hum_env0) voiced = false;
    }
    if (trace) { trace[trace_w] = voiced ? m : 0.0f; trace_w = (trace_w + 1) % NTRACE; }
    if (voiced) {
        int cand = (int)lroundf(m);
        if (hum_cand >= 0 && abs(cand - hum_cand) <= 1) {
            hum_m = m;
            float lo = 55.0f + 12.0f * P(LD_OCT), hi = lo + 24.0f;   // G3..F#5, moved by OCTAVE
            float lm = m;
            while (lm >= hi) lm -= 12.0f;
            while (lm < lo) lm += 12.0f;
            int nn = snap_scale(lm);
            if (hum_note >= 0 && nn != hum_note && fabsf(lm - (float)hum_note) < fabsf(lm - (float)nn) + 0.35f) nn = hum_note;
            if (nn != hum_note) {
                hum_env0 = v_mic_env;
                int vel = clampi((int)(v_mic_env / 0.15f * 127.0f), 40, 127);
                push_cmd(C_ON, nn, vel, hum_note >= 0, 0);
                hum_note = nn;
                coach_done(LEARN_INST + 4);
            }
            // SNAP below 100 %: the rest of the way to what was sung, as a bend
            int cents = (int)lroundf((1.0f - P(LD_SNAP) * 0.01f) * (lm - (float)nn) * 100.0f);
            if (cents != last_cents) { push_cmd(C_BEND, 0, 0, false, cents); last_cents = cents; }
            hum_quiet_ms = 0;
        }
        hum_cand = cand;
    } else hum_cand = -1;
}

static void hum_stop(void)
{
    if (hum_note >= 0) { push_cmd(C_OFF, 0, 0, false, 0); hum_note = -1; }
    if (last_cents) { push_cmd(C_BEND, 0, 0, false, 0); last_cents = 0; }
    hum_voiced = 0;
}

static void tick(const ctl_t *c, bool live)
{
    int mode = (int)P(LD_MODE);
    a_mode = mode;
    if (P(LD_SOUND) != (float)model_req) model_req = (int)P(LD_SOUND);
    // the song's loop: steps follow its length; a new loop (or none) forgets ours. A loop start moved by a
    // frame or two (the link following another board's clock) is the same loop
    int32_t moved = my_loop_len ? (int32_t)(song.loop_start - my_loop_start) % (int32_t)my_loop_len : 0;
    if (song.loop_len != my_loop_len || (moved > 64 || moved < -64)) {
        if (!(rec_on && rec_first)) {
            if (my_loop_len) clear_steps();
            my_loop_len = song.loop_len; my_loop_start = song.loop_start;
            nsteps = song.loop_len && song.beats ? clampi(song.beats * FINE, 1, MAXSTEP) : 0;
        }
    } else my_loop_start = song.loop_start;
    if (live) {
        live_ms = was_live ? live_ms + 10 : 0;
        // TILT: the tilt picks a rung of the ladder (flat = the chord's root)
        if (c->has_imu) {
            float p = c->lean_y / 7.0f + 5.0f;
            if (p > (float)ladder + 0.65f) ladder++;
            if (p < (float)ladder - 0.65f) ladder--;
            ladder = clampi(ladder, 0, 12);
            // ARP: the bottom edge down widens it by octaves, up drops it an octave; sideways thins or fills it
            if (mode == M_ARP) {
                float o = c->lean_y / 14.0f;
                int ao = arp_oct;
                if (o > (float)ao + 0.7f) ao++;
                if (o < (float)ao - 0.7f) ao--;
                arp_oct = clampi(ao, -1, 2);
                float d = c->lean_x / 28.0f;
                arp_dens = fabsf(d) < 0.15f ? 0.0f : clampf(d, -1.0f, 1.0f);
                lean_flt = 0;
            } else lean_flt = clampf(c->lean_x / 40.0f, -0.8f, 0.8f);
        } else ladder = 5;
        knock_note = scale_note(song.degree - 5 + ladder, 4) + 12 * (int)P(LD_OCT);
        // the stick
        if (c->has_joy) {
            stick_x = c->jx;
            oct_add = c->jy > 0.6f ? 12 : 0;
            bend_st = c->jy < -0.25f ? clampf((-c->jy - 0.25f) / 0.7f, 0.0f, 1.0f) * 2.0f : 0.0f;
        }
        // ARP: the player joins after a second here with the menu closed
        if (mode == M_ARP && !c->menu_open && live_ms >= 1000 && !muted) arp_on = 1;
        if (mode != M_ARP) arp_on = 0;
        if (mode == M_HUM && yring) {
            hum_tick();
            hum_voiced = hum_note >= 0;
            if (hum_note >= 0 && hum_cand < 0 && (hum_quiet_ms += 10) >= 120) hum_stop();
        } else if (hum_note >= 0) hum_stop();
        // REC: hold = record (or add), tap = mute; held while muted = a new take (the old steps go at the press, and come
        // back if it was a tap)
        if (c->rec_press) {
            rec_on = true; rec_start = c->frames; rec_first = song.loop_len == 0; npend = 0; open_step = -1;
            if (bk) for (int i = 0; i < MAXSTEP; i++) bk[i] = (bk_t){ st_vel[i], st_len[i], st_flag[i], st_chroma[i], st_rel[i] };
            retake = muted && !rec_first && bk;
            if (retake) clear_steps();
        }
        if (retake && rec_on && c->rec && c->rec_ms >= 300) { retake = false; muted = 0; toast("NEW TAKE", 900); coach_done(LEARN_INST + 11); }
        if (c->rec_release && rec_on) {
            rec_on = false;
            retake = false;
            if (c->rec_ms < 300) {                           // a tap: nothing was meant to be recorded
                if (!rec_first && bk) for (int i = MAXSTEP - 1; i >= 0; i--) { st_rel[i] = bk[i].rel; st_chroma[i] = bk[i].chroma; st_len[i] = bk[i].len; st_flag[i] = bk[i].flag; st_vel[i] = bk[i].vel; }
                npend = 0;
                muted = !muted;
                if (muted) arp_on = 0;
                toast(muted ? "LEAD MUTED" : "LEAD ON", 900);
            } else {
                if (rec_first) first_loop_done(c->frames);
                coach_done(LEARN_INST + 5);
            }
        }
    } else {
        if (was_live) { hum_stop(); bend_st = 0; oct_add = 0; stick_x = 0; lean_flt = 0; }
        if (rec_on) { rec_on = false; if (rec_first && npend) first_loop_done(c->frames); }
        live_ms = 0;
    }
    was_live = live;
    // what sounded: MIDI, the loop, the scene
    ev_t e;
    while (levq_pop(&evs, &e)) {
        record(&e);
        if (midi_cur >= 0) { host_midi_note(L_LEAD, midi_cur, 0); midi_cur = -1; }
        float t = (float)e.frame * (1.0f / (float)AUDIO_SR);
        if (roll) for (int i = 0; i < NROLL; i++) if (roll[i].open) { roll[i].open = false; roll[i].t1 = t; }
        if (e.on) {
            host_midi_note(L_LEAD, e.note, e.vel); midi_cur = e.note;
            if (roll) { roll[roll_w] = (roll_t){ t, t, e.note, e.vel, e.src, true }; roll_w = (roll_w + 1) % NROLL; }
            note_shown = e.note;
            flash_ms = 100;
            if (live && ps) parts_burst(ps, 81, 30, (e.flags & F_ACCENT) ? 5 : 3, 35.0f, 0.4f, PART_DOT, &pseed);
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
        push_cmd(C_ON, d1, d2, legato, 0);
    } else if (k == 0x80 || (k == 0x90 && d2 == 0)) {
        int j = -1;
        for (int i = 0; i < nheld; i++) if (held[i] == d1) j = i;
        if (j < 0) return;
        bool top = j == nheld - 1;
        for (int i = j; i < nheld - 1; i++) held[i] = held[i + 1];
        nheld--;
        if (!top) return;
        if (nheld > 0) push_cmd(C_ON, held[nheld - 1], 100, true, 0);
        else push_cmd(C_OFF, 0, 0, false, 0);
    } else if (k == 0xB0 && d1 == 123) { nheld = 0; push_cmd(C_OFF, 0, 0, false, 0); }
}

static void changed(int p) { (void)p; }        // everything is read per block or per tick: safe before init

static int state(void)
{
    if (rec_on) return LOOP_REC;
    bool any = arp_on;
    for (int i = 0; i < nsteps && !any; i++) if (st_vel[i]) any = true;
    if (!any) return muted ? LOOP_OFF : LOOP_NONE;
    return muted ? LOOP_OFF : LOOP_ON;
}

static bool playing(void) { return arp_on && !muted && P(LD_MODE) == M_ARP; }

// ---------------------------------------------------------------- the scene
// Left, a piano roll running right to left: the scale's notes as dotted lines (the chord's denser), every note
// played as a bar, the one sounding now glowing at the head; in HUM what you sing runs as a thin trace that the
// bars snap to. Right, the mode, the note, the sound; in ARP the chord's tones as dots, the one playing big; in
// TILT the ladder the tilt walks. Below, the loop on a lane (when the core leaves the rows free).
#define RX0 1
#define RX1 80                                      // the roll's head (now)
#define RY0 17
#define RY1 44
#define ROLL_S 2.4f                                 // seconds across the roll

static int note_y(float m) { return RY1 - (int)lroundf((m - view_lo) / (view_hi - view_lo) * (float)(RY1 - RY0)); }

static void draw_waveform(int x, int y, int w, int h, float lvl, uint32_t frame)
{
    int model = V.model;
    float ph0 = (float)(frame % 50u) / 50.0f, amp = 0.25f + 0.75f * fminf(1.0f, lvl * 1.4f);
    float col = P(LD_COLOR) * 0.01f;
    int py = 0;
    for (int i = 0; i < w; i++) {
        float p = wrap1((float)i / (float)w * 2.0f + ph0), s;
        switch (model) {
        case S_SQUARE: s = p < 0.5f - 0.36f * col ? 1.0f : -1.0f; break;
        case S_CHIP: s = p < (col < 0.34f ? 0.5f : col < 0.67f ? 0.25f : 0.125f) ? 1.0f : -1.0f; break;
        case S_BELL: s = sin_turns(p + 0.3f * col * sin_turns(3.5f * p)); break;
        case S_WHISTLE: s = sin_turns(p) + 0.25f * col * rng_bi(&pseed); break;
        case S_SYNC: s = 2.0f * wrap1(p * (1.6f + 2.0f * col)) - 1.0f; break;
        case S_SUPER: s = 0.5f * (2.0f * p - 1.0f) + 0.5f * (2.0f * wrap1(p * 1.03f + 0.2f) - 1.0f); break;
        default: s = 2.0f * p - 1.0f; break;
        }
        int yy = y + h / 2 - (int)lroundf(s * amp * (float)(h / 2));
        if (i > 0) oled_line(x + i - 1, py, x + i, yy, true);
        py = yy;
    }
}

static void draw(const view_t *v)
{
    int mode = (int)P(LD_MODE);
    float now = now_s();
    if (!roll || !trace || !ps) return;
    parts_step(ps, 0.04f, 0.0f, 0.9f);
    // the window follows the notes of the last seconds (and the trace), never narrower than an octave and a half
    float lo = 999, hi = -999;
    for (int i = 0; i < NROLL; i++) if (roll[i].note && (roll[i].open || now - roll[i].t1 < ROLL_S)) { lo = fminf(lo, roll[i].note); hi = fmaxf(hi, roll[i].note); }
    if (mode == M_HUM) for (int i = 0; i < NTRACE; i++) if (trace[i] > 0) { lo = fminf(lo, trace[i]); hi = fmaxf(hi, trace[i]); }
    if (mode == M_TILT) { lo = fminf(lo, (float)knock_note); hi = fmaxf(hi, (float)knock_note); }
    if (lo > hi) { lo = 60; hi = 72; }
    float mid = 0.5f * (lo + hi), span = fmaxf(18.0f, hi - lo + 4.0f);
    view_lo += 0.15f * ((mid - 0.5f * span) - view_lo);
    view_hi += 0.15f * ((mid + 0.5f * span) - view_hi);
    // the scale's lines: chord tones denser
    int ilo = (int)ceilf(view_lo), ihi = (int)floorf(view_hi);
    for (int m = ilo; m <= ihi; m++) {
        int pc = ((m - song.key) % 12 + 12) % 12;
        bool in_scale = false, in_chord = false;
        for (int k = 0; k < song.scale_n && song.scale; k++) if (song.scale[k] == pc) in_scale = true;
        for (int k = 0; k < song.chord_n; k++) if ((song.chord[k] - m) % 12 == 0) in_chord = true;
        if (!in_scale) continue;
        int y = note_y((float)m);
        if (y < RY0 || y > RY1) continue;
        if (in_chord) oled_hline_pat(RX0, y, RX1 - RX0, 0x01, true);      // the chord's notes: faint lines
        oled_rect(RX1 - 2, y, 2, 1, true);                               // every note of the scale: a tick at the head
    }
    // HUM: what was sung
    if (mode == M_HUM) {
        int prevx = -1, prevy = 0;
        for (int k = 0; k < NTRACE; k++) {
            int i = (trace_w + k) % NTRACE;
            int x = RX1 - (int)((float)(NTRACE - 1 - k) * 0.02f / ROLL_S * (float)(RX1 - RX0));
            if (trace[i] <= 0 || x < RX0) { prevx = -1; continue; }
            int y = note_y(trace[i]);
            if (y < RY0 || y > RY1) { prevx = -1; continue; }
            if (prevx >= 0) oled_line(prevx, prevy, x, y, true); else oled_pixel(x, y, true);
            prevx = x; prevy = y;
        }
    }
    // the notes
    float pxs = (float)(RX1 - RX0) / ROLL_S;
    int head_y = -1;
    for (int i = 0; i < NROLL; i++) {
        const roll_t *r = &roll[i];
        if (!r->note) continue;
        float t1 = r->open ? now : r->t1;
        if (now - t1 > ROLL_S) continue;
        int x0 = RX1 - (int)((now - r->t0) * pxs), x1 = RX1 - (int)((now - t1) * pxs);
        if (x0 < RX0) x0 = RX0;
        if (x1 <= x0) x1 = x0 + 1;
        int y = note_y((float)r->note);
        if (y < RY0 + 1 || y > RY1 - 1) continue;
        bool loud = r->vel >= 110;
        if (mode == M_HUM && x1 - x0 > 2) oled_frame(x0, y - 1, x1 - x0, 3);      // HUM: the note as a box the trace runs through
        else {
            oled_rect(x0, y - 1, x1 - x0, loud ? 3 : 2, true);
            if (r->src == SRC_LOOP && x1 - x0 > 2) oled_rect(x0 + 1, y, x1 - x0 - 2, 1, false);   // loop notes hollow
        }
        if (r->open && v_level > 0.02f) head_y = y;
    }
    oled_line(RX1 + 1, RY0, RX1 + 1, RY1, true);                // now
    if (head_y >= 0) {
        int r = 2 + (int)(v_level * 1.6f) + (flash_ms > 0 ? 1 : 0);
        oled_disc(RX1 + 1, head_y, r, true);
        if (v->frame & 1) oled_pixel(RX1 + 1, head_y, false);
    }
    parts_draw(ps);
    // the right side
    int rx = 89;
    bool self = playing();
    int pw = oled_pill(F_SMALL, rx, 17, mode_l[mode], self);
    const char *sub = mode == M_ARP ? pat_l[(int)P(LD_PAT)] : mode == M_TILT ? "KNOCK" : "SING";
    if (rx + pw + 2 + oled_ftext_w(F_TINY, sub, 1) <= OLED_W) oled_ftext_ex(F_TINY, rx + pw + 2, 19, sub, 1);
    char nm[8] = "-";
    int shown = note_shown >= 0 && v_level > 0.01f ? note_shown : mode == M_TILT ? knock_note : -1;
    if (shown >= 0) ui_note_name(shown, nm, sizeof nm);
    oled_ftext_ex(F_MED, rx, 27, nm, 1);
    if (muted) { oled_rrect(rx - 1, 26, 40, 11, 2, false); oled_rframe(rx - 1, 26, 40, 11, 2); oled_ftext_c(F_TINY, rx + 19, 29, "MUTED", 1); }
    if (mode == M_ARP) {                                        // the chord's tones as ARP climbs them
        int L[24], N = arp_list(L), cur = arp_idx;
        if (N > 10) N = 10;
        int top = L[N - 1] > L[0] ? L[N - 1] - L[0] : 1;
        for (int k = 0; k < N; k++) {
            int x = rx + 2 + k * 38 / N, y = 44 - (L[k] - L[0]) * 7 / top;
            if (k == cur && self && v_level > 0.01f) { oled_rect(x - 1, y - 1, 3, 3, true); oled_frame(x - 2, y - 2, 5, 5); }
            else oled_rect(x, y, 2, 1, true);
        }
    } else if (mode == M_TILT) {                                // the ladder the tilt walks
        for (int k = 0; k < 13; k++) {
            int x = rx + 1 + k * 3;
            if (k == ladder) oled_rect(x - 1, 39, 3, 5, true);
            else oled_rect(x, k == 5 ? 40 : 42, 1, k == 5 ? 4 : 2, true);
        }
    } else draw_waveform(rx, 37, 38, 8, v_level, v->frame);
    // the loop's lane (the core may cover these rows)
    if (!v->cover) {
        oled_hline_pat(0, 47, OLED_W, 0x11, true);
        int ns = nsteps;
        if (ns > 0) {
            for (int s = 0; s < ns; s++) {
                if (!st_vel[s]) continue;
                int x = 2 + s * 124 / ns, w = st_len[s] * 124 / ns; if (w < 1) w = 1;
                int y = 61 - clampi((step_note(s) - 48) * 11 / 36, 0, 11);
                oled_rect(x, y, w, (st_flag[s] & F_ACCENT) ? 2 : 1, true);
            }
            float b = song_beat(audio_frames()) * (float)FINE;
            int x = 2 + (int)(b * 124.0f / (float)ns) % 124;
            oled_rect(x, 49, 1, 14, true);
            if (rec_on && ((v->frame >> 3) & 1)) oled_pill(F_TINY, 1, 49, "REC", true);
        } else if (rec_on) {
            oled_pill(F_SMALL, 2, 51, "REC", true);
            char b[16]; snprintf(b, sizeof b, "%.1fS", (double)((float)(audio_frames() - rec_start) / (float)AUDIO_SR));
            oled_ftext_ex(F_SMALL, 30, 53, b, 1);
        } else {
            oled_ftext_ex(F_TINY, 2, 50, sound_l[V.model], 1);
            draw_waveform(2, 56, 24, 6, v_level, v->frame);
            oled_ftext_r(F_TINY, OLED_W - 2, 55, mode == M_ARP ? "TIP: RANGE  SIDE: MORE" : mode == M_TILT ? "TILT: NOTE  KNOCK: PLAY" : "SING: IT FOLLOWS", 1);
        }
    }
}

static void icon(int x, int y, bool lit)
{
    (void)lit;
    // a saw wave climbing into a note head
    oled_line(x, y + 8, x + 3, y + 3, true); oled_line(x + 3, y + 3, x + 3, y + 8, true);
    oled_line(x + 3, y + 8, x + 6, y + 3, true); oled_line(x + 6, y + 3, x + 6, y + 8, true);
    oled_line(x + 7, y + 6, x + 9, y + 6, true);
    oled_disc(x + 9, y + 6, 1, true); oled_line(x + 10, y + 6, x + 10, y, true); oled_pixel(x + 11, y + 1, true);
}

const layer_t layer_lead = {
    .name = "LEAD", .letter = 'l', .pages = pages, .npages = 2, .params = params, .nparams = LD_N,
    .events = true, .init = init, .audio = audio, .tick = tick, .draw = draw, .icon = icon,
    .changed = changed, .clear = clear, .midi_in = midi_in, .state = state, .playing = playing,
};

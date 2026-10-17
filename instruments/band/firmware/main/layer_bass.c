// BASS: the jam's bass. One voice (legato, last note wins), six sounds, played four ways (MODE):
//   KNOCK   a knock on the box plays the note the tilt points at, on a ladder of the song's scale around the
//           chord's root (flat = the root); how hard sets how loud
//   HUM     sing or hum: it follows your pitch, folded down into the bass range and snapped to the scale
//   FOLLOW  it plays by itself: a bassline from the song's chord on the song's grid (ROOT, ROOT+5, OCTAVES,
//           WALK, ARP, ACID), on the drums' kicks with LOCK KICK. Once it has played a second while live it
//           keeps going when another layer is live (the band's bass player); tap REC mutes it
//   LINE    a line you write: 16 steps (a bar of sixteenths, or two bars of eighths with LINE STEP 1/8), each
//           off or a note given as a step of the scale from the chord's root, so the line follows the chords.
//           A note is short, or HELD (it rings on to the next note); the ready lines also accent and slide.
//           It plays on the song's grid like FOLLOW (and goes on under another layer once it has played a
//           second live). LINE loads a ready line; writing makes it MINE. The line is five hidden params, so
//           sounds and flash keep it.
// The line's editor: hold the knob while BASS is live (or BASS > EDIT LINE). A piano roll: steps across, the
// scale's notes up and down (the chord's root, third and fifth marked). Turn: the cursor walks the steps. Tap:
// the step goes off -> note -> held -> off. Press and turn: the step's note goes up or down the scale (you hear
// it). Hold: done. REC tapped: the bass stops / plays; held 1 s: the line is cleared. The stick: up / down the
// note, left / right the steps.
// In every mode leaning picks the chord for the whole jam (song.degree, on a ladder: iii ii IV I V vi vii).
// The stick: X = the filter, up = an octave up, down = accent (more envelope and drive).
// REC: hold = record (or add) what it plays to its loop on the song's sixteenths (the first loop on any layer
// sets the song's loop through host_first_loop); tap = mute / unmute (the loop, FOLLOW and the line). Muted, a
// hold is a new take: the old loop goes and the new one is heard as it is made. Loop notes come first; FOLLOW
// or the line fill the steps the loop leaves empty (the line itself is not recorded: it loops already). A knock
// heard right after REC goes down or just before it comes up is its click, and is not recorded.
#include "band.h"
#include "ui.h"
#include "spsc.h"
#include "params.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"          // the line's helpers on the audio path in internal RAM by themselves
#else
#define IRAM_ATTR
#endif

// ---------------------------------------------------------------- params
enum { B_MODE, B_LEDIT, B_LINE, B_LSTEP, B_PAT, B_RATE, B_KICK, B_FILL, B_OCT, B_LEAN, B_RIFF,
       B_SOUND, B_CUT, B_RES, B_ENV, B_DEC, B_GLIDE, B_DRIVE, B_FX, B_L0, B_L1, B_L2, B_L3, B_L4, B_N };
enum { M_KNOCK, M_HUM, M_FOLLOW, M_LINE };
enum { PT_ROOT, PT_FIFTH, PT_OCT, PT_WALK, PT_ARP, PT_ACID };
enum { S_SUB, S_ACID, S_808, S_FINGER, S_REESE, S_FM, S_N };
#define P(i) LP(L_BASS, i)

// The line: 16 steps, each 7 bits of a 120-bit stream kept in the five hidden params B_L0..B_L4 (24 bits each,
// exact in a float): the pitch (0 off; 1..13: the scale step -3..+9 from the chord's root, so 4 is the root, 8
// the fifth, 11 the octave), HELD (it rings on to the next note), ACCENT, SLIDE (glides in from the note before).
#define LSTEPS 16
#define LS_PMIN (-3)
#define LS_PMAX 9
enum { LC_HELD = 16, LC_ACC = 32, LC_SLIDE = 64 };
// Ready lines, step by step: "." off, a scale step from the root (0 root, 2 third, 4 fifth, 7 octave), then "~"
// held, "!" accent, "/" slid into. The defaults of B_L0..B_L4 are ROOT EIGHTHS packed (test_bass.c checks it).
typedef struct { const char *name; uint8_t step; const char *txt; } line_t;
static const line_t lines[] = {
    { "MINE",         0, NULL },
    { "ROOT EIGHTHS", 0, "0 . 0 . 0 . 0 . 0 . 0 . 0 . 0 ." },
    { "OCTAVE POP",   0, "0 . 7! . 0 . 7! . 0 . 7! . 0 . 7! ." },
    { "WALKING",      1, "0~ . 2~ . 4~ . 5~ . 7~ . 5~ . 4~ . 2~ ." },
    { "ROOT-FIFTH",   0, "0~! . . . -3~ . . . 0~ . . . -3~ . . ." },
    { "FUNK",         0, "0~! . . 0 . . 7! 0 . . 4 . 0! . 6 7" },
    { "ONE DROP",     0, ". . 0~! . . . 0 2 4~! . . . . 2 0~ ." },
    { "ACID SLIDE",   0, "0! 0 7 6/ 0 . 4! 7 0 . 0 2/! 4 . 7 6/" },
};
#define NLINES ((int)(sizeof lines / sizeof lines[0]))
static const char *const line_l[NLINES] = { "MINE", "ROOT EIGHTHS", "OCTAVE POP", "WALKING", "ROOT-FIFTH", "FUNK", "ONE DROP", "ACID SLIDE" };

static const char *const pages[2] = { "BASS", "BASS TONE" };
static const char *const mode_l[] = { "KNOCK", "HUM", "FOLLOW", "LINE" };
static const char *const pat_l[] = { "ROOT", "ROOT+5", "OCTAVES", "WALK", "ARP", "ACID" };
static const char *const rate_l[] = { "1/4", "1/8", "1/16" };
static const char *const lstep_l[] = { "1/16", "1/8" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const sound_l[] = { "SUB", "ACID", "808", "FINGER", "REESE", "FM" };
#define LHIDE (PF_INT | PF_HIDE | PF_NOCC)
static const param_t params[B_N] = {
    [B_MODE]  = { "MODE", "bmod", 0, PK_ENUM, 0, 0, 3, 1, M_FOLLOW, "", mode_l, "KNOCK, HUM, FOLLOW OR LINE" },
    [B_LEDIT] = { "EDIT LINE", "bled", 0, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "WRITE YOUR OWN BASS LINE" },
    [B_LINE]  = { "LINE", "blin", 0, PK_ENUM, 0, 0, NLINES - 1, 1, 1, "", line_l, "A READY LINE, OR YOURS" },
    [B_LSTEP] = { "LINE STEP", "blst", 0, PK_ENUM, 0, 0, 1, 1, 0, "", lstep_l, "16 STEPS: A BAR, OR TWO" },
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
    [B_L0]    = { "LINE 1", "bln0", 0, PK_NUM, LHIDE, 0, 16777215, 1, 0x010004, "", NULL, "THE LINE'S STEPS" },
    [B_L1]    = { "LINE 2", "bln1", 0, PK_NUM, LHIDE, 0, 16777215, 1, 0x100040, "", NULL, "THE LINE'S STEPS" },
    [B_L2]    = { "LINE 3", "bln2", 0, PK_NUM, LHIDE, 0, 16777215, 1, 0x000400, "", NULL, "THE LINE'S STEPS" },
    [B_L3]    = { "LINE 4", "bln3", 0, PK_NUM, LHIDE, 0, 16777215, 1, 0x004001, "", NULL, "THE LINE'S STEPS" },
    [B_L4]    = { "LINE 5", "bln4", 0, PK_NUM, LHIDE, 0, 16777215, 1, 0x000010, "", NULL, "THE LINE'S STEPS" },
};

// ---------------------------------------------------------------- between the tasks
typedef struct { uint8_t kind, note, vel, legato; } cmd_t;               // UI -> audio: live notes
enum { C_ON, C_OFF, C_PING };                                            // C_PING: a short note (the editor's)
enum { SRC_LIVE, SRC_FOLLOW, SRC_LOOP, SRC_LINE };
typedef struct { uint32_t frame; uint8_t note, vel, on, src, flags; int8_t step; } ev_t;   // audio -> UI: what sounded (step: the line's)
SPSC(bcmdq, cmd_t, 32)
SPSC(bevq, ev_t, 64)
static bcmdq_t cmds;
static bevq_t evs;

// The loop: one note per sixteenth of the song's loop. The UI writes (note, len, flags, then vel last; vel 0 =
// empty), the audio task reads.
#define MAXSTEP 128
enum { F_SLIDE = 1, F_ACCENT = 2, F_KNOCK = 4 };                         // F_KNOCK: played by a knock (never stored)
static volatile uint8_t st_vel[MAXSTEP], st_note[MAXSTEP], st_len[MAXSTEP], st_flag[MAXSTEP];
static volatile uint32_t st_at[MAXSTEP];  // the frame the note was played at: a note recorded just before its sixteenth is not played again there
static volatile int nsteps;              // steps in the loop (song.beats * 4, at most MAXSTEP); 0: no loop
static volatile int muted;               // tap REC: loop and FOLLOW silent
static volatile int follow_on;           // the FOLLOW bass player is on
static volatile int line_on;             // the line plays on under other layers
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

static void report(uint32_t frame, int note, int vel, bool on, int src, int flags, int step)
{
    ev_t e = { frame, (uint8_t)note, (uint8_t)vel, (uint8_t)on, (uint8_t)src, (uint8_t)flags, (int8_t)step };
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

// ---------------------------------------------------------------- LINE: the line written on the grid
// Step i's 7 bits (any task).
static inline IRAM_ATTR uint32_t line_code(int i)
{
    int b = 7 * (i & (LSTEPS - 1)), k = b / 24, o = b % 24;
    uint32_t v = (uint32_t)P(B_L0 + k) >> o;
    if (o > 17) v |= (uint32_t)P(B_L0 + k + 1) << (24 - o);     // the step runs on into the next word
    return v & 0x7Fu;
}
static inline bool lc_on(uint32_t c) { return (c & 15u) && (c & 15u) <= 13u; }
static inline int  lc_pitch(uint32_t c) { return (int)(c & 15u) - 4; }

// The note of scale step s above the chord's root at a frame (the root in C2..B2, as song.chord[0]).
static IRAM_ATTR int line_note(int s, uint32_t frame)
{
    int d = host_degree_at(frame), r = scale_note(d, 2), pc = ((r % 12) + 12) % 12;
    return 36 + pc + (scale_note(d + s, 2) - r) + 12 * (int)P(B_OCT);
}

// The line's step on sixteenth ka (counted from loop position 0, or from the session's beat 0). sp16: sixteenths
// a step. With a loop shorter than the line (a bar's loop and LINE STEP 1/8) the line runs on from pass to pass;
// otherwise it starts again with the loop. frame: a frame inside that sixteenth (which pass it is in).
static IRAM_ATTR int line_index(uint32_t ka, uint32_t frame, int sp16)
{
    uint32_t ls = ka / (uint32_t)sp16;
    if (song.loop_len && song.beats) {
        uint32_t per = (uint32_t)(song.beats * 4 / sp16);
        if (per && per < LSTEPS && LSTEPS % per == 0) {
            int32_t d = (int32_t)(frame - song.loop_start), L = (int32_t)song.loop_len;
            int32_t pass = d >= 0 ? d / L : -((-d + L - 1) / L);
            return (int)(((uint32_t)pass * per + ls) % LSTEPS);   // (2^32 is whole lines: a negative pass counts right)
        }
    }
    return (int)(ls % LSTEPS);
}

// trig_t, for the audio below
typedef struct { int at, note, vel, gate, flags, src, step; } trig_t;

// What the line plays on sixteenth ka (at frame, the sixteenth's start): false if no note starts there. A short
// note sounds 0.6 of a step, a held one up to the next note, and a note slid into is reached legato.
static IRAM_ATTR bool line_step(uint32_t ka, uint32_t frame, float fps16, trig_t *t)
{
    int sp16 = P(B_LSTEP) >= 1.0f ? 2 : 1;
    if (ka % (uint32_t)sp16) return false;
    int j = line_index(ka, frame + (uint32_t)(fps16 * 0.5f), sp16);
    uint32_t c = line_code(j), nc = c;
    if (!lc_on(c)) return false;
    int d = 1;
    for (; d < LSTEPS; d++) { nc = line_code(j + d); if (lc_on(nc)) break; }
    if (d == LSTEPS) nc = c;                                      // alone: the next note is itself, a line later
    float sf = fps16 * (float)sp16;
    bool acc = (c & LC_ACC) != 0, beat = j % (4 / sp16) == 0;
    t->note = line_note(lc_pitch(c), frame);
    t->vel = acc ? 124 : beat ? 104 : 88;
    t->gate = (nc & LC_SLIDE) ? (int)((float)d * sf * 1.05f) : (c & LC_HELD) ? (int)((float)d * sf) + 64 : (int)(0.6f * sf) + 64;
    t->flags = (acc ? F_ACCENT : 0) | ((c & LC_SLIDE) ? F_SLIDE : 0);
    t->src = SRC_LINE; t->step = j;
    return true;
}

// ---------------------------------------------------------------- audio
// Per block: the live notes from the UI, the steps the block crosses (the loop's, else FOLLOW's or the line's), a knock in the
// mic (KNOCK). Then the voice, rendered in segments between those triggers; the controls are smoothed per block
// and the filter's coefficients worked out at each segment's ends and interpolated per sample (no libm, no
// division per sample).
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
        if (V.gate > 0 && --V.gate == 0) { voice_off(); report(f0 + (uint32_t)(a + i), (int)V.target, 0, false, SRC_FOLLOW, 0, -1); }
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
    report(f0 + (uint32_t)t->at, t->note + oct_add, t->vel, true, t->src, t->flags, t->step);
}

static void audio(const float *mic, float *dry, float *send, int n, bool live)
{
    uint32_t f0 = audio_frames();
    a_live = live;
    nblk = n;
    // live notes from the UI (HUM, MIDI), at the block's start
    cmd_t c;
    while (bcmdq_pop(&cmds, &c)) {
        if (c.kind == C_ON) { voice_on(c.note, c.vel, c.legato, false); report(f0, c.note + oct_add, c.vel, true, SRC_LIVE, c.legato ? F_SLIDE : 0, -1); }
        else if (c.kind == C_PING) { voice_on(c.note, c.vel, false, false); V.gate = AUDIO_SR / 5; report(f0, c.note + oct_add, c.vel, true, SRC_LIVE, 0, -1); }
        else { voice_off(); report(f0, (int)V.target, 0, false, SRC_LIVE, 0, -1); }
    }
    float dec = P(B_DEC);
    // the steps this block crosses: loop notes first, FOLLOW or the line fill the rest
    trig_t tr[10]; int ntr = 0;
    float fps16 = song_frames_per_beat() * 0.25f;
    bool fol = !muted && P(B_MODE) == M_FOLLOW && (follow_on || (live && a_mode == M_FOLLOW));
    bool lin = !muted && P(B_MODE) == M_LINE && (line_on || (live && a_mode == M_LINE));
    bool loop = !muted && nsteps > 0 && song.loop_len;
    if (fol || lin || loop) {
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
                if (loop && st >= 0 && st_vel[st] && (int32_t)(f0 + (uint32_t)at - st_at[st]) >= (int32_t)fps16) { note = st_note[st]; vel = st_vel[st]; len = st_len[st]; fl = st_flag[st]; }
                else if (lin) { trig_t t; if (line_step(ka, f0 + (uint32_t)at, fps16, &t)) { t.at = at; tr[ntr++] = t; } continue; }
                else if (fol) { note = follow_step(ka, &vel, &len, &fl); src = SRC_FOLLOW; }
                if (note < 0) continue;
                if (len < 1) len = 1;
                int gate = (fl & F_SLIDE) ? (int)((float)len * fps16 * 1.05f) : (int)((float)len * fps16 * 0.6f) + 64;
                tr[ntr++] = (trig_t){ at, note, vel, gate, fl, src, -1 };
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
                trig_t t = { i, knock_note, vel, (int)(fmaxf(dec, 0.12f) * (float)AUDIO_SR), (vel > 110 ? F_ACCENT : 0) | F_KNOCK, SRC_LIVE, -1 };
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
#define CLICK_POST (AUDIO_SR * 12 / 100)                     // REC's click: a knock this soon after the press,
#define CLICK_PRE  (AUDIO_SR * 4 / 100)                      // or this soon before the release, is not recorded
static bool rec_on, rec_first, rec_fresh;                    // rec_fresh: REC went down while muted (held: a new take)
static uint32_t rec_start;
static ev_t pend[160]; static int npend;
static uint8_t bk_vel[MAXSTEP], bk_note[MAXSTEP], bk_len[MAXSTEP], bk_flag[MAXSTEP];
static int open_step = -1; static uint32_t open_frame;
static int knock_st = -1; static uint32_t knock_fr;          // the last step a knock wrote in this take
static uint32_t my_loop_len, my_loop_start;
// MIDI keys held (last note wins)
static uint8_t held[8]; static int nheld;
// the scene
static float pump;
static int   midi_cur = -1;                                 // the note MIDI has on
static UI_RAM parts_t ps;
static uint32_t pseed = 0x51ed27u;
static int note_shown = -1, flash_ms;
static float lfl[LSTEPS];                                   // the line's notes flashing as they sound

static int quantize(uint32_t frame)
{
    if (!nsteps) return -1;
    int s = (int)floorf(song_beat(frame) * 4.0f + 0.5f);
    return ((s % nsteps) + nsteps) % nsteps;
}

static void write_step(int st, int note, int vel, int len, int flags, uint32_t frame)
{
    if (st < 0 || st >= MAXSTEP) return;
    st_vel[st] = 0;
    st_at[st] = frame;
    st_note[st] = (uint8_t)note; st_len[st] = (uint8_t)clampi(len, 1, 16); st_flag[st] = (uint8_t)(flags & (F_SLIDE | F_ACCENT));
    __atomic_store_n(&st_vel[st], (uint8_t)clampi(vel, 1, 127), __ATOMIC_RELEASE);
}

static void clear_steps(void) { for (int i = 0; i < MAXSTEP; i++) st_vel[i] = 0; }
// LOOPS > CLEAR BASS (or erased in the mixer): the loop goes, FOLLOW and the line stop until BASS is live again (the
// line's notes stay: they are a setting), and a muted bass is unmuted
static void clear(void) { clear_steps(); follow_on = 0; line_on = 0; npend = 0; rec_on = false; muted = 0; }

static bool knock_click(const ev_t *e, uint32_t end)     // a knock that is REC's click (end: the release, or 0)
{
    if (!(e->flags & F_KNOCK) || !e->on) return false;
    int32_t a = (int32_t)(e->frame - rec_start);
    if (a > -CLICK_PRE && a < CLICK_POST) return true;
    return end && (int32_t)(end - e->frame) < CLICK_PRE;
}

static void record(const ev_t *e)
{
    if (!rec_on || e->src == SRC_LOOP || e->src == SRC_LINE) return;   // the line loops already
    if (knock_click(e, 0)) { open_step = -1; return; }
    if (rec_first) { if (npend < (int)(sizeof pend / sizeof pend[0])) pend[npend++] = *e; return; }
    if (e->on) {
        int st = quantize(e->frame);
        int len = e->src == SRC_FOLLOW ? 1 : 2;
        write_step(st, e->note - oct_add, e->vel, len, e->flags, e->frame);
        open_step = st; open_frame = e->frame;
        if (e->flags & F_KNOCK) { knock_st = st; knock_fr = e->frame; }
    } else if (open_step >= 0) {
        float fps16 = song_frames_per_beat() * 0.25f;
        int len = (int)((float)(e->frame - open_frame) / fps16 + 0.5f);
        if (len >= 1) st_len[open_step] = (uint8_t)clampi(len, 1, 16);
        open_step = -1;
    }
}

static void first_loop_done(uint32_t end)
{
    if (!song.loop_len) {                                    // the loop starts at the first note: the silence before it is cut
        uint32_t s0 = rec_start;
        for (int i = 0; i < npend; i++) if (pend[i].on && !knock_click(&pend[i], end)) { s0 = pend[i].frame; break; }
        if ((int32_t)(s0 - rec_start) < 0 || (int32_t)(end - s0) < AUDIO_SR / 4) s0 = rec_start;
        host_first_loop(s0, end - s0);
    }
    nsteps = song.loop_len && song.beats ? clampi(song.beats * 4, 1, MAXSTEP) : 0;
    my_loop_len = song.loop_len; my_loop_start = song.loop_start;
    clear_steps();
    open_step = -1;
    for (int i = 0; i < npend; i++) {
        ev_t *e = &pend[i];
        if (!nsteps) break;
        if (knock_click(e, end)) { open_step = -1; continue; }
        float beat = (float)song_mod((int32_t)(e->frame - song.loop_start), (int32_t)song.loop_len) * (float)song.beats / (float)song.loop_len;
        int st = ((int)floorf(beat * 4.0f + 0.5f) % nsteps + nsteps) % nsteps;
        if (e->on) { write_step(st, e->note - oct_add, e->vel, e->src == SRC_FOLLOW ? 1 : 2, e->flags, e->frame); open_step = st; open_frame = e->frame; }
        else if (open_step >= 0) {
            float fps16 = song_frames_per_beat() * 0.25f;
            int len = (int)((float)(e->frame - open_frame) / fps16 + 0.5f);
            if (len >= 1) st_len[open_step] = (uint8_t)clampi(len, 1, 16);
            open_step = -1;
        }
    }
    npend = 0;
}

// ---------------------------------------------------------------- UI: the line
static int  last_line_sel = -1;
static uint32_t mine[5]; static bool mine_ok;
static bool ed_guard;                                       // our own edits: LINE becomes MINE without loading anything

static bool line_any(void) { for (int i = 0; i < LSTEPS; i++) if (lc_on(line_code(i))) return true; return false; }

// a ready line's text, step by step ("0~!" = the root, held, accented), into the five words
static uint32_t line_tok(const char **p)
{
    const char *s = *p;
    while (*s == ' ') s++;
    uint32_t c = 0;
    if (*s == '.') s++;
    else if (*s) {
        bool neg = *s == '-';
        if (neg) s++;
        int v = 0;
        while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
        c = (uint32_t)clampi((neg ? -v : v) - LS_PMIN + 1, 1, 13);
        for (; *s && *s != ' '; s++) c |= *s == '~' ? LC_HELD : *s == '!' ? LC_ACC : *s == '/' ? LC_SLIDE : 0u;
    }
    *p = s;
    return c;
}
static void line_pack(const char *txt, uint32_t w[5])
{
    for (int k = 0; k < 5; k++) w[k] = 0;
    const char *p = txt;
    for (int i = 0; i < LSTEPS && txt; i++) {
        uint32_t c = line_tok(&p);
        int b = 7 * i, k = b / 24, o = b % 24;
        w[k] |= (c << o) & 0xFFFFFFu;
        if (o > 17) w[k + 1] |= c >> (24 - o);
    }
}
void bass_line_words(int line, uint32_t w[5]) { line_pack(lines[clampi(line, 0, NLINES - 1)].txt, w); }   // for tools/sim

static void line_set(const uint32_t w[5])                   // our own edit: the line is MINE now
{
    ed_guard = true;
    for (int k = 0; k < 5; k++) if ((float)(w[k] & 0xFFFFFFu) != P(B_L0 + k)) LP_SET(L_BASS, B_L0 + k, (float)(w[k] & 0xFFFFFFu));
    if (P(B_LINE) != 0.0f) LP_SET(L_BASS, B_LINE, 0);
    ed_guard = false;
}
static void line_put(int i, uint32_t c)
{
    uint32_t w[5];
    for (int k = 0; k < 5; k++) w[k] = (uint32_t)P(B_L0 + k);
    int b = 7 * i, k = b / 24, o = b % 24;
    w[k] = (w[k] & ~(0x7Fu << o)) | (c << o);
    if (o > 17) { int sh = 24 - o; w[k + 1] = (w[k + 1] & ~(0x7Fu >> sh)) | (c >> sh); }
    line_set(w);
}

// the line's step playing now (for the screen)
static int line_head(void)
{
    uint32_t f = audio_frames();
    int sp16 = P(B_LSTEP) >= 1.0f ? 2 : 1;
    uint32_t ka = (uint32_t)floorf(song_beat(f) * 4.0f);
    return line_index(ka - ka % (uint32_t)sp16, f, sp16);
}

// ---------------------------------------------------------------- UI: the line's editor
// The piano roll: 16 steps across (a pixel more between beats), 9 of the 13 notes up and down, following the
// cursor. Rows of the chord's root dotted, its third and fifth dotted lighter.
#define VROWS 9                // rows on the screen
#define RX0 9                  // the first step's x, the step pitch, the top row's y, the row pitch
#define RDX 7
#define RY0 18
#define RDY 4
static bool  ed_on, ed_seen, ed_drag;
static int   ed_cur, ed_pitch, ed_frames, ed_hint_ms, ed_rec_stage, ed_jx_ms, ed_jy_ms, ed_lo_t = -1;
static float ed_cx = -1, ed_cy = -1, ed_lo = -1;            // the cursor drawn and the lowest row shown, sliding
static float pop[LSTEPS];

static int   step_x(int st) { return RX0 + st * RDX + st / 4; }
static float row_yf(int p) { return (float)RY0 + (ed_lo + (float)(VROWS - 1) - (float)p) * (float)RDY; }

static void ping(int s)                                     // the note under the cursor, heard as it is written
{
    cmd_t cm = { C_PING, (uint8_t)clampi(line_note(s, audio_frames()), 0, 127), 96, 0 };
    bcmdq_push(&cmds, &cm);
}
static void cell_pop(int st, int p, int n)
{
    pop[st] = 0.0f;
    parts_burst(&ps, (float)(step_x(st) + 3), row_yf(p) + 1.0f, n, 45.0f, 0.25f, PART_SPARK, &pseed);
}
static void ed_window(void)                                 // the rows follow the cursor, a row from the edge
{
    if (ed_pitch < ed_lo_t + 1) ed_lo_t = ed_pitch - 1;
    if (ed_pitch > ed_lo_t + VROWS - 2) ed_lo_t = ed_pitch - VROWS + 2;
    ed_lo_t = clampi(ed_lo_t, LS_PMIN, LS_PMAX - VROWS + 1);
}
static void cursor_to(int st)
{
    ed_cur = ((st % LSTEPS) + LSTEPS) % LSTEPS;
    uint32_t c = line_code(ed_cur);
    if (lc_on(c)) ed_pitch = lc_pitch(c);                    // on a note the cursor is at its pitch
    ed_window();
}
static void pitch_move(int d)                               // press and turn: the step takes the note
{
    ed_pitch = clampi(ed_pitch + d, LS_PMIN, LS_PMAX);
    uint32_t c = line_code(ed_cur), nc = (lc_on(c) ? (c & ~15u) : 0u) | (uint32_t)(ed_pitch - LS_PMIN + 1);
    if (nc != c) { line_put(ed_cur, nc); if (!lc_on(c)) cell_pop(ed_cur, ed_pitch, 3); }
    ed_window();
    ping(ed_pitch);
}
static void ed_tap(void)                                    // off -> note -> held -> off
{
    uint32_t c = line_code(ed_cur);
    if (!lc_on(c)) { line_put(ed_cur, (uint32_t)(ed_pitch - LS_PMIN + 1)); cell_pop(ed_cur, ed_pitch, 4); ping(ed_pitch); }
    else if (!(c & LC_HELD)) { line_put(ed_cur, c | LC_HELD); cell_pop(ed_cur, ed_pitch, 6); }
    else { line_put(ed_cur, 0); parts_burst(&ps, (float)(step_x(ed_cur) + 3), row_yf(ed_pitch) + 1.0f, 3, 25.0f, 0.2f, PART_DOT, &pseed); }
}

static void editor_open(void)
{
    if (ed_on) return;
    if (rec_on) { rec_on = false; if (rec_first && npend) first_loop_done(audio_frames()); }
    ed_on = true; ed_drag = false; ed_frames = 0; ed_rec_stage = 0; ed_jx_ms = ed_jy_ms = 0;
    ed_cx = ed_cy = -1;
    ed_hint_ms = ed_seen ? 3000 : 5000; ed_seen = true;          // until the first turn or press
    for (int i = 0; i < LSTEPS; i++) pop[i] = 1.0f;
    ps.n = 0;                                                    // the woofer's rings stay with the woofer
    if (P(B_MODE) != (float)M_LINE) LP_SET(L_BASS, B_MODE, M_LINE);   // you hear what you write
    line_on = 1; muted = 0;
    // the rows: the line's notes and the root, a row under the lowest
    int lo = 0, hi = 0;
    for (int i = 0; i < LSTEPS; i++) { uint32_t c = line_code(i); if (lc_on(c)) { lo = lc_pitch(c) < lo ? lc_pitch(c) : lo; hi = lc_pitch(c) > hi ? lc_pitch(c) : hi; } }
    ed_lo_t = clampi(lo - 1, LS_PMIN, LS_PMAX - VROWS + 1);
    if (hi > ed_lo_t + VROWS - 1) ed_lo_t = clampi(hi - VROWS + 1, LS_PMIN, LS_PMAX - VROWS + 1);
    cursor_to(ed_cur);
    ed_lo = (float)ed_lo_t;
    knob_grab(true);
    coach_done(LEARN_INST + 12);
}
static void editor_close(void)
{
    if (!ed_on) return;
    ed_on = false; ed_drag = false;
    ps.n = 0;
    knob_grab(false);
}
bool bass_editing(void) { return ed_on; }                               // for tools/sim
int  bass_cursor(void) { return ed_cur; }
int  bass_cursor_pitch(void) { return ed_pitch; }

static void editor_tick(const ctl_t *c)
{
    if (c->knob_hold) { editor_close(); return; }
    if (c->knob_tap) { ed_tap(); ed_hint_ms = 0; }
    if (c->knob) {
        if (c->knob_down) { ed_drag = true; pitch_move(c->knob); }   // press and turn: up or down the scale
        else cursor_to(ed_cur + c->knob * (c->knob > 1 || c->knob < -1 ? 2 : 1));
        ed_hint_ms = 0;
    }
    if (!c->knob_down) ed_drag = false;
    // the stick: the note up / down, the steps left / right, repeating while held over
    if (c->has_joy) {
        int dy = c->jy > 0.6f ? 1 : c->jy < -0.6f ? -1 : 0, dx = c->jx > 0.6f ? 1 : c->jx < -0.6f ? -1 : 0;
        if (dy) { if (ed_jy_ms == 0 || (ed_jy_ms >= 350 && (ed_jy_ms - 350) % 120 < 10)) pitch_move(dy); ed_jy_ms += 10; }
        else ed_jy_ms = 0;
        if (dx) { if (ed_jx_ms == 0 || (ed_jx_ms >= 350 && (ed_jx_ms - 350) % 120 < 10)) cursor_to(ed_cur + dx); ed_jx_ms += 10; }
        else ed_jx_ms = 0;
    }
    // REC: a tap stops or starts the bass; held 1 s, the line is cleared
    if (c->rec_press) ed_rec_stage = 0;
    if (c->rec && c->rec_ms >= 1000 && ed_rec_stage == 0) {
        const uint32_t z[5] = { 0, 0, 0, 0, 0 };
        line_set(z); ed_rec_stage = 1; toast("LINE CLEARED", 900);
    }
    if (c->rec_release && c->rec_ms < 300 && ed_rec_stage == 0) {
        muted = !muted;
        toast(muted ? "BASS STOPS" : "BASS PLAYS", 700);
    }
    if (ed_hint_ms > 0) ed_hint_ms -= 10;
}

// LINE: a ready line loads its steps and its LINE STEP; back on MINE, the one you wrote comes back (this session)
static void changed(int p)
{
    if (p < 0) { last_line_sel = (int)P(B_LINE); return; }
    if (p != B_LINE) return;
    int b = clampi((int)P(B_LINE), 0, NLINES - 1);
    if (ed_guard) { last_line_sel = b; return; }
    if (b == last_line_sel) return;
    if (last_line_sel == 0) { for (int k = 0; k < 5; k++) mine[k] = (uint32_t)P(B_L0 + k); mine_ok = true; }
    last_line_sel = b;
    if (b == 0 && !mine_ok) return;
    uint32_t w[5];
    if (b) line_pack(lines[b].txt, w); else memcpy(w, mine, sizeof w);
    for (int k = 0; k < 5; k++) LP_SET(L_BASS, B_L0 + k, (float)w[k]);
    if (b) LP_SET(L_BASS, B_LSTEP, (float)lines[b].step);
}

static void action(int p)
{
    if (p != B_LEDIT) return;
    menu_leave();
    if (host_live() != L_BASS) {                                      // the editor is the live layer's scene
        int lp = param_by_tag("slay");
        if (lp >= 0) param_set(lp, (float)L_BASS);
    }
    editor_open();
}
static bool b_hold(void) { editor_open(); return true; }
static bool b_playing(void)
{
    if (muted) return false;
    int m = (int)P(B_MODE);
    return (m == M_FOLLOW && follow_on) || (m == M_LINE && line_on && line_any());
}

// ---------------------------------------------------------------- UI: per tick
static void tick(const ctl_t *c, bool live)
{
    int mode = (int)P(B_MODE);
    a_mode = mode;
    if (chord_ovr_on && P(B_LEAN) < 1.0f) { host_chord_override(-1); chord_ovr_on = false; }   // the progression has it again
    if (P(B_SOUND) != (float)model_req) model_req = (int)P(B_SOUND);
    // the song's loop: steps follow its length; a new loop (or none) forgets ours. A start that moved by a few frames
    // (the link following another board's clock) or by whole loops is the same loop
    if (song.loop_len == my_loop_len && song.loop_len && song.loop_start != my_loop_start) {
        int32_t d = song_mod((int32_t)(song.loop_start - my_loop_start), (int32_t)song.loop_len);
        if (d < 64 || d > (int32_t)song.loop_len - 64) my_loop_start = song.loop_start;
    }
    if (song.loop_len != my_loop_len || song.loop_start != my_loop_start) {
        if (!(rec_on && rec_first)) {
            if (my_loop_len) clear_steps();
            my_loop_len = song.loop_len; my_loop_start = song.loop_start;
            nsteps = song.loop_len && song.beats ? clampi(song.beats * 4, 1, MAXSTEP) : 0;
        }
    }
    if (ed_on && !live) editor_close();                                // another layer was made live: the editor goes
    if (live) {
        live_ms = was_live ? live_ms + 10 : 0;
        if (!was_live) {                                     // arriving: the lean as it is now picks nothing
            float p = c->lean_x / 11.0f;
            lean_pos = clampi((int)lroundf(p) + 3, 0, 6);
        }
        // the chord, by leaning (only when it moves to a new rung; not while writing a line, the hands on the knob)
        if (c->has_imu && P(B_LEAN) >= 1.0f && !ed_on) {
            float p = c->lean_x / 11.0f + 3.0f;
            int np = lean_pos;
            if (p > (float)lean_pos + 0.65f) np = lean_pos + 1;
            if (p < (float)lean_pos - 0.65f) np = lean_pos - 1;
            np = clampi(np, 0, 6);
            if (np != lean_pos) { lean_pos = np; host_chord_override(lean_deg[np]); chord_ovr_on = true; coach_done(LEARN_INST + 13); }
        }
        // KNOCK: the tilt picks a rung of the ladder (flat = the chord's root)
        if (c->has_imu) {
            float p = c->lean_y / 7.0f + 3.0f;              // flat: rung 3, the root; bottom edge down: higher
            if (p > (float)ladder + 0.65f) ladder++;
            if (p < (float)ladder - 0.65f) ladder--;
            ladder = clampi(ladder, 0, 9);
        } else ladder = 3;
        knock_note = scale_note(song.degree - 3 + ladder, 2) + 12 * (int)P(B_OCT);
        // the stick (the editor's while it is open)
        if (c->has_joy && !ed_on) {
            stick_x = c->jx;
            oct_add = c->jy > 0.6f ? 12 : 0;
            accent_amt = c->jy < -0.3f ? clampf((-c->jy - 0.3f) / 0.6f, 0, 1) : 0;
        } else if (ed_on) { stick_x = 0; oct_add = 0; accent_amt = 0; }
        // FOLLOW and the line: they go on under other layers after a second here with the menu closed
        if (mode == M_FOLLOW && !c->menu_open && live_ms >= 1000 && !muted) follow_on = 1;
        if (mode != M_FOLLOW) follow_on = 0;
        if (mode == M_LINE && !c->menu_open && live_ms >= 1000 && !muted) line_on = 1;
        if (mode != M_LINE) line_on = 0;
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
                            coach_done(LEARN_INST + 14);
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
        if (ed_on) editor_tick(c);                           // the editor has REC
        else {
            // REC: hold = record (or add; muted, a new take), tap = mute
            if (c->rec_press) {
                rec_on = true; rec_start = c->frames; rec_first = song.loop_len == 0; npend = 0; open_step = -1; knock_st = -1;
                for (int i = 0; i < MAXSTEP; i++) { bk_vel[i] = st_vel[i]; bk_note[i] = st_note[i]; bk_len[i] = st_len[i]; bk_flag[i] = st_flag[i]; }
                rec_fresh = muted && !rec_first;
                if (rec_fresh) clear_steps();                // not heard (muted); a tap brings it back
            }
            if (rec_on && c->rec && c->rec_ms >= 300 && muted) {   // held: a take, heard as it is made
                muted = 0;
                toast(rec_fresh ? "NEW TAKE" : "BASS ON", 900);
                if (rec_fresh) coach_done(LEARN_INST + 11);
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
                    if (knock_st >= 0 && (int32_t)(c->frames - knock_fr) < CLICK_PRE) {   // the release's click
                        int k = knock_st;
                        st_vel[k] = 0;
                        if (!rec_fresh && bk_vel[k]) { st_note[k] = bk_note[k]; st_len[k] = bk_len[k]; st_flag[k] = bk_flag[k]; st_vel[k] = bk_vel[k]; }
                    }
                    if (rec_first) first_loop_done(c->frames);
                    coach_done(LEARN_INST + 3);
                }
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
            if (live && !ed_on) {
                parts_emit(&ps, 20, 31, 0, 0, 0.45f, PART_RING, (e.flags & F_ACCENT) ? 4 : 2);
                if ((e.flags & F_ACCENT) || e.vel > 112) parts_burst(&ps, 20, 31, 6, 40.0f, 0.6f, PART_DOT, &pseed);
            }
            if (e.src == SRC_LINE && e.step >= 0 && e.step < LSTEPS) lfl[e.step] = (e.flags & F_ACCENT) ? 1.4f : 1.0f;
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

static int state(void)
{
    if (rec_on) return LOOP_REC;
    bool any = follow_on || (line_on && P(B_MODE) == (float)M_LINE && line_any());
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

// the next note after step st (1..16 steps on; 16: none but itself)
static int line_next(int st)
{
    int d = 1;
    while (d < LSTEPS && !lc_on(line_code(st + d))) d++;
    return d;
}

// ---- the editor's piano roll
// y of a row as drawn: the rows drop in from under the header when the editor opens, the top one first
static int ry(int p, bool *vis)
{
    float yf = row_yf(p), r = (yf - (float)RY0) / (float)RDY;
    float t = clampf(((float)ed_frames - r) / 8.0f, 0.0f, 1.0f);
    int y = (int)lroundf(yf - (1.0f - ease_out(t)) * 26.0f);
    if (vis) *vis = yf > (float)RY0 - 2.5f && yf < (float)(RY0 + (VROWS - 1) * RDY) + 2.5f && y >= 16;
    return y;
}

static void draw_editor(const view_t *v)
{
    oled_rect(0, 16, OLED_W, OLED_H - 16, false);
    ed_frames++;
    for (int i = 0; i < LSTEPS; i++) if (pop[i] < 1.0f) pop[i] = fminf(1.0f, pop[i] + 1.0f / 6.0f);
    ed_lo += 0.35f * ((float)ed_lo_t - ed_lo);
    if (fabsf(ed_lo - (float)ed_lo_t) < 0.02f) ed_lo = (float)ed_lo_t;
    int sp16 = P(B_LSTEP) >= 1.0f ? 2 : 1, xr = step_x(LSTEPS - 1) + RDX - 2;   // xr: the roll's right edge
    int head = muted ? -1 : line_head();
    // over the steps: the bars and beats, and the playhead
    for (int st = 0; st < LSTEPS; st++) {
        int x = step_x(st);
        if (st % (16 / sp16) == 0) oled_rect(x + 2, 16, 2, 2, true);
        else if (st % (4 / sp16) == 0) oled_pixel(x + 2, 17, true);
    }
    if (head >= 0) { int x = step_x(head); oled_rect(x, 16, RDX - 1, 2, true); oled_dither(x, RY0, RDX - 1, VROWS * RDY - 1, 1, true); }
    // the rows: the chord's root dotted (its name at the left), its third and fifth lighter
    char nm[8];
    ui_note_name(song.chord_n ? song.chord[0] : 36, nm, sizeof nm);
    { int l = (int)strlen(nm); while (l > 0 && nm[l - 1] >= '0' && nm[l - 1] <= '9') nm[--l] = 0; }
    for (int p = LS_PMIN; p <= LS_PMAX; p++) {
        bool vis; int y = ry(p, &vis);
        if (!vis) continue;
        int dg = ((p % 7) + 7) % 7;
        if (dg == 0) { oled_hline_pat(RX0, y + 1, xr - RX0 + 1, 0x11, true); oled_ftext_ex(F_TINY, 0, y - 1, nm, 0); }
        else if (dg == 2 || dg == 4) { oled_hline_pat(RX0, y + 1, xr - RX0 + 1, 0x01, true); oled_rect(6, y + 1, 2, 1, true); }
    }
    // the notes: short ones a cell, held ones up to the next note (touching it when it is slid into)
    for (int st = 0; st < LSTEPS; st++) {
        uint32_t c = line_code(st);
        if (!lc_on(c)) continue;
        int p = lc_pitch(c), x = step_x(st), d = line_next(st), nx = st + d;
        uint32_t nc = line_code(nx);
        bool vis; int y = ry(p, &vis);
        if (!vis) {                                                   // above or below the rows: a mark on that edge
            int ey = row_yf(p) < (float)RY0 ? RY0 - 1 : RY0 + VROWS * RDY - 1;
            if (ed_frames > 8) oled_rect(x + 2, ey, 2, 1, true);
            continue;
        }
        bool held = (c & LC_HELD) || (nc & LC_SLIDE);
        int xe = !held ? x + 3 : nx < LSTEPS ? step_x(nx) - ((nc & LC_SLIDE) ? 1 : 2) : xr;
        float e = ease_back(pop[st]);
        int w = (int)lroundf((float)(xe - x + 1) * e), h = (int)lroundf(3.0f * e);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        oled_rect(x, y + 1 - h / 2, w, h, true);
        if (held && nx >= LSTEPS && step_x(nx - LSTEPS) - 2 > RX0)            // it rings on past the end: the start of the roll
            oled_rect(RX0, y, step_x(nx - LSTEPS) - 2 - RX0, 3, true);
        if (c & LC_ACC) oled_rect(x, y - 1, 2, 5, true);              // an accent: a taller head
        if ((nc & LC_SLIDE) && nx < LSTEPS && lc_pitch(nc) != p) {    // a slide: a line into the next note
            bool v2; int y2 = ry(lc_pitch(nc), &v2);
            if (v2) oled_line(xe, y + 1, step_x(nx), y2 + 1, true);
        }
        float fl = lfl[st];
        if (fl > 0.25f) { int g = fl > 0.9f ? 2 : 1; oled_frame(x - g, y - g, xe - x + 1 + 2 * g, 3 + 2 * g); }
    }
    // the cursor slides to its step and note and blinks (steady while the note is dragged)
    float tx = (float)(step_x(ed_cur) - 1), ty = row_yf(ed_pitch) - 1.0f;
    if (ed_cx < 0) { ed_cx = tx; ed_cy = ty; }
    ed_cx += 0.55f * (tx - ed_cx); ed_cy += 0.55f * (ty - ed_cy);
    if (ed_frames > 12) {
        int cx = (int)lroundf(ed_cx), cy = (int)lroundf(ed_cy);
        if (ed_drag || (v->frame / 5) % 3 != 2) oled_invert(cx, cy, RDX + 1, RDY + 1);
        else oled_frame(cx, cy, RDX + 1, RDY + 1);
    }
    // where the rows are among the 13 (a thumb at the right)
    int span = VROWS * RDY - 1, th = span * VROWS / 13;
    int t0 = RY0 + (int)lroundf((float)(LS_PMAX - ((int)lroundf(ed_lo) + VROWS - 1)) * (float)(span - th) / (float)(13 - VROWS));
    oled_rect(126, t0, 1, th, true);
    parts_step(&ps, 0.04f, 60.0f, 3.0f);
    parts_draw(&ps);
    // the bottom line: the hints the first time, then the line, the cursor's note and whether it plays
    oled_rect(0, 54, OLED_W, 10, false);
    oled_hline_pat(0, 54, OLED_W, 0x11, true);
    if (ed_hint_ms > 0) oled_ftext_c(F_TINY, 64, 57, (ed_frames / 40) % 2 ? "PRESS + TURN: NOTE  HOLD: DONE" : "TURN: STEP  TAP: NOTE, LONG, OFF", 0);
    else {
        int x = oled_pill(F_TINY, 1, 55, line_l[clampi((int)P(B_LINE), 0, NLINES - 1)], P(B_LINE) == 0.0f);
        char cn[8]; ui_note_name(clampi(line_note(ed_pitch, audio_frames()), 0, 127), cn, sizeof cn);
        int nx = x + 6, nw = oled_ftext_w(F_TINY, cn, 1);
        oled_ftext_ex(F_TINY, nx, 57, cn, 1);
        if (ed_drag) oled_invert(nx - 1, 56, nw + 2, 7);
        bool on = !muted;
        if (on) { oled_line(119, 56, 119, 60, true); oled_line(120, 57, 120, 59, true); oled_pixel(121, 58, true); }   // a play triangle
        else oled_rect(118, 56, 4, 5, true);
        oled_ftext_r(F_TINY, 114, 57, on ? "PLAYS" : "STOPPED", 1);
    }
}

// the line small, along the bottom of the bass's scene: 16 steps across, the notes by pitch, the playhead
static void draw_line_lane(int head)
{
    for (int st = 0; st < LSTEPS; st++) {
        uint32_t c = line_code(st);
        if (!lc_on(c)) continue;
        int x = 2 + st * 31 / 4, nx = st + line_next(st);
        uint32_t nc = line_code(nx);
        int xe = ((c & LC_HELD) || (nc & LC_SLIDE)) ? (nx < LSTEPS ? 2 + nx * 31 / 4 - 1 : 125) : x + 3;
        int y = 61 - (lc_pitch(c) - LS_PMIN);
        bool fl = lfl[st] > 0.3f;
        oled_rect(x - (fl ? 1 : 0), y - (fl ? 1 : 0), xe - x + 1 + (fl ? 2 : 0), ((c & LC_ACC) ? 2 : 1) + (fl ? 1 : 0), true);
    }
    if (head >= 0) { int x = 2 + head * 31 / 4; oled_rect(x, 48, 6, 1, true); }
}

static void draw(const view_t *v)
{
    for (int i = 0; i < LSTEPS; i++) lfl[i] *= 0.72f;
    if (ed_on) { draw_editor(v); return; }
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
    bool plays = mode == M_FOLLOW ? (follow_on || a_live) : mode == M_LINE ? (line_on || a_live) : false;
    int px = oled_pill(F_SMALL, tx, 17, mode_l[mode], plays && !muted);
    const char *sub = mode == M_FOLLOW ? pat_l[(int)P(B_PAT)] : mode == M_LINE ? line_l[clampi((int)P(B_LINE), 0, NLINES - 1)] : mode == M_KNOCK ? "TILT" : "SING LOW";
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
    // ---- the loop (or the line): its notes on a lane, the playhead (the bottom rows: the core may cover them)
    if (!v->cover) {
        oled_hline_pat(0, 47, OLED_W, 0x11, true);
        int ns = nsteps;
        if (mode == M_LINE && line_any() && !rec_on) draw_line_lane(!muted && (line_on || a_live) ? line_head() : -1);
        else if (ns > 0) {
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
        } else {
            const char *h = mode == M_FOLLOW ? "LEAN: CHORD   HOLD REC: LOOP" : mode == M_KNOCK ? "KNOCK THE BOX   TILT: NOTE" : mode == M_HUM ? "HUM A LOW NOTE" : "";
            if ((v->frame / 75) & 1 || !*h) h = "HOLD THE KNOB: WRITE A BASS LINE";   // every 3 s
            oled_ftext_c(F_TINY, 64, 55, h, 1);
        }
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
    .changed = changed, .clear = clear, .midi_in = midi_in, .action = action, .state = state,
    .hold = b_hold, .playing = b_playing,
};

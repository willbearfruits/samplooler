// HOWL: feedback in tune. The speaker plays what the mic hears through a bank of tuned band passes, so
// only the chosen chord's notes can build up into a howl. INNER closes the loop inside the box as well (so
// it howls on headphones); AIR leaves the loop to the speaker and the room; BOTH does both.
//
//   the bank      3..8 band passes on the chord's notes (ROOT, OCTAVE, CHORD, BANDS, SPREAD), RING = their Q
//   the loops     tight: the bank's output one sample later (the band passes' own phase keeps a howl on its
//                 note, measured in the family twin); room: a delay line (SIZE) with damping and a frequency
//                 shifter (SHIFT: howls that climb or fall for ever), mixed in by ROOM
//   the gain      loop gain = DRIVE (tilt, joystick, GAIN, the tide) times the governor. The governor is an
//                 automatic gain on the log of the level: down fast whenever the howl passes HOWL LEVEL, up no
//                 faster than SWELL allows, never above 0 dB. So the sustained level is LEVEL, whatever happens.
//   spotlight     one band gets the most gain (lean or the joystick's x picks it, with hysteresis): near the
//                 edge of howling it is the one that sings, so leaning plays a slow melody through the chord
//   colour        the loop's saturation, each with unit slope at zero so the howling threshold stays put
//   freeze        the mic and the seed noise are cut and the inner loop holds whatever is howling
//
// Threads: tick() (UI) works out the band frequencies, weights and the requested drive; audio() smooths
// everything towards them per block and per sample. Shared through plain floats.
#include "core.h"
#include "dsp.h"
#include "ui.h"
#include "oled.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- params
enum {
    P_ROOT, P_OCT, P_CHORD, P_BANDS, P_SPREAD, P_DRIFT, P_GLIDE, P_EVOLVE, P_KEYS,
    P_MODE, P_GAIN, P_RING, P_SPOT, P_LEVEL, P_SWELL, P_MICIN, P_SEED, P_FREEZE,
    P_ROOM, P_SIZE, P_DAMP, P_SHIFT, P_FOLD, P_COLOUR, P_HEAT, P_TIDE, P_TIDERATE,
    P_TILT, P_JOY, P_SENS,
    P_N
};
enum { G_CHORD, G_FEEDBACK, G_ROOM, G_MOTION };
static const char *const groups[] = { "CHORD", "FEEDBACK", "ROOM", "MOTION" };

static const char *const roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const chord_l[] = { "MAJOR", "MINOR", "SUS4", "MAJ7", "MIN7", "DOM7", "ADD9", "FIFTHS",
                                       "OVERTONES", "WHOLE TONE", "DIMINISHED", "HIRAJOSHI", "CLUSTER" };
static const char *const spread_l[] = { "CLOSE", "OPEN", "WIDE" };
static const char *const evolve_l[] = { "OFF", "SLOW", "FAST" };
static const char *const keys_l[] = { "OFF", "HOLD", "LATCH" };
static const char *const mode_l[] = { "INNER", "AIR", "BOTH" };
static const char *const onoff_l[] = { "OFF", "ON" };
static const char *const colour_l[] = { "SOFT", "WARM", "HARD", "FOLD" };
static const char *const tilt_l[] = { "GAIN+NOTE", "GAIN", "NOTE", "SHIFT+NOTE", "OFF" };
static const char *const joy_l[] = { "NOTE+GAIN", "SHIFT+ROOM", "OFF" };

static const param_t params[P_N] = {
    [P_ROOT]     = { "ROOT", "root", G_CHORD, PK_ENUM, 0, 0, 11, 1, 0, "", roots, "THE CHORD'S BOTTOM NOTE" },
    [P_OCT]      = { "OCTAVE", "octv", G_CHORD, PK_NUM, PF_INT, 3, 6, 1, 5, "", NULL, "WHERE THE ROOT SITS" },
    [P_CHORD]    = { "CHORD", "chrd", G_CHORD, PK_ENUM, 0, 0, 12, 1, 0, "", chord_l, "THE NOTES THAT CAN HOWL" },
    [P_BANDS]    = { "BANDS", "band", G_CHORD, PK_NUM, PF_INT, 3, 8, 1, 6, "", NULL, "HOW MANY NOTES CAN HOWL" },
    [P_SPREAD]   = { "SPREAD", "sprd", G_CHORD, PK_ENUM, 0, 0, 2, 1, 0, "", spread_l, "NOTES CLOSE OR WIDE" },
    [P_DRIFT]    = { "DRIFT", "drft", G_CHORD, PK_NUM, PF_INT, 0, 50, 1, 4, "ct", NULL, "SLOW DETUNE: HOWLS BEAT" },
    [P_GLIDE]    = { "GLIDE", "glid", G_CHORD, PK_NUM, PF_LOG, 0.01f, 4.0f, 1.15f, 0.25f, "s", NULL, "NEW NOTES SLIDE IN" },
    [P_EVOLVE]   = { "EVOLVE", "evol", G_CHORD, PK_ENUM, 0, 0, 2, 1, 0, "", evolve_l, "THE CHORD WANDERS" },
    [P_KEYS]     = { "MIDI KEYS", "keys", G_CHORD, PK_ENUM, PF_SYS, 0, 2, 1, 1, "", keys_l, "MIDI NOTES SET THE CHORD" },
    [P_MODE]     = { "MODE", "mode", G_FEEDBACK, PK_ENUM, 0, 0, 2, 1, 0, "", mode_l, "AIR NEEDS THE SPEAKER" },
    [P_GAIN]     = { "GAIN", "gain", G_FEEDBACK, PK_NUM, PF_INT, 0, 100, 2, 50, "%", NULL, "MOVES THE HOWLING EDGE" },
    [P_RING]     = { "RING", "ring", G_FEEDBACK, PK_NUM, PF_LOG, 6, 150, 1.08f, 30, "", NULL, "HOW NARROW EACH NOTE IS" },
    [P_SPOT]     = { "SPOTLIGHT", "spot", G_FEEDBACK, PK_NUM, PF_INT, 0, 90, 5, 60, "%", NULL, "HOW MUCH ONE NOTE LEADS" },
    [P_LEVEL]    = { "HOWL LEVEL", "levl", G_FEEDBACK, PK_NUM, PF_INT, -30, -6, 1, -14, "dB", NULL, "THE LOUDEST A HOWL GETS" },
    [P_SWELL]    = { "SWELL", "swel", G_FEEDBACK, PK_NUM, PF_LOG, 0.2f, 10.0f, 1.15f, 1.5f, "s", NULL, "HOW SLOWLY HOWLS GROW" },
    [P_MICIN]    = { "MIC IN", "micl", G_FEEDBACK, PK_NUM, PF_INT, 0, 200, 5, 100, "%", NULL, "HOW MUCH ROOM GETS IN" },
    [P_SEED]     = { "SEED", "seed", G_FEEDBACK, PK_NUM, PF_INT, -90, -40, 2, -66, "dB", NULL, "A WHISPER TO START ON" },
    [P_FREEZE]   = { "FREEZE", "frez", G_FEEDBACK, PK_ENUM, 0, 0, 1, 1, 0, "", onoff_l, "THE HOWL RINGS ON" },
    [P_ROOM]     = { "ROOM", "room", G_ROOM, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "A ROOM INSIDE THE LOOP" },
    [P_SIZE]     = { "SIZE", "size", G_ROOM, PK_NUM, PF_LOG, 0.002f, 0.25f, 1.08f, 0.04f, "s", NULL, "THE INNER ROOM'S DELAY" },
    [P_DAMP]     = { "DAMP", "damp", G_ROOM, PK_NUM, PF_LOG, 400, 12000, 1.1f, 4000, "Hz", NULL, "THE ROOM'S DULL WALLS" },
    [P_SHIFT]    = { "SHIFT", "shft", G_ROOM, PK_NUM, PF_SIGNED, -20, 20, 0.1f, 0, "Hz", NULL, "HOWLS CLIMB OR FALL" },
    [P_FOLD]     = { "OCTAVE GLOW", "fold", G_ROOM, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "FEEDS OCTAVES BACK IN" },
    [P_COLOUR]   = { "COLOUR", "colr", G_ROOM, PK_ENUM, 0, 0, 3, 1, 0, "", colour_l, "HOW THE LOOP SATURATES" },
    [P_HEAT]     = { "HEAT", "heat", G_ROOM, PK_NUM, PF_LOG, 1, 10, 1.1f, 1, "x", NULL, "HOW HARD THE COLOUR BITES" },
    [P_TIDE]     = { "TIDE", "tide", G_ROOM, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE GAIN BREATHES" },
    [P_TIDERATE] = { "TIDE RATE", "tdrt", G_ROOM, PK_NUM, PF_LOG, 0.02f, 2.0f, 1.12f, 0.1f, "Hz", NULL, "HOW FAST IT BREATHES" },
    [P_TILT]     = { "TILT DOES", "tilt", G_MOTION, PK_ENUM, 0, 0, 4, 1, 0, "", tilt_l, "TIP: GAIN. LEAN: WHICH NOTE" },
    [P_JOY]      = { "STICK DOES", "joyd", G_MOTION, PK_ENUM, 0, 0, 2, 1, 0, "", joy_l, "X AND Y OF THE JOYSTICK" },
    [P_SENS]     = { "TILT RANGE", "sens", G_MOTION, PK_NUM, PF_INT, 20, 80, 5, 50, "deg", NULL, "DEGREES FROM QUIET TO FULL" },
};

static const preset_t presets[] = {
    { "FIRST HOWL",   "chrd=0 octv=5 room=10 size=0.03 drft=4" },
    { "MOON CHOIR",   "root=9 chrd=1 sprd=2 band=7 drft=14 room=35 size=0.12 damp=3000 tide=30 tdrt=0.08 glid=0.8" },
    { "GLASS",        "root=2 chrd=8 band=8 ring=110 octv=4 spot=70 colr=0" },
    { "SHEPARD",      "root=7 chrd=9 band=8 room=100 size=0.05 damp=8000 shft=4 octv=5 ring=8 spot=0" },
    { "FALLING",      "root=4 chrd=12 band=8 room=100 size=0.09 damp=5000 shft=-3 ring=9 spot=0 octv=5" },
    { "CATHEDRAL",    "root=5 chrd=3 sprd=2 band=8 room=45 size=0.25 damp=2600 ring=55 swel=2.5 glid=1.2" },
    { "WOLF PACK",    "root=2 chrd=2 drft=24 colr=1 heat=3 fold=25 band=6 ring=28" },
    { "FURNACE",      "root=0 chrd=12 octv=4 colr=2 heat=6 fold=55 gain=62 tide=40 tdrt=0.3 ring=40" },
    { "TIDE POOL",    "root=10 chrd=9 room=30 size=0.09 tide=60 tdrt=0.2 drft=10" },
    { "HIRAJOSHI",    "root=4 chrd=11 band=7 evol=1 glid=1.5 drft=6 room=20 size=0.07" },
    { "WANDERER",     "root=0 chrd=3 evol=2 glid=0.9 drft=10 room=30 size=0.1 sprd=1" },
    { "SIREN",        "root=7 chrd=7 room=60 size=0.02 shft=11 damp=9000 colr=3 heat=4 band=5 ring=12" },
    { "THE ROOM",     "mode=1 chrd=0 band=6 micl=100 seed=-90" },
    { "AIR CHOIR",    "mode=2 chrd=4 root=9 sprd=1 drft=8 room=20 size=0.05" },
    { "DRONE KEYS",   "chrd=7 band=6 glid=0.4 ring=40 room=25 size=0.08" },
};
#define N_PRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { LEARN_LEAN = LEARN_INST, LEARN_HOWL };
static const hint_t hints[] = {
    { "TIP MY BOTTOM EDGE DOWN TO HOWL", LEARN_TILT },
    { "LEAN LEFT OR RIGHT TO PICK THE NOTE", LEARN_LEAN },
    { "TURN THE KNOB FOR ANOTHER SOUND", LEARN_TURN },
    { "HOLD REC TO CATCH A HOWL IN A LOOP", LEARN_REC },
    { "STICK: X PICKS THE NOTE, Y SWELLS", LEARN_JOY },
    { "HOLD THE KNOB TO KEEP THE TILT", LEARN_HOLD },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

// ---------------------------------------------------------------- chords
#define NB 8
typedef struct { const char *name; float iv[8]; uint8_t n; bool series; } chord_t;
static const chord_t chords[] = {
    { "MAJ",   { 0, 4, 7 }, 3, false },
    { "MIN",   { 0, 3, 7 }, 3, false },
    { "SUS4",  { 0, 5, 7 }, 3, false },
    { "MAJ7",  { 0, 4, 7, 11 }, 4, false },
    { "MIN7",  { 0, 3, 7, 10 }, 4, false },
    { "7",     { 0, 4, 7, 10 }, 4, false },
    { "ADD9",  { 0, 4, 7, 14 }, 4, false },
    { "5",     { 0, 7 }, 2, false },
    { "OVERT", { 0, 7.02f, 12, 15.86f, 19.02f, 21.69f, 24, 26.04f }, 8, true },   // harmonics 2..9 of the root an octave down
    { "WHOLE", { 0, 2, 4, 6, 8, 10 }, 6, false },
    { "DIM",   { 0, 3, 6, 9 }, 4, false },
    { "HIRA",  { 0, 2, 3, 7, 8 }, 5, false },
    { "CLUST", { 0, 1, 2, 3, 4, 5 }, 6, false },
};
#define N_CHORDS ((int)(sizeof chords / sizeof chords[0]))

// ---------------------------------------------------------------- shared between the UI and the audio task
static volatile float band_hz[NB];          // UI -> audio: targets
static volatile float band_w[NB];           //   weights (0 for unused bands)
static volatile float drive_req = 0.6f;     //   the loop gain asked for, before the governor
static volatile float shift_req;            //   extra shift (tilt, joystick), Hz
static volatile float room_req;             //   extra room (joystick), 0..1
static volatile float band_lvl[NB];         // audio -> UI: each band's level (peak per block, smoothed)
static volatile float howl_lvl;             //   the governed output level (envelope)
static volatile float gov_db;               //   the governor's gain, dB (<= 0)

// ---------------------------------------------------------------- audio side
typedef struct { float x1[4], x2[4], y_prev[4], y2[4]; } ap4_t;
static const float HA[4] = { 0.6923878f * 0.6923878f, 0.9360654f * 0.9360654f, 0.9882295f * 0.9882295f, 0.9987488f * 0.9987488f };
static const float HB[4] = { 0.4021921f * 0.4021921f, 0.8561711f * 0.8561711f, 0.9722910f * 0.9722910f, 0.9952885f * 0.9952885f };
static inline float ap4(ap4_t *a, const float *c, float x)
{
    for (int k = 0; k < 4; k++) {
        float y = c[k] * (x + a->y2[k]) - a->x2[k];
        a->x2[k] = a->x1[k]; a->x1[k] = x; a->y2[k] = a->y_prev[k]; a->y_prev[k] = flush_denormal(y);
        x = y;
    }
    return x;
}

#define ROOM_N 8192                          // 256 ms
static float *room_buf;
static dline_t room_dl;
static svf_t   bp[NB];
static float   f_s[NB], k_s[NB], w_s[NB], drift_ph[NB];
static float   y_prev, room_out, room_lp, fold_hp, fold_x1;
static dcb_t   dcb;
static float   drive_s = 0.6f, fb_t_s = 0.92f, fb_r_s, mic_s = 1.0f, frz_s, delay_s = 1280.0f, shift_s, damp_a = 0.5f;
static float   env, agc_db = -6.0f, tide_ph, tide_s = 1.0f, g_s = 0.5f, brake = 1.0f;
static float   tgt_s = 0.2f, env_o, allow = 1e-4f, og = 1.0f, heat_s = 1.0f;
static float   rot_c = 1.0f, rot_s;                // the shifter's oscillator (a turning phasor)
static ap4_t   hil_a, hil_b;
static float   hil_a_z;                            // path A's extra sample of delay
static int     col_cur, col_prev;
static float   col_x = 1.0f;
static float   frz_drive;
static uint32_t seed = 0x2468ace1u;
static float   warm_off;

static inline float colour(int c, float v)
{
    switch (c) {
    case 1: return (soft_tanh(v + 0.35f) - warm_off) * 1.13f;          // WARM: biased, even harmonics
    case 2: return soft_knee(v);                                        // HARD: clean, then a short knee
    case 3: return sin_turns(v * 0.1591549f);                           // FOLD: sin(v), folds past pi/2
    default: return soft_tanh(v);                                       // SOFT
    }
}

static void init(void)
{
    room_buf = app_alloc(ROOM_N * sizeof(float));
    room_dl = (dline_t){ room_buf, ROOM_N - 1, 0 };
    warm_off = soft_tanh(0.35f);
    for (int b = 0; b < NB; b++) { f_s[b] = 523.0f; k_s[b] = 1.0f / 30.0f; svf_set(&bp[b], f_s[b], 30.0f); drift_ph[b] = 0.137f * (float)b; }
}

static void audio(const float *in, float *out, int n)
{
    if (!room_buf) { memset(out, 0, (size_t)n * sizeof(float)); return; }
    const float bdt = (float)n / (float)AUDIO_SR;
    // ---- per block: the bank
    float glide_k = 1.0f - expf(-bdt / fmaxf(pv[P_GLIDE], 0.005f));
    float ring = pv[P_RING];
    float frz_t = pv[P_FREEZE] >= 1.0f ? 1.0f : 0.0f;
    float drift = pv[P_DRIFT];
    float w[NB], cf[NB][4], dc_[NB][4];
    for (int b = 0; b < NB; b++) {
        float ft = band_hz[b];
        if (ft < 30.0f) ft = 30.0f;
        drift_ph[b] = wrap1(drift_ph[b] + bdt * (0.05f + 0.031f * (float)b));
        float det = drift * (0.6f * sin_turns(drift_ph[b]) + 0.4f * sin_turns(drift_ph[b] * 2.618f + 0.3f));
        ft *= fast_exp2(det / 1200.0f);
        f_s[b] += glide_k * (ft - f_s[b]);
        float kt = 1.0f / (frz_s > 0.5f ? fmaxf(ring, 120.0f) : ring);
        k_s[b] *= exp2f(0.0133f * log2f(kt / k_s[b]));        // Q glides in ratio, ~150 ms: no step in a band's gain
        // The one-sample loop adds a phase of -w: a howl locks where the band pass gives +w back, a little
        // below its centre and cos(w) down in gain. Tuned up and boosted by that much, every band howls on
        // its note and at the same gain (only as far as the tight loop is in use).
        float wc = TWO_PI * f_s[b] / (float)AUDIO_SR, tight = fb_t_s * (1.0f / 0.92f);
        float fc = f_s[b] * (1.0f + tight * 0.5f * wc * k_s[b]);
        svf_t nx; svf_set(&nx, fc, 1.0f / k_s[b]);
        cf[b][0] = bp[b].a1; cf[b][1] = bp[b].a2; cf[b][2] = bp[b].a3; cf[b][3] = bp[b].k;
        dc_[b][0] = (nx.a1 - bp[b].a1) / (float)n; dc_[b][1] = (nx.a2 - bp[b].a2) / (float)n;
        dc_[b][2] = (nx.a3 - bp[b].a3) / (float)n; dc_[b][3] = (nx.k - bp[b].k) / (float)n;
        if (frz_s < 0.5f) w_s[b] += 0.05f * (band_w[b] - w_s[b]);       // frozen: the spotlight stays
        w[b] = w_s[b] * (1.0f + tight * (1.0f / cosf(wc) - 1.0f));
    }
    // ---- per block: the loops
    int mode = (int)pv[P_MODE];
    float fb_t_t = mode == 1 ? 0.0f : mode == 2 ? 0.6f : 0.92f;
    float room_t = clampf(pv[P_ROOM] * 0.01f + room_req, 0.0f, 1.0f);
    float fb_r_t = fb_t_t * room_t;
    fb_t_t *= 1.0f - room_t;
    if (frz_t > 0.5f && fb_t_t + fb_r_t < 0.9f) { fb_t_t = 0.92f * (1.0f - room_t); fb_r_t = 0.92f * room_t; }   // frozen in AIR: the inner loop holds it
    float delay_t = clampf(pv[P_SIZE], 0.002f, 0.25f) * (float)AUDIO_SR;
    float damp_t = 1.0f - expf(-TWO_PI * pv[P_DAMP] / (float)AUDIO_SR);
    damp_a += 0.1f * (damp_t - damp_a);
    float shift_t = clampf(pv[P_SHIFT] + shift_req, -40.0f, 40.0f);
    shift_s += 0.05f * (shift_t - shift_s);
    float wr = TWO_PI * shift_s / (float)AUDIO_SR, rc = cosf(wr), rs = sinf(wr);
    { float m = 1.0f / sqrtf(rot_c * rot_c + rot_s * rot_s); rot_c *= m; rot_s *= m; }
    bool shifting = fabsf(shift_s) > 0.005f;
    // the drive: what was asked for, breathing with the tide; frozen: the drive at the moment of freezing
    tide_ph = wrap1(tide_ph + bdt * pv[P_TIDERATE]);
    float tide_t = 1.0f - pv[P_TIDE] * 0.01f * 0.5f * (1.0f - sin_turns(tide_ph));
    float tide0 = tide_s, dtide = (tide_t - tide_s) / (float)n;
    tide_s = tide_t;
    float drive_t = drive_req;
    if (frz_t > 0.5f && frz_s < 0.01f) frz_drive = fmaxf(drive_s, 1.25f);
    if (frz_t > 0.5f) drive_t = frz_drive;
    float swell = pv[P_SWELL];
    float up = 1.0f - expf(-1.0f / ((float)AUDIO_SR * swell)), down = 1.0f - expf(-1.0f / ((float)AUDIO_SR * 0.05f));   // grows slowly, falls fast
    float mic_t = pv[P_MICIN] * 0.01f * (1.0f - frz_t);
    float seed_amp = pv[P_SEED] <= -89.0f ? 0.0f : exp2f(pv[P_SEED] * 0.16609640f) * (1.0f - frz_t);
    int col_t = (int)pv[P_COLOUR];
    if (col_t != col_cur && col_x >= 1.0f) { col_prev = col_cur; col_cur = col_t; col_x = 0.0f; }
    float fold = pv[P_FOLD] * 0.01f * 0.5f;
    float heat0 = heat_s, dheat = (pv[P_HEAT] - heat_s) / (float)n;
    heat_s = pv[P_HEAT];

    // the governor: proportional on the log of the level, down fast, up no faster than SWELL allows
    float target_db = pv[P_LEVEL], target = exp2f(target_db * 0.16609640f);
    {
        float env_db = env > 1e-6f ? 20.0f * log10f(env) : -120.0f;
        float err = target_db - env_db;
        if (err < 0) agc_db += fmaxf(err * 0.05f, -0.6f);
        else agc_db += fminf(err * 0.02f, bdt * 30.0f / swell);
        agc_db = clampf(agc_db, -60.0f, 0.0f);
    }
    float g1 = exp2f(agc_db * 0.16609640f), g = g_s, dg = (g1 - g_s) / (float)n;
    g_s = g1;
    // the brake: per sample, for a loop (a loud speaker) that grows faster than a block; it lets go at SWELL's pace
    brake = fminf(1.0f, brake * exp2f(bdt * 15.0f / swell));
    float t0_ = tgt_s;
    tgt_s += 0.15f * (target - tgt_s);                    // HOWL LEVEL moves the ceiling over about 25 ms
    const float dt_ = (tgt_s - t0_) / (float)n;
    const float rise = 1.000432f;                         // what leaves the box rises at most 120 dB/s

    float blv[NB] = { 0 };
    for (int i = 0; i < n; i++) {
        g += dg;
        float tide = tide0 + dtide * (float)i;
        drive_s += (drive_t > drive_s ? up : down) * (drive_t - drive_s);
        fb_t_s += 0.002f * (fb_t_t - fb_t_s);
        fb_r_s += 0.002f * (fb_r_t - fb_r_s);
        mic_s += 0.002f * (mic_t - mic_s);
        frz_s += 0.0005f * (frz_t - frz_s);
        delay_s += 0.0004f * (delay_t - delay_s);
        // into the bank: the room, the inner loops, a whisper of noise, the octave glow
        float x = in[i] * mic_s + seed_amp * rng_bi(&seed) + fb_t_s * y_prev + fb_r_s * room_out + fold * fold_hp;
        float s = 0;
        for (int b = 0; b < NB; b++) {
            bp[b].a1 = cf[b][0] += dc_[b][0]; bp[b].a2 = cf[b][1] += dc_[b][1];
            bp[b].a3 = cf[b][2] += dc_[b][2]; bp[b].k = cf[b][3] += dc_[b][3];
            if (w[b] < 1e-4f) continue;
            float v = svf_bp(&bp[b], x, NULL) * w[b];
            s += v;
            float a = fabsf(v); if (a > blv[b]) blv[b] = a;
        }
        float v = s * drive_s * tide * g * brake;
        // The colour saturates at 1.3x HOWL LEVEL: the loop limits its own amplitude there at every instant,
        // whatever the gain. HEAT drives the curve harder at the same level. The governor (per block) then
        // eases the gain down until the howl sits at the level, mostly clean.
        float tg = t0_ + dt_ * (float)i, ts = 1.3f * tg, ceil_ = 1.25f * tg;
        float h = heat0 + dheat * (float)i, u = v * h / ts;
        float y = ts * colour(col_cur, u);
        if (col_x < 1.0f) { y = lerpf(ts * colour(col_prev, u), y, col_x); col_x += 1.0f / 160.0f; }
        y = dcb_run(&dcb, y);
        if (!(fabsf(y) < 4.0f)) y = 0.0f;                                // a runaway: never
        float a = fabsf(y);
        env += (a > env ? 0.05f : 0.0004f) * (a - env);
        if (env > 1.5f * tg) brake *= 0.996f;
        y_prev = flush_denormal(y);
        // the room: damped, delayed, shifted
        room_lp = flush_denormal(room_lp + damp_a * (y - room_lp));
        dline_write(&room_dl, room_lp);
        float r = dline_read(&room_dl, delay_s);
        if (shifting) {                                    // a Hilbert pair (path A one sample late) and a turning phasor
            float ia = ap4(&hil_a, HA, r);
            float qb = ap4(&hil_b, HB, r);
            float i_ = hil_a_z; hil_a_z = ia;
            float c = rot_c, sn = rot_s;
            rot_c = c * rc - sn * rs; rot_s = c * rs + sn * rc;
            r = i_ * c + qb * sn;                          // + : up by SHIFT Hz (checked: < 1 % ripple to 6 kHz)
        }
        room_out = r;
        // octave glow: the rectified howl, its DC taken off
        float fr = fabsf(y);
        fold_hp = flush_denormal(fr - fold_x1 + 0.995f * fold_hp); fold_x1 = fr;
        // What leaves the box (and so the speaker's share of an AIR loop) never rises faster than 120 dB/s
        // and never passes 1.25x HOWL LEVEL.
        env_o += (a > env_o ? 0.2f : 0.0015f) * (a - env_o);
        if (env_o > allow) allow *= rise; else allow = env_o;
        if (allow < 1e-4f) allow = 1e-4f;
        float og_t = env_o > allow ? allow / env_o : 1.0f;
        og += 0.03f * (og_t - og);
        out[i] = ceil_ * soft_knee(y * og / ceil_);
    }
    if (brake < 1e-3f) brake = 1e-3f;
    for (int b = 0; b < NB; b++) band_lvl[b] = band_lvl[b] * 0.85f + blv[b] * 0.15f;
    howl_lvl = env;
    gov_db = agc_db;
}

// ---------------------------------------------------------------- UI side
static int   spot = 2;                      // the lit band
static float spot_vis = 2.0f;
static int   nb_now = 6;
static float notes[NB];                     // the bands' notes (MIDI, fractional)
static int   evo_off, evo_chord = -1, evo_n;
static float evo_t;
static uint8_t held[16]; static int nheld, latched_n; static uint8_t latched[16];
static int   sounding = -1, last_cc = -1, cc_ms;
static float ratio_s;                       // howl level / HOWL LEVEL, smoothed (0 .. ~1)
static float u_gain, u_thresh;              // for the gauge
static bool  has_joy_now, has_imu_now;
static char  chord_name[24];
static float lean_y_vis;

static void chord_notes(void)
{
    int root = (int)pv[P_ROOT] + evo_off, ci = evo_chord >= 0 ? evo_chord : (int)pv[P_CHORD];
    int nb = (int)pv[P_BANDS], sp = (int)pv[P_SPREAD];
    float base = (float)(12 * ((int)pv[P_OCT] + 1) + root);
    float L[48]; int nl = 0;
    const uint8_t *keys = nheld ? held : latched;
    int nk = nheld ? nheld : ((int)pv[P_KEYS] == 2 ? latched_n : 0);
    if ((int)pv[P_KEYS] == 0) nk = 0;
    if (nk > 0) {                                           // MIDI keys: the held notes, repeated up the octaves
        for (int o = -1; o < 5 && nl < 40; o++) for (int k = 0; k < nk && nl < 40; k++) L[nl++] = (float)(keys[k] + 12 * o);
        snprintf(chord_name, sizeof chord_name, "KEYS");
    } else {
        const chord_t *c = &chords[ci];
        if (c->series) { for (int k = 0; k < c->n; k++) L[nl++] = base - 12.0f + c->iv[k]; for (int k = 0; k < c->n && nl < 40; k++) L[nl++] = base + 12.0f + c->iv[k]; }
        else for (int o = -1; o < 5 && nl < 40; o++) for (int k = 0; k < c->n && nl < 40; k++) L[nl++] = base + 12.0f * (float)o + c->iv[k];
        snprintf(chord_name, sizeof chord_name, "%s %s", roots[((root % 12) + 12) % 12], c->name);
    }
    // sorted, no repeats
    for (int i = 1; i < nl; i++) for (int j = i; j > 0 && L[j] < L[j - 1]; j--) { float t = L[j]; L[j] = L[j - 1]; L[j - 1] = t; }
    int m = 0; for (int i = 0; i < nl; i++) if (L[i] >= 40.0f && L[i] <= 103.0f && (!m || L[i] - L[m - 1] > 0.3f)) L[m++] = L[i];
    nl = m;
    // the voicing: CLOSE takes the notes from the root up; OPEN every other one; WIDE every other one from an
    // octave down. A chord of one or two pitch classes has nothing to leave out: it is never thinned.
    float from = base - (sp == 2 ? 12.0f : 0.0f);
    if (nk > 0) { from = 127; for (int k = 0; k < nk; k++) if ((float)keys[k] < from) from = (float)keys[k]; }
    int start = 0;
    while (start < nl - 1 && L[start] < from - 0.01f) start++;
    int classes = nk > 0 ? nk : chords[ci].n;
    int step = sp == 0 || classes <= 2 ? 1 : 2;
    int got = 0;
    bool used[48] = { 0 };
    for (int i = start; i < nl && got < nb; i += step) { notes[got++] = L[i]; used[i] = true; }
    for (int i = start; i < nl && got < nb; i++) if (!used[i]) { notes[got++] = L[i]; used[i] = true; }     // then fill in
    for (int i = start - 1; i >= 0 && got < nb; i--) if (!used[i]) { notes[got++] = L[i]; used[i] = true; }  // then below
    while (got < nb) { notes[got] = notes[got - 1]; got++; }
    // the bank wants them low to high
    for (int i = 1; i < nb; i++) for (int j = i; j > 0 && notes[j] < notes[j - 1]; j--) { float t = notes[j]; notes[j] = notes[j - 1]; notes[j - 1] = t; }
    nb_now = nb;
}

static void evolve(float dt)
{
    int e = (int)pv[P_EVOLVE];
    if (!e) { evo_off = 0; evo_chord = -1; evo_t = 0; return; }
    evo_t += dt;
    float period = e == 1 ? 16.0f : 5.0f;
    if (evo_t < period) return;
    evo_t = 0; evo_n++;
    static const int8_t moves[6] = { 5, 7, -5, -7, 3, -3 };
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    int mv = moves[seed % 6];
    int ci = evo_chord >= 0 ? evo_chord : (int)pv[P_CHORD];
    if ((mv == 3 || mv == -3) && (ci == 0 || ci == 1)) ci = ci == 0 ? 1 : 0;     // the relative major or minor
    else if (mv == 3 || mv == -3) mv = mv > 0 ? 5 : -5;
    evo_off += mv;
    while (evo_off > 6) evo_off -= 12;
    while (evo_off < -6) evo_off += 12;
    evo_chord = ci;
}

static void changed(int p)
{
    if (p == -1 || p == P_CHORD || p == P_ROOT || p == P_EVOLVE) { evo_off = 0; evo_chord = -1; evo_t = 0; }
}

static void tick(const ctl_t *c)
{
    has_joy_now = c->has_joy; has_imu_now = c->has_imu;
    evolve(c->dt);
    chord_notes();
    // the spotlight: lean (or the joystick's x) walks it along the bands, one at a time, with hysteresis
    int tm = (int)pv[P_TILT], jm = (int)pv[P_JOY];
    bool tilt_gain = c->has_imu && (tm == 0 || tm == 1), tilt_note = c->has_imu && (tm == 0 || tm == 2 || tm == 3);
    bool tilt_shift = c->has_imu && tm == 3;
    float sens = pv[P_SENS];
    float want = spot_vis;
    if (tilt_note) want = clampf((c->lean_x + 35.0f) / 70.0f, 0.0f, 1.0f) * (float)(nb_now - 1);
    bool joy_note = c->has_joy && jm == 0 && fabsf(c->jx) > 0.15f;
    if (joy_note) want = (c->jx * 0.5f + 0.5f) * (float)(nb_now - 1);
    if (!tilt_note && !joy_note) want = (float)(nb_now - 1) * 0.5f;
    if (fabsf(want - (float)spot) > 0.6f) {
        int ns = (int)lroundf(want);
        if (ns != spot && (tilt_note || joy_note)) coach_done(LEARN_LEAN);
        spot = ns;
    }
    spot = clampi(spot, 0, nb_now - 1);
    spot_vis += 0.25f * ((float)spot - spot_vis);
    float floor_w = 1.0f - pv[P_SPOT] * 0.01f;
    for (int b = 0; b < NB; b++) {
        float d = (float)b - (float)spot;
        band_w[b] = b < nb_now ? floor_w + (1.0f - floor_w) * expf(-d * d / 0.5f) : 0.0f;
        band_hz[b] = midi_hz(b < nb_now ? notes[b] : notes[nb_now - 1]);
    }
    // the gain: tip the bottom edge down (0 at -0.3 of the range, 1 at +0.7), GAIN moves the edge,
    // the joystick's y swells it
    float u = 0.5f;
    if (tilt_gain) u = clampf((c->lean_y + 0.3f * sens) / sens, 0.0f, 1.0f);
    u += (pv[P_GAIN] - 50.0f) * 0.01f;
    if (c->has_joy && jm == 0) u += 0.35f * c->jy;
    u = clampf(u, 0.0f, 1.0f);
    u_gain = u;
    drive_req = 0.3f * expf(2.59f * u);                       // 0.3 .. 4: the family's measured range
    // where the lit band starts to howl in the inner loop: 0.92 x drive = 1
    int mode = (int)pv[P_MODE];
    float fb = mode == 1 ? 0.0f : mode == 2 ? 0.6f : 0.92f;
    u_thresh = fb > 0 ? clampf(logf(1.0f / (fb * 0.3f)) / 2.59f, 0.0f, 1.0f) : -1.0f;
    float sh = 0, rm = 0;
    if (tilt_shift) sh += clampf(c->lean_y / sens, -1.0f, 1.0f) * 10.0f;
    if (c->has_joy && jm == 1) { sh += 10.0f * c->jx; rm = 0.5f * c->jy; }
    shift_req = sh; room_req = rm;
    lean_y_vis = c->lean_y;

    // how much it howls (for the screen, the LED and MIDI)
    float target = exp2f(pv[P_LEVEL] * 0.16609640f);
    float r = clampf(howl_lvl / target, 0.0f, 1.5f);
    ratio_s += (r > ratio_s ? 0.3f : 0.05f) * (r - ratio_s);
    if (ratio_s > 0.6f) coach_done(LEARN_HOWL);
    static float awake_x, awake_y;                           // moving the box is playing it: keep the screen on
    if (fabsf(c->lean_x - awake_x) > 4.0f || fabsf(c->lean_y - awake_y) > 4.0f) { awake_x = c->lean_x; awake_y = c->lean_y; ui_awake(); }
    // the strongest band, as a MIDI note while it howls
    int best = 0; float bl = 0;
    for (int b = 0; b < nb_now; b++) if (band_lvl[b] > bl) { bl = band_lvl[b]; best = b; }
    int note = (int)lroundf(notes[best]);
    if (ratio_s > 0.45f && note != sounding) {
        if (sounding >= 0) midi_note(sounding, 0);
        midi_note(note, clampi(40 + (int)(ratio_s * 80.0f), 1, 127));
        sounding = note;
    } else if (ratio_s < 0.25f && sounding >= 0) { midi_note(sounding, 0); sounding = -1; }
    if ((cc_ms += 10) >= 20) {
        cc_ms = 0;
        int cv = clampi((int)(ratio_s * 127.0f), 0, 127);
        if (abs(cv - last_cc) >= 2 || (cv != last_cc && (cv == 0 || cv == 127))) { midi_cc(11, cv); last_cc = cv; }
    }
    // the LED: night blue when quiet, through violet to moonlight when it howls
    float l = clampf(ratio_s, 0.0f, 1.0f);
    led_rgb((uint8_t)(10 + 200 * l), (uint8_t)(4 + 160 * l * l), (uint8_t)(50 + 170 * l));
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t k = st & 0xF0;
    if ((int)pv[P_KEYS] == 0) return;
    if (k == 0x90 && d2 > 0) {
        if (!nheld) latched_n = 0;
        if (nheld < 16) held[nheld++] = d1;
        if (latched_n < 16) latched[latched_n++] = d1;
    } else if (k == 0x80 || (k == 0x90 && d2 == 0)) {
        for (int i = 0; i < nheld; i++) if (held[i] == d1) { held[i] = held[--nheld]; break; }
    } else if (k == 0xB0 && d1 == 123) { nheld = 0; latched_n = 0; }
}

// ---------------------------------------------------------------- drawing
static uint8_t star_x[28], star_y[28], star_p[28];
static bool stars_ok;
static parts_t parts;
// the howl, as arcs of sound travelling from the wolf's mouth to the moon
typedef struct { float x, y, vx, vy, age; } arc_t;
static arc_t arcs[12];
static int narcs;
static void arc_emit(float x, float y, float tx, float ty, float speed)
{
    if (narcs >= 12) return;
    float dx = tx - x, dy = ty - y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
    arcs[narcs++] = (arc_t){ x, y, dx / d * speed, dy / d * speed, 0 };
}
static void arcs_step_draw(float dt, float stop_x)
{
    int j = 0;
    for (int i = 0; i < narcs; i++) {
        arc_t a = arcs[i];
        a.age += dt; a.x += a.vx * dt; a.y += a.vy * dt;
        if (a.age > 1.6f || a.x > stop_x || a.y < 12.0f) continue;
        float dir = atan2f(a.vy, a.vx) * 57.29578f, r = 2.0f + a.age * 5.0f;
        float span = 50.0f;
        for (float an = dir - span; an <= dir + span; an += 40.0f / (r + 1.0f)) {
            float t = an * 0.0174533f;
            int px = (int)lroundf(a.x + r * cosf(t)), py = (int)lroundf(a.y + r * sinf(t));
            if (py >= 17 && (a.age < 1.2f || ((px + py) & 1))) oled_pixel(px, py, true);
        }
        arcs[j++] = a;
    }
    narcs = j;
}
static uint32_t pseed = 0x51ed27u;
static float head_a;                        // the wolf's head, 0 level .. 1 thrown back
static int  ring_cd;
static float last_lvl[NB];

static void stars_init(void)
{
    if (stars_ok) return;
    uint32_t s = 0x9e3779b9u;
    for (int i = 0; i < 28; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        star_x[i] = (uint8_t)(s % 128u); star_y[i] = (uint8_t)(17 + (s >> 8) % 26u); star_p[i] = (uint8_t)(s >> 16);
    }
    stars_ok = true;
}

static void draw_stars(float t, int y_max, float fade)
{
    stars_init();
    for (int i = 0; i < 28; i++) {
        if (star_y[i] > y_max) continue;
        if ((float)(star_p[i] & 63) / 63.0f > fade) continue;
        float tw = sin_turns(t * (0.2f + (float)(star_p[i] & 7) * 0.09f) + (float)star_p[i] * 0.01f);
        if (tw < -0.55f) continue;                                    // twinkling off
        int x = star_x[i], y = star_y[i];
        oled_pixel(x, y, true);
        if ((star_p[i] & 0x30) == 0x30 && tw > 0.7f) {                // the bright ones flare
            oled_pixel(x - 1, y, true); oled_pixel(x + 1, y, true); oled_pixel(x, y - 1, true); oled_pixel(x, y + 1, true);
        }
    }
}

// scanline fill of a polygon (even-odd), float vertices
static void fill_poly(const float *px, const float *py, int n, bool on)
{
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; i++) { if (py[i] < ymin) ymin = py[i]; if (py[i] > ymax) ymax = py[i]; }
    for (int y = (int)ceilf(ymin - 0.5f); y <= (int)floorf(ymax + 0.5f); y++) {
        float yc = (float)y, xs[16]; int k = 0;
        for (int i = 0; i < n && k < 16; i++) {
            float x0 = px[i], y0 = py[i], x1 = px[(i + 1) % n], y1_ = py[(i + 1) % n];
            if ((y0 <= yc && y1_ > yc) || (y1_ <= yc && y0 > yc)) xs[k++] = x0 + (yc - y0) / (y1_ - y0) * (x1 - x0);
        }
        for (int i = 1; i < k; i++) for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        for (int i = 0; i + 1 < k; i += 2)
            for (int x = (int)lroundf(xs[i]); x <= (int)lroundf(xs[i + 1]); x++) oled_pixel(x, y, on);
    }
}

// The wolf, sitting, facing right; (x, g) is its rump on the ground. head 0 = level, 1 = howling at the sky.
// mouth 0..1 opens the jaw. Returns the tip of the nose in *mx, *my.
static void wolf(float x, float g, float head, float mouth, float tail, float *mx, float *my)
{
    float px[16], py[16];
    // the body: rump, the back rising to a heavy neck and mane, the chest, the front legs
    static const float bx[] = { 0, -1, 1, 5, 9, 12, 15, 18, 19, 20, 21 };
    static const float by[] = { 0, -5, -9, -12, -14, -16, -17, -15, -11, -6, 0 };
    for (int i = 0; i < 11; i++) { px[i] = x + bx[i]; py[i] = g + by[i]; }
    fill_poly(px, py, 11, true);
    oled_disc((int)lroundf(x + 6), (int)lroundf(g - 5), 5, true);          // the haunch
    for (int k = 1; k <= 5; k++) oled_pixel((int)lroundf(x + 18), (int)lroundf(g - (float)k), false);   // two front legs
    oled_pixel((int)lroundf(x + 11), (int)lroundf(g - 1), false);           // the hind paw's line
    // the tail: bushy, lying on the ground behind, swaying a little
    float tx[] = { x + 1, x - 3, x - 7 + tail, x - 10 + tail, x - 12 + tail, x - 11 + tail, x - 6, x };
    float ty[] = { g - 4, g - 6, g - 6, g - 4, g - 1, g, g, g };
    fill_poly(tx, ty, 8, true);
    // the head turns about the neck
    float an = head * 0.9f;                                                // up to about 52 degrees
    float ca = cosf(an), sa = sinf(an), nx = x + 15, ny = g - 15;
#define ROT(i, lx, ly) (px[i] = nx + (lx) * ca + (ly) * sa, py[i] = ny - (lx) * sa + (ly) * ca)
    static const float hx[] = { -3, -1, 3, 7, 12, 14, 14, 9, 6, 2, -3 };
    static const float hy[] = { 0, -3, -4, -3, -2, -1, 1, 1, 2, 4, 3 };
    for (int i = 0; i < 11; i++) {
        float lx = hx[i], ly = hy[i];
        if (i == 6 || i == 7) ly += mouth * (i == 6 ? 3.0f : 1.5f);          // the jaw drops
        ROT(i, lx, ly);
    }
    fill_poly(px, py, 11, true);
    if (mouth > 0.25f) { ROT(0, 8, 0.5f); ROT(1, 14, -0.3f); ROT(2, 13, 0.6f + 2.4f * mouth); fill_poly(px, py, 3, false); }   // the open mouth
    ROT(0, 0, -3); ROT(1, 1, -8); ROT(2, 3, -4); fill_poly(px, py, 3, true);   // the ears
    ROT(0, 3, -4); ROT(1, 5, -8); ROT(2, 6, -3.5f); fill_poly(px, py, 3, true);
    ROT(0, 5, -1.5f); oled_pixel((int)lroundf(px[0]), (int)lroundf(py[0]), false);   // the eye
    ROT(0, 14.5f, 0); *mx = px[0]; *my = py[0];
#undef ROT
}

static void hill(int x0, int x1, int top, int base)
{
    for (int x = x0; x <= x1; x++) {
        float u = ((float)(x - x0) / (float)(x1 - x0)) * 2.0f - 1.0f;
        int h = (int)lroundf((float)top + (float)(base - top) * u * u);
        for (int y = h; y <= base; y++) if (((x + y) & 1) == 0 || y == h) oled_pixel(x, y, true);
    }
}

static void moon(int cx, int cy, int r, float glow)
{
    // (the glow stays out of the header band)
    // the glow: dotted rings that grow with the howl
    for (int k = 1; k <= 3; k++) {
        if (glow < 0.25f * (float)k) break;
        int rr = r + 2 * k + (int)(glow * 2.0f);
        for (int a = 0; a < 360; a += 360 / (8 + rr * 2)) {
            float t = (float)a * 0.0174533f;
            int gx = cx + (int)lroundf((float)rr * cosf(t)), gy = cy + (int)lroundf((float)rr * sinf(t));
            if ((((a / 9) + k) & 1) && gy >= 17) oled_pixel(gx, gy, true);
        }
    }
    oled_disc(cx, cy, r, true);
    oled_pixel(cx - r / 3, cy - r / 3, false); oled_pixel(cx + r / 4, cy + r / 3, false);   // seas
    oled_pixel(cx + r / 3, cy - r / 4, false); oled_pixel(cx - r / 4 + 1, cy + r / 2, false);
}

// Each band is a string with a standing wave; it shivers with its level, glows when it takes off.
static void strings(float t, int x0, int x1, int y0, int yb)
{
    float span = (float)(x1 - x0) / (float)(nb_now > 1 ? nb_now - 1 : 1);
    float target = exp2f(pv[P_LEVEL] * 0.16609640f);
    for (int b = 0; b < nb_now; b++) {
        int xb = x0 + (int)lroundf(span * (float)b);
        float lv = clampf(band_lvl[b] / (target * 1.2f), 0.0f, 1.0f);
        float A = 3.2f * sqrtf(lv);
        int m = 1 + b % 3;
        float ph = sin_turns(t * (1.1f + 0.45f * (float)b));
        int px = xb;
        bool hot = lv > 0.55f;
        if (hot) oled_dither(xb - 2, y0 + 2, 5, yb - y0 - 3, 1, true);     // it glows
        for (int y = y0; y <= yb; y++) {
            float s = sin_turns(0.5f * (float)m * (float)(y - y0) / (float)(yb - y0));
            int x = xb + (int)lroundf(A * s * ph);
            if (y > y0) oled_line(px, y - 1, x, y, true); else oled_pixel(x, y, true);
            if (hot) oled_pixel(x + 1, y, true);
            px = x;
        }
        oled_pixel(xb, y0 - 1, true); oled_pixel(xb, yb + 1, true);       // the pegs
        if (lv > 0.55f && last_lvl[b] <= 0.55f) parts_burst(&parts, (float)xb, (float)y0, 6, 18.0f, 0.5f, PART_SPARK, &pseed);
        last_lvl[b] = lv;
    }
    // the spotlight: the lit band's note over its string
    int xs = x0 + (int)lroundf(span * spot_vis);
    char nm[6]; ui_note_name((int)lroundf(notes[spot]), nm, sizeof nm);
    int w = oled_ftext_w(F_TINY, nm, 1);
    int lx = clampi(xs - w / 2, x0 - 4, x1 + 4 - w);
    oled_ftext_ex(F_TINY, lx, 16, nm, 1);
}

static void draw(const view_t *v)
{
    float dt = 1.0f / 25.0f;
    ui_head(NULL, false);
    float r = clampf(ratio_s, 0.0f, 1.2f);
    // the sky
    draw_stars(v->t, 45, 1.0f);
    moon(113, 27, 6 + (int)lroundf(2.0f * clampf(r, 0, 1)), r);
    strings(v->t, 48, 96, 23, 41);
    // the wolf on its hill: its head goes back as the howl blooms
    hill(-8, 46, 42, 46);
    float want = smoothstep(0.25f, 0.8f, r);
    head_a += (want > head_a ? 0.25f : 0.08f) * (want - head_a);
    float mx, my;
    wolf(9, 43, head_a, smoothstep(0.35f, 0.9f, r), 1.5f * sin_turns(v->t * 0.3f), &mx, &my);
    if (r > 0.5f && --ring_cd <= 0) {                                       // the howl travels to the moon
        arc_emit(mx + 1.0f, my, 113.0f, 27.0f, 34.0f + 14.0f * r);
        ring_cd = r > 0.9f ? 5 : 8;
    }
    arcs_step_draw(dt, 100.0f);
    parts_step(&parts, dt, 0.0f, 0.3f);
    parts_draw(&parts);
    // the strip: mode, the chord, the gain gauge, the looper
    if (v->cover == 0) {
        oled_hline_pat(44, 46, 84, 0x11, true);
        bool frz = pv[P_FREEZE] >= 1.0f;
        int x = oled_pill(F_SMALL, 1, 48, frz ? "FROZEN" : mode_l[(int)pv[P_MODE]], frz) + 4;
        ui_text_fit(F_SMALL, x, 50, 88 - x, chord_name);
        int gx = 92, gw = 34;
        oled_rframe(gx, 49, gw, 7, 2);
        int f = (int)lroundf(u_gain * (float)(gw - 4));
        oled_rect(gx + 2, 51, f, 3, true);
        if (u_thresh >= 0) { int tx = gx + 2 + (int)lroundf(u_thresh * (float)(gw - 4)); oled_rect(tx, 48, 1, 9, true); oled_pixel(tx, 51, false); oled_pixel(tx, 53, false); }
        ui_loop(1, 56, 126);
    }
}

// ---------------------------------------------------------------- splash and screensaver
static bool splash(int f)
{
    float t = (float)f / 25.0f;
    float rise = ease_out(clampf((float)f / 38.0f, 0.0f, 1.0f));
    draw_stars(t, 50, clampf((float)f / 30.0f, 0.0f, 1.0f));
    // the moon comes up over the horizon
    int my = (int)lroundf(64.0f - 36.0f * rise);
    float glow = f > 52 ? clampf((float)(f - 52) / 20.0f, 0.0f, 1.0f) : 0.0f;
    moon(102, my, 9, glow);
    oled_rect(56, 53, OLED_W - 56, 11, false);
    oled_hline_pat(56, 53, OLED_W - 56, 0x11, true);
    // the wolf climbs onto its hill, sits, throws its head back and howls
    hill(-8, 54, 46, 53);
    float wy = 47.0f + 18.0f * (1.0f - ease_out(clampf((float)(f - 12) / 26.0f, 0.0f, 1.0f)));
    float mx, my2;
    wolf(12, wy, smoothstep(44.0f, 56.0f, (float)f), smoothstep(50.0f, 58.0f, (float)f), 0, &mx, &my2);
    oled_rect(0, 54, 56, 10, false);
    if (f >= 54 && f < 92 && f % 6 == 0) arc_emit(mx + 1.0f, my2, 102.0f, (float)my, 40.0f);
    arcs_step_draw(0.04f, 90.0f);
    // the title, a letter at a time, in the band at the top
    if (f >= 60) {
        static const char *L[4] = { "H", "O", "W", "L" };
        int shown = clampi((f - 60) / 4 + 1, 0, 4), x = 47;
        for (int i = 0; i < shown; i++) x = oled_ftext_ex(F_MED, x, 3, L[i], 0) + 6;
    }
    if (f >= 78) oled_ftext_c(F_TINY, 64, 57, INST.tagline, 1);
    return f < 100;
}

static void saver(uint32_t f)
{
    float t = (float)f / 25.0f;
    stars_init();
    for (int i = 0; i < 28; i++) {                                          // the sky turns slowly
        int x = (int)((float)star_x[i] + t * 0.6f) % 128, y = star_y[i] - 3 + (star_p[i] & 3) * 8;
        if (sin_turns(t * 0.3f + (float)star_p[i] * 0.02f) > -0.4f) oled_pixel(x, y, true);
    }
    float u = fmodf(t / 90.0f, 2.0f), p = u < 1.0f ? u : 2.0f - u;             // the moon crosses and comes back
    int mx = 10 + (int)lroundf(108.0f * ease_in_out(p));
    int myp = 20 + (int)lroundf(22.0f * (2.0f * p - 1.0f) * (2.0f * p - 1.0f));
    moon(mx, myp, 6, 0.3f);
    int k = (int)(t / 9.0f), ph = (int)(f % 225u);
    if (ph < 12) {                                                          // now and then, a shooting star
        int sx = 20 + (k * 37) % 80, sy = 8 + (k * 13) % 20;
        oled_line(sx + ph * 3, sy + ph, sx + ph * 3 - 6, sy + ph - 2, true);
    }
}

const instrument_t INST = {
    .name = "HOWL", .tagline = "FEEDBACK IN TUNE",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 4,
    .presets = presets, .npresets = N_PRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .looper = true, .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .midi_in = midi_in, .midi_notes = true, .saver = saver,
};

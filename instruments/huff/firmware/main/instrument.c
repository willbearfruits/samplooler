// HUFF: a wind instrument you blow into. The mic follows your breath (its level over the room's), the
// breath drives a physical model, and tilting picks the note from a scale: bottom edge down for higher
// notes, as if the box were a fipple flute held in front of you. Seven models on three engines:
//
//   JET   (FLUTE, PAN PIPE, SHAKUHACHI, WHISTLE)  Perry Cook's STK Flute loop: a jet delay, the jet's
//          cubic and a bore delay. The jet's length sets the mode: 0.45 of the bore holds the fundamental,
//          0.22 the octave (measured in tools/sim), so blowing hard overblows an octave on its own.
//   REED  (CLARINET)  STK's Clarinet: a reed table on a half-length bore, odd harmonics; blowing hard
//          jumps a twelfth (the register key).
//   VESSEL (OCARINA, BOTTLE)  a Helmholtz tone: a sine with a little 2nd and 3rd, air noise rung through
//          a band pass at the note, a chiff on the attack, the pitch rising a little with the breath as a
//          real ocarina's does.
//
// Around them: a harmony voice (a third, fifth, octave or a drone), vibrato with a delay, bends from the
// joystick, a four-line feedback delay network reverb and an echo. The core's looper records it all.
//
// Pitch comes from the tilt (a ladder of notes, with hysteresis), the joystick, or your own humming (YIN on
// the mic); MIDI notes in override it while held, so a keyboard can play it while you blow. MIDI out:
// notes, breath as CC 2, the bend as pitch bend.
#include "core.h"
#include "dsp.h"
#include "ui.h"
#include "oled.h"
#include "looper.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------- params
enum {
    P_MODEL, P_SCALE, P_KEY, P_OCT, P_NOTES,
    P_SOURCE, P_SNAP, P_SPAN, P_GLIDE, P_BENDR, P_LEAN, P_JOYY, P_TUNE,
    P_SENSE, P_CURVE, P_GATE, P_OVER, P_BPITCH, P_BSETUP,
    P_AIR, P_CHIFF, P_BRIGHT, P_VIB, P_VIBR, P_VIBD, P_HARM, P_HLVL,
    P_REVERB, P_SIZE, P_DAMP, P_ECHO, P_ETIME, P_EFB,
    P_BRANGE,
    P_N
};
enum { G_PLAY, G_PITCH, G_BREATH, G_TONE, G_SPACE };
static const char *const groups[] = { "PLAY", "PITCH", "BREATH", "TONE", "SPACE" };

enum { M_OCARINA, M_FLUTE, M_PANPIPE, M_SHAKU, M_CLARINET, M_WHISTLE, M_BOTTLE, M_N };
static const char *const model_l[M_N] = { "OCARINA", "FLUTE", "PAN PIPE", "SHAKUHACHI", "CLARINET", "WHISTLE", "BOTTLE" };
enum { SC_N = 10 };
static const char *const scale_l[SC_N] = { "MAJ PENTA", "MIN PENTA", "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "HIJAZ", "BLUES", "IN SEN", "CHROMATIC" };
static const struct { int n; int8_t st[12]; } scales[SC_N] = {
    { 5, { 0, 2, 4, 7, 9 } }, { 5, { 0, 3, 5, 7, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 11 } }, { 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { 7, { 0, 2, 3, 5, 7, 9, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 10 } }, { 7, { 0, 1, 4, 5, 7, 8, 10 } }, { 6, { 0, 3, 5, 6, 7, 10 } },
    { 5, { 0, 1, 5, 7, 10 } }, { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
};
static const char *const key_l[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const source_l[] = { "TILT", "JOYSTICK", "HUMMING" };
static const char *const onoff_l[] = { "OFF", "ON" };
static const char *const lean_l[] = { "VIBRATO", "BEND", "NOTHING" };
static const char *const joyy_l[] = { "VIBRATO", "BRIGHTNESS", "OCTAVE" };
static const char *const curve_l[] = { "SOFT", "EVEN", "HARD" };
static const char *const harm_l[] = { "OFF", "3RD UP", "5TH UP", "OCTAVE UP", "OCTAVE DN", "DRONE", "DRONE 5TH" };
enum { H_OFF, H_THIRD, H_FIFTH, H_OCTUP, H_OCTDN, H_DRONE, H_DRONE5 };

static const param_t params[P_N] = {
    [P_MODEL]  = { "MODEL", "modl", G_PLAY, PK_ENUM, 0, 0, M_N - 1, 1, M_OCARINA, "", model_l, "WHAT YOU ARE BLOWING INTO" },
    [P_SCALE]  = { "SCALE", "scal", G_PLAY, PK_ENUM, 0, 0, SC_N - 1, 1, 0, "", scale_l, "THE NOTES THE TILT WALKS" },
    [P_KEY]    = { "KEY", "key ", G_PLAY, PK_ENUM, 0, 0, 11, 1, 0, "", key_l, "THE SCALE'S HOME NOTE" },
    [P_OCT]    = { "OCTAVE", "octv", G_PLAY, PK_NUM, PF_INT | PF_SIGNED, -2, 2, 1, 0, "", NULL, "SHIFT ALL NOTES BY OCTAVES" },
    [P_NOTES]  = { "NOTES", "nots", G_PLAY, PK_NUM, PF_INT, 3, 15, 1, 10, "", NULL, "NOTES ON THE TILT LADDER" },
    [P_SOURCE] = { "PITCH FROM", "psrc", G_PITCH, PK_ENUM, 0, 0, 2, 1, 0, "", source_l, "WHAT CHOOSES THE NOTE" },
    [P_SNAP]   = { "SNAP", "snap", G_PITCH, PK_ENUM, 0, 0, 1, 1, 1, "", onoff_l, "OFF: SLIDE BETWEEN NOTES" },
    [P_SPAN]   = { "TILT SPAN", "span", G_PITCH, PK_NUM, PF_INT, 30, 120, 5, 70, "deg", NULL, "DEGREES FROM LOW TO HIGH" },
    [P_GLIDE]  = { "GLIDE", "glid", G_PITCH, PK_NUM, PF_LOG, 0.002f, 1.0f, 1.25f, 0.025f, "s", NULL, "TIME TO SLIDE TO A NOTE" },
    [P_BENDR]  = { "BEND RANGE", "bndr", G_PITCH, PK_NUM, PF_INT, 1, 12, 1, 2, "st", NULL, "HOW FAR THE STICK BENDS" },
    [P_LEAN]   = { "LEAN DOES", "lean", G_PITCH, PK_ENUM, 0, 0, 2, 1, 0, "", lean_l, "SIDE TO SIDE TILT" },
    [P_JOYY]   = { "STICK UP/DN", "joyy", G_PITCH, PK_ENUM, 0, 0, 2, 1, 0, "", joyy_l, "WHAT UP/DOWN ON IT DOES" },
    [P_TUNE]   = { "A4 TUNING", "tune", G_PITCH, PK_NUM, PF_INT, 415, 466, 1, 440, "Hz", NULL, "TUNE TO OTHER PLAYERS" },
    [P_SENSE]  = { "SENSITIVITY", "sens", G_BREATH, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "MORE: SOFT BREATH, LOUD" },
    [P_CURVE]  = { "CURVE", "curv", G_BREATH, PK_ENUM, 0, 0, 2, 1, 1, "", curve_l, "HOW BREATH MAPS TO LEVEL" },
    [P_GATE]   = { "GATE", "gate", G_BREATH, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "MORE: IGNORE THE ROOM" },
    [P_OVER]   = { "OVERBLOW AT", "ovbl", G_BREATH, PK_NUM, PF_INT, 50, 100, 5, 85, "%", NULL, "JUMP UP HERE. 100: NEVER" },
    [P_BPITCH] = { "BREATH PITCH", "bpch", G_BREATH, PK_NUM, PF_INT, 0, 50, 2, 12, "ct", NULL, "HARD BREATH GOES SHARP" },
    [P_BSETUP] = { "BREATH SETUP", "bset", G_BREATH, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "IT LEARNS YOUR BREATH" },
    [P_AIR]    = { "BREATHINESS", "air ", G_TONE, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "HOW MUCH AIR YOU HEAR" },
    [P_CHIFF]  = { "CHIFF", "chif", G_TONE, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "THE PUFF AS A NOTE STARTS" },
    [P_BRIGHT] = { "BRIGHTNESS", "brit", G_TONE, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK TO BRIGHT" },
    [P_VIB]    = { "VIBRATO", "vibd", G_TONE, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "PITCH WOBBLE, ALWAYS ON" },
    [P_VIBR]   = { "VIB RATE", "vibr", G_TONE, PK_NUM, 0, 2, 9, 0.25f, 5.5f, "Hz", NULL, "WOBBLES PER SECOND" },
    [P_VIBD]   = { "VIB DELAY", "vdly", G_TONE, PK_NUM, 0, 0, 1.5f, 0.05f, 0.3f, "s", NULL, "WAIT BEFORE THE WOBBLE" },
    [P_HARM]   = { "HARMONY", "harm", G_TONE, PK_ENUM, 0, 0, 6, 1, H_OFF, "", harm_l, "A SECOND VOICE" },
    [P_HLVL]   = { "HARM LEVEL", "hlvl", G_TONE, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SECOND VOICE LEVEL" },
    [P_REVERB] = { "REVERB", "rvb ", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "HOW MUCH ROOM" },
    [P_SIZE]   = { "ROOM SIZE", "rsiz", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "SMALL ROOM TO CATHEDRAL" },
    [P_DAMP]   = { "DAMPING", "damp", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "SOFT WALLS: DARKER TAIL" },
    [P_ECHO]   = { "ECHO", "echo", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "A CANYON BEHIND YOU" },
    [P_ETIME]  = { "ECHO TIME", "etim", G_SPACE, PK_NUM, 0, 0.05f, 0.6f, 0.01f, 0.33f, "s", NULL, "TIME BETWEEN REPEATS" },
    [P_EFB]    = { "REPEATS", "efb ", G_SPACE, PK_NUM, PF_INT, 0, 90, 5, 35, "%", NULL, "HOW LONG THE ECHO LASTS" },
    [P_BRANGE] = { "BREATH RANGE", "brng", G_BREATH, PK_NUM, PF_HIDE | PF_SYS | PF_NOCC | PF_LOG, 0.01f, 1.0f, 1.1f, 0.14f, "", NULL, "SET BY BREATH SETUP" },
};

// ---------------------------------------------------------------- the sounds
static const preset_t presets[] = {
    { "OCARINA",       "modl=0 scal=0 rvb=30 air=30 chif=40 vibd=15" },
    { "FLUTE",         "modl=1 scal=2 rvb=25 air=30 chif=30 vibd=20 vdly=0.4" },
    { "SHAKUHACHI",    "modl=3 scal=8 key=2 octv=-1 air=70 chif=70 vibd=10 vdly=0.6 rvb=45 rsiz=75 bndr=3" },
    { "PAN PIPES",     "modl=2 scal=1 key=9 air=55 chif=50 vibd=12 rvb=35 echo=25 etim=0.28" },
    { "CLARINET",      "modl=4 scal=4 key=10 octv=-1 air=15 chif=20 vibd=0 rvb=25 brit=45" },
    { "TIN WHISTLE",   "modl=5 scal=2 key=2 octv=-1 air=20 chif=55 vibd=10 vdly=0.5 rvb=20" },
    { "BOTTLE",        "modl=6 scal=7 key=4 octv=-1 air=80 chif=30 vibd=0 rvb=40 nots=8" },
    { "CAVE",          "modl=0 scal=1 key=9 rvb=70 rsiz=95 damp=60 echo=35 etim=0.45 efb=45 vibd=20" },
    { "TWIN OCARINA",  "modl=0 scal=2 harm=1 hlvl=60 rvb=35 vibd=15" },
    { "DESERT",        "modl=1 scal=6 key=4 air=45 vibd=30 vibr=6 vdly=0.2 echo=30 etim=0.38 rvb=40 bndr=3" },
    { "DRONE PIPES",   "modl=2 scal=4 key=2 harm=5 hlvl=45 air=50 rvb=50 rsiz=80" },
    { "THEREMIN",      "modl=1 snap=0 nots=12 span=80 vibd=45 vibr=6 glid=0.08 air=10 chif=0 rvb=45" },
    { "HUM ALONG",     "modl=0 psrc=2 scal=0 rvb=35 glid=0.04" },
    { "BLUES REED",    "modl=4 scal=7 key=7 octv=-1 bndr=3 vibd=10 rvb=30 echo=20 etim=0.25" },
};
#define N_PRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_BLOW = LEARN_INST, L_OVERBLOW, L_SETUP };
static const hint_t hints[] = {
    { "BLOW GENTLY AT THE MIC", L_BLOW },
    { "TILT TO CHANGE THE NOTE", LEARN_TILT },
    { "BLOW SOFT, THEN HARDER: IT FOLLOWS", L_OVERBLOW },
    { "TURN THE KNOB: NEW SOUND", LEARN_TURN },
    { "STICK LEFT OR RIGHT BENDS THE NOTE", LEARN_JOY },
    { "HOLD REC TO LOOP, THEN PLAY ALONG", LEARN_REC },
    { "HOLD THE KNOB TO KEEP THE NOTE", LEARN_HOLD },
    { "PRESS THE KNOB FOR MORE", LEARN_MENU },
};

// ---------------------------------------------------------------- the models
enum { E_VESSEL, E_JET, E_REED };
typedef struct {
    uint8_t eng;
    int8_t  shift;          // semitones above the ladder (whistle +12, bottle -12)
    float   noise;          // breath noise in the pressure (jet, reed); the air level (vessel)
    float   pole;           // jet: the bore end's low pass; higher is darker
    float   tune_a, tune_k; // jet, reed: how sharp the loop plays at MIDI 76 (cents) and per semitone above it
                            // (measured in tools/sim, HUFF_CAL=1, at a normal breath); taken off the bore delay
    float   jet_hi;         // jet: overblown jet length (0.45 of the bore normally)
    float   ob_st;          // overblow in semitones: 12 (octave, jet: the jet does it), 19.02 (twelfth), 0 none
    float   h2, h3, q;      // vessel: overtones, the air resonance's Q
    float   gain;           // level: about -18 dBFS rms at a normal breath
    float   tone;           // tone filter, in harmonics, at BRIGHTNESS 50 %
    float   chiff_f;        // chiff band, in harmonics
} model_t;
static const model_t models[M_N] = {
    [M_OCARINA]  = { E_VESSEL, 0,   0.30f, 0,     0,     0,      0,     0,      0.10f, 0.035f, 7.0f,  0.56f, 5.0f, 3.0f },
    [M_FLUTE]    = { E_JET,    0,   0.12f, 0.63f, 28.5f, -0.96f, 0.22f, 12.0f,  0, 0, 0,             0.17f, 8.0f, 4.0f },
    [M_PANPIPE]  = { E_JET,    0,   0.24f, 0.70f, 14.5f, -1.67f, 0.22f, 12.0f,  0, 0, 0,             0.17f, 5.0f, 3.0f },
    [M_SHAKU]    = { E_JET,    0,   0.30f, 0.66f, 22.6f, -1.32f, 0.22f, 12.0f,  0, 0, 0,             0.17f, 6.0f, 2.5f },
    [M_CLARINET] = { E_REED,   0,   0.12f, 0,     0,     0,      0,     19.02f, 0, 0, 0,             0.36f, 9.0f, 5.0f },
    [M_WHISTLE]  = { E_JET,    12,  0.06f, 0.55f, 39.7f, -0.08f, 0.22f, 12.0f, 0, 0, 0,             0.16f, 10.0f, 5.0f },
    [M_BOTTLE]   = { E_VESSEL, -12, 0.75f, 0,     0,     0,      0,     0,      0.02f, 0.01f, 22.0f, 0.8f, 3.0f, 2.0f },
};

// ---------------------------------------------------------------- shared between the UI and the audio task
static volatile float a_midi = 72.0f, a_midi_h = 79.0f;   // target notes (MIDI, fractional), before bends
static volatile float a_bend;                            // semitones
static volatile float a_vib;                             // vibrato depth now, semitones
static volatile float a_bright_add;                      // brightness from the joystick, -0.5..0.5
static volatile int   a_harm_on;                         // the harmony voice sounds
static volatile int   a_mute;                            // breath setup listens with the voice off
static volatile float a_cc_breath;                       // MIDI CC 2 in, 0..1 (decays when it stops)
static volatile int   a_onset_req;                       // the UI saw a new note while blowing: a small chiff
// audio -> UI
static volatile float v_breath, v_amp, v_benv, v_floor, v_oenv;
static float v_lvl;
float huff_breath(void) { return v_breath; }      // for tools/sim

static volatile int   v_over;
static volatile uint32_t v_onsets;

// ---------------------------------------------------------------- the audio side
#define BORE_N 1024
typedef struct {
    float bore_buf[BORE_N], jet_buf[512];
    dline_t bore, jet;
    float bore_last, refl_lp, jet_s, reed_z;
    dcb_t dcb;
    float ph;                  // vessel oscillator
    svf_t air_bp, chiff_bp;
    float m_s;                 // smoothed note, MIDI
    float ob_s;                // smoothed overblow (semitones, reed)
    float press_s, amp_s, dyn, tone_z, tone_z2;
    float chiff;
} voice_t;
static voice_t vo[2];
static int    model_cur, model_want;
static float  duck = 1.0f;     // the model switch: down to 0, swap, back to 1
static float  harm_g;          // the harmony voice's level, ramped
static uint32_t seed = 0x2b8e1d33u;
// breath
static float benv, bms, bfloor = 0.01f, b_s, b_smooth, ms_k;
static biquad_t out_hp;
static float blp[4], olp[4], oenv, lp_k;         // breath lives low: the mic and our own output, four poles at 250 Hz
static int   over;
static float b_slow;
static float vib_ph;
// YIN feed: the mic at 8 kHz
#define YR 2048
static float yring[YR];
static volatile uint32_t yring_w;
static float ylp1, ylp2; static int ydec;
// reverb: four lines, a Householder mix, damping in each line
static const int rv_len[4] = { 1123, 1381, 1621, 1871 };
static float rv_buf[4][2048];
static float rv_lp[4], rv_ap_buf[2][512];
static int   rv_w, rv_apw[2];
static const int rv_aplen[2] = { 211, 337 };
static float rv_g_s = 0.7f;
// echo (PSRAM)
#define ECHO_N 20480
static float *echo_buf;
static int    echo_w;
static float  echo_d_s = 0.33f * AUDIO_SR, echo_lp;
static float  fx_rvb_s, fx_echo_s;
static float  a4_s = 440.0f, bend_s, vib_s;

static inline float jet_table(float x) { float y = x * (x * x - 1.0f); return clampf(y, -1.0f, 1.0f); }
static inline float reed_table(float x) { return clampf(0.7f - 0.3f * x, -1.0f, 1.0f); }

static void voice_reset(voice_t *v)
{
    memset(v->bore_buf, 0, sizeof v->bore_buf); memset(v->jet_buf, 0, sizeof v->jet_buf);
    v->bore = (dline_t){ v->bore_buf, BORE_N - 1, 0 };
    v->jet = (dline_t){ v->jet_buf, 511, 0 };
    v->bore_last = v->refl_lp = v->reed_z = 0; v->dcb = (dcb_t){ 0, 0 };
    v->jet_s = 0.45f; v->press_s = v->amp_s = v->dyn = 0; v->tone_z = v->tone_z2 = 0; v->chiff = 0;
    v->air_bp = (svf_t){ 0 }; v->chiff_bp = (svf_t){ 0 };
    v->ob_s = 0;
}

// One voice for one sample. m: the note (MIDI, with bends and vibrato), b: breath 0..1, ob: overblown.
static inline float voice_run(voice_t *v, const model_t *md, float m, float b, int ob, float a4, float air, float noise)
{
    float f = a4 * fast_exp2((m - 69.0f) * (1.0f / 12.0f));
    float y;
    switch (md->eng) {
    case E_JET: {
        if (f > 2000.0f) f *= 0.5f;                                 // past about B6 the jet loses its mode: an octave down
        // the jet speaks from a pressure of about 0.85 and its pitch moves with it: keep the pressure in a
        // narrow band that speaks on every note and take the loudness from the breath instead
        float p_t = b > 0.02f ? 0.9f + 0.22f * b : 0.0f;
        v->press_s += 0.004f * (p_t - v->press_s);
        v->dyn += 0.003f * ((b > 0.02f ? 0.15f + 0.85f * b : 0.0f) - v->dyn);
        v->jet_s += 0.0015f * ((ob ? md->jet_hi : 0.45f) - v->jet_s);
        float pressure = v->press_s * (1.0f + (md->noise * (0.4f + air) + 0.6f * v->chiff) * noise);
        v->refl_lp = (1.0f - md->pole) * v->bore_last + md->pole * v->refl_lp;
        float temp = dcb_run(&v->dcb, v->refl_lp);
        float fc = f * fast_exp2(-(md->tune_a + md->tune_k * (m - 76.0f)) * (1.0f / 1200.0f));
        float bd = clampf((float)AUDIO_SR / fc - 1.5f, 4.0f, (float)(BORE_N - 4));
        float jd = bd * v->jet_s;
        float jo = dline_read(&v->jet, jd > 1.0f ? jd : 1.0f);
        dline_write(&v->jet, pressure - 0.5f * temp);
        float pd = jet_table(jo) + 0.5f * temp;
        float bo = dline_read(&v->bore, bd);
        dline_write(&v->bore, pd);
        v->bore_last = flush_denormal(bo);
        v->refl_lp = flush_denormal(v->refl_lp);
        y = bo * v->dyn;
        v->amp_s = v->dyn;
        break;
    }
    case E_REED: {
        float p_t = b > 0.02f ? 0.55f + 0.33f * b : 0.0f;
        v->press_s += 0.004f * (p_t - v->press_s);
        v->ob_s += 0.003f * ((ob ? md->ob_st : 0.0f) - v->ob_s);
        f *= fast_exp2(v->ob_s * (1.0f / 12.0f));
        float breath = v->press_s * (1.0f + (md->noise * (0.4f + air) + 0.5f * v->chiff) * noise);
        float last = v->bore_last;
        float fl = 0.5f * (last + v->reed_z); v->reed_z = last;    // the one-zero loss filter
        float pd = -0.95f * fl - breath;
        float in = breath + pd * reed_table(pd);
        float bd = clampf(0.5f * (float)AUDIO_SR / f - 1.5f, 2.0f, (float)(BORE_N - 4));
        float bo = dline_read(&v->bore, bd);
        dline_write(&v->bore, in);
        v->bore_last = flush_denormal(bo);
        v->dyn += 0.003f * ((b > 0.02f ? 0.3f + 0.7f * b : 0.0f) - v->dyn);     // the reed's own dynamics are small
        y = bo * v->dyn;
        v->amp_s = v->dyn;
        break;
    }
    default: {                                                       // vessel
        float a_t = b > 0.02f ? b * (0.35f + 0.65f * b) : 0.0f;
        v->press_s += (a_t > v->press_s ? 0.0025f : 0.0015f) * (a_t - v->press_s);
        float dt = f * (1.0f / (float)AUDIO_SR);
        v->ph = wrap1(v->ph + dt);
        float rich = 0.6f + 0.8f * v->press_s;
        float tone = sin_turns(v->ph) + md->h2 * rich * sin_turns(2.0f * v->ph) + md->h3 * rich * sin_turns(3.0f * v->ph);
        float nz = svf_bp(&v->air_bp, noise, NULL);
        y = v->press_s * ((1.0f - 0.6f * md->noise) * tone + md->noise * (1.2f + 2.0f * air) * nz);
        v->amp_s = v->press_s;
        break;
    }
    }
    // chiff: a short burst of air rung a few harmonics up
    if (v->chiff > 0.001f) { y += v->chiff * v->amp_s * 1.4f * svf_bp(&v->chiff_bp, noise, NULL); v->chiff *= 0.9985f; }
    else v->chiff = 0;
    return y;
}

static void fx_init(void)
{
    memset(rv_buf, 0, sizeof rv_buf); memset(rv_ap_buf, 0, sizeof rv_ap_buf); memset(rv_lp, 0, sizeof rv_lp);
    rv_w = 0; rv_apw[0] = rv_apw[1] = 0;
    if (echo_buf) memset(echo_buf, 0, ECHO_N * sizeof(float));
    echo_w = 0;
}

static inline float reverb_run(float x, float g, float damp)
{
    // input diffusion: two allpasses
    for (int k = 0; k < 2; k++) {
        float *b = rv_ap_buf[k];
        int r = rv_apw[k] - rv_aplen[k]; if (r < 0) r += 512;
        float d = b[r], u = x + 0.6f * d;
        b[rv_apw[k]] = u; rv_apw[k] = (rv_apw[k] + 1) & 511;
        x = d - 0.6f * u;
    }
    float s[4];
    for (int i = 0; i < 4; i++) {
        int r = (rv_w - rv_len[i]) & 2047;
        rv_lp[i] += damp * (rv_buf[i][r] - rv_lp[i]);
        rv_lp[i] = flush_denormal(rv_lp[i]);
        s[i] = rv_lp[i];
    }
    float h = 0.5f * (s[0] + s[1] + s[2] + s[3]);
    for (int i = 0; i < 4; i++) rv_buf[i][rv_w] = x + g * (s[i] - h);
    rv_w = (rv_w + 1) & 2047;
    return 0.5f * (s[0] - s[1] + s[2] - s[3]);
}

static void audio(const float *in, float *out, int n)
{
    // ---- breath: the level of the mic's low band (wind on the mic is mostly below 250 Hz; notes are mostly
    // above) over a floor that follows the room down fast and up slowly
    uint32_t yw = yring_w;
    for (int i = 0; i < n; i++) {
        float x = in[i];
        for (int j = 0; j < 4; j++) { blp[j] += lp_k * (x - blp[j]); x = blp[j]; }
        bms += ms_k * (x * x - bms);
        ylp1 += 0.33f * (in[i] - ylp1); ylp2 += 0.33f * (ylp1 - ylp2);
        if (++ydec >= 4) { ydec = 0; yring[yw & (YR - 1)] = ylp2; yw++; }
    }
    yring_w = yw;
    for (int j = 0; j < 4; j++) blp[j] = flush_denormal(blp[j]);
    benv = 2.8f * sqrtf(bms);                                     // a 30 ms RMS: steadier than a peak on gusty air
    float gate = pv[P_GATE] * 0.01f;
    float thr = bfloor * (1.6f + 3.6f * gate) + 0.002f + 1.2f * oenv;     // the speaker's own low band must not count as breath
    // the room's floor: follows a quieter room at once, a noisier one slowly, and only between breaths, so
    // a long note does not raise it and fade itself out
    if (benv < bfloor) bfloor += 0.2f * (benv - bfloor);
    else if (benv < thr) bfloor += 0.0005f * (benv - bfloor);
    float range = pv[P_BRANGE] * exp2f((50.0f - pv[P_SENSE]) / 25.0f);
    float b = clampf((benv - thr) / range, 0.0f, 1.0f);
    int cv = (int)pv[P_CURVE];
    if (cv == 0) b = sqrtf(b) * (0.4f + 0.6f * sqrtf(b)); else if (cv == 2) b = b * b;
    // breath on a real mic is gusty: smooth it (about 25 ms up, 60 ms down), or the pressure itself rumbles
    // a note starts fast (about 12 ms from silence), then the level moves at about 50 ms up, 60 ms down, and a
    // breath that stops stops in about 20 ms
    float kb = b > b_smooth ? (b_smooth < 0.1f ? 0.16f : 0.04f) : (b < 0.05f ? 0.1f : 0.033f);
    b_smooth += kb * (b - b_smooth);
    b = b_smooth;
    float ccb = a_cc_breath;
    if (ccb > b) b = ccb;
    if (a_mute) b = 0;
    // overblowing, with some hysteresis
    float ob_at = pv[P_OVER] * 0.01f;
    b_slow += 0.025f * (b - b_slow);                               // about 80 ms: a gust is not a decision
    if (!over && b_slow > ob_at && ob_at < 0.999f) over = 1; else if (over && b_slow < ob_at - 0.15f) over = 0;
    const model_t *md = &models[model_cur];
    int ob = over && md->ob_st > 0.0f;
    if (b > 0.06f && b_s <= 0.06f) { vo[0].chiff = vo[1].chiff = 0.25f + 0.75f * pv[P_CHIFF] * 0.01f; v_onsets++; }
    if (a_onset_req) { a_onset_req = 0; if (b > 0.06f) { vo[0].chiff = vo[1].chiff = 0.35f * pv[P_CHIFF] * 0.01f; v_onsets++; } }
    b_s = b;

    // ---- block-rate settings
    if (model_want != model_cur && duck <= 0.0f) {
        model_cur = model_want; md = &models[model_cur];
        voice_reset(&vo[0]); voice_reset(&vo[1]);
        ob = over && md->ob_st > 0.0f;
    }
    a4_s += 0.25f * (pv[P_TUNE] - a4_s);
    float a4 = a4_s;
    float air = pv[P_AIR] * 0.01f;
    float bright = clampf(pv[P_BRIGHT] * 0.01f + a_bright_add, 0.0f, 1.0f);
    float glide_k = onepole_s(pv[P_GLIDE] * 0.35f);
    float tgt = a_midi + (float)md->shift, tgt_h = a_midi_h + (float)md->shift;
    float bpitch = pv[P_BPITCH] * 0.01f * (b - 0.5f);
    float bend = a_bend + bpitch, vib = a_vib;
    float vib_inc = pv[P_VIBR] / (float)AUDIO_SR;
    // per-voice filters at the note
    for (int k = 0; k < 2; k++) {
        float m = k ? vo[1].m_s : vo[0].m_s;
        float f = a4 * exp2f((m - 69.0f) / 12.0f);
        svf_set(&vo[k].air_bp, f, md->eng == E_VESSEL ? md->q : 2.0f);
        svf_set(&vo[k].chiff_bp, f * md->chiff_f, 1.5f);
    }
    float tone_hz = a4 * exp2f((vo[0].m_s - 69.0f) / 12.0f) * md->tone * exp2f((bright - 0.5f) * 3.0f);
    float tone_k = onepole_hz(clampf(tone_hz, 300.0f, 14000.0f));
    float hw = a_harm_on ? pv[P_HLVL] * 0.01f : 0.0f;
    // effects
    float size = pv[P_SIZE] * 0.01f, rt = 0.5f + 5.5f * size * size;
    float g_t = exp2f(-3.0f * 3.3219f * 1500.0f / (rt * (float)AUDIO_SR));     // per pass of an average line
    float damp = 0.95f - 0.75f * pv[P_DAMP] * 0.01f;
    float rvb_t = pv[P_REVERB] * 0.01f, echo_t = echo_buf ? pv[P_ECHO] * 0.01f : 0.0f;
    float ed_t = pv[P_ETIME] * (float)AUDIO_SR, efb = pv[P_EFB] * 0.01f;

    float pk = 0, vpk = 0;
    for (int i = 0; i < n; i++) {
        // duck for a model switch: 8 ms down, swap at the next block, 8 ms up
        if (model_want != model_cur) { duck -= 1.0f / 256.0f; if (duck < 0) duck = 0; }
        else if (duck < 1.0f) { duck += 1.0f / 256.0f; if (duck > 1) duck = 1; }
        harm_g += 0.0005f * (hw - harm_g);
        vib_ph = wrap1(vib_ph + vib_inc);
        vib_s += 0.0008f * (vib - vib_s);
        bend_s += 0.0015f * (bend - bend_s);
        float vs = vib_s * sin_turns(vib_ph);
        vo[0].m_s += glide_k * (tgt - vo[0].m_s);
        vo[1].m_s += glide_k * (tgt_h - vo[1].m_s);
        float nz = rng_bi(&seed);
        float y = voice_run(&vo[0], md, vo[0].m_s + bend_s + vs, b, ob, a4, air, nz);
        if (harm_g > 0.002f) y += harm_g * voice_run(&vo[1], md, vo[1].m_s + bend_s + vs, b, ob, a4, air, rng_bi(&seed));
        // tone: two one-poles at a few harmonics of the note
        vo[0].tone_z += tone_k * (y - vo[0].tone_z);
        vo[0].tone_z2 += tone_k * (vo[0].tone_z - vo[0].tone_z2);
        y = biquad_run(&out_hp, (0.35f * y + 0.65f * vo[0].tone_z2) * md->gain * duck);   // no rumble, no DC
        float ay = fabsf(y); if (ay > vpk) vpk = ay;
        // effects
        fx_echo_s += 0.0005f * (echo_t - fx_echo_s);
        fx_rvb_s += 0.0005f * (rvb_t - fx_rvb_s);
        rv_g_s += 0.0005f * (g_t - rv_g_s);
        float e = 0;
        if (echo_buf) {
            echo_d_s += 0.0002f * (ed_t - echo_d_s);
            float rp = (float)echo_w - echo_d_s;
            if (rp < 0) rp += (float)ECHO_N;
            int i0 = (int)rp; float fr = rp - (float)i0; int i1 = i0 + 1 >= ECHO_N ? 0 : i0 + 1;
            e = echo_buf[i0] + fr * (echo_buf[i1] - echo_buf[i0]);
            echo_lp += 0.35f * (e - echo_lp);
            echo_buf[echo_w] = flush_denormal(y * fx_echo_s + echo_lp * efb);
            if (++echo_w >= ECHO_N) echo_w = 0;
        }
        float wet = reverb_run((y + 0.5f * e) * 0.3f, rv_g_s, damp);
        float o = y + fx_echo_s * e + fx_rvb_s * 1.2f * wet;
        out[i] = o;
        float a = fabsf(o); if (a > pk) pk = a;
    }
    // our own low band, as the mic would hear it from the speaker (for the next block's threshold)
    float opk = 0;
    for (int i = 0; i < n; i++) {
        float x = out[i];
        for (int j = 0; j < 4; j++) { olp[j] += lp_k * (x - olp[j]); x = olp[j]; }
        if (fabsf(x) > opk) opk = fabsf(x);
    }
    for (int j = 0; j < 4; j++) olp[j] = flush_denormal(olp[j]);
    oenv = opk > oenv ? opk : oenv * 0.97f;
    (void)pk;
    v_breath = b; v_benv = benv; v_floor = bfloor; v_over = ob; v_oenv = oenv;
    v_lvl = vpk > v_lvl ? vpk : v_lvl * 0.97f;
    v_amp = clampf(v_lvl / 0.3f, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- the UI side
#define MAXN 15
static int   note_midi[MAXN], nnotes, root_i;
static int   idx = 3;
static float pos_s;                      // where the tilt is on the ladder, continuous (for the screen)
static float midi_now = 72, midi_h_now = 79;
static int   held[8], nheld;             // MIDI notes held, last on top
static float bend_in;                    // pitch bend in, semitones
static int   sounding = -1, last_cc2 = -1, last_pb = -1;
static float vib_env;                    // the vibrato delay's ramp, 0..1
static float breath_s, amp_s, cc_breath_t;
static int   blow_ms, over_was, over_flash;
static float voice_hz, voice_conf;       // HUMMING: what YIN heard
static float voice_m = -1;               // ... snapped, held until the voice moves well away
float huff_dbg(int k) { return k == 0 ? v_benv : k == 1 ? v_floor : k == 2 ? v_oenv : k == 3 ? voice_hz : voice_m; }   // for tools/sim
static int   ytick;
// breath setup
static int   setup_step, setup_ms;
static float setup_quiet, setup_blow, setup_peak;
static int   setup_n;
static const char *setup_msg;

static void build_notes(void)
{
    int sc = (int)pv[P_SCALE], n = scales[sc].n;
    nnotes = (int)pv[P_NOTES];
    if (nnotes > MAXN) nnotes = MAXN;
    root_i = nnotes / 3;
    int base = 72 + (int)pv[P_KEY] + 12 * (int)pv[P_OCT];
    for (int k = 0; k < nnotes; k++) {
        int d = k - root_i, oc = d >= 0 ? d / n : -((-d + n - 1) / n), st = d - oc * n;
        note_midi[k] = base + 12 * oc + scales[sc].st[st];
    }
    if (idx >= nnotes) idx = nnotes - 1;
}

// the nearest scale note to a MIDI pitch (any octave)
static float snap_to_scale(float m)
{
    int sc = (int)pv[P_SCALE], key = (int)pv[P_KEY];
    float best = m, bd = 99;
    int c = (int)floorf(m);
    for (int k = c - 7; k <= c + 7; k++) {
        int pc = ((k - key) % 12 + 12) % 12;
        bool in = false;
        for (int i = 0; i < scales[sc].n; i++) if (scales[sc].st[i] == pc) in = true;
        if (in && fabsf((float)k - m) < bd) { bd = fabsf((float)k - m); best = (float)k; }
    }
    return best;
}

static void changed(int p)
{
    if (p < 0 || p == P_SCALE || p == P_KEY || p == P_OCT || p == P_NOTES) build_notes();
    if (p < 0 || p == P_MODEL) model_want = (int)pv[P_MODEL];
}

static void init(void)
{
    ms_k = onepole_s(0.03f); lp_k = onepole_hz(250.0f);
    biquad_set(&out_hp, BQ_HP, 60.0f, 0.707f, 0);
    for (int k = 0; k < 2; k++) voice_reset(&vo[k]);
    echo_buf = app_alloc(ECHO_N * sizeof(float));
    fx_init();
    model_cur = model_want = (int)pv[P_MODEL];
    build_notes();
    idx = root_i;
    a_midi = midi_now = (float)note_midi[idx];
    vo[0].m_s = vo[1].m_s = a_midi;
}

static void action(int p)
{
    if (p != P_BSETUP) return;
    menu_leave();
    setup_step = 1; setup_ms = 0; setup_quiet = setup_blow = setup_peak = 0; setup_n = 0; setup_msg = NULL;
    a_mute = 1;
}

static void setup_tick(void)
{
    setup_ms += 10;
    float e = v_benv;
    if (setup_step == 1) {                                         // 2 s of quiet: the room
        if (setup_ms > 400) { setup_quiet += e; setup_n++; }
        if (setup_ms >= 2200) { setup_quiet /= (float)(setup_n ? setup_n : 1); setup_step = 2; setup_ms = 0; setup_n = 0; }
    } else if (setup_step == 2) {                                  // 3 s of a normal breath
        if (e > setup_quiet * 3.0f + 0.003f) { setup_blow += e; setup_n++; if (e > setup_peak) setup_peak = e; }
        if (setup_ms >= 3500) {
            a_mute = 0; setup_step = 0;
            if (setup_n < 60) { toast("DID NOT HEAR YOU: TRY AGAIN", 2000); return; }
            float normal = setup_blow / (float)setup_n;
            float thr = setup_quiet * (1.6f + 3.6f * pv[P_GATE] * 0.01f) + 0.002f;
            float r = (normal - thr) / 0.7f;                            // a normal breath reads about 60 % once it plays (its own sound
                                                                        // in the mic's low band raises the threshold a little)
            param_set(P_BRANGE, clampf(r, 0.01f, 1.0f));
            param_set(P_SENSE, 50);
            coach_done(L_SETUP);
            toast("BREATH SET", 1500);
        }
    }
}

// YIN on the 8 kHz copy of the mic (HUMMING): 256 samples, 66..1140 Hz
#define YW 256
#define TMAX 120
#define TMIN 7
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
    // an octave (or a twelfth) too low is YIN's usual mistake on a buzzy voice: if a fraction of the lag
    // repeats as well, the voice is that much higher
    for (int dv = 4; dv >= 2; dv--) {
        int h = (t + dv / 2) / dv;
        if (h - 1 < TMIN) continue;
        int hb = h;
        for (int k = h - 1; k <= h + 1; k++) if (yd[k] < yd[hb]) hb = k;
        // the dip between two lags is deeper than either: judge it by its parabola's bottom
        float a = yd[hb - 1], b = yd[hb], c = hb + 1 <= TMAX ? yd[hb + 1] : yd[hb], den = a - 2.0f * b + c;
        float depth = den > 1e-6f ? b - (a - c) * (a - c) / (8.0f * den) : b;
        if (depth < 0.3f) { t = hb; break; }
    }
    *conf = 1.0f - yd[t];
    float a = yd[t - 1], b = yd[t], c = t + 1 <= TMAX ? yd[t + 1] : yd[t], den = a - 2.0f * b + c;
    float sh = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
    return (float)AUDIO_SR / 4.0f / ((float)t + clampf(sh, -1.0f, 1.0f));
}

static int harm_note(int i, int h)
{
    switch (h) {
    case H_THIRD:  return i + 2 < nnotes ? note_midi[i + 2] : note_midi[i] + 4;
    case H_FIFTH:  return note_midi[i] + 7;
    case H_OCTUP:  return note_midi[i] + 12;
    case H_OCTDN:  return note_midi[i] - 12;
    case H_DRONE:  return note_midi[root_i] - 12;
    case H_DRONE5: return note_midi[root_i] - 5;
    default:       return note_midi[i];
    }
}

static void midi_out(float b, int note)
{
    if (b > 0.06f && sounding != note) {
        if (sounding >= 0) midi_note(sounding, 0);
        midi_note(note, 24 + (int)(b * 103.0f)); sounding = note;
    } else if (b < 0.02f && sounding >= 0) { midi_note(sounding, 0); sounding = -1; }
    int cc = (int)(b * 127.0f);
    if (cc != last_cc2 && (abs(cc - last_cc2) >= 2 || cc == 0 || cc == 127)) { midi_cc(2, cc); last_cc2 = cc; }
}

static void tick(const ctl_t *c)
{
    if (setup_step) { setup_tick(); coach_pause(); return; }
    float b = v_breath;
    breath_s += 0.3f * (b - breath_s);
    amp_s += 0.3f * (v_amp - amp_s);
    if (b > 0.05f) ui_awake();
    if (b > 0.2f) { if ((blow_ms += 10) > 1000) coach_done(L_BLOW); } else blow_ms = 0;
    if (v_over && !over_was) over_flash = 12;
    if (b > 0.75f) coach_done(L_OVERBLOW);
    over_was = v_over;
    if (cc_breath_t > 0) { cc_breath_t -= 0.01f; if (cc_breath_t <= 0) a_cc_breath = 0; }

    // ---- where the note comes from
    int src = (int)pv[P_SOURCE];
    if (src == 1 && !c->has_joy) src = 0;
    float pos;
    if (src == 1) pos = (c->jy * 0.5f + 0.5f) * (float)nnotes - 0.5f;
    else pos = c->has_imu ? (c->lean_y / pv[P_SPAN] + 0.5f) * (float)nnotes - 0.5f : (float)root_i;
    pos = clampf(pos, 0.0f, (float)(nnotes - 1));
    pos_s += 0.4f * (pos - pos_s);
    if (pos > (float)idx + 0.65f) idx = (int)(pos + 0.35f);
    if (pos < (float)idx - 0.65f) idx = (int)(pos + 0.65f);
    idx = clampi(idx, 0, nnotes - 1);
    float m = (float)note_midi[idx];
    if (pv[P_SNAP] < 0.5f && src != 2) {                          // slide: between the ladder's notes
        int i0 = (int)pos, i1 = i0 + 1 < nnotes ? i0 + 1 : i0;
        float fr = pos - (float)i0;
        m = (float)note_midi[i0] + fr * (float)(note_midi[i1] - note_midi[i0]);
    }
    if (src == 2) {                                               // humming: YIN every other tick
        if (++ytick % 2 == 0) {
            float conf, f = yin(&conf);
            // a new pitch counts once two readings in a row agree (within a semitone)
            static float cand;
            if (conf > 0.75f && f > 60.0f && v_benv > 0.004f) {
                if (cand > 0 && fabsf(12.0f * log2f(f / cand)) < 1.0f) { voice_hz = f; voice_conf = conf; }
                cand = f;
            } else { voice_conf *= 0.9f; cand = 0; }
        }
        if (voice_hz > 0) {
            float vm = 69.0f + 12.0f * log2f(voice_hz / pv[P_TUNE]);
            while (vm < (float)note_midi[0] - 6.0f) vm += 12.0f;          // fold into the instrument's range
            while (vm > (float)note_midi[nnotes - 1] + 6.0f) vm -= 12.0f;
            if (pv[P_SNAP] < 0.5f) m = vm;
            else {
                float cand = snap_to_scale(vm);                            // a wavering voice keeps its note: move
                if (voice_m < 0 || (cand != voice_m && fabsf(vm - cand) + 0.35f < fabsf(vm - voice_m))) voice_m = cand;   // only when clearly nearer
                m = voice_m;
            }
        }
    }
    if (nheld > 0) m = (float)held[nheld - 1];                      // MIDI notes in win while held
    int jy_mode = (int)pv[P_JOYY];
    if (c->has_joy && jy_mode == 2 && src != 1) m += c->jy > 0.6f ? 12.0f : c->jy < -0.6f ? -12.0f : 0.0f;
    if (m != midi_now) {
        if (fabsf(m - midi_now) >= 0.99f) { a_onset_req = 1; vib_env = 0; }
        midi_now = m;
    }
    a_midi = m;
    int hm = (int)pv[P_HARM];
    a_harm_on = hm != H_OFF;
    midi_h_now = (float)harm_note(idx, hm) + (m - (float)note_midi[idx]);
    if (hm == H_DRONE || hm == H_DRONE5) midi_h_now = (float)harm_note(idx, hm);
    a_midi_h = midi_h_now;

    // ---- bends and vibrato
    float bend = bend_in;
    if (c->has_joy) bend += c->jx * pv[P_BENDR];
    int lm = (int)pv[P_LEAN];
    if (c->has_imu && lm == 1) bend += clampf(c->lean_x / 40.0f, -1.0f, 1.0f) * pv[P_BENDR];
    a_bend = bend;
    float vdel = pv[P_VIBD];
    if (b > 0.05f) vib_env = vdel > 0.01f ? fminf(1.0f, vib_env + 0.01f / vdel) : 1.0f; else vib_env = 0;
    float depth = pv[P_VIB] * 0.01f * vib_env;
    if (c->has_imu && lm == 0) depth += clampf((fabsf(c->lean_x) - 10.0f) / 30.0f, 0.0f, 1.0f);
    if (c->has_joy && jy_mode == 0 && c->jy > 0) depth += c->jy;
    a_vib = 0.5f * depth;                                         // 100 %: half a semitone each way
    a_bright_add = c->has_joy && jy_mode == 1 ? 0.5f * c->jy : 0.0f;

    // ---- MIDI out, the LED
    int note = (int)lroundf(m) + models[(int)pv[P_MODEL]].shift + (v_over ? (pv[P_MODEL] == M_CLARINET ? 19 : 12) : 0);
    midi_out(b, note);
    int pb = 8192 + (int)(clampf(bend / pv[P_BENDR], -1.0f, 1.0f) * 8191.0f);
    if (abs(pb - last_pb) > 64 || (pb == 8192 && last_pb != 8192)) { last_pb = pb; midi_bend(pb); }
    float deg = (float)(((int)lroundf(m) - 72 - (int)pv[P_KEY]) % 12 + 12) / 12.0f;
    float hue = deg * 6.0f, lv = clampf(amp_s * 2.2f, 0.0f, 1.0f);
    int hs = (int)hue % 6; float f = hue - floorf(hue);
    float r = 0, g = 0, bl = 0;
    switch (hs) { case 0: r = 1; g = f; break; case 1: r = 1 - f; g = 1; break; case 2: g = 1; bl = f; break;
                  case 3: g = 1 - f; bl = 1; break; case 4: r = f; bl = 1; break; default: r = 1; bl = 1 - f; break; }
    if (over_flash > 0) { r = g = bl = 1; lv = 1; }
    led_rgb((uint8_t)(r * 255 * lv), (uint8_t)(g * 255 * lv), (uint8_t)((bl * 255 * lv) + (b > 0.02f && lv < 0.1f ? 12 : 0)));
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t k = st & 0xF0;
    if (k == 0x90 && d2 > 0) {
        for (int i = 0; i < nheld; i++) if (held[i] == d1) { memmove(&held[i], &held[i + 1], (size_t)(nheld - i - 1) * sizeof held[0]); nheld--; break; }
        if (nheld < 8) held[nheld++] = d1;
    } else if (k == 0x80 || k == 0x90) {
        for (int i = 0; i < nheld; i++) if (held[i] == d1) { memmove(&held[i], &held[i + 1], (size_t)(nheld - i - 1) * sizeof held[0]); nheld--; break; }
    } else if (k == 0xB0 && d1 == 2) { a_cc_breath = (float)d2 / 127.0f; cc_breath_t = 2.0f; }
    else if (k == 0xB0 && d1 == 123) nheld = 0;
    else if (k == 0xE0) bend_in = ((float)((d2 << 7) | d1) - 8192.0f) / 8192.0f * pv[P_BENDR];
}

// ---------------------------------------------------------------- the screen
// The ocarina, seen from above: a sweet-potato body, the mouthpiece to the left, the window (where the
// sound comes out) where they meet. Twelve holes open one by one as the note climbs the ladder, low to
// high: the two small sub holes, the right hand from the little finger in, the left hand, then the two
// thumb holes underneath (ticks on the bottom edge). A covered hole is drawn filled, an open one as a ring.
#define OC_CX 38
#define OC_CY 33
typedef struct { int8_t x, y, r; uint8_t under; } hole_t;
static const hole_t holes[12] = {
    { 41, 36, 1, 0 }, { 37, 36, 1, 0 },                       // sub holes
    { 55, 33, 1, 0 }, { 51, 31, 2, 0 }, { 46, 29, 2, 0 }, { 41, 29, 2, 0 },   // right hand: little finger .. index
    { 31, 36, 1, 0 }, { 34, 29, 2, 0 }, { 29, 29, 2, 0 }, { 24, 31, 2, 0 },   // left hand: little finger, ring .. index
    { 30, 44, 1, 1 }, { 46, 42, 1, 1 },                       // thumbs, underneath
};
static parts_t air, notes_p;
static uint32_t pseed = 0x7f3a11c5u;
static float note_wob;

static bool oc_inside(int x, int y)
{
    float dx = (float)(x - OC_CX) / 20.0f;
    if (dx >= -1.0f && dx <= 1.0f) {                          // the body: round at the mouth end, tapering to a raised tail
        float t = dx > 0 ? dx : 0, ry = 9.5f - 3.5f * t, cy = (float)OC_CY - 2.5f * t * t;
        float dy = ((float)y - cy) / ry;
        if (dx * dx + dy * dy <= 1.0f) return true;
    }
    if (x >= 4 && x <= 20) {                                  // the mouthpiece, flaring into the body
        float half = 2.2f + 0.12f * (float)(x - 4);
        return fabsf((float)y - ((float)OC_CY + 1.0f)) <= half;
    }
    return false;
}

static void ocarina(int open, float glow)
{
    for (int y = 21; y <= 44; y++)
        for (int x = 3; x <= 60; x++)
            if (oc_inside(x, y) && (!oc_inside(x - 1, y) || !oc_inside(x + 1, y) || !oc_inside(x, y - 1) || !oc_inside(x, y + 1)))
                oled_pixel(x, y, true);
    oled_rect(5, OC_CY, 2, 2, true);                          // the mouth hole
    oled_rect(18, OC_CY - 1, 3, 3, true);                     // the window
    if (glow > 0.05f) { int g = 1 + (int)(glow * 3.0f); oled_hline_pat(16, OC_CY - 3 - g, 7, 0x55, true); }
    for (int i = 0; i < 12; i++) {
        const hole_t *h = &holes[i];
        bool is_open = i < open;
        if (h->under) { oled_rect(h->x - 1, h->y + (is_open ? 1 : 0), 3, is_open ? 1 : 2, true); continue; }
        if (h->r > 1) { if (is_open) oled_circle(h->x, h->y, 2, true); else oled_disc(h->x, h->y, 2, true); }
        else if (is_open) { oled_pixel(h->x - 1, h->y, true); oled_pixel(h->x + 1, h->y, true); oled_pixel(h->x, h->y - 1, true); oled_pixel(h->x, h->y + 1, true); }
        else oled_rect(h->x - 1, h->y - 1, 3, 3, true);
    }
}

static const char *const note_spr[6] = { "...##", "...#.#", "...#..", ".###..", "####..", ".##..." };
static const char *const note2_spr[6] = { ".#####", ".#...#", ".#...#", "##..##", "##..##", "......" };

static void ladder(int x, int y0, int y1)
{
    int n = nnotes;
    float step = n > 1 ? (float)(y1 - y0) / (float)(n - 1) : 0;
    for (int k = 0; k < n; k++) {
        int y = y1 - (int)lroundf((float)k * step);
        if (k == idx) oled_rrect(x, y - 1, 9, 3, 1, true);
        else if (k == root_i) oled_rect(x + 1, y, 7, 1, true);
        else oled_rect(x + 2, y, 5, 1, true);
    }
    int by = y1 - (int)lroundf(pos_s * step);                  // where the tilt (or the stick) is: a pointer
    oled_line(x - 4, by - 2, x - 2, by, true); oled_line(x - 4, by + 2, x - 2, by, true); oled_pixel(x - 4, by - 1, true); oled_pixel(x - 4, by + 1, true); oled_pixel(x - 3, by, true);
}

static void scene(const view_t *v)
{
    float b = breath_s, amp = amp_s;
    const int mx = 4;                                                   // the mouthpiece's open end
    // air into the mouthpiece: more and faster with the breath
    int emit = b > 0.03f ? 1 + (int)(b * 3.0f) : 0;
    for (int i = 0; i < emit; i++)
        parts_emit(&air, -6.0f, (float)OC_CY + 1.0f + 1.5f * rng_bi(&pseed), 50.0f + 150.0f * b, 0, 0.3f, b > 0.55f ? PART_STREAK : PART_DOT, 1);
    parts_step(&air, 0.04f, 0, 0);
    for (int i = 0; i < air.n; i++) if (air.p[i].x >= (float)mx) air.p[i].life = 0;
    parts_draw(&air);
    // notes out of the window, more with more sound, swaying with the vibrato
    static float acc;
    acc += amp * 0.7f;
    if (acc > 1.0f && notes_p.n < 8) {
        acc = 0;
        parts_emit(&notes_p, 16.0f, (float)(OC_CY - 12), 10.0f + 10.0f * rng_uni(&pseed), -16.0f - 10.0f * amp, 3.0f, PART_SQUARE, (uint8_t)(rng_next(&pseed) & 1));
    }
    parts_step(&notes_p, 0.04f, 0, 0);
    note_wob += 0.04f * pv[P_VIBR] * 6.2831853f;
    for (int i = 0; i < notes_p.n; i++) {
        particle_t *q = &notes_p.p[i];
        if (q->y < 16.0f || q->x > 60.0f) { q->life = 0; continue; }
        int wx = (int)lroundf(sinf(note_wob + q->y * 0.25f) * a_vib * 4.0f);
        ui_sprite((int)q->x + wx, (int)q->y, q->size ? note2_spr : note_spr, 6, false, true);
    }
    // the ocarina, its holes for this note
    int lo = note_midi[0], hi = note_midi[nnotes - 1];
    int open = hi > lo ? (int)lroundf((midi_now - (float)lo) / (float)(hi - lo) * 12.0f) : 0;
    ocarina(clampi(open, 0, 12), amp);
    if (over_flash > 0) { int r = 13 - over_flash; oled_circle(19, OC_CY, r, true); over_flash--; }
    // the note, big, with the bend
    char nm[8];
    int m = (int)pv[P_MODEL];
    int shown = (int)lroundf(midi_now) + models[m].shift + (v_over ? (m == M_CLARINET ? 19 : 12) : 0);
    ui_note_name(shown, nm, sizeof nm);
    int wob = a_vib > 0.05f && amp > 0.05f ? (int)lroundf(sinf(note_wob) * a_vib * 2.0f) : 0;
    const int tx = 64;
    oled_text2(tx + wob, 19, nm);
    if (fabsf(a_bend) > 0.08f) {                                         // a bend: an arrow up or down after the name
        int bx = tx + 12 * (int)strlen(nm) + 1, d = a_bend > 0 ? -1 : 1, n = 1 + (int)clampf(fabsf(a_bend) * 2.0f, 0.0f, 4.0f);
        oled_line(bx + 2, 26, bx + 2, 26 + d * (2 + n), true);
        oled_line(bx, 26 + d * n, bx + 2, 26 + d * (2 + n), true); oled_line(bx + 4, 26 + d * n, bx + 2, 26 + d * (2 + n), true);
    }
    if (v_over) oled_pill(F_TINY, tx, 34, m == M_CLARINET ? "TWELFTH UP" : "OCTAVE UP", true);
    else ui_text_fit(F_TINY, tx, 35, 44, model_l[m]);
    // breath: a bar with the overblow mark
    const int bw = 42;
    oled_hline_pat(tx, 43, bw, 0x11, true);
    oled_rect(tx, 42, (int)(clampf(b, 0, 1) * (float)bw), 3, true);
    float oba = pv[P_OVER] * 0.01f;
    if (oba < 0.999f && models[m].ob_st > 0) oled_rect(tx + (int)(oba * (float)bw), 40, 1, 7, true);
    ladder(117, 18, 44);
    (void)v;
}

static void draw_setup(const view_t *v)
{
    oled_pill(F_SMALL, 0, 2, "BREATH SETUP", true);
    char b[16]; snprintf(b, sizeof b, "%d/2", setup_step); oled_ftext_r(F_SMALL, OLED_W - 1, 5, b, 1);
    float t = (float)setup_ms / (setup_step == 1 ? 2200.0f : 3500.0f);
    if (setup_step == 1) {
        oled_ftext_c(F_SMALL, 64, 20, "STAY QUIET A MOMENT", 1);
        oled_ftext_c(F_TINY, 64, 29, "LISTENING TO THE ROOM", 1);
    } else {
        oled_ftext_c(F_SMALL, 64, 20, "NOW BLOW, NORMALLY", 1);
        oled_ftext_c(F_TINY, 64, 29, "AS IF PLAYING A SOFT NOTE", 1);
        int n = (int)(sqrtf(v_benv / 0.15f) * 4.0f);
        for (int i = 0; i < 4 && i < n; i++) parts_emit(&air, -2.0f, 37.0f + 1.5f * rng_bi(&pseed), 140.0f, 0, 1.0f, PART_STREAK, 1);
    }
    parts_step(&air, 0.04f, 0, 0); parts_draw(&air);
    // the level, live, and the time left
    float lv = clampf(sqrtf(v_benv / 0.15f), 0.0f, 1.0f);             // a normal breath fills about two thirds
    oled_rframe(14, 40, 100, 8, 2); oled_rect(16, 42, (int)(lv * 96.0f), 4, true);
    ui_arc(64, 56, 6, -90.0f, -90.0f + 360.0f * clampf(t, 0, 1));
    (void)v;
}

static void draw(const view_t *v)
{
    if (setup_step) { draw_setup(v); return; }
    ui_head(NULL, false);
    scene(v);
    if (!v->cover) {
        oled_hline_pat(0, 47, OLED_W, 0x11, true);
        if (breath_s < 0.02f && v->t < 30.0f && looper_state() == LOOP_EMPTY) oled_ftext_c(F_SMALL, 64, 52, "BLOW AT THE MIC", 1);
        else ui_loop(2, 50, 124);
        char sc[32]; snprintf(sc, sizeof sc, "%s %s", key_l[(int)pv[P_KEY]], scale_l[(int)pv[P_SCALE]]);
        oled_ftext_c(F_TINY, 64, 59, sc, 1);
    }
}

// ---------------------------------------------------------------- the splash: someone walks in, breathes, plays
static const char *const big_note[9] = { "...##.", "...#.#", "...#..", "...#..", "...#..", ".###..", "####..", "####..", ".##..." };
static const char *const big_pair[9] = { "..######", "..#....#", "..#....#", "..#....#", ".##...##", "###..###", "###..###", ".#....#.", "........" };

// A player 18 rows tall. phase >= 0: walking; -1: standing with the ocarina up; lift 0..1 raises the arms.
static void player(int x, int floor, int phase, float lift, int breath_up)
{
    int sh = floor - 12 - breath_up, hy = floor - 16 - breath_up;
    oled_circle(x, hy, 2, true);
    oled_pixel(x + 1, hy - 1, true);                               // an eye, looking right
    oled_line(x, hy + 3, x, floor - 6, true);                      // the body
    if (phase >= 0) {
        static const int8_t L[4][4] = { { 3, 0, -3, 0 }, { 1, -1, -1, 0 }, { -3, 0, 3, 0 }, { -1, 0, 1, -1 } };
        const int8_t *l = L[phase & 3];
        oled_line(x, floor - 6, x + l[0], floor + l[1], true); oled_line(x, floor - 6, x + l[2], floor + l[3], true);
        int sw = (phase & 2) ? 3 : -3;
        oled_line(x, sh, x + sw, sh + 4, true); oled_line(x, sh, x - sw, sh + 4, true);
        return;
    }
    oled_line(x, floor - 6, x - 2, floor, true); oled_line(x, floor - 6, x + 2, floor, true);
    // arms from hanging (lift 0) to holding the ocarina at the mouth (lift 1)
    int hx = x + (int)lroundf(2.0f + 3.0f * lift), hyy = sh + (int)lroundf(5.0f - 6.0f * lift);
    oled_line(x, sh, hx, hyy, true); oled_line(x, sh, hx + 1, hyy + 1, true);
    if (lift > 0.5f) { oled_rrect(hx, hyy - 2, 7, 4, 1, true); oled_pixel(hx + 2, hyy - 1, false); oled_pixel(hx + 4, hyy - 1, false); }
}

static bool splash(int f)
{
    const int floor = 61, px = 24;
    oled_hline_pat(0, floor + 1, OLED_W, 0x11, true);
    if (f < 22) player(-10 + (px + 10) * f / 22, floor, f / 2, 0, 0);
    else {
        float lift = clampf((float)(f - 22) / 5.0f, 0.0f, 1.0f);
        int breath_up = f >= 27 && f < 34 ? 1 : 0;                   // a breath in: the shoulders rise
        int sway = f >= 34 && f < 70 ? (int)lroundf(sinf((float)f * 0.35f)) : 0;
        player(px + sway, floor, -1, lift, breath_up);
        if (f >= 27 && f < 34)                                      // air drawn in
            for (int i = 0; i < 3; i++) { int ax = px + 16 - ((f - 27) * 2 + i * 4) % 12; oled_pixel(ax, floor - 15 + i, true); }
    }
    // three notes out of the ocarina, each with a puff, rising to the right
    for (int k = 0; k < 3; k++) {
        int t0 = 34 + k * 8;
        if (f < t0) continue;
        float t = clampf((float)(f - t0) / 26.0f, 0.0f, 1.0f);
        int nx = px + 10 + (int)(t * (8.0f + 5.0f * (float)k)), ny = floor - 22 - (int)(ease_out(t) * (10.0f + 7.0f * (float)k));
        if (t < 1.0f || f < 80) ui_sprite(nx, ny, k == 1 ? big_pair : big_note, 9, false, true);
        if (f - t0 < 5) oled_circle(px + 11, floor - 16, 1 + (f - t0), true);
    }
    // the name pops up letter by letter, then what to do
    static const char *name = "HUFF";
    for (int i = 0; i < 4; i++) {
        int t0 = 50 + i * 4;
        if (f < t0) continue;
        int dy = (int)lroundf((1.0f - ease_back(clampf((float)(f - t0) / 8.0f, 0.0f, 1.0f))) * 8.0f);
        char c[2] = { name[i], 0 };
        oled_text2(66 + i * 13, 20 + dy, c);
    }
    if (f >= 70) oled_ftext_r(F_TINY, OLED_W - 2, 40, "BLOW. TILT. PLAY.", 1);
    return f < 96;
}

// ---------------------------------------------------------------- screensaver: notes drifting up
static void saver(uint32_t f)
{
    for (int k = 0; k < 7; k++) {
        float sp = 0.15f + 0.05f * (float)k;
        int x = (int)(fmodf((float)k * 37.0f + sinf((float)f * 0.01f + (float)k) * 10.0f, 120.0f));
        int y = 64 - (int)(fmodf((float)f * sp + (float)k * 23.0f, 80.0f));
        ui_sprite(x, y, k & 1 ? note2_spr : note_spr, 6, false, true);
    }
}

const instrument_t INST = {
    .name = "HUFF", .tagline = "A TILT OCARINA YOU BLOW",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 5,
    .presets = presets, .npresets = N_PRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .looper = true, .midi_notes = true,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver,
};

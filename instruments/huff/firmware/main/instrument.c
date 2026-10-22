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
//   BRASS (TRUMPET, TROMBONE, DIDGERIDOO)  STK's Brass: a lip resonance blowing into a bore twice the note's
//          period; the lip picks the bore's mode, so blowing hard tightens the lip and the note jumps an octave.
//          The didgeridoo has a vocal formant that opens with the breath.
//   STRING (GUITAR)  a string in a loop (Karplus-Strong) plucked when a note starts and fed by the breath while
//          it lasts, a soft clip in the loop: held, it swells and sings like a guitar on a sustainer. Let go and
//          it rings out. DRIVE on the TONE page is the amp.
//   SYNTH (SYNTH LEAD)  two detuned saws and a sub octave through a resonant low pass the breath opens.
//   VOICE (CHOIR)  a glottal pulse through three formants, from OO to AH as the breath grows.
//
// The breath is the mic's low band (wind on a MEMS mic is rumble) over the room's floor, and it only counts
// while that band stands well above the mid band (speech and music have a middle, wind has not: the ratio is
// learned by BREATH SETUP and ROOM NOISE sets how strict), held for a moment first (a kick drum's thump peaks
// and is gone, a breath keeps coming). The room's own level is subtracted, so a breath over noise keeps its
// dynamics.
//
// Pitch comes from the tilt (a ladder of notes, with hysteresis), the joystick, or your own humming (YIN on
// the mic); MIDI notes in override it while held, so a keyboard can play it while you blow. MIDI out:
// notes, breath as CC 2, the bend as pitch bend.
//
// The link (link.h): KEY and SCALE are the flock's harmony (change them here and the other boards follow, as
// theirs change HUFF's), the SONG page's TEMPO is the flock's, and ECHO BEATS puts the echo on its beat.
#include "core.h"
#include "dsp.h"
#include "ui.h"
#include "oled.h"
#include "looper.h"
#include "front.h"
#include "link.h"
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
    P_ESYNC, P_TEMPO, P_LINK, P_GROUP, P_RANGE, P_PEERS,      // added with the link (2026-10-19): after the hidden one, so CCs stay
    P_DRIVE, P_NOISE, P_BWIND,                                // 2026-10-22: the amp, the room, the learned breath/room band ratio
    P_N
};
enum { G_PLAY, G_PITCH, G_BREATH, G_TONE, G_SPACE, G_SONG, G_LINK, G_N };
static const char *const groups[G_N] = { "PLAY", "PITCH", "BREATH", "TONE", "SPACE", "SONG", "LINK" };

enum { M_OCARINA, M_FLUTE, M_PANPIPE, M_SHAKU, M_CLARINET, M_WHISTLE, M_BOTTLE, M_TRUMPET, M_TROMBONE, M_DIDGE, M_GUITAR, M_LEAD, M_CHOIR, M_N };
static const char *const model_l[M_N] = { "OCARINA", "FLUTE", "PAN PIPE", "SHAKUHACHI", "CLARINET", "WHISTLE", "BOTTLE",
                                          "TRUMPET", "TROMBONE", "DIDGERIDOO", "GUITAR", "SYNTH LEAD", "CHOIR" };
// the scales: the link's six (MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR, numbered as SING does) and HUFF's own
enum { SC_MAJPENTA, SC_MINPENTA, SC_MAJOR, SC_MINOR, SC_DORIAN, SC_MIXO, SC_HIJAZ, SC_BLUES, SC_INSEN, SC_CHROMATIC, SC_PHRYGIAN, SC_HARMMIN, SC_N };
static const char *const scale_l[SC_N] = { "MAJ PENTA", "MIN PENTA", "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "HIJAZ", "BLUES", "IN SEN", "CHROMATIC", "PHRYGIAN", "HARM MINOR" };
static const struct { int n; int8_t st[12]; } scales[SC_N] = {
    { 5, { 0, 2, 4, 7, 9 } }, { 5, { 0, 3, 5, 7, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 11 } }, { 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { 7, { 0, 2, 3, 5, 7, 9, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 10 } }, { 7, { 0, 1, 4, 5, 7, 8, 10 } }, { 6, { 0, 3, 5, 6, 7, 10 } },
    { 5, { 0, 1, 5, 7, 10 } }, { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    { 7, { 0, 1, 3, 5, 7, 8, 10 } }, { 7, { 0, 2, 3, 5, 7, 8, 11 } },
};
// each scale as the link numbers it (the shared scale its notes sit in); -1: none, the flock keeps its own
static const int8_t scale_to_link[SC_N] = { 0, 1, 0, 1, 2, 3, 4, 1, 4, -1, 4, 5 };
static const int8_t scale_from_link[6] = { SC_MAJOR, SC_MINOR, SC_DORIAN, SC_MIXO, SC_PHRYGIAN, SC_HARMMIN };
static const char *const key_l[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const source_l[] = { "TILT", "JOYSTICK", "HUMMING" };
static const char *const onoff_l[] = { "OFF", "ON" };
static const char *const lean_l[] = { "VIBRATO", "BEND", "NOTHING" };
static const char *const joyy_l[] = { "VIBRATO", "BRIGHTNESS", "OCTAVE" };
static const char *const curve_l[] = { "SOFT", "EVEN", "HARD" };
static const char *const harm_l[] = { "OFF", "3RD UP", "5TH UP", "OCTAVE UP", "OCTAVE DN", "DRONE", "DRONE 5TH" };
enum { H_OFF, H_THIRD, H_FIFTH, H_OCTUP, H_OCTDN, H_DRONE, H_DRONE5 };
static const char *const esync_l[] = { "OFF", "1/8", "1/8 DOT", "1/4", "1/4 DOT", "1/2" };
static const float esync_beats[] = { 0, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };
#define SYS (PF_SYS | PF_NOCC)

static const param_t params[P_N] = {
    [P_MODEL]  = { "MODEL", "modl", G_PLAY, PK_ENUM, 0, 0, M_N - 1, 1, M_OCARINA, "", model_l, "WHAT YOU ARE BLOWING INTO" },
    [P_SCALE]  = { "SCALE", "scal", G_PLAY, PK_ENUM, 0, 0, SC_N - 1, 1, 0, "", scale_l, "THE NOTES THE TILT WALKS" },
    [P_KEY]    = { "KEY", "key ", G_PLAY, PK_ENUM, 0, 0, 11, 1, 0, "", key_l, "THE SCALE'S HOME NOTE" },
    [P_OCT]    = { "OCTAVE", "octv", G_PLAY, PK_NUM, PF_INT | PF_SIGNED, -4, 2, 1, 0, "", NULL, "SHIFT ALL NOTES BY OCTAVES" },
    [P_NOTES]  = { "NOTES", "nots", G_PLAY, PK_NUM, PF_INT, 3, 24, 1, 10, "", NULL, "NOTES ON THE TILT LADDER" },
    [P_SOURCE] = { "PITCH FROM", "psrc", G_PITCH, PK_ENUM, 0, 0, 2, 1, 0, "", source_l, "WHAT CHOOSES THE NOTE" },
    [P_SNAP]   = { "SNAP", "snap", G_PITCH, PK_ENUM, 0, 0, 1, 1, 1, "", onoff_l, "OFF: SLIDE BETWEEN NOTES" },
    [P_SPAN]   = { "TILT SPAN", "span", G_PITCH, PK_NUM, PF_INT, 30, 180, 5, 70, "deg", NULL, "DEGREES FROM LOW TO HIGH" },
    [P_GLIDE]  = { "GLIDE", "glid", G_PITCH, PK_NUM, PF_LOG, 0.002f, 1.0f, 1.25f, 0.025f, "s", NULL, "TIME TO SLIDE TO A NOTE" },
    [P_BENDR]  = { "BEND RANGE", "bndr", G_PITCH, PK_NUM, PF_INT, 1, 12, 1, 2, "st", NULL, "HOW FAR THE STICK BENDS" },
    [P_LEAN]   = { "LEAN DOES", "lean", G_PITCH, PK_ENUM, 0, 0, 2, 1, 0, "", lean_l, "SIDE TO SIDE TILT" },
    [P_JOYY]   = { "STICK UP/DN", "joyy", G_PITCH, PK_ENUM, 0, 0, 2, 1, 0, "", joyy_l, "WHAT UP/DOWN ON IT DOES" },
    [P_TUNE]   = { "A4 TUNING", "tune", G_PITCH, PK_NUM, PF_INT, 415, 466, 1, 440, "Hz", NULL, "TUNE TO OTHER PLAYERS" },
    [P_SENSE]  = { "SENSITIVITY", "sens", G_BREATH, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "MORE: SOFT BREATH, LOUD" },
    [P_CURVE]  = { "CURVE", "curv", G_BREATH, PK_ENUM, 0, 0, 2, 1, 1, "", curve_l, "HOW BREATH MAPS TO LEVEL" },
    [P_GATE]   = { "GATE", "gate", G_BREATH, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "MORE: IGNORE THE ROOM" },
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
    [P_BRANGE] = { "BREATH RANGE", "brng", G_BREATH, PK_NUM, PF_HIDE | PF_SYS | PF_NOCC | PF_LOG, 0.01f, 4.0f, 1.1f, 1.0f, "", NULL, "SET BY BREATH SETUP" },
    [P_ESYNC]  = { "ECHO BEATS", "esyn", G_SPACE, PK_ENUM, 0, 0, 5, 1, 0, "", esync_l, "ON THE FLOCK'S BEAT" },
    [P_TEMPO]  = { "TEMPO", "stmp", G_SONG, PK_NUM, PF_INT | PF_SYS, 40, 180, 1, 100, "bpm", NULL, "SHARED WITH THE FLOCK" },
    [P_LINK]   = { "LINK", "nlnk", G_LINK, PK_ENUM, SYS, 0, 1, 1, 1, "", onoff_l, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]  = { "GROUP", "ngrp", G_LINK, PK_NUM, SYS | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]  = { "RANGE", "npow", G_LINK, PK_ENUM, SYS, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_PEERS]  = { "WHO IS THERE", "nwho", G_LINK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
    [P_DRIVE]  = { "DRIVE", "driv", G_TONE, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE AMP: CLEAN TO FUZZ" },
    [P_NOISE]  = { "ROOM NOISE", "nois", G_BREATH, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "MORE: ONLY WIND COUNTS" },
    [P_BWIND]  = { "BREATH BANDS", "bwnd", G_BREATH, PK_NUM, PF_HIDE | PF_SYS | PF_NOCC | PF_LOG, 0.3f, 20.0f, 1.1f, 0.6f, "", NULL, "SET BY BREATH SETUP" },
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
    { "GUITAR SOLO",   "modl=10 scal=1 key=9 octv=-1 driv=70 lean=1 bndr=2 vibd=12 vdly=0.25 rvb=25 echo=25 etim=0.3 efb=30 brit=55" },
    { "CLEAN GUITAR",  "modl=10 scal=2 octv=-1 driv=0 lean=1 bndr=2 vibd=0 rvb=35 rsiz=50 brit=45" },
    { "POWER CHORD",   "modl=10 scal=1 key=4 octv=-2 driv=95 harm=2 hlvl=90 lean=1 bndr=2 vibd=0 rvb=20 brit=50 nots=8" },
    { "SYNTH SOLO",    "modl=11 scal=1 key=2 octv=-1 driv=25 lean=1 bndr=2 vibd=20 vdly=0.2 glid=0.04 echo=30 etim=0.38 efb=40 rvb=30 brit=55" },
    { "ACID LEAD",     "modl=11 scal=7 key=9 octv=-2 driv=45 brit=75 glid=0.06 snap=0 nots=12 echo=20 etim=0.19 rvb=15" },
    { "TRUMPET",       "modl=7 scal=2 key=10 octv=-1 air=15 chif=25 vibd=12 vdly=0.4 rvb=30 rsiz=60 brit=55" },
    { "TROMBONE",      "modl=8 scal=2 key=10 octv=-1 air=15 chif=15 vibd=8 vdly=0.5 rvb=35 rsiz=70 brit=40 snap=0 glid=0.06 nots=8" },
    { "DIDGERIDOO",    "modl=9 scal=9 key=2 nots=3 air=60 chif=0 vibd=0 rvb=45 rsiz=80 damp=50 brit=50 lean=2" },
    { "CHOIR",         "modl=12 scal=2 rvb=65 rsiz=85 damp=55 vibd=10 vdly=0.5 harm=1 hlvl=55 air=20 chif=0" },
    { "DEEP CHOIR",    "modl=12 scal=3 key=2 octv=-2 rvb=70 rsiz=95 damp=60 vibd=6 vdly=0.8 harm=5 hlvl=60 air=20 chif=0 nots=8" },
};
#define N_PRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_BLOW = LEARN_INST, L_OVERBLOW, L_SETUP };
static const hint_t hints[] = {
    { "BLOW GENTLY AT THE MIC", L_BLOW },
    { "TOO EASY, OR DEAF? MENU: BREATH SETUP", L_SETUP },
    { "TILT TO CHANGE THE NOTE", LEARN_TILT },
    { "BLOW SOFT, THEN HARDER: IT FOLLOWS", L_OVERBLOW },
    { "TURN THE KNOB: NEW SOUND", LEARN_TURN },
    { "STICK LEFT OR RIGHT BENDS THE NOTE", LEARN_JOY },
    { "HOLD REC TO LOOP, THEN PLAY ALONG", LEARN_REC },
    { "HOLD THE KNOB TO KEEP THE NOTE", LEARN_HOLD },
    { "PRESS THE KNOB FOR MORE", LEARN_MENU },
};

// ---------------------------------------------------------------- the models
enum { E_VESSEL, E_JET, E_REED, E_BRASS, E_STRING, E_SYNTH, E_VOICE };
typedef struct {
    uint8_t eng;
    int8_t  shift;          // semitones above the ladder (whistle +12, bottle -12)
    float   noise;          // breath noise in the pressure (jet, reed); the air level (vessel)
    float   pole;           // jet: the bore end's low pass; higher is darker
    float   tune_a, tune_k; // jet, reed, brass, string: how sharp the loop plays at MIDI 76 (cents) and per semitone above
                            // it (measured in tools/sim, HUFF_CAL=1, at a normal breath); taken off the bore delay
    float   jet_hi;         // jet: overblown jet length (0.45 of the bore normally)
    float   ob_st;          // overblow in semitones: 12 (octave, jet: the jet does it), 19.02 (twelfth), 0 none
    float   h2, h3, q;      // vessel: overtones, the air resonance's Q
    float   gain;           // level: about -18 dBFS rms at a normal breath
    float   tone;           // tone filter, in harmonics, at BRIGHTNESS 50 %
    float   chiff_f;        // chiff band, in harmonics
    const float *tune_tab;  // or, when the error is no line: cents sharp at MIDI 24, 28, .. 96 (19), interpolated
} model_t;
#define TUNE_N 19
static float tune_cents(const model_t *md, float m)              // block rate
{
    if (!md->tune_tab) return md->tune_a + md->tune_k * (m - 76.0f);
    float o = clampf((m - 24.0f) / 4.0f, 0.0f, (float)(TUNE_N - 1) - 0.001f);
    int i = (int)o; float fr = o - (float)i;
    return md->tune_tab[i] + fr * (md->tune_tab[i + 1] - md->tune_tab[i]);
}
// measured with HUFF_CAL=1 at a normal breath (2026-10-22): cents sharp at MIDI 24, 28, .. 96, taken off the model's
// frequency. Where a model does not speak (the jets under about 100 Hz, the brass under 60) the last good value holds.
static const float tune_flute[TUNE_N]   = { -2.9f, -4.7f, 114.8f, 92.1f, 71.4f, 57.9f, 55.1f, 50.5f, 46.1f, 43.4f, 40.2f, 36.2f, 33.5f, 29.7f, 24.8f, 20.3f, 17.1f, 13.1f, 13.1f };
static const float tune_pan[TUNE_N]     = { 101.3f, 1.3f, 117.2f, 87.5f, 72.4f, 57.7f, 52.4f, 47, 41.2f, 37.7f, 32.4f, 27.2f, 21.8f, 14.8f, 5.9f, -1.2f, -9.9f, -14.5f, -14.5f };
static const float tune_shaku[TUNE_N]   = { -5.5f, -5.5f, -0.2f, 2.6f, 75.4f, 56.3f, 53.9f, 49.1f, 43.9f, 41, 36.7f, 32.5f, 29, 23.7f, 16.5f, 10.7f, 5.2f, -1, -1 };
static const float tune_whistle[TUNE_N] = { 43.9f, 43.9f, 43.9f, 91.3f, 71.2f, 58.7f, 57.3f, 53.3f, 49.4f, 47.7f, 45.6f, 42.9f, 42.3f, 40.5f, 38.6f, 37.4f, 36.8f, 36.7f, 36.7f };
static const float tune_brass[TUNE_N]   = { 145.8f, 102.2f, 69.8f, 58.5f, 52.9f, 45, 39.9f, 32, 27.7f, 24.1f, 20.8f, 19.2f, 19.5f, 19.4f, 19.1f, 20.3f, 16.9f, 9.8f, 2 };
static const float tune_string[TUNE_N]  = { -21, -16.4f, -13.9f, -10.5f, -7.9f, -7.1f, -5, -3.8f, -4.4f, -3.1f, -2.2f, -3.2f, -2.5f, -2.9f, -2.9f, -2.9f, -2.9f, -2.9f, -2.9f };
static const model_t models[M_N] = {
    [M_OCARINA]  = { E_VESSEL, 0,   0.30f, 0,     0,     0,      0,     0,      0.10f, 0.035f, 7.0f,  0.56f, 5.0f, 3.0f, NULL },
    [M_FLUTE]    = { E_JET,    0,   0.12f, 0.63f, 0,     0,      0.22f, 12.0f,  0, 0, 0,             0.17f, 8.0f, 4.0f, tune_flute },
    [M_PANPIPE]  = { E_JET,    0,   0.24f, 0.70f, 0,     0,      0.22f, 12.0f,  0, 0, 0,             0.17f, 5.0f, 3.0f, tune_pan },
    [M_SHAKU]    = { E_JET,    0,   0.30f, 0.66f, 0,     0,      0.22f, 12.0f,  0, 0, 0,             0.17f, 6.0f, 2.5f, tune_shaku },
    [M_CLARINET] = { E_REED,   0,   0.12f, 0,     0,     0,      0,     19.02f, 0, 0, 0,             0.36f, 9.0f, 5.0f, NULL },
    [M_WHISTLE]  = { E_JET,    12,  0.06f, 0.55f, 0,     0,      0.22f, 12.0f, 0, 0, 0,             0.16f, 10.0f, 5.0f, tune_whistle },
    [M_BOTTLE]   = { E_VESSEL, -12, 0.75f, 0,     0,     0,      0,     0,      0.02f, 0.01f, 22.0f, 0.8f, 3.0f, 2.0f, NULL },
    [M_TRUMPET]  = { E_BRASS,  0,   0.05f, 0,     0,     0,      0,     12.0f,  0, 0, 0,             1.3f,  9.0f, 4.0f, tune_brass },
    [M_TROMBONE] = { E_BRASS,  -12, 0.05f, 0,     0,     0,      0,     12.0f,  0, 0, 0,             1.3f,  6.0f, 3.0f, tune_brass },
    [M_DIDGE]    = { E_BRASS,  -36, 0.10f, 0,     0,     0,      0,     0,      0, 0, 4.0f,          2.0f,  12.0f, 2.0f, tune_brass },
    [M_GUITAR]   = { E_STRING, -12, 0,     0.5f,  0,     0,      0,     0,      0, 0, 0,             0.8f,  6.0f, 2.0f, tune_string },
    [M_LEAD]     = { E_SYNTH,  0,   0,     0,     0,     0,      0,     12.0f,  0, 0, 2.5f,          0.20f, 12.0f, 3.0f, NULL },
    [M_CHOIR]    = { E_VOICE,  -12, 0.12f, 0,     0,     0,      0,     0,      0, 0, 0,             0.7f,  8.0f, 2.0f, NULL },
};

// ---------------------------------------------------------------- shared between the UI and the audio task
static volatile float a_midi = 72.0f, a_midi_h = 79.0f;   // target notes (MIDI, fractional), before bends
static volatile float a_bend;                            // semitones
static volatile float a_vib;                             // vibrato depth now, semitones
static volatile float a_bright_add;                      // brightness from the joystick, -0.5..0.5
static volatile int   a_harm_on;                         // the harmony voice sounds
static volatile int   a_mute;                            // breath setup listens with the voice off
static volatile int   a_hum;                             // PITCH FROM HUMMING: a voice at the mic is the point, not noise
static volatile float a_cc_breath;                       // MIDI CC 2 in, 0..1 (decays when it stops)
static volatile int   a_onset_req;                       // the UI saw a new note while blowing: a small chiff
static volatile uint32_t a_fpb = 19200;                  // the flock's frames per beat (the echo's beats)
// audio -> UI
static volatile float v_breath, v_amp, v_benv, v_floor, v_oenv, v_menv;
static volatile int   v_open;                            // the breath gate is open
static volatile float v_mclean, v_thr, v_r266, v_r200;   // ... and what it saw (logged by tick() for tuning on a board)
static volatile int   v_windy, v_tonal, v_loud;
static float v_lvl;
float huff_breath(void) { return v_breath; }      // for tools/sim

static volatile int   v_over;
static volatile uint32_t v_onsets;

// ---------------------------------------------------------------- the audio side
#define BORE_N 4096            // the brass bore is two periods long: 15.6 Hz at the bottom
typedef struct {
    float bore_buf[BORE_N], jet_buf[512];
    dline_t bore, jet;
    float bore_last, refl_lp, jet_s, reed_z;
    dcb_t dcb;
    float ph, ph2, ph3;        // vessel oscillator; the synth's second saw and its sub octave
    float lip_y1, lip_y2;      // brass: the lip's resonance
    float str_k, str_d, str_g; // string: the loss filter's coefficient, the delay it adds, the loop gain that holds the note (block rate)
    float tune_f;              // the model's tuning error at this note, as a factor on the frequency (block rate)
    float glot;                // voice: the glottal pulse's rounding
    svf_t air_bp, chiff_bp, f3;
    float m_s;                 // smoothed note, MIDI
    float ob_s;                // smoothed overblow (semitones, reed)
    float press_s, amp_s, dyn, tone_z, tone_z2;
    float chiff;
    bool  fold;                // jet: the note is past the jet's reach (about 2 kHz): played an octave down (hysteresis, block rate)
    bool  awake;               // it sounds, or still rings; asleep it costs nothing
    int   quiet;               // blocks in a row with no breath and nothing coming out
} voice_t;
static voice_t vo[2];
static int    model_cur, model_want;
static float  duck = 1.0f;     // the model switch: down to 0, swap, back to 1
static float  harm_g;          // the harmony voice's level, ramped
static uint32_t seed = 0x2b8e1d33u;
// breath
static float benv, bms, bfloor = 0.01f, b_s, b_raw_s, b_smooth, ms_k;   // b_raw_s: the last block's breath before the setup's mute
static biquad_t out_hp;
static float blp[4], olp[4], oenv, lp_k;         // breath lives low: the mic and our own output, four poles at 250 Hz
static float mlp1[3], mlp2, mms, menv, mk1, mk2; // the mic's middle (500 Hz to 4 kHz, the high pass three poles steep so the
                                                 // breath's rumble does not leak into it): words and music live there, wind does not
static float mfloor = 0.001f;                    // the room's middle, learned as the low band's floor is
static bool  bopen; static int open_n, close_n, unwind_n, cool_n, voice_n; static float open_pk;   // the breath gate
static float lring[512]; static uint32_t lw; static float ac200, ac266;   // the low band's autocorrelation at two lags: a tone or not
static float drive_s, cab_z, cab_k;              // the amp
static int   over;
static float b_slow;
static float vib_ph;
static float tone_k_s = 0.5f, damp_s = 0.6f, efb_s;
// YIN feed: the mic at 8 kHz
#define YR 2048
static float yring[YR];
static volatile uint32_t yring_w;
static float ylp1, ylp2; static int ydec;
// reverb: four lines, a Householder mix, damping in each line. It sleeps once its tail has gone
static const int rv_len[4] = { 1123, 1381, 1621, 1871 };
static float rv_buf[4][2048];
static float rv_lp[4], rv_ap_buf[2][512];
static int   rv_w, rv_apw[2];
static const int rv_aplen[2] = { 211, 337 };
static float rv_g_s = 0.7f;
static bool  rvb_awake; static int rvb_quiet;
// echo (PSRAM): up to 1.5 s, so a slow tempo's beats fit; it sleeps when it is off and its repeats have died
#define ECHO_N 49152
static float *echo_buf;
static int    echo_w;
static float  echo_d_s = 0.33f * AUDIO_SR, echo_lp;
static bool   echo_awake; static int echo_quiet;
static float  fx_rvb_s, fx_echo_s;
static float  a4_s = 440.0f, bend_s, vib_s;
static float  blk_0005, blk_0008, blk_0015;       // the per-sample smoothers' steps over a whole block (set in init)
static volatile int v_sleep;                      // what sleeps now: 1 the voice, 2 the harmony, 4 the reverb, 8 the echo (for tools/sim)
int huff_sleep(void) { return v_sleep; }

static inline float jet_table(float x) { float y = x * (x * x - 1.0f); return clampf(y, -1.0f, 1.0f); }
static inline float reed_table(float x) { return clampf(0.7f - 0.3f * x, -1.0f, 1.0f); }

static void voice_reset(voice_t *v)
{
    memset(v->bore_buf, 0, sizeof v->bore_buf); memset(v->jet_buf, 0, sizeof v->jet_buf);
    v->bore = (dline_t){ v->bore_buf, BORE_N - 1, 0 };
    v->jet = (dline_t){ v->jet_buf, 511, 0 };
    v->bore_last = v->refl_lp = v->reed_z = 0; v->dcb = (dcb_t){ 0, 0 };
    v->jet_s = 0.45f; v->press_s = v->amp_s = v->dyn = 0; v->tone_z = v->tone_z2 = 0; v->chiff = 0;
    v->air_bp = (svf_t){ 0 }; v->chiff_bp = (svf_t){ 0 }; v->f3 = (svf_t){ 0 };
    v->ph2 = v->ph3 = 0; v->lip_y1 = v->lip_y2 = 0; v->glot = 0; v->str_k = 0.6f; v->str_d = 0.67f; v->str_g = 1.0f; v->tune_f = 1.0f;
    v->ob_s = 0; v->fold = false; v->awake = false; v->quiet = 0;
}

// One voice for one sample. m: the note (MIDI, with bends and vibrato), b: breath 0..1, ob: overblown.
static inline float voice_run(voice_t *v, const model_t *md, float m, float b, int ob, float a4, float air, float noise)
{
    float f = a4 * fast_exp2((m - 69.0f) * (1.0f / 12.0f));
    float y;
    switch (md->eng) {
    case E_JET: {
        if (v->fold) f *= 0.5f;                                     // past about B6 the jet loses its mode: an octave down
        // the jet speaks from a pressure of about 0.85 and its pitch moves with it: keep the pressure in a
        // narrow band that speaks on every note and take the loudness from the breath instead
        float p_t = b > 0.02f ? 0.9f + 0.22f * b : 0.0f;
        v->press_s += 0.004f * (p_t - v->press_s);
        v->dyn += 0.003f * ((b > 0.02f ? 0.15f + 0.85f * b : 0.0f) - v->dyn);
        v->jet_s += 0.0015f * ((ob ? md->jet_hi : 0.45f) - v->jet_s);
        float pressure = v->press_s * (1.0f + (md->noise * (0.4f + air) + 0.6f * v->chiff) * noise);
        v->refl_lp = (1.0f - md->pole) * v->bore_last + md->pole * v->refl_lp;
        float temp = dcb_run(&v->dcb, v->refl_lp);
        float fc = f * v->tune_f;
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
        float bd = clampf(0.5f * (float)AUDIO_SR / (f * v->tune_f) - 1.5f, 2.0f, (float)(BORE_N - 4));
        float bo = dline_read(&v->bore, bd);
        dline_write(&v->bore, in);
        v->bore_last = flush_denormal(bo);
        v->dyn += 0.003f * ((b > 0.02f ? 0.3f + 0.7f * b : 0.0f) - v->dyn);     // the reed's own dynamics are small
        y = bo * v->dyn;
        v->amp_s = v->dyn;
        break;
    }
    case E_BRASS: {
        // STK's Brass: the lip is a two-pole resonance at the note, blown against the bore's pressure; the bore is two
        // periods long, so its modes are the note's harmonics and the lip picks one. Blown hard the lip tightens an
        // octave (ob_s) and the note jumps. The didgeridoo (q > 0) adds a vocal formant the breath opens.
        float p_t = b > 0.02f ? 0.55f + 0.45f * b : 0.0f;
        v->press_s += 0.004f * (p_t - v->press_s);
        v->ob_s += 0.003f * ((ob ? md->ob_st : 0.0f) - v->ob_s);
        float fc = f * v->tune_f;
        float fl = fc * fast_exp2(v->ob_s * (1.0f / 12.0f));
        float breath = v->press_s * (1.0f + (md->noise * (0.4f + air) + 0.3f * v->chiff) * noise);
        float mouth = 0.3f * breath, borep = 0.85f * v->bore_last;
        float d = mouth - borep;
        float w = fl * (1.0f / (float)AUDIO_SR);
        float r = 1.0f - 0.3927f * w;                                        // radius: a Q of 8 on every note (bandwidth f / 8)
        float c1 = 2.0f * r * sin_turns(w + 0.25f), c2 = -r * r;            // the gain set so the lip's rest opening is the
        float ly = 3.0f * (1.0f - c1 - c2) * d + c1 * v->lip_y1 + c2 * v->lip_y2;   // same on every note
        v->lip_y2 = v->lip_y1; v->lip_y1 = flush_denormal(ly);
        float dd = ly * ly; if (dd > 1.0f) dd = 1.0f;
        float pin = dd * mouth + (1.0f - dd) * borep;
        float bd = clampf(2.0f * (float)AUDIO_SR / fc + 3.0f, 4.0f, (float)(BORE_N - 4));
        float bo = dline_read(&v->bore, bd);
        dline_write(&v->bore, dcb_run(&v->dcb, pin));
        v->bore_last = flush_denormal(bo);
        v->dyn += 0.003f * ((b > 0.02f ? 0.3f + 0.7f * b : 0.0f) - v->dyn);
        y = bo * v->dyn;
        if (md->q > 0.0f) y = 0.5f * y + 1.6f * svf_bp(&v->air_bp, y, NULL);
        v->amp_s = v->dyn;
        break;
    }
    case E_STRING: {
        // a string: one period of delay with a loss filter, plucked when a note starts (the chiff is the pick), fed a
        // little noise while the breath lasts and clipped softly in the loop, so a held note swells and sings as on a
        // sustainer. No breath: it rings out like a guitar.
        v->press_s += (b > v->press_s ? 0.002f : 0.0007f) * (b - v->press_s);
        float fc = f * v->tune_f;
        float bd = clampf((float)AUDIO_SR / fc - v->str_d, 2.0f, (float)(BORE_N - 4));
        float so = dline_read(&v->bore, bd);
        v->refl_lp += v->str_k * (so - v->refl_lp);
        float g = v->str_g;
        float exc = v->chiff * (1.5f + 2.0f * b) * noise + v->press_s * v->press_s * 0.12f * svf_bp(&v->air_bp, noise, NULL);   // the sustainer feeds the note
        float x = 0.5f * soft_tanh(2.0f * (v->refl_lp * g + exc));          // the loop's ceiling: 0.5
        float dcy = x - v->dcb.x1 + 0.9995f * v->dcb.y1; v->dcb.x1 = x; v->dcb.y1 = flush_denormal(dcy);   // no DC in the loop (with a
        dline_write(&v->bore, dcy);                                          // gain over 1 it fills with it); a gentle one: its phase lead shortens the loop
        v->bore_last = flush_denormal(so);
        v->refl_lp = flush_denormal(v->refl_lp);
        v->chiff *= 0.994f;                                          // the pick is short
        v->dyn += 0.01f * (fabsf(so) - v->dyn);
        y = so * (0.6f + 0.4f * v->press_s);
        v->amp_s = v->dyn;
        break;
    }
    case E_SYNTH: {
        // a lead: two saws a little apart and a square an octave down, through a resonant low pass the breath opens
        v->press_s += (b > v->press_s ? 0.004f : 0.002f) * (b - v->press_s);
        v->ob_s += 0.003f * ((ob ? md->ob_st : 0.0f) - v->ob_s);
        float fs = f * fast_exp2(v->ob_s * (1.0f / 12.0f));
        float dt = fs * (1.0f / (float)AUDIO_SR), dt2 = dt * 1.006f, dts = 0.5f * dt;
        v->ph = wrap1(v->ph + dt); v->ph2 = wrap1(v->ph2 + dt2); v->ph3 = wrap1(v->ph3 + dts);
        float x = 0.5f * (saw_blep(v->ph, dt) + saw_blep(v->ph2, dt2)) + 0.25f * square_blep(v->ph3, dts);
        y = svf_lp(&v->air_bp, x) * v->press_s;
        v->amp_s = v->press_s;
        break;
    }
    case E_VOICE: {
        // a voice: a rounded glottal pulse, a little breath, through three formants (set at block rate: OO to AH)
        v->press_s += (b > v->press_s ? 0.0025f : 0.0015f) * (b - v->press_s);
        float dt = f * (1.0f / (float)AUDIO_SR);
        v->ph = wrap1(v->ph + dt);
        v->glot += 0.25f * (saw_blep(v->ph, dt) - v->glot);
        float src = v->glot + md->noise * (0.5f + air) * noise;
        float y1 = svf_bp(&v->air_bp, src, NULL), y2 = svf_bp(&v->chiff_bp, src, NULL), y3 = svf_bp(&v->f3, src, NULL);
        y = (y1 + 0.6f * y2 + 0.3f * y3 + 0.15f * v->glot) * v->press_s;
        v->amp_s = v->press_s;
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
        float d = b[r], u = flush_denormal(x + 0.6f * d);
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
        lring[lw & 511] = x;                                        // half a period of 60 Hz and of 80 Hz back: a kick's thump
        ac266 += ms_k * (x * lring[(lw - 266) & 511] - ac266);      // or a bass note correlates negatively there, a breath's
        ac200 += ms_k * (x * lring[(lw - 200) & 511] - ac200);      // rumble not at all
        lw++;
        float h = in[i];
        for (int j = 0; j < 3; j++) { mlp1[j] += mk1 * (h - mlp1[j]); h -= mlp1[j]; }   // the middle: 500 Hz (three poles) to 4 kHz
        mlp2 += mk2 * (h - mlp2);
        mms += ms_k * (mlp2 * mlp2 - mms);
        ylp1 += 0.33f * (in[i] - ylp1); ylp2 += 0.33f * (ylp1 - ylp2);
        if (++ydec >= 4) { ydec = 0; yring[yw & (YR - 1)] = ylp2; yw++; }
    }
    yring_w = yw;
    for (int j = 0; j < 4; j++) blp[j] = flush_denormal(blp[j]);
    for (int j = 0; j < 3; j++) mlp1[j] = flush_denormal(mlp1[j]);
    mlp2 = flush_denormal(mlp2); mms = flush_denormal(mms); ac200 = flush_denormal(ac200); ac266 = flush_denormal(ac266);
    benv = 2.8f * sqrtf(bms);                                     // a 30 ms RMS: steadier than a peak on gusty air
    menv = 2.8f * sqrtf(mms);
    float gate = pv[P_GATE] * 0.01f;
    // the room's own low band comes off in power (subtracted, not just stepped over: a breath over noise keeps its
    // dynamics), then a threshold over what is left; the speaker's own low band must not count as breath either
    float over_room = bms * 7.84f - bfloor * bfloor;
    float bclean = over_room > 0.0f ? sqrtf(over_room) : 0.0f;
    float thr = bfloor * (1.0f + 3.6f * gate) + 0.002f + 1.2f * oenv;
    // wind, not words: a breath's low band stands well over its middle (BREATH SETUP learns the ratio, ROOM NOISE sets
    // how strict), and it keeps coming (a kick's thump peaks and is gone), so the gate opens after a moment and
    // closes when the breath is gone for 30 ms or has sounded like a voice for 100 ms. HUMMING is a voice: it passes.
    float nz = pv[P_NOISE] * 0.01f;
    float range = pv[P_BRANGE] * exp2f((50.0f - pv[P_SENSE]) / 25.0f);
    bool loud = bclean > thr;
    // the bands compared are what stands over the room in each (a talker's steady middle is the room's, the breath's own
    // middle and the syllable on top of it are what counts against its low band). Measured on a board (2026-10-22): a
    // soft breath's low band is 5 to 20x its middle, a hard one's 1 to 2x (it is broadband at the port), so the band test
    // is asked only of sounds under half a full breath (a voice never rumbles like a breath at the mouth), and only to
    // open the gate, never to close it. (A tone test on the low band, for kicks and bass notes, was tried and dropped:
    // real breath rumble repeats itself half a period later just as well.)
    float over_mid = mms * 7.84f - mfloor * mfloor;
    float mclean = over_mid > 0.0f ? sqrtf(over_mid) : 0.0f;
    float inv = 1.0f / (bms + 1e-9f);
    bool windy = nz <= 0.001f || a_hum || bclean > 0.4f * range || bclean > (mclean + 0.0005f) * exp2f(2.0f * nz * log2f(pv[P_BWIND]));
    bool wind_in = windy;
    // the room's floor: follows a quieter room at once, a noisier one in about 4 s while nothing wind-like is heard
    // (between breaths, or under words and music), never under a breath, so a held note does not fade itself out.
    // (A steady rumble with no middle, a bass line alone, passes for a breath and is not learned: ROOM NOISE up, or
    // BREATH SETUP in a quiet room first, is the answer; learning it slowly under the open gate was tried on 2026-10-22
    // and ate long notes through the threshold.)
    // (b_s is the last block's breath: a gate that has been open at under 12 % for seconds is the room, not a breath;
    // measured on a board 2026-10-22, a room's low band at 0.03..0.05 kept it open at b 0.01..0.1 and the floor at 0.01)
    bool room_like = bopen && b_raw_s < 0.12f;
    if (benv < bfloor) bfloor += 0.2f * (benv - bfloor);
    else if (bclean < thr || room_like) bfloor += 0.0005f * (benv - bfloor);
    else if (!bopen && !windy) bfloor += 0.0001f * (benv - bfloor);    // 20 s: on a board a hard breath can read as not wind
    if (menv < mfloor) mfloor += 0.2f * (menv - mfloor);
    else if (bclean < thr || room_like) mfloor += 0.0005f * (menv - mfloor);
    else if (!bopen && !windy) mfloor += 0.0001f * (menv - mfloor);
    int hold = (int)(nz * 20.0f);                                 // blocks: 0..40 ms
    // a sound that was just a voice (or a kick's click) may not open the gate for a while (0..100 ms by ROOM NOISE):
    // the thump that follows a click would, after the click has left the middle band's 30 ms window. Silence is not
    // a voice: the wait only starts from a sound over the threshold that has sounded like one for 6 ms (the first
    // blocks of a breath that starts at once read as middle: the low band's filters lag a little)
    if (loud && !wind_in) { if (++voice_n >= 3) cool_n = (int)(nz * 50.0f); } else voice_n = 0;
    if ((wind_in || !loud) && cool_n > 0) cool_n--;
    if (!bopen) {
        if (loud && wind_in && cool_n == 0) { if (bclean > open_pk) open_pk = bclean; if (++open_n > hold && bclean > 0.6f * open_pk) bopen = true; }
        else { open_n = 0; open_pk = 0; }
    } else {
        if (bclean < 0.7f * thr) { if (++close_n >= 15) bopen = false; } else close_n = 0;
        unwind_n = 0;
        if (!bopen) { open_n = 0; open_pk = 0; close_n = unwind_n = 0; }
    }
    float b = bopen ? clampf((bclean - 0.7f * thr) / range, 0.0f, 1.0f) : 0.0f;
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
    b_raw_s = b;
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
    float hw = a_harm_on ? pv[P_HLVL] * 0.01f : 0.0f;
    // per-voice filters at the note; the jet's fold with a semitone of hysteresis, so vibrato on B6 does not flip octaves
    for (int k = 0; k < 2; k++) {
        voice_t *v = &vo[k];
        float f = a4 * exp2f((v->m_s - 69.0f) / 12.0f);
        v->tune_f = exp2f(-tune_cents(md, v->m_s + bend_s) / 1200.0f);
        switch (md->eng) {
        case E_BRASS:                                                // the didgeridoo's mouth: a formant the breath opens
            if (md->q > 0.0f) svf_set(&v->air_bp, 350.0f + 1400.0f * b * b, md->q);
            svf_set(&v->chiff_bp, f * md->chiff_f, 1.5f);
            break;
        case E_STRING: {                                             // the loss filter: darker strings lose more; its delay;
            float k = clampf(0.2f + 0.75f * bright, 0.2f, 0.95f);    // the loop gain: the note rings 1.5 s on its own, and the
            v->str_k = k; v->str_d = (1.0f - k) / k - 0.1f;          // breath holds its fundamental at exactly 1 (the clip bounds it)
            svf_set(&v->air_bp, f, 3.0f);                            // the sustainer's band: the note itself
            float w = TWO_PI * f / (float)AUDIO_SR, cw = cosf(w), hlp = k / sqrtf(1.0f - 2.0f * (1.0f - k) * cw + (1.0f - k) * (1.0f - k));
            hlp *= sqrtf((2.0f - 2.0f * cw) / (1.0f + 0.9995f * 0.9995f - 2.0f * 0.9995f * cw));   // the loop's DC blocker, at the note
            v->str_d -= atanf(2.55f / f) / w;                                 // ... and its phase lead, in samples (2.55 Hz corner)
            float g_nat = 1.0f - clampf(1.0f / (1.5f * f), 0.0005f, 0.05f);
            // blown, the loop's gain goes over 1 by up to 8 %: the cubic clip in the loop compresses more the louder the
            // string, so the breath sets where it settles (about 0.26 at full breath), and it gets there in a few periods
            v->str_g = (g_nat + (1.0f - g_nat) * v->press_s + 0.1f * v->press_s * v->press_s) / hlp;
            svf_set(&v->chiff_bp, f * md->chiff_f, 1.5f);
            break;
        }
        case E_SYNTH: {                                              // the low pass the breath opens
            float cut = f * (1.0f + 14.0f * b * sqrtf(b)) * exp2f((bright - 0.5f) * 2.0f);
            svf_set(&v->air_bp, clampf(cut, 80.0f, 12000.0f), md->q);
            svf_set(&v->chiff_bp, f * md->chiff_f, 1.5f);
            break;
        }
        case E_VOICE: {                                              // the formants: OO to AH as the breath grows
            float o = clampf(b * 1.6f, 0.0f, 1.0f);
            svf_set(&v->air_bp, 300.0f + 400.0f * o, 5.0f);
            svf_set(&v->chiff_bp, 870.0f + 300.0f * o, 6.0f);
            svf_set(&v->f3, 2240.0f + 300.0f * o, 6.0f);
            break;
        }
        default:
            svf_set(&v->air_bp, f, md->eng == E_VESSEL ? md->q : 2.0f);
            svf_set(&v->chiff_bp, f * md->chiff_f, 1.5f);
            break;
        }
        float fb = a4 * exp2f((v->m_s + bend_s - 69.0f) / 12.0f);
        if (md->eng == E_JET) { if (!v->fold && fb > 2000.0f) v->fold = true; else if (v->fold && fb < 1890.0f) v->fold = false; }
        else v->fold = false;
        // sleep: no breath, nothing left to ring (the breath's gate also wakes it through the chiff and the onset)
        bool fed = (b > 0.02f || v->chiff > 0.001f) && (k == 0 || hw > 0.0f || harm_g > 0.002f);
        if (fed) { v->awake = true; v->quiet = 0; }
    }
    float tone_hz = a4 * exp2f((vo[0].m_s - 69.0f) / 12.0f) * md->tone * exp2f((bright - 0.5f) * 3.0f);
    tone_k_s += 0.3f * (onepole_hz(clampf(tone_hz, 300.0f, 14000.0f)) - tone_k_s);
    float tone_k = tone_k_s;
    float drv = pv[P_DRIVE] * 0.01f;
    // effects
    float size = pv[P_SIZE] * 0.01f, rt = 0.5f + 5.5f * size * size;
    float g_t = exp2f(-3.0f * 3.3219f * 1500.0f / (rt * (float)AUDIO_SR));     // per pass of an average line
    damp_s += 0.05f * ((0.95f - 0.75f * pv[P_DAMP] * 0.01f) - damp_s);
    float damp = damp_s;
    float rvb_t = pv[P_REVERB] * 0.01f, echo_t = echo_buf ? pv[P_ECHO] * 0.01f : 0.0f;
    int   es = (int)pv[P_ESYNC];
    float ed_t = es > 0 && es < 6 ? clampf(esync_beats[es] * (float)a_fpb, 0.05f * (float)AUDIO_SR, (float)(ECHO_N - 64)) : pv[P_ETIME] * (float)AUDIO_SR;
    float efb = pv[P_EFB] * 0.01f;
    // the effects wake when something feeds them (or they are turned up) and sleep once their tails have gone
    if (rvb_t > 0.001f || fx_rvb_s > 0.001f) { if (vo[0].awake || vo[1].awake || echo_awake) { rvb_awake = true; rvb_quiet = 0; } }
    if (echo_t > 0.001f || fx_echo_s > 0.001f) { if (vo[0].awake || vo[1].awake) { echo_awake = true; echo_quiet = 0; } }
    bool run_rvb = rvb_awake, run_echo = echo_awake && echo_buf;
    if (!run_echo) echo_d_s = ed_t;                                  // asleep, the echo's time just follows (no tape to bend)

    float pk = 0, ypk = 0, vpk = 0, hpk = 0, epk = 0, wpk = 0;
    if (!vo[0].awake && !vo[1].awake && !run_rvb && !run_echo && duck >= 1.0f && model_want == model_cur) {
        // nothing sounds: the block is silence, and the smoothers take their n steps at once
        memset(out, 0, (size_t)n * sizeof(float));
        float kg = 1.0f - powf(1.0f - glide_k, (float)n);
        harm_g += blk_0005 * (hw - harm_g); efb_s += blk_0005 * (efb - efb_s);
        fx_echo_s += blk_0005 * (echo_t - fx_echo_s); fx_rvb_s += blk_0005 * (rvb_t - fx_rvb_s); rv_g_s += blk_0005 * (g_t - rv_g_s);
        vib_s += blk_0008 * (vib - vib_s); bend_s += blk_0015 * (bend - bend_s); drive_s += blk_0005 * (drv - drive_s);
        vib_ph = wrap1(vib_ph + (float)n * vib_inc);
        vo[0].m_s += kg * (tgt - vo[0].m_s); vo[1].m_s += kg * (tgt_h - vo[1].m_s);
    } else
    for (int i = 0; i < n; i++) {
        // duck for a model switch: 8 ms down, swap at the next block, 8 ms up
        if (model_want != model_cur) { duck -= 1.0f / 256.0f; if (duck < 0) duck = 0; }
        else if (duck < 1.0f) { duck += 1.0f / 256.0f; if (duck > 1) duck = 1; }
        harm_g += 0.0005f * (hw - harm_g);
        vib_ph = wrap1(vib_ph + vib_inc);
        vib_s += 0.0008f * (vib - vib_s);
        bend_s += 0.0015f * (bend - bend_s);
        efb_s += 0.0005f * (efb - efb_s);
        float vs = vib_s * sin_turns(vib_ph);
        vo[0].m_s += glide_k * (tgt - vo[0].m_s);
        vo[1].m_s += glide_k * (tgt_h - vo[1].m_s);
        float y = 0;
        if (vo[0].awake) {
            y = voice_run(&vo[0], md, vo[0].m_s + bend_s + vs, b, ob, a4, air, rng_bi(&seed));
            float ay = fabsf(y); if (ay > vpk) vpk = ay;
        }
        if (vo[1].awake && harm_g > 0.002f) {
            float h = voice_run(&vo[1], md, vo[1].m_s + bend_s + vs, b, ob, a4, air, rng_bi(&seed));
            float ah = fabsf(h); if (ah > hpk) hpk = ah;
            y += harm_g * h;
        }
        // tone: two one-poles at a few harmonics of the note
        vo[0].tone_z += tone_k * (y - vo[0].tone_z);
        vo[0].tone_z2 += tone_k * (vo[0].tone_z - vo[0].tone_z2);
        y = biquad_run(&out_hp, (0.35f * y + 0.65f * vo[0].tone_z2) * md->gain * duck);   // no rumble, no DC
        // the amp: a soft clip with up to 41x in front, levelled after, then a cabinet's low pass; faded in by DRIVE
        drive_s += 0.0005f * (drv - drive_s);
        if (drive_s > 0.0005f) {
            float pre = 1.0f + 40.0f * drive_s * drive_s;
            float o = soft_tanh(y * 3.0f * pre) * (0.2f + 0.8f / pre);
            cab_z += cab_k * (o - cab_z);
            o += drive_s * (cab_z - o);
            y += drive_s * (o - y);
        }
        float ay = fabsf(y); if (ay > ypk) ypk = ay;
        // effects
        fx_echo_s += 0.0005f * (echo_t - fx_echo_s);
        fx_rvb_s += 0.0005f * (rvb_t - fx_rvb_s);
        rv_g_s += 0.0005f * (g_t - rv_g_s);
        float e = 0;
        if (run_echo) {
            echo_d_s += 0.0002f * (ed_t - echo_d_s);
            float rp = (float)echo_w - echo_d_s;
            if (rp < 0) rp += (float)ECHO_N;
            int i0 = (int)rp; float fr = rp - (float)i0; int i1 = i0 + 1 >= ECHO_N ? 0 : i0 + 1;
            e = echo_buf[i0] + fr * (echo_buf[i1] - echo_buf[i0]);
            echo_lp += 0.35f * (e - echo_lp);
            echo_buf[echo_w] = flush_denormal(y * fx_echo_s + echo_lp * efb_s);
            if (++echo_w >= ECHO_N) echo_w = 0;
            float ae = fabsf(e); if (ae > epk) epk = ae;
        }
        float wet = 0;
        if (run_rvb) {
            wet = reverb_run((y + 0.5f * e) * 0.3f, rv_g_s, damp);
            float aw = fabsf(wet); if (aw > wpk) wpk = aw;
        }
        float o = y + fx_echo_s * e + fx_rvb_s * 1.2f * wet;
        out[i] = o;
        float a = fabsf(o); if (a > pk) pk = a;
    }
    // what has gone quiet sleeps: a voice after 16 blocks (32 ms) with no breath and no sound; the echo once nothing
    // above -100 dBFS has come out of it for a whole delay's length, the reverb for its longest line (the little that
    // is left in their lines stays there, far below hearing)
    for (int k = 0; k < 2; k++) {
        voice_t *v = &vo[k];
        float p = k ? hpk : vpk;
        bool silent = b <= 0.02f && v->chiff <= 0.001f && p < 1e-4f && v->press_s < 1e-4f;
        if (k == 1 && harm_g <= 0.002f) { silent = true; v->chiff = 0; }   // not run: its chiff would wait for the harmony
        if (v->awake && silent) { if (++v->quiet >= 16) { v->awake = false; v->quiet = 0; } }
        v->press_s = flush_denormal(v->press_s); v->dyn = flush_denormal(v->dyn); v->amp_s = flush_denormal(v->amp_s);
        v->tone_z = flush_denormal(v->tone_z); v->tone_z2 = flush_denormal(v->tone_z2);
    }
    if (run_echo) {
        bool fed = (vo[0].awake || vo[1].awake) && fx_echo_s > 0.001f;
        if (!fed && epk < 1e-5f && fabsf(echo_lp) < 1e-5f) { if (++echo_quiet * n >= (int)echo_d_s + 8 * n) { echo_awake = false; echo_quiet = 0; } } else echo_quiet = 0;
        echo_lp = flush_denormal(echo_lp);
    }
    if (run_rvb) {
        bool fed = vo[0].awake || vo[1].awake || echo_awake;
        if (!fed && wpk < 1e-5f) { if (++rvb_quiet >= 32) { rvb_awake = false; rvb_quiet = 0; } } else rvb_quiet = 0;
    }
    harm_g = flush_denormal(harm_g); vib_s = flush_denormal(vib_s); bend_s = flush_denormal(bend_s); cab_z = flush_denormal(cab_z);
    fx_echo_s = flush_denormal(fx_echo_s); fx_rvb_s = flush_denormal(fx_rvb_s); efb_s = flush_denormal(efb_s);
    bms = flush_denormal(bms); b_smooth = flush_denormal(b_smooth); b_slow = flush_denormal(b_slow);
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
    v_breath = b; v_benv = benv; v_floor = bfloor; v_over = ob; v_oenv = oenv; v_menv = menv; v_open = bopen;
    v_mclean = mclean; v_thr = thr; v_r266 = ac266 * inv; v_r200 = ac200 * inv; v_windy = windy; v_tonal = 0; v_loud = loud;
    v_lvl = ypk > v_lvl ? ypk : v_lvl * 0.97f;
    v_amp = clampf(v_lvl / 0.3f, 0.0f, 1.0f);
    v_sleep = (vo[0].awake ? 0 : 1) | (vo[1].awake ? 0 : 2) | (rvb_awake ? 0 : 4) | (echo_awake ? 0 : 8);
}

// ---------------------------------------------------------------- the UI side
#define MAXN 24
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
float huff_dbg(int k) { return k == 0 ? v_benv : k == 1 ? v_floor : k == 2 ? v_oenv : k == 3 ? voice_hz : k == 4 ? voice_m : k == 5 ? v_menv : k == 6 ? (float)v_open : k == 7 ? vo[0].dyn : k == 8 ? vo[0].str_g : k == 9 ? vo[0].press_s : ac266 / (bms + 1e-9f); }   // for tools/sim
static int   ytick;
// breath setup
static int   setup_step, setup_ms;
static float setup_quiet, setup_blow, setup_peak, setup_wind, setup_qmid;
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

// ---------------------------------------------------------------- the song and the link (as MANTRA's)
// KEY and SCALE are the flock's harmony: a change here (the menu, MIDI, a sound) goes out, and a change heard comes
// in. The link numbers six scales; HUFF's others tell the flock the shared scale their notes sit in and stay as
// they are while the flock is on that one. TEMPO is the flock's time: HUFF only plays its echo on it (ECHO BEATS).
static bool link_up, applying;
static int  peers_was, beat_flash;
static uint32_t song_fpb = 19200, song_origin, last_beat = 0xFFFFFFFFu;
int huff_peers(void) { return peers_was; }      // for tools/sim

static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }
static int link_scale_here(void) { int ls = scale_to_link[(int)pv[P_SCALE]]; return ls < 0 ? link_harm()->scale : ls; }

static void link_start(void)
{
    link_init("HUFF", 0);                                            // a melody: any rhythm board makes a better clock
    link_boot_time(tempo_fpb(pv[P_TEMPO]), 0, 4);
    int ls = scale_to_link[(int)pv[P_SCALE]];
    link_boot_harm((int)pv[P_KEY], ls < 0 ? 0 : ls, 0, 0, 0);
    link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    link_up = true;
}
static void tempo_set_here(void)
{
    uint32_t fpb = tempo_fpb(pv[P_TEMPO]);
    if (fpb == song_fpb) return;
    if (link_flags_any() & LINKF_LOOP) {                              // a loop somewhere keeps the tempo
        applying = true; param_set(P_TEMPO, (float)(int)(60.0f * (float)AUDIO_SR / (float)song_fpb + 0.5f)); applying = false;
        toast("A LOOP KEEPS THE TEMPO", 1200);
        return;
    }
    uint32_t now = audio_frames();
    float beat = (float)((now - song_origin) % song_fpb) / (float)song_fpb;   // stay on the beat's phase
    uint32_t origin = now - (uint32_t)(beat * (float)fpb);
    song_fpb = fpb; song_origin = origin; a_fpb = fpb;
    link_set_time(fpb, origin, 4, 0);
}
static void harm_set_here(void)
{
    const link_harm_t *h = link_harm();
    link_set_harm((int)pv[P_KEY], link_scale_here(), h->prog, h->hold, h->bars, h->live);
}
static void link_follow(void)
{
    if (link_synced()) {
        uint32_t o; float f; link_origin(&o, &f);
        uint32_t fpb = link_fpb();
        if (fpb) {
            bool tempo = fpb != song_fpb;
            song_fpb = fpb; song_origin = o; a_fpb = fpb;
            int bpm = (int)(60.0f * (float)AUDIO_SR / (float)fpb + 0.5f);
            if (tempo && bpm != (int)pv[P_TEMPO]) { applying = true; param_set(P_TEMPO, (float)bpm); applying = false; }
        }
    }
    link_time_news();
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        applying = true;
        if (h->key < 12) param_set(P_KEY, (float)h->key);
        if (h->scale < 6 && scale_to_link[(int)pv[P_SCALE]] != h->scale) param_set(P_SCALE, (float)scale_from_link[h->scale]);
        applying = false;
    }
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    if (np != peers_was) {
        char b[40];
        if (np > peers_was && np > 0) snprintf(b, sizeof b, "LINKED: %s", pr[np - 1].name);
        else snprintf(b, sizeof b, np ? "A BOARD LEFT" : "LINK: ALONE");
        toast(b, 1500);
        peers_was = np;
    }
}

static void changed(int p)
{
    if (p < 0 || p == P_SCALE || p == P_KEY || p == P_OCT || p == P_NOTES) { build_notes(); voice_m = -1; }
    if (p < 0 || p == P_MODEL) model_want = (int)pv[P_MODEL];
    if (!link_up || applying) return;
    if (p < 0) { tempo_set_here(); harm_set_here(); link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]); return; }
    switch (p) {
    case P_TEMPO: tempo_set_here(); break;
    case P_KEY: case P_SCALE: harm_set_here(); break;
    case P_LINK: case P_GROUP: case P_RANGE:
        link_enable(false, 1, 1);
        link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
        peers_was = 0;
        break;
    }
}

static void init(void)
{
    ms_k = onepole_s(0.03f); lp_k = onepole_hz(250.0f); mk1 = onepole_hz(500.0f); mk2 = onepole_hz(4000.0f); cab_k = onepole_hz(4500.0f);
    blk_0005 = 1.0f - powf(1.0f - 0.0005f, (float)AUDIO_BLOCK); blk_0008 = 1.0f - powf(1.0f - 0.0008f, (float)AUDIO_BLOCK);
    blk_0015 = 1.0f - powf(1.0f - 0.0015f, (float)AUDIO_BLOCK);
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
    if (p == P_PEERS) {
        link_peer_t pr[LINK_PEERS_MAX];
        int np = link_peers(pr, LINK_PEERS_MAX);
        char b[48];
        if (!link_radio()) snprintf(b, sizeof b, "LINK IS OFF");
        else if (!np) snprintf(b, sizeof b, "NO BOARD IN GROUP %d YET", (int)pv[P_GROUP]);
        else {
            int k = snprintf(b, sizeof b, "%s", pr[0].name);
            for (int i = 1; i < np && k < (int)sizeof b - 10; i++) k += snprintf(b + k, sizeof b - (size_t)k, " + %s", pr[i].name);
        }
        toast(b, 2500);
        return;
    }
    if (p != P_BSETUP) return;
    menu_leave();
    setup_step = 1; setup_ms = 0; setup_quiet = setup_blow = setup_peak = setup_wind = setup_qmid = 0; setup_n = 0; setup_msg = NULL;
    a_mute = 1;
}

static void setup_tick(void)
{
    setup_ms += 10;
    float e = v_benv;
    if (setup_step == 1) {                                         // 2 s of quiet: the room
        if (setup_ms > 400) { setup_quiet += e; setup_qmid += v_menv; setup_n++; }
        if (setup_ms >= 2200) { setup_quiet /= (float)(setup_n ? setup_n : 1); setup_qmid /= (float)(setup_n ? setup_n : 1); setup_step = 2; setup_ms = 0; setup_n = 0; }
    } else if (setup_step == 2) {                                  // 3 s of a normal breath
        if (e > setup_quiet * 2.0f + 0.003f) { setup_blow += e; setup_wind += v_menv; setup_n++; if (e > setup_peak) setup_peak = e; }
        if (setup_ms >= 3500) {
            a_mute = 0; setup_step = 0;
            if (setup_n < 60) { toast("DID NOT HEAR YOU: TRY AGAIN", 2000); return; }
            float normal = setup_blow / (float)setup_n, mid = setup_wind / (float)setup_n;
            float over = normal * normal - setup_quiet * setup_quiet;  // the breath's own low band: the room's taken off in power
            float clean = over > 0.0f ? sqrtf(over) : 0.0f;
            float over_m = mid * mid - setup_qmid * setup_qmid;         // ... and its own middle
            float clean_m = over_m > 0.0f ? sqrtf(over_m) : 0.0f;
            float thr = setup_quiet * (1.0f + 3.6f * pv[P_GATE] * 0.01f) + 0.002f;
            float r = (clean - 0.7f * thr) / 0.6f;                      // a normal breath reads about 60 % once it plays
            param_set(P_BRANGE, clampf(r, 0.01f, 4.0f));
            param_set(P_SENSE, 50);
            // the breath's low band over its middle: the gate asks for a third of that ratio (ROOM NOISE 50), so the
            // breath passes with room to spare (a hard one has far less of a ratio than a normal one) and words and
            // music, whose middle is their body, do not. Learned only
            // in a room quiet enough for the breath's own middle to show (the room's taken off in power is too rough
            // when the room moves); in a noisy one the ratio stays as it was
            bool quiet_room = setup_qmid < 0.5f * mid;
            if (quiet_room) param_set(P_BWIND, clampf(0.3f * clean / (clean_m + 0.001f), 0.3f, 20.0f));
            coach_done(L_SETUP);
            toast(quiet_room ? "BREATH SET" : "BREATH SET. NOISY ROOM: BANDS KEPT", 1800);
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
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    uint32_t bt = (c->frames - song_origin) / (song_fpb ? song_fpb : 19200u);
    if (bt != last_beat) { last_beat = bt; beat_flash = (bt % 4 == 0) ? 3 : 2; }
    else if (beat_flash > 0 && c->ticks % 4 == 0) beat_flash--;
    if (setup_step) { setup_tick(); coach_pause(); return; }
    float b = v_breath;
    breath_s += 0.3f * (b - breath_s);
    amp_s += 0.3f * (v_amp - amp_s);
    if (b > 0.05f) ui_awake();
    // the detector's numbers, four times a second while something is heard (read with sl_midi.py log; for tuning on a board)
    if (c->ticks % 25 == 0 && (v_loud || v_open || v_benv > 3.0f * v_floor + 0.003f))
        app_log("breath: low %.4f mid %.4f (over room %.4f, x%.1f) floor %.4f thr %.4f %s%s%s b %.2f",
                (double)v_benv, (double)v_menv, (double)v_mclean, (double)(v_benv / (v_mclean + 0.0005f)),
                (double)v_floor, (double)v_thr, v_loud ? "loud " : "", v_windy ? "windy " : "", v_open ? "OPEN" : "shut", (double)b);
    if (b > 0.2f) { if ((blow_ms += 10) > 1000) coach_done(L_BLOW); } else blow_ms = 0;
    if (v_over && !over_was) over_flash = 12;
    if (b > 0.75f) coach_done(L_OVERBLOW);
    over_was = v_over;
    if (cc_breath_t > 0) { cc_breath_t -= 0.01f; if (cc_breath_t <= 0) a_cc_breath = 0; }

    // ---- where the note comes from
    int src = (int)pv[P_SOURCE];
    if (src == 1 && !c->has_joy) src = 0;
    a_hum = src == 2;
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
    int note = (int)lroundf(m) + models[(int)pv[P_MODEL]].shift + (v_over ? (int)lroundf(models[(int)pv[P_MODEL]].ob_st) : 0);
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

// ---- the other pictures, one per engine, in the same 58 x 24 corner. frac: where the note sits in the ladder, 0 low
// to 1 high. Each has a mouth the air streams into and a window the notes come out of (pic_mouth / pic_window).
static void flute(float frac, float glow)
{
    oled_rframe(6, 29, 52, 7, 3);
    oled_rect(10, 29, 3, 2, true);                                // the embouchure hole
    if (glow > 0.05f) { int g = 1 + (int)(glow * 3.0f); oled_hline_pat(8, 27 - g, 7, 0x55, true); }
    int open = (int)lroundf(frac * 6.0f);                         // the holes open from the far end as the note climbs
    for (int k = 0; k < 6; k++) {
        int x = 26 + 5 * k;
        if (5 - k < open) { oled_pixel(x - 1, 32, true); oled_pixel(x + 1, 32, true); oled_pixel(x, 31, true); oled_pixel(x, 33, true); }
        else oled_rect(x - 1, 31, 3, 3, true);
    }
}
static void panpipes(float frac)
{
    static const int8_t top[5] = { 22, 26, 29, 32, 34 };
    int played = clampi((int)(frac * 4.99f), 0, 4);
    for (int k = 0; k < 5; k++) {
        int x = 14 + 9 * k;
        oled_frame(x, top[k], 6, 45 - top[k]);
        if (k == played) oled_rect(x + 1, top[k] + 1, 4, 3, true);
    }
    oled_rect(12, 40, 49, 1, true); oled_rect(12, 42, 49, 1, true);   // the binding
}
static void clarinet(float frac)
{
    oled_line(4, 33, 10, 30, true); oled_line(4, 33, 10, 36, true);    // the mouthpiece
    oled_frame(10, 30, 7, 7); oled_frame(16, 31, 33, 5);                // the barrel, the body
    oled_line(48, 31, 58, 27, true); oled_line(48, 35, 58, 39, true); oled_line(58, 27, 58, 39, true);   // the bell
    int open = (int)lroundf(frac * 7.0f);
    for (int k = 0; k < 7; k++) {
        int x = 20 + 4 * k;
        if (6 - k < open) oled_pixel(x, 33, true); else oled_rect(x - 1, 32, 3, 3, true);
    }
    oled_rect(22, 29, 3, 1, true); oled_rect(30, 29, 3, 1, true); oled_rect(38, 29, 3, 1, true);   // keys
}
static void trumpet(float frac, float amp, bool over)
{
    oled_rect(4, 31, 3, 5, true); oled_rect(7, 32, 3, 3, true);         // the mouthpiece
    oled_rect(10, 32, 31, 2, true);                                     // the lead pipe
    oled_rframe(14, 33, 27, 8, 3);                                      // the tuning slide below
    int combo = clampi((int)(frac * 7.99f), 0, 7);                      // the valves: pressed ones sit lower
    for (int k = 0; k < 3; k++) {
        int x = 20 + 6 * k, down = (combo >> k) & 1 ? 2 : 0;
        oled_rect(x, 27 + down, 3, 5 - down, true); oled_rect(x - 1, 25 + down, 5, 2, true);
    }
    oled_line(41, 31, 58, 23, true); oled_line(41, 35, 58, 43, true); oled_line(58, 23, 58, 43, true);   // the bell
    if (over) { oled_line(60, 26, 62, 24, true); oled_line(60, 40, 62, 42, true); oled_rect(60, 33, 3, 1, true); }
    else if (amp > 0.3f) { oled_pixel(60, 29, true); oled_pixel(60, 37, true); oled_pixel(61, 33, true); }
}
static void didgeridoo(float amp, uint32_t frame)
{
    oled_line(4, 40, 56, 26, true); oled_line(4, 43, 56, 37, true); oled_line(56, 26, 56, 37, true);
    oled_line(4, 40, 4, 43, true);
    for (int k = 1; k < 8; k++) { int x = 4 + 7 * k, yt = 40 - (14 * (x - 4)) / 52, yb = 43 - (6 * (x - 4)) / 52; oled_pixel(x, yt + 2, true); oled_pixel(x, yb - 2, true); if (k & 1) oled_pixel(x, (yt + yb) / 2, true); }
    if (amp > 0.1f) { int r = 2 + (int)(((frame / 2) % 4)); oled_circle(60, 31, r, true); }   // the drone, rippling out
}
static void guitar(float frac, float amp, uint32_t frame)
{
    oled_disc(13, 29, 5, true); oled_disc(15, 38, 7, true);               // the body
    oled_rect(10, 33, 7, 1, false);                                       // the pickup
    oled_frame(18, 31, 41, 7);                                            // the neck
    for (int x = 23; x < 58; x += 5) oled_line(x, 32, x, 36, true);       // frets
    int fx = 55 - (int)lroundf(frac * 33.0f);                             // the fretted spot: high notes near the body
    for (int k = 0; k < 3; k++) {                                         // three strings, shaking with the sound
        int y0 = 32 + 2 * k;
        for (int x = 19; x < 58; x++) {
            float w = amp > 0.03f ? amp * 1.6f * sin_turns((float)x * 0.06f + (float)frame * 0.37f + (float)k * 0.3f) : 0.0f;
            int y = y0 + (int)lroundf(w);
            if (y >= 32 && y <= 36) oled_pixel(x, y, x < fx || (x - 19) % 2 == 0 || k == 1);
        }
    }
    oled_rect(fx - 1, 33, 3, 3, true);
    oled_rect(58, 30, 3, 9, true);                                        // the head
}
static void keyboard(float frac, float b, uint32_t frame)
{
    oled_rrect(3, 21, 6, 3, 1, true); oled_line(9, 22, 14, 22, true); oled_line(14, 22, 14, 31, true);   // the breath tube
    for (int k = 0; k < 8; k++) oled_frame(8 + 6 * k, 32, 7, 13);
    static const uint8_t black[8] = { 1, 1, 0, 1, 1, 1, 0, 0 };
    for (int k = 0; k < 8; k++) if (black[k]) oled_rect(12 + 6 * k, 32, 4, 7, true);
    int key = clampi((int)(frac * 7.99f), 0, 7);
    oled_rect(9 + 6 * key, 40, 5, 4, true);                               // the pressed key
    for (int x = 16; x < 58; x++) {                                       // the wave above: the filter opens with the breath
        float y = 26.0f + 3.5f * b * sin_turns((float)x * (0.04f + 0.08f * b) + (float)frame * 0.11f);
        oled_pixel(x, (int)lroundf(y), true);
    }
}
static void face(float b, float amp, bool choir, uint32_t frame)
{
    if (choir) { oled_circle(42, 31, 8, true); oled_pixel(39, 29, true); oled_pixel(45, 29, true); oled_rrect(39, 34, 7, 1 + (int)(b * 4.0f), 1, true); }
    oled_circle(22, 32, 10, true);
    bool blink = frame % 90 < 3;
    if (blink) { oled_rect(17, 29, 3, 1, true); oled_rect(25, 29, 3, 1, true); } else { oled_rect(18, 28, 2, 2, true); oled_rect(26, 28, 2, 2, true); }
    oled_line(14, 25, 18, 22, true); oled_line(22, 22, 24, 21, true); oled_line(27, 22, 30, 25, true);   // hair
    int mh = 1 + (int)(b * 6.0f);
    oled_rrect(17, 37 - mh / 2, 11, mh, 2, true);                         // the mouth, open with the breath
    if (amp > 0.3f) oled_rect(19, 37 - mh / 2 + 1, 7, 1, false);          // teeth
}
// where each picture's mouth is (the air streams there) and its window (the notes come out there)
static void pic_mouth(int eng, int model, int *x, int *y)
{
    switch (eng) {
    case E_JET:    if (model == M_PANPIPE) { *x = 20; *y = 24; } else { *x = 10; *y = 27; } break;
    case E_REED:   *x = 4; *y = 33; break;
    case E_BRASS:  if (model == M_DIDGE) { *x = 4; *y = 41; } else { *x = 4; *y = 33; } break;
    case E_STRING: *x = 26; *y = 34; break;
    case E_SYNTH:  *x = 3; *y = 22; break;
    case E_VOICE:  *x = 12; *y = 33; break;
    default:       *x = 4; *y = OC_CY + 1; break;
    }
}
static void pic_window(int eng, int model, int *x, int *y)
{
    switch (eng) {
    case E_JET:    if (model == M_PANPIPE) { *x = 44; *y = 24; } else { *x = 56; *y = 26; } break;
    case E_REED:   *x = 56; *y = 24; break;
    case E_BRASS:  *x = 56; *y = 21; break;
    case E_STRING: *x = 10; *y = 22; break;
    case E_SYNTH:  *x = 50; *y = 21; break;
    case E_VOICE:  *x = 30; *y = 32; break;
    default:       *x = 16; *y = OC_CY - 12; break;
    }
}

static const char *const note_spr[6] = { "...##", "...#.#", "...#..", ".###..", "####..", ".##..." };
static const char *const note2_spr[6] = { ".#####", ".#...#", ".#...#", "##..##", "##..##", "......" };

static float lad_w0;                                           // the ladder's window: the first rung shown (smoothed)
static void ladder(int x, int y0, int y1)
{
    int n = nnotes;
    float step = n > 1 ? (float)(y1 - y0) / (float)(n - 1) : 0;
    int vis = n;
    if (step < 2.0f) {                                         // too many rungs for the rows: a window slides with the note
        step = 2.0f; vis = (y1 - y0) / 2 + 1;
        float want = clampf((float)idx - (float)vis * 0.5f + 0.5f, 0.0f, (float)(n - vis));
        lad_w0 += 0.3f * (want - lad_w0);
    } else lad_w0 = 0;
    int w0 = (int)lroundf(lad_w0);
    for (int k = w0; k < n && k < w0 + vis; k++) {
        int y = y1 - (int)lroundf((float)(k - w0) * step);
        if (k == idx) oled_rrect(x, y - 1, 9, 3, 1, true);
        else if (k == root_i) oled_rect(x + 1, y, 7, 1, true);
        else oled_rect(x + 2, y, 5, 1, true);
    }
    if (w0 > 0) { oled_pixel(x + 4, y1 + 2, true); oled_pixel(x + 3, y1 + 3, true); oled_pixel(x + 5, y1 + 3, true); }           // more below
    if (w0 + vis < n) { oled_pixel(x + 4, y0 - 3, true); oled_pixel(x + 3, y0 - 2, true); oled_pixel(x + 5, y0 - 2, true); }     // more above
    int by = y1 - (int)lroundf((pos_s - lad_w0) * step);          // where the tilt (or the stick) is: a pointer
    by = clampi(by, y0 - 1, y1 + 1);
    oled_line(x - 4, by - 2, x - 2, by, true); oled_line(x - 4, by + 2, x - 2, by, true); oled_pixel(x - 4, by - 1, true); oled_pixel(x - 4, by + 1, true); oled_pixel(x - 3, by, true);
}

static void scene(const view_t *v)
{
    float b = breath_s, amp = amp_s;
    int m = (int)pv[P_MODEL], eng = models[m].eng, mx, my, wx, wy;
    pic_mouth(eng, m, &mx, &my); pic_window(eng, m, &wx, &wy);
    // air into the mouthpiece: more and faster with the breath
    int emit = b > 0.03f ? 1 + (int)(b * 3.0f) : 0;
    for (int i = 0; i < emit; i++)
        parts_emit(&air, (float)mx - 10.0f, (float)my + 1.5f * rng_bi(&pseed), 50.0f + 150.0f * b, 0, 0.3f, b > 0.55f ? PART_STREAK : PART_DOT, 1);
    parts_step(&air, 0.04f, 0, 0);
    for (int i = 0; i < air.n; i++) if (air.p[i].x >= (float)mx) air.p[i].life = 0;
    parts_draw(&air);
    // notes out of the window, more with more sound, swaying with the vibrato
    static float acc;
    acc += amp * 0.7f;
    if (acc > 1.0f && notes_p.n < 8) {
        acc = 0;
        parts_emit(&notes_p, (float)wx, (float)wy, 10.0f + 10.0f * rng_uni(&pseed), -16.0f - 10.0f * amp, 3.0f, PART_SQUARE, (uint8_t)(rng_next(&pseed) & 1));
    }
    parts_step(&notes_p, 0.04f, 0, 0);
    note_wob += 0.04f * pv[P_VIBR] * 6.2831853f;
    if (note_wob > 6.2831853f) note_wob -= 6.2831853f;
    for (int i = 0; i < notes_p.n; i++) {
        particle_t *q = &notes_p.p[i];
        if (q->y < 16.0f || q->x > 60.0f) { q->life = 0; continue; }
        int wx = (int)lroundf(sinf(note_wob + q->y * 0.25f) * a_vib * 4.0f);
        ui_sprite((int)q->x + wx, (int)q->y, q->size ? note2_spr : note_spr, 6, false, true);
    }
    // the instrument, its holes (valves, frets, keys) for this note
    int lo = note_midi[0], hi = note_midi[nnotes - 1];
    float frac = hi > lo ? clampf((midi_now - (float)lo) / (float)(hi - lo), 0.0f, 1.0f) : 0.0f;
    switch (eng) {
    case E_JET:    if (m == M_PANPIPE) panpipes(frac); else flute(frac, amp); break;
    case E_REED:   clarinet(frac); break;
    case E_BRASS:  if (m == M_DIDGE) didgeridoo(amp, v->frame); else trumpet(frac, amp, v_over); break;
    case E_STRING: guitar(frac, amp, v->frame); break;
    case E_SYNTH:  keyboard(frac, b, v->frame); break;
    case E_VOICE:  face(b, amp, a_harm_on, v->frame); break;
    default:       ocarina(clampi((int)lroundf(frac * 12.0f), 0, 12), amp); break;
    }
    if (over_flash > 0) { int r = 13 - over_flash; oled_circle(wx > 40 ? 54 : 19, wx > 40 ? 33 : OC_CY, r, true); over_flash--; }
    // the note, big, with the bend
    char nm[8];
    int shown = (int)lroundf(midi_now) + models[m].shift + (v_over ? (int)lroundf(models[m].ob_st) : 0);
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
    ladder(117, 19, 43);
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

// ---------------------------------------------------------------- the header: the core's, with the link's glyph
// (as BAND's and SING's: blinking while alone, two boards that fill on the beat once linked and in step)
static void link_glyph(int x, int y, const view_t *v)
{
    if (!link_radio()) return;
    int bf = beat_flash;
    if (!peers_was) { if ((v->frame / 12) % 2 == 0) { oled_pixel(x + 2, y + 3, true); ui_arc(x + 2, y + 3, 3, -60, 60); } return; }
    bool on = bf > 1 || (bf > 0 && (v->frame & 1));
    oled_frame(x, y + 1, 3, 6); oled_frame(x + 4, y + 1, 3, 6);
    if (link_synced() && link_error_ms() < 2.0f) { if (on) { oled_rect(x, y + 1, 3, 6, true); oled_rect(x + 4, y + 1, 3, 6, true); } }
    else if ((v->frame / 3) % 2 == 0) oled_pixel(x + 3, y + 3, true);
}

static void header(const view_t *v)
{
    const front_status_t *st = front_status();
    char lb[24]; int x_end;
    loop_state_t ls = looper_state();
    float l = looper_len_s();
    bool filled = ls == LOOP_RECORDING || ls == LOOP_DUBBING;
    const char *left = NULL;
    switch (ls) {
    case LOOP_RECORDING: snprintf(lb, sizeof lb, "REC %.1fs", (double)l); left = lb; break;
    case LOOP_DUBBING:   snprintf(lb, sizeof lb, "ADD %.1fs", (double)l); left = lb; break;
    case LOOP_PLAYING:   snprintf(lb, sizeof lb, "LOOP %.1fs", (double)l); left = lb; break;
    case LOOP_STOPPED:   snprintf(lb, sizeof lb, "STOP %.1fs", (double)l); left = lb; break;
    default: break;
    }
    if (left) x_end = oled_pill(F_SMALL, 0, 2, left, filled);
    else x_end = oled_ftext_ex(F_MED, 1, 3, INST.name, 1);
    int rx = OLED_W - 22;                                           // the meter
    ui_meter(rx, 3, 22, 9, st->out_level, st->clip);
    rx -= 3;
    if (link_radio()) { link_glyph(rx - 7, 4, v); rx -= 10; }
    if (st->frozen) {                                               // the padlock
        int x = rx - 6, y = 3;
        oled_rframe(x, y + 3, 6, 5, 1); oled_pixel(x + 2, y + 5, true); oled_pixel(x + 3, y + 5, true);
        oled_line(x + 1, y + 3, x + 1, y + 1, true); oled_line(x + 4, y + 3, x + 4, y + 1, true);
        oled_line(x + 2, y, x + 3, y, true);
        rx -= 9;
    }
    char nm[28];
    snprintf(nm, sizeof nm, "%s%s", st->sound ? st->sound : "", st->edited ? "*" : "");
    int room = rx - (x_end + 5);
    if (room > 8) {
        int w = oled_ftext_w(F_SMALL, nm, 1);
        if (w <= room) oled_ftext_ex(F_SMALL, rx - w, 5, nm, 1);
        else ui_text_fit(F_SMALL, x_end + 5, 5, room, nm);
    }
}

static void draw(const view_t *v)
{
    if (setup_step) { draw_setup(v); return; }
    header(v);
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
    .params = params, .nparams = P_N, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = N_PRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .looper = true, .midi_notes = true,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver,
};

// DWELL: a drone you live inside (inst_dwell.h says what it is). This file is the UI side: the params and sounds, the
// tilt's walk on the lattice of ratios and the trail of tones it leaves (only while the box is calm), DRIFT (every
// change by a swell or a glide), the knob from PURE to DENSE, ENERGY (movement pumps it in, stillness lets it fall: the
// crescendo) and SHAKE, the knock (a strum, or a new fundamental fading in on the other layer), the stick (the lead's
// octave, lead against chord), REC (hold: your tone, sampled; tap: the drone rests), ROOM PASS's control, the radios'
// events as interference, the link (tempo, key; FOLLOW a linked chord; SPREAD one tone per board), MIDI and the LED.
#include "core.h"
#include "dsp.h"
#include "link.h"
#include "inst_dwell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "DRONE", "ENERGY", "TAP", "LATTICE", "CLUSTER", "SOURCE", "DIRT", "ECHO", "SPACE", "RADIO", "ROOM", "SONG" };
enum { G_DRONE, G_ENERGY, G_TAP, G_LAT, G_CLUS, G_SRC, G_DIRT, G_ECHO, G_SPACE, G_RADIO, G_ROOM, G_SONG, G_N };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const move_l[] = { "SWELL", "GLIDE" };
static const char *const limit_l[] = { "7 (YOUNG)", "5", "3" };
static const char *const source_l[NSRC] = { "SINE", "REED", "BOW", "TANPURA", "SHRUTI", "GURDY", "WAVE", "ROOM" };
static const char *const bsync_l[] = { "OFF", "TEMPO" };
static const char *const knock_l[] = { "OFF", "STRUM", "NEXT ROOT" };
static const char *const next_l[NNX] = { "LEAN", "3/2", "4/3", "7/4", "8/7", "9/8" };
static const char *const esync_l[] = { "OFF", "1 BEAT", "3/4", "1/2", "1/4" };
static const float esync_beats[] = { 0.0f, 1.0f, 0.75f, 0.5f, 0.25f };
static const char *const radio_l[] = { "OFF", "WI-FI", "BLUETOOTH", "BOTH" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const scale_l[] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };   // SING's, as the link numbers them
static const int8_t scales[][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                    { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
#define NSCALES 6
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };
#define SYS (PF_SYS | PF_NOCC)

static const param_t params[P_N] = {
    [P_DENSE]   = { "DENSE", "dens", G_DRONE, PK_NUM, PF_INT, 0, 100, 2, 0, "%", NULL, "0 PURE SINES, 100 A WALL" },
    [P_VOICES]  = { "VOICES", "voic", G_DRONE, PK_NUM, PF_INT, 1, 6, 1, 4, "", NULL, "TONES THAT STAY WITH YOU" },
    [P_ROOT]    = { "ROOT", "root", G_DRONE, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "1/1 AN OCTAVE UNDER ALL" },
    [P_DRIFT]   = { "DRIFT", "drft", G_DRONE, PK_NUM, PF_LOG, 0.5f, 300.0f, 1.15f, 20.0f, "s", NULL, "HOW LONG A CHANGE TAKES" },
    [P_MOVE]    = { "MOVE", "move", G_DRONE, PK_ENUM, 0, 0, 1, 1, MOVE_SWELL, "", move_l, "NEW TONES SWELL, OR SLIDE" },
    [P_LEVEL]   = { "LEVEL", "levl", G_DRONE, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD" },
    [P_CLIMB]   = { "CLIMB", "clmb", G_ENERGY, PK_NUM, PF_LOG, 1.0f, 60.0f, 1.15f, 8.0f, "s", NULL, "MOVING THIS LONG: FULL" },
    [P_FALL]    = { "FALL", "fall", G_ENERGY, PK_NUM, PF_LOG, 2.0f, 120.0f, 1.15f, 20.0f, "s", NULL, "STILL THIS LONG: BACK DOWN" },
    [P_REST]    = { "REST LEVEL", "rstl", G_ENERGY, PK_NUM, PF_INT | PF_SIGNED, -40, 0, 1, -14, "dB", NULL, "HOW QUIET IT IS UNMOVED" },
    [P_TODENSE] = { "TO DENSE", "tdns", G_ENERGY, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "MOVING THICKENS IT" },
    [P_TODRIVE] = { "TO DIRT", "tdrv", G_ENERGY, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "MOVING DRIVES IT" },
    [P_HAND]    = { "HAND", "hand", G_ENERGY, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "100: IT SOUNDS AS YOU MOVE" },
    [P_SHAKE]   = { "SHAKE", "shak", G_ENERGY, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "WHAT A SHAKE THROWS IN" },
    [P_KNOCK]   = { "KNOCK", "knck", G_TAP, PK_ENUM, 0, 0, 2, 1, KN_NEXT, "", knock_l, "A STRUM, OR A NEW ROOT" },
    [P_NEXT]    = { "NEXT ROOT", "next", G_TAP, PK_ENUM, 0, 0, NNX - 1, 1, NX_LEAD, "", next_l, "THE STEP TO THE NEW ROOT" },
    [P_FADE]    = { "FADE", "fade", G_TAP, PK_NUM, PF_LOG, 0.5f, 60.0f, 1.15f, 8.0f, "s", NULL, "THE NEW ROOT FADES IN" },
    [P_STRUM]   = { "STRUM", "strm", G_TAP, PK_NUM, PF_LOG, 0.2f, 8.0f, 1.15f, 2.0f, "s", NULL, "HOW LONG A STRUM RINGS" },
    [P_SENSE]   = { "SENSE", "sens", G_TAP, PK_NUM, PF_INT | PF_SYS, 0, 100, 5, 50, "%", NULL, "HOW EASILY A KNOCK COUNTS" },
    [P_HOME]    = { "HOME", "home", G_TAP, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "FADE BACK TO THE KEY" },
    [P_LIMIT]   = { "LIMIT", "limt", G_LAT, PK_ENUM, 0, 0, 2, 1, LIM_7, "", limit_l, "THE PRIMES THE RATIOS USE" },
    [P_OCTAVE]  = { "OCTAVE", "octv", G_LAT, PK_NUM, PF_INT, 1, 4, 1, 3, "", NULL, "WHERE 1/1 SITS" },
    [P_TSTEP]   = { "TILT STEP", "tstp", G_LAT, PK_NUM, PF_INT | PF_SYS, 6, 20, 1, 10, "", NULL, "DEGREES PER CELL" },
    [P_DWELLMS] = { "DWELL", "dwel", G_LAT, PK_NUM, PF_LOG | PF_SYS, 0.1f, 2.0f, 1.2f, 0.4f, "s", NULL, "STAY ON A CELL THIS LONG" },
    [P_COPIES]  = { "COPIES", "copy", G_CLUS, PK_NUM, PF_INT, 1, 8, 1, 6, "", NULL, "COPIES PER TONE AT DENSE" },
    [P_BEAT]    = { "BEAT", "beat", G_CLUS, PK_NUM, PF_LOG, 0.05f, 8.0f, 1.12f, 0.5f, "Hz", NULL, "HOW FAST THE COPIES BEAT" },
    [P_BSYNC]   = { "BEAT SYNC", "bsyn", G_CLUS, PK_ENUM, 0, 0, 1, 1, 0, "", bsync_l, "BEATS AT THE FLOCK'S TEMPO" },
    [P_BRIGHT]  = { "BRIGHT", "brit", G_CLUS, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "HARMONICS" },
    [P_DIFF]    = { "DIFF TONE", "diff", G_CLUS, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "A SUB AT THE DIFFERENCE" },
    [P_SOURCE]  = { "SOURCE", "srce", G_SRC, PK_ENUM, 0, 0, NSRC - 1, 1, SRC_SINE, "", source_l, "WHAT EACH TONE IS" },
    [P_PLUCK]   = { "PLUCK", "pluk", G_SRC, PK_NUM, PF_LOG, 0.2f, 4.0f, 1.12f, 0.8f, "s", NULL, "TANPURA: PLUCKS THIS OFTEN" },
    [P_JAWARI]  = { "JAWARI", "jawa", G_SRC, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "TANPURA: THE BRIDGE'S BUZZ" },
    [P_WHEEL]   = { "WHEEL", "whel", G_SRC, PK_NUM, PF_LOG, 0.3f, 4.0f, 1.12f, 1.2f, "Hz", NULL, "GURDY: THE WHEEL'S TURN" },
    [P_BREATH]  = { "BREATH", "brth", G_SRC, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "SHRUTI: THE BELLOWS" },
    [P_BODY]    = { "BODY", "body", G_SRC, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "BOW, REED: ITS WOODEN BOX" },
    [P_DRIVE]   = { "DRIVE", "drve", G_DIRT, PK_NUM, PF_INT, 0, 100, 5, 10, "%", NULL, "OVERDRIVE: MORE OVERTONES" },
    [P_TONE]    = { "TONE", "tone", G_DIRT, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "DARK TO BRIGHT AFTER IT" },
    [P_DMIX]    = { "DIRT MIX", "dmix", G_DIRT, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "CLEAN TO DRIVEN" },
    [P_ECHO]    = { "ECHO", "echo", G_ECHO, PK_NUM, PF_LOG, 0.02f, 1.6f, 1.12f, 0.45f, "s", NULL, "THE ECHO'S TIME" },
    [P_ESYNC]   = { "ECHO SYNC", "esyn", G_ECHO, PK_ENUM, 0, 0, 4, 1, 0, "", esync_l, "ON THE FLOCK'S BEAT" },
    [P_EFB]     = { "FEEDBACK", "efb ", G_ECHO, PK_NUM, PF_INT, 0, 95, 5, 35, "%", NULL, "HOW OFTEN IT COMES BACK" },
    [P_EMIX]    = { "ECHO MIX", "emix", G_ECHO, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "DRY TO ALL ECHO" },
    [P_SPACE]   = { "SPACE", "spce", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "HOW MUCH HALL" },
    [P_TAIL]    = { "TAIL", "tail", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "A SMALL ROOM TO ENDLESS" },
    [P_RADIO]   = { "RADIO", "radi", G_RADIO, PK_ENUM, 0, 0, 3, 1, RAD_OFF, "", radio_l, "THE AIR'S TRAFFIC AS NOISE" },
    [P_ZAP]     = { "INTERFERE", "zap ", G_RADIO, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "HOW HARD THE AIR CUTS IN" },
    [P_CHAN]    = { "CHANNEL", "chan", G_RADIO, PK_NUM, PF_INT | PF_SYS, 1, 13, 1, 1, "", NULL, "WI-FI CHANNEL, LINK OFF" },
    [P_PASSES]  = { "PASSES", "pass", G_ROOM, PK_NUM, PF_INT, 1, 30, 1, 12, "", NULL, "TIMES THROUGH THE ROOM" },
    [P_PASSLEN] = { "PASS LEN", "plen", G_ROOM, PK_NUM, PF_INT, 2, ROOM_MAX_S, 1, 4, "s", NULL, "SECONDS PER PASS" },
    [P_PASS]    = { "ROOM PASS", "rpas", G_ROOM, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "PLAY, RECORD, AGAIN, AGAIN" },
    [P_TEMPO]   = { "TEMPO", "tmpo", G_SONG, PK_NUM, PF_INT | PF_SYS, 50, 200, 1, 100, "", NULL, "SHARED WITH LINKED BOARDS" },
    [P_KEY]     = { "KEY", "skey", G_SONG, PK_ENUM, PF_SYS, 0, 11, 1, 0, "", key_l, "1/1 IS THIS NOTE" },
    [P_SCALE]   = { "SCALE", "sscl", G_SONG, PK_ENUM, PF_SYS, 0, NSCALES - 1, 1, 0, "", scale_l, "FOR CHORDS FROM THE LINK" },
    [P_FOLLOW]  = { "FOLLOW", "folw", G_SONG, PK_ENUM, PF_SYS, 0, 1, 1, 1, "", off_on, "A LINKED CHORD MOVES 1/1" },
    [P_SPREAD]  = { "SPREAD", "sprd", G_SONG, PK_ENUM, PF_SYS, 0, 1, 1, 0, "", off_on, "EACH BOARD ONE TONE" },
    [P_LINK]    = { "LINK", "nlnk", G_SONG, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]   = { "GROUP", "ngrp", G_SONG, PK_NUM, SYS | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]   = { "RANGE", "npow", G_SONG, PK_ENUM, SYS, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_PEERS]   = { "WHO IS THERE", "nwho", G_SONG, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
};

// ---------------------------------------------------------------- sounds
static const preset_t presets[] = {
    { "DREAM HOUSE",    "dens=0 voic=4 root=1 limt=0 srce=0 drft=20 spce=20 tail=50 rstl=-8 tdns=40 tdrv=25 emix=10" },
    { "DRUMM",          "srce=2 dens=30 copy=8 beat=0.2 brit=70 voic=3 root=1 rstl=-30 clmb=20 fall=45 tdns=100 tdrv=100 drve=30 tone=60 echo=0.7 efb=60 emix=30 spce=50 tail=80 body=60" },
    { "MIASMA",         "srce=5 dens=60 voic=4 rstl=-20 clmb=6 fall=15 drve=70 tdrv=100 tone=90 shak=100 echo=0.12 efb=80 emix=40 spce=30" },
    { "IMPERIAL",       "srce=1 dens=15 voic=5 rstl=-24 clmb=30 fall=60 tdrv=30 drve=0 tone=30 echo=1.2 efb=50 emix=25 spce=70 tail=90" },
    { "BOWED BY HAND",  "srce=2 hand=100 clmb=3 fall=6 rstl=-6 dens=40 tdrv=40 voic=3 body=70" },
    { "NIBLOCK",        "dens=80 copy=8 beat=0.3 srce=2 brit=60 voic=3 root=0 spce=30 levl=85 body=60 rstl=-10 drve=15" },
    { "ADNOS",          "dens=30 srce=1 move=1 drft=90 voic=4 beat=0.12 brit=30 spce=40 tail=60 rstl=-12 fall=40 tdrv=10 emix=10 fade=30" },
    { "IN A ROOM",      "srce=7 pass=12 plen=4 voic=2 dens=20 spce=10 tdrv=40" },
    { "TANPURA",        "srce=3 voic=4 root=1 pluk=0.8 jawa=55 dens=20 spce=30 tail=55 tdrv=30" },
    { "SHRUTI BOX",     "srce=4 voic=3 brth=50 dens=25 beat=0.25 spce=35" },
    { "HURDY GURDY",    "srce=5 voic=3 whel=1.2 dens=40 body=60 knck=1 strm=1 drve=20" },
    { "STRUMMING",      "srce=0 brit=60 strm=5 knck=1 voic=6 dens=35 spce=50 tail=70" },
    { "CONRAD",         "srce=2 limt=1 dens=55 copy=4 beat=0.8 brit=70 voic=3 root=1 body=70 drve=25" },
    { "DAVACHI",        "srce=1 move=1 drft=45 voic=6 dens=15 brit=35 body=40 spce=45 tail=65 tdrv=15" },
    { "DEEP LISTENING", "srce=1 dens=20 beat=0.1 spce=70 tail=90 voic=5 brth=30 tdrv=10 emix=20 echo=0.9" },
    { "INTERFERENCE",   "radi=3 zap=80 srce=0 dens=20 drve=40 tdrv=60 echo=0.3 efb=50 emix=25" },
    { "TEMPO BEATS",    "bsyn=1 dens=60 copy=4 srce=0 brit=50 voic=3 esyn=2 emix=25" },
    { "YOUR TONE",      "srce=6 dens=50 copy=6 beat=0.4 voic=3" },
    { "TARTINI",        "diff=100 voic=2 root=0 dens=0 srce=0 spce=10 tdrv=0" },
    { "PYTHAGORAS",     "limt=2 srce=0 voic=5 dens=10 spce=30 next=1" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_ROLL = LEARN_INST, L_KNOCK, L_REST, L_ROOM, L_ENERGY, L_SHAKE };
static const hint_t hints[] = {
    { "SWING IT ABOUT: IT GROWS", L_ENERGY },
    { "TIP IT AWAY FROM YOU: UP THE FIFTHS", LEARN_TILT },
    { "KNOCK ONCE: A NEW ROOT FADES IN", L_KNOCK },
    { "SHAKE IT: DIRT AND ECHO", L_SHAKE },
    { "ROLL IT RIGHT: UP THE SEVENTHS", L_ROLL },
    { "TURN THE KNOB: PURE TO DENSE", LEARN_TURN },
    { "HOLD REC AND SING ONE NOTE", LEARN_REC },
    { "TAP REC: IT RESTS. TAP: IT RETURNS", L_REST },
    { "ROOM > ROOM PASS: THE ROOM SINGS", L_ROOM },
    { "HOLD THE KNOB: THE TILT STAYS PUT", LEARN_HOLD },
    { "PRESS THE KNOB: SOUNDS, SETTINGS", LEARN_MENU },
};

// ---------------------------------------------------------------- state
dw_ui_t D;
static uint32_t now;
static bool link_up, applying, booted;
static float cur_b, cur_c;                 // the cursor on the lattice, in cells
static int   lead_b, lead_c;               // the cell the lead holds
static int   cand_b, cand_c; static float cand_s;   // a cell the cursor has moved to, not yet dwelt on
static float fund_hz = 65.4f;              // the key's 1/1
static float follow_ratio = 1.0f;          // FOLLOW: a linked chord's root as a ratio on the key
static int   oct_shift;                    // the stick: the lead's octave
static float balance;                      // the stick: -1 chord .. 1 lead
static float rest;                         // REC tapped
static int   midi_note_out = -1;
static float led_breath;
static struct { bool recording, started, analysing, ignore_release; int ms; } R;
static struct { bool checked0; } RM;
// ENERGY and SHAKE
static float energy, motion, mfast, shake_s;
static vec3_t acc_prev; static bool acc_have;
// the knock: a jolt on a still box, confirmed when nothing follows it for 120 ms (a shake is jolt after jolt)
static int   knock_wait = -1, knock_cool = 100; static float knock_amp;

typedef struct {
    bool  on, root, lead;
    int8_t b, c;
    float age;                 // seconds since it took its cell
    float lvl;                 // its level target before SPREAD and the balance
    bool  pend;                // SWELL: fading out, then it takes (pb, pc)
    int8_t pb, pc;
    float pend_s;
} slot_t;
// two layers: CL sounds, the other is silent or fading out after a new root came in
static slot_t sl[2][NVL];
static int    CL;
static int    root_b[2], root_c[2];        // each layer's fundamental, as a cell from the key's 1/1
static bool   next_queued;

static int my_rank;

static float hz_midi_f(float hz) { return 69.0f + 12.0f * log2f(hz / 440.0f); }
static int   eidx(int layer, int i) { return layer * NVL + i; }

// a layer's fundamental against the key's 1/1: 3^b p^c, brought within half an octave of it
static float root_ratio(int layer)
{
    int limit = (int)pv[P_LIMIT], p = limit == LIM_5 ? 5 : limit == LIM_3 ? 3 : 7;
    int b = root_b[layer], c = limit == LIM_3 ? 0 : root_c[layer];
    float r = 1.0f;
    for (int i = 0; i < b; i++) r *= 3.0f;
    for (int i = 0; i < -b; i++) r /= 3.0f;
    for (int i = 0; i < c; i++) r *= (float)p;
    for (int i = 0; i < -c; i++) r /= (float)p;
    while (r >= 1.41421356f) r *= 0.5f;
    while (r < 0.70710678f) r *= 2.0f;
    return r;
}
static bool fading(void) { return fabsf(eng_layer_x - (float)CL) > 0.0005f; }
// its name: "" at home, the ratio while it is on the lattice's window, else its cents above the key
static void root_label(int layer, char *b, int n)
{
    float rr = root_ratio(layer);
    if (!root_b[layer] && !root_c[layer]) b[0] = 0;
    else if (abs(root_b[layer]) <= LAT_B && abs(root_c[layer]) <= LAT_C) lat_name((int)pv[P_LIMIT], root_b[layer], root_c[layer], b, n);
    else snprintf(b, (size_t)n, "%+dC", (int)lroundf(1200.0f * log2f(rr)));
}

// ---------------------------------------------------------------- the slots: what each voice plays
static float slot_hz(int layer, const slot_t *s)
{
    float r = lat_ratio((int)pv[P_LIMIT], s->b, s->c) * fund_hz * follow_ratio * root_ratio(layer);
    if (s->root) return r * 0.5f;
    if (s->lead) r *= oct_shift > 0 ? 2.0f : oct_shift < 0 ? 0.5f : 1.0f;
    return r;
}

static int slot_free(void)
{
    for (int i = 0; i < NVL; i++) if (!sl[CL][i].on) return i;
    return -1;
}
static int slot_oldest_trail(void)
{
    int k = -1; float age = -1.0f;
    for (int i = 0; i < NVL; i++) { slot_t *s = &sl[CL][i]; if (s->on && !s->root && !s->lead && s->age > age) { age = s->age; k = i; } }
    return k;
}
static int slot_lead(void) { for (int i = 0; i < NVL; i++) if (sl[CL][i].on && sl[CL][i].lead) return i; return -1; }
static int slot_at(int b, int c)
{
    for (int i = 0; i < NVL; i++) { slot_t *s = &sl[CL][i]; if (s->on && !s->root && s->b == b && s->c == c && !s->pend) return i; }
    return -1;
}
static int slots_wanted(void)
{
    int root = pv[P_ROOT] >= 1.0f;
    return clampi((int)pv[P_VOICES], 1, NVL - root);                  // tones besides the root
}

static void midi_lead(void)
{
    float hz = lat_ratio((int)pv[P_LIMIT], lead_b, lead_c) * fund_hz * follow_ratio * root_ratio(CL);
    float n = hz_midi_f(hz);
    int note = clampi((int)lroundf(n), 0, 127);
    if (midi_note_out >= 0) midi_note(midi_note_out, 0);
    midi_bend(clampi(8192 + (int)((n - (float)note) * 4096.0f), 0, 16383));
    midi_note(note, 90);
    midi_note_out = note;
}

// the lead moves to (b, c): a tone that is already there becomes the lead; otherwise a free slot, or the oldest
// tone of the trail (SWELL: it fades out first, then comes back on the new cell; GLIDE: it slides there)
static void lead_move(int b, int c)
{
    slot_t *L = sl[CL];
    int old = slot_lead();
    int here = slot_at(b, c);
    if (here >= 0 && here != old) {
        if (old >= 0) { L[old].lead = false; L[old].age = 0.0f; }
        L[here].lead = true; L[here].age = 0.0f;
    } else if (here < 0) {
        int want = slots_wanted();
        int inuse = 0; for (int i = 0; i < NVL; i++) inuse += L[i].on && !L[i].root;
        int spare = inuse < want ? slot_free() : -1;
        if (spare < 0) spare = slot_oldest_trail();
        if (pv[P_MOVE] == MOVE_GLIDE && old >= 0) {
            // GLIDE: the lead itself slides to the new cell; the cell it leaves goes to the spare slot, which slides to it
            if (spare >= 0 && spare != old) {
                slot_t *t = &L[spare];
                t->on = true; t->root = false; t->lead = false; t->pend = false; t->b = L[old].b; t->c = L[old].c; t->age = 0.0f;
            }
            L[old].b = (int8_t)b; L[old].c = (int8_t)c; L[old].age = 0.0f; L[old].pend = false;
        } else {
            // SWELL: a new slot swells in on the new cell (a spare one, or the oldest of the trail after it has faded out);
            // the old lead keeps its tone and becomes trail
            int k = spare >= 0 ? spare : old;                          // one voice: the lead itself moves
            if (old >= 0 && old != k) { L[old].lead = false; L[old].age = 0.0f; }
            slot_t *s = &L[k];
            bool was_on = s->on;
            s->on = true; s->root = false; s->lead = true; s->age = 0.0f;
            if (was_on) { s->pend = true; s->pb = (int8_t)b; s->pc = (int8_t)c; s->pend_s = 0.0f; }
            else { s->pend = false; s->b = (int8_t)b; s->c = (int8_t)c; }
        }
    }
    lead_b = b; lead_c = c;
    scene_cell_new(b, c);
    midi_lead();
}

// the root and the number of voices, as the params say
static void slots_shape(void)
{
    slot_t *L = sl[CL];
    bool root = pv[P_ROOT] >= 1.0f;
    for (int i = 0; i < NVL; i++) if (L[i].root && !root) { L[i].on = false; L[i].root = false; }
    if (root) {
        int r = -1; for (int i = 0; i < NVL; i++) if (L[i].on && L[i].root) r = i;
        if (r < 0) { r = slot_free(); if (r < 0) r = slot_oldest_trail(); if (r >= 0) L[r] = (slot_t){ .on = true, .root = true, .b = 0, .c = 0 }; }
    }
    int want = slots_wanted();
    for (;;) {                                                         // too many tones: the oldest go
        int n = 0; for (int i = 0; i < NVL; i++) n += L[i].on && !L[i].root;
        if (n <= want) break;
        int k = slot_oldest_trail(); if (k < 0) break;
        L[k].on = false;
    }
    if (slot_lead() < 0) lead_move(lead_b, lead_c);
}

static float slot_level(const slot_t *s)
{
    float lvl = s->root ? 0.8f : s->lead ? 1.0f : 0.7f;
    return s->lead ? lvl * (1.0f + 0.5f * balance) : lvl * (1.0f - 0.5f * balance);
}

static void slots_tick(float dt)
{
    float drift = pv[P_DRIFT];
    int spread = pv[P_SPREAD] >= 1.0f && D.peers > 0;
    bool fd = fading();
    for (int layer = 0; layer < 2; layer++) {
        slot_t *L = sl[layer];
        bool cur = layer == CL;
        int sound_k = -1;
        if (cur && spread) {                                           // this board's one tone: by its rank among the DWELLs
            int nvoice = 0; for (int i = 0; i < NVL; i++) nvoice += L[i].on;
            if (nvoice > 0) { int k = my_rank % nvoice, j = 0; for (int i = 0; i < NVL; i++) if (L[i].on) { if (j == k) sound_k = i; j++; } }
        }
        for (int i = 0; i < NVL; i++) {
            slot_t *s = &L[i];
            int e = eidx(layer, i);
            if (!cur && !fd) s->on = false;                            // the old layer, faded out: its voices go
            if (!s->on) { eng_slot_lvl[e] = 0.0f; if (!(eng_slot_hz[e] > 0.0f)) eng_slot_hz[e] = fund_hz; continue; }
            s->age += dt;
            if (s->pend) {                                             // SWELL: out, then back on the new cell
                s->pend_s += dt;
                eng_slot_lvl[e] = 0.0f;
                if (eng_slot_lvl_now[e] < 0.03f || s->pend_s > 1.3f * drift) { s->pend = false; s->b = s->pb; s->c = s->pc; s->age = 0.0f; }
                else continue;
            }
            float lvl = slot_level(s);
            if (cur && spread && i != sound_k) lvl = 0.0f;
            s->lvl = lvl;
            eng_slot_hz[e] = slot_hz(layer, s);
            eng_slot_lvl[e] = lvl;
        }
    }
}

// ---------------------------------------------------------------- the next root: the other layer fades in on it
static void root_next(int sb, int sc)
{
    if (fading()) { next_queued = true; return; }                     // one at a time: this one waits for the fade
    int O = 1 - CL;
    int nb = root_b[CL] + sb, nc = root_c[CL] + sc;
    if (abs(nb) > 12 || abs(nc) > 6) { toast("AS FAR AS IT GOES: HOME", 1200); nb = 0; nc = 0; }
    root_b[O] = nb; root_c[O] = nc;
    for (int i = 0; i < NVL; i++) {                                    // the same chord on the new root
        slot_t s = sl[CL][i];
        if (s.pend) { s.pend = false; s.b = s.pb; s.c = s.pc; }
        s.age = 0.0f;
        sl[O][i] = s;
    }
    // its targets first, then the switch: the new layer is silent until then, so its voices start where they belong
    CL = O;
    slots_tick(0.0f);
    eng_fade_s = pv[P_FADE];
    __atomic_store_n(&eng_layer, O, __ATOMIC_RELEASE);
    midi_lead();
    char nm[12], b[32];
    root_label(O, nm, sizeof nm);
    snprintf(b, sizeof b, nm[0] ? "ROOT %s" : "HOME", nm);
    toast(b, 900);
}
static void root_step(void)
{
    int sb = 0, sc = 0;
    switch ((int)pv[P_NEXT]) {
    case NX_LEAD:     sb = lead_b; sc = lead_c; if (!sb && !sc) sb = 1; break;   // level: a fifth up
    case NX_FIFTH:    sb = 1; break;
    case NX_FOURTH:   sb = -1; break;
    case NX_SEVENTH:  if (pv[P_LIMIT] == LIM_3) sb = 2; else sc = 1; break;
    case NX_SEPTIMAL: if (pv[P_LIMIT] == LIM_3) sb = -2; else sc = -1; break;
    default:          sb = 2; break;
    }
    root_next(sb, sc);
}

// ---------------------------------------------------------------- the tilt: the cursor on the lattice
static void cursor_tick(const ctl_t *c)
{
    float step = pv[P_TSTEP];
    int cmax = pv[P_LIMIT] == LIM_3 ? 0 : LAT_C;
    if (c->has_imu && !c->frozen) {
        cur_b = clampf(-c->lean_y / step, -(float)LAT_B, (float)LAT_B);
        cur_c = clampf(c->lean_x / step, -(float)cmax, (float)cmax);
    } else if (!c->has_imu && c->has_joy) {
        cur_b = clampf(cur_b + c->jy * 1.5f * c->dt, -(float)LAT_B, (float)LAT_B);
        cur_c = clampf(cur_c + c->jx * 1.5f * c->dt, -(float)cmax, (float)cmax);
    }
    if (cmax == 0) cur_c = 0.0f;
    // the cell under the cursor, with some stickiness round the lead's own cell; it counts only on a calm box (a
    // swing passes cells by: where the box comes to rest is where the lead goes)
    bool calm = c->gyro_mag < 60.0f && shake_s < 0.3f;
    int b = (int)lroundf(cur_b), cc = (int)lroundf(cur_c);
    if (fabsf(cur_b - (float)lead_b) < 0.7f) b = lead_b;
    if (fabsf(cur_c - (float)lead_c) < 0.7f) cc = lead_c;
    if ((b != lead_b || cc != lead_c) && calm) {
        if (b != cand_b || cc != cand_c) { cand_b = b; cand_c = cc; cand_s = 0.0f; }
        cand_s += c->dt;
        if (cand_s >= pv[P_DWELLMS]) {
            if (cc != lead_c) coach_done(L_ROLL);
            lead_move(b, cc);
        }
    } else cand_s = 0.0f;
    // the stick: up and down an octave for the lead, left and right the chord against the lead
    if (c->has_joy && c->has_imu) {
        int o = c->jy > 0.6f ? 1 : c->jy < -0.6f ? -1 : 0;
        if (o != oct_shift) { oct_shift = o; coach_done(LEARN_JOY); midi_lead(); }
        balance += (c->jx - balance) * 0.2f;
    } else { oct_shift = 0; balance *= 0.9f; }
}

// ---------------------------------------------------------------- ENERGY and SHAKE
// Movement: rotation over 25 deg/s (a hand holding still is under it) up to 375, and jolts over 0.12 g. It pumps the
// energy up at 1/CLIMB a second while it lasts; still, it falls at 1/FALL a second: gesture after gesture adds up.
// A shake: the jolts of back and forth, change of acceleration from tick to tick over 0.35 g; it rises in about 160 ms
// (a knock's one or two jolts barely move it) and dies in about 0.7 s, and kicks the energy too.
static void energy_tick(const ctl_t *c)
{
    float m = 0.0f, jerk = 0.0f;
    if (c->has_imu && !c->frozen) {
        float am = sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z);
        m = clampf((c->gyro_mag - 25.0f) / 350.0f, 0.0f, 1.0f) + 0.5f * clampf((fabsf(am - 1.0f) - 0.12f) / 0.8f, 0.0f, 1.0f);
        m = clampf(m, 0.0f, 1.0f);
        if (acc_have) {
            float dx = c->acc.x - acc_prev.x, dy = c->acc.y - acc_prev.y, dz = c->acc.z - acc_prev.z;
            jerk = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        acc_prev = c->acc; acc_have = true;
    } else acc_have = false;
    motion += (m - motion) * 0.3f;
    mfast += (m - mfast) * (m > mfast ? 0.5f : 0.08f);
    float si = clampf((jerk - 0.35f) / 1.0f, 0.0f, 1.0f);
    shake_s += (si - shake_s) * (si > shake_s ? 0.06f : 0.015f);    // a knock's one or two jolts barely move it
    if (shake_s < 1e-4f) shake_s = 0.0f;
    float climb = pv[P_CLIMB], fall = pv[P_FALL], sk = shake_s * pv[P_SHAKE] * 0.01f;
    energy += c->dt * (motion / climb - (1.0f - motion) / fall + sk / climb);
    energy = clampf(energy, 0.0f, 1.0f);
    if (energy > 0.5f) coach_done(L_ENERGY);
    if (sk > 0.4f) coach_done(L_SHAKE);
    if (motion > 0.2f || shake_s > 0.2f) ui_awake();
}

// ---------------------------------------------------------------- the knock: a strum, or the next root
static void knock_fire(float s)
{
    int k = (int)pv[P_KNOCK];
    scene_knock(s);
    D.knock_frames = 0;
    coach_done(L_KNOCK);
    ui_awake();
    if (k == KN_STRUM) eng_strum(s);
    else if (k == KN_NEXT) root_step();
}
static void knock_tick(const ctl_t *c)
{
    knock_cool++;
    D.knock_frames += (int)(c->dt * (float)AUDIO_SR);
    if (!c->has_imu || pv[P_KNOCK] < 1.0f) { knock_wait = -1; return; }
    float am = sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z);
    float jolt = fabsf(am - 1.0f);
    float thr = 0.95f - 0.75f * pv[P_SENSE] * 0.01f;                 // 0.575 g at 50
    if (knock_wait >= 0) {
        knock_wait++;
        if ((knock_wait > 3 && jolt > thr) || c->gyro_mag > 120.0f || shake_s > 0.45f) { knock_wait = -1; knock_cool = 0; }   // more followed: a shake
        else if (knock_wait >= 12) { knock_wait = -1; knock_cool = 0; knock_fire(knock_amp); }
    } else if (jolt > thr && knock_cool > 25 && c->gyro_mag < 80.0f && shake_s < 0.3f) {
        knock_wait = 0;
        knock_amp = clampf(0.35f + (jolt - thr) / 0.8f, 0.35f, 1.0f);
    }
}

// ---------------------------------------------------------------- REC: hold, your tone; tap, a rest
static void rec_logic(const ctl_t *c)
{
    if (c->rec_press) {
        R.ignore_release = false;
        if (eng_room_state == ROOM_PASSING) { eng_room_stop(); toast("ROOM PASS STOPPED", 1000); R.ignore_release = true; }
    }
    if (c->rec && !R.ignore_release && !R.recording && !R.analysing && c->rec_ms >= 300 && eng_room_state != ROOM_PASSING) {
        R.recording = true; R.started = false; R.ms = 0;
        eng_take_begin();
        toast("SING ONE NOTE, LET GO", 1500);
    }
    if (c->rec_release) {
        if (R.ignore_release) { R.ignore_release = false; return; }
        if (R.recording) {
            R.recording = false;
            eng_take_end();
            R.analysing = true;
        } else if (c->rec_ms < 300) {
            rest = rest > 0.5f ? 0.0f : 1.0f;
            eng_rest = rest;
            coach_done(L_REST);
            toast(rest > 0.5f ? "RESTING: TAP REC TO RETURN" : "IT RETURNS", 900);
        }
    }
    // the take starts in the audio task: it is full once it has been seen running and has stopped by itself
    if (R.recording) {
        R.ms += 10;
        if (eng_take_on) R.started = true;
        else if (R.started || R.ms > 500) { R.recording = false; eng_take_end(); R.analysing = true; }
    }
    if (R.analysing && wave_analyse()) {
        R.analysing = false;
        if (eng_wave_hz > 0.0f) {
            char b[40]; snprintf(b, sizeof b, "YOUR TONE: %.0f HZ", (double)eng_wave_hz); toast(b, 1500);
            applying = true; param_set(P_SOURCE, SRC_WAVE); applying = false;
        } else toast("NO STEADY TONE HEARD", 1400);
    }
}

// ---------------------------------------------------------------- ROOM PASS
static void room_start(void)
{
    if (eng_room_state == ROOM_PASSING) { toast("A PASS IS RUNNING", 900); return; }
    int passes = clampi((int)pv[P_PASSES], 1, 30);
    eng_room_start(passes, (int)pv[P_PASSLEN] * AUDIO_SR, fund_hz * follow_ratio * root_ratio(CL));
    RM.checked0 = false;
    char b[40]; snprintf(b, sizeof b, "PASS 1 OF %d: THE ROOM LISTENS", passes); toast(b, 1500);
    coach_done(L_ROOM);
    menu_leave();
}
static void room_tick(void)
{
    static int last_state = ROOM_IDLE, last_pass = -1;
    int st = eng_room_state;
    if (st == ROOM_PASSING) {
        ui_awake();
        int p = eng_room_pass;
        if (p >= 1 && !RM.checked0) {                                 // the first recording: is there a room at all?
            RM.checked0 = true;
            if (eng_room_peak < 0.004f) { eng_room_stop(); toast("NO ROOM HEARD: USE THE SPEAKER", 2000); }
        }
        if (p != last_pass && p > 0 && p < eng_room_passes) { char b[40]; snprintf(b, sizeof b, "PASS %d OF %d", p + 1, eng_room_passes); toast(b, 700); }
        last_pass = p;
    }
    if (st == ROOM_DONE && last_state == ROOM_PASSING) {
        char b[40]; snprintf(b, sizeof b, "THE ROOM, %d PASSES", eng_room_passes); toast(b, 1500);
        applying = true; param_set(P_SOURCE, SRC_ROOM); applying = false;
    }
    last_state = st;
}

// ---------------------------------------------------------------- the radios: what they hear, as interference
static void radio_tick(void)
{
    int r = (int)pv[P_RADIO];
    bool w = r == RAD_WIFI || r == RAD_BOTH, b = r == RAD_BLE || r == RAD_BOTH;
    air_want(w, b, (int)pv[P_CHAN], pv[P_LINK] >= 1.0f);
    float amt = pv[P_ZAP] * 0.01f;
    air_ev_t e;
    int k = 0;
    while (air_pop(&e)) {
        if (k >= 24) continue;                                         // a crowded room: the rest are lost in these
        if ((e.kind == ZAP_WIFI && !w) || (e.kind == ZAP_BLE && !b)) continue;
        float a = clampf(((float)e.rssi + 92.0f) / 55.0f, 0.0f, 1.0f);
        a = a * a * amt;
        if (a < 0.01f) continue;
        int len = e.kind == ZAP_WIFI ? clampi(8 + e.len / 6, 8, 256) : clampi(20 + e.len, 24, 64);
        eng_zap(e.frame + 480, len, a, e.kind);                       // 15 ms late, so each lands where it was heard
        k++;
    }
    eng_zap_gate = r != RAD_OFF ? 0.8f * amt : 0.0f;
    D.radio = r; D.zap = eng_zap_env;
}

// ---------------------------------------------------------------- the link: tempo and key shared, FOLLOW, SPREAD
static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }
static void harm_here(void)
{
    const link_harm_t *h = link_harm();
    link_set_harm((int)pv[P_KEY], (int)pv[P_SCALE], h->prog, h->hold, h->bars, -1);
}
static void tempo_here(void)
{
    uint32_t fpb = tempo_fpb(pv[P_TEMPO]), old = link_fpb();
    if (fpb == old) return;
    uint32_t o; float f; link_origin(&o, &f);
    float beat = old ? (float)((int32_t)(now - o) % (int32_t)old) / (float)old : 0.0f;
    if (beat < 0.0f) beat += 1.0f;
    link_set_time(fpb, now - (uint32_t)(beat * (float)fpb), 4, 0);
}
static void link_start(void)
{
    link_init("DWELL", 1);
    link_boot_time(tempo_fpb(pv[P_TEMPO]), 0, 4);
    link_boot_harm((int)pv[P_KEY], (int)pv[P_SCALE], 0, 0, 0);
    link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    link_up = true;
}
static uint32_t my_id(void)
{
    uint8_t m[6]; plat_link_mac(m);
    return (uint32_t)m[2] << 24 | (uint32_t)m[3] << 16 | (uint32_t)m[4] << 8 | m[5];
}
static void fund_update(void)
{
    int oct = clampi((int)pv[P_OCTAVE], 1, 4);
    fund_hz = midi_hz((float)(12 * (oct + 1) + (int)pv[P_KEY]));
}
static void link_follow(void)
{
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    D.peers = np;
    uint32_t me = my_id();
    int rank = 0, dwells = 0;
    for (int i = 0; i < np; i++) if (!strncmp(pr[i].name, "DWELL", 5)) { dwells++; if (pr[i].id < me) rank++; }
    my_rank = rank;
    D.rank = rank; D.spread = pv[P_SPREAD] >= 1.0f && dwells > 0;
    if (link_time_news()) {
        uint32_t fpb = link_fpb();
        int bpm = fpb ? (int)(60.0f * (float)AUDIO_SR / (float)fpb + 0.5f) : 0;
        if (bpm && bpm != (int)pv[P_TEMPO]) { applying = true; param_set(P_TEMPO, (float)clampi(bpm, 50, 200)); applying = false; }
    }
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        applying = true;
        if ((int)pv[P_KEY] != h->key % 12) param_set(P_KEY, (float)(h->key % 12));
        if ((int)pv[P_SCALE] != h->scale && h->scale < NSCALES) param_set(P_SCALE, (float)h->scale);
        applying = false;
    }
    // FOLLOW: a chord played live on a linked board (a scale degree) moves 1/1 to the lattice's nearest ratio
    const link_harm_t *h = link_harm();
    float want = 1.0f;
    if (pv[P_FOLLOW] >= 1.0f && h->live >= 0) {
        int sc = clampi((int)pv[P_SCALE], 0, NSCALES - 1);
        int semis = scales[sc][h->live % 7];
        int b, c;
        if (lat_nearest((int)pv[P_LIMIT], (float)semis * 100.0f, &b, &c)) want = lat_ratio((int)pv[P_LIMIT], b, c);
    }
    follow_ratio = want;
    uint32_t fpb = link_fpb();
    D.bpm = fpb ? 60.0f * (float)AUDIO_SR / (float)fpb : 0.0f;
    D.follow = pv[P_FOLLOW] >= 1.0f && h->live >= 0;
}

// ---------------------------------------------------------------- the LED
static void led_tick(const ctl_t *c)
{
    (void)c;
    if (R.recording || eng_room_state == ROOM_PASSING) {
        float l = 0.4f + 0.3f * sin_turns((float)now / (float)AUDIO_SR * 0.8f);
        led_rgb((uint8_t)(255.0f * l), (uint8_t)(20.0f * l), 0);
        return;
    }
    // the sevenths' axis from blue to amber, whitening and brightening with the energy; breathing at the beat when
    // dense; a flicker of the interference; near dark when resting
    float t = (float)(lead_c + LAT_C) / (float)(2 * LAT_C);
    float r = 60.0f + 195.0f * t, g = 60.0f + 80.0f * t, b = 255.0f - 230.0f * t;
    r += (255.0f - r) * energy * 0.6f; g += (255.0f - g) * energy * 0.6f; b += (255.0f - b) * energy * 0.6f;
    float breathe = 0.6f + 0.4f * (0.5f + 0.5f * sin_turns(eng_beat_phase)) * D.dense;
    led_breath += (breathe - led_breath) * 0.3f;
    float l = (0.1f + 0.3f * energy + 0.4f * clampf(eng_out_lvl * 3.0f, 0.0f, 1.0f) + 0.3f * eng_zap_env) * led_breath * (1.0f - 0.85f * rest);
    l = clampf(l, 0.0f, 1.0f);
    led_rgb((uint8_t)(r * l), (uint8_t)(g * l), (uint8_t)(b * l));
}

// ---------------------------------------------------------------- the core's calls
static void controls(void)
{
    float E = energy, sk = shake_s * pv[P_SHAKE] * 0.01f;
    eng_glide_s = pv[P_DRIFT];
    // the energy: the VCA from REST LEVEL to full, HAND multiplying by the movement itself
    float vca = expf(0.1151293f * pv[P_REST] * (1.0f - E));           // dB to gain
    float hand = pv[P_HAND] * 0.01f;
    vca *= (1.0f - hand) + hand * clampf(mfast * 1.6f, 0.0f, 1.0f);
    eng_vca = vca;
    float dense = pv[P_DENSE] * 0.01f, td = pv[P_TODENSE] * 0.01f;
    eng_dense = dense + (1.0f - dense) * E * td;
    eng_copies = clampi((int)pv[P_COPIES], 1, NCOPY);
    float beat = pv[P_BEAT];
    if (pv[P_BSYNC] >= 1.0f && D.bpm > 0.0f) beat = D.bpm / 60.0f;
    eng_beat_hz = beat * (1.0f + 5.0f * sk);                            // a shake: the copies beat faster
    float bright = pv[P_BRIGHT] * 0.01f;
    eng_bright = bright + (1.0f - bright) * E * td * 0.5f;
    eng_diff = pv[P_DIFF] * 0.01f;
    eng_source = (int)pv[P_SOURCE];
    eng_pluck_s = pv[P_PLUCK];
    eng_jawari = pv[P_JAWARI] * 0.01f;
    eng_wheel_hz = pv[P_WHEEL];
    eng_breath = pv[P_BREATH] * 0.01f;
    eng_body = pv[P_BODY] * 0.01f;
    eng_level = pv[P_LEVEL] * 0.01f;
    eng_space = pv[P_SPACE] * 0.01f;
    eng_tail = pv[P_TAIL] * 0.01f;
    eng_strum_s = pv[P_STRUM];
    eng_rest = rest;
    // the dirt: DRIVE, raised by the energy and a shake (which also throws grit in)
    float drive = pv[P_DRIVE] * 0.01f;
    drive += (1.0f - drive) * E * pv[P_TODRIVE] * 0.01f;
    drive += (1.0f - drive) * sk * 0.5f;
    eng_drive = drive;
    eng_tone = pv[P_TONE] * 0.01f;
    eng_dmix = pv[P_DMIX] * 0.01f;
    eng_grit = sk;
    // the echo: its time (or on the beat), its feedback swelling with a shake
    int es = clampi((int)pv[P_ESYNC], 0, 4);
    eng_echo_s = es && D.bpm > 0.0f ? clampf(esync_beats[es] * 60.0f / D.bpm, 0.02f, 1.6f) : pv[P_ECHO];
    float fb = pv[P_EFB] * 0.01f;
    eng_echo_fb = fb + (0.95f - fb) * sk;
    eng_echo_mix = pv[P_EMIX] * 0.01f;
    if (!fading()) eng_fade_s = pv[P_FADE];
}

static void ui_fill(void)
{
    int limit = (int)pv[P_LIMIT];
    D.lead_b = lead_b; D.lead_c = lead_c; D.cur_b = cur_b; D.cur_c = cur_c;
    D.limit = limit; D.fund_hz = fund_hz * follow_ratio * root_ratio(CL);
    D.voices = slots_wanted(); D.root_on = pv[P_ROOT] >= 1.0f; D.move = (int)pv[P_MOVE];
    for (int i = 0; i < NVL; i++) {
        dw_slot_t *d = &D.slot[i];
        const slot_t *s = &sl[CL][i];
        int e = eidx(CL, i);
        d->on = s->on; d->b = s->b; d->c = s->c; d->is_lead = s->lead; d->is_root = s->root;
        d->hz = eng_slot_hz_now[e]; d->lvl = eng_slot_lvl_now[e]; d->age = s->age;
        if (s->on) lat_name(limit, s->b, s->c, d->name, sizeof d->name); else d->name[0] = 0;
    }
    float r = lat_ratio(limit, lead_b, lead_c);
    lat_name(limit, lead_b, lead_c, D.lead_name, sizeof D.lead_name);
    D.lead_cents = lat_cents(r); D.lead_et = lat_cents_et(r); D.lead_hz = r * D.fund_hz;
    D.dense = eng_dense; D.source = (int)pv[P_SOURCE];
    D.beat_hz = eng_beat_hz; D.diff_hz = eng_diff_hz; D.strum = eng_strum_env; D.rest = rest > 0.5f;
    D.rec = R.recording; D.rec_s = (float)eng_take_frames / (float)AUDIO_SR; D.analysing = R.analysing; D.wave_hz = eng_wave_hz;
    D.room_state = eng_room_state; D.room_pass = eng_room_pass; D.room_passes = eng_room_passes;
    D.room_frac = eng_room_frames ? (float)eng_room_pos / (float)eng_room_frames : 0.0f; D.room_peak = eng_room_peak;
    D.energy = energy; D.motion = motion; D.shake = shake_s;
    D.fade = fading() ? (CL ? eng_layer_x : 1.0f - eng_layer_x) : -1.0f;
    D.root_b = root_b[CL]; D.root_c = root_c[CL];
    float rr = root_ratio(CL);
    D.root_cents = lat_cents(rr < 1.0f ? rr * 2.0f : rr);
    root_label(CL, D.root_name, sizeof D.root_name);
}

static void tick(const ctl_t *c)
{
    now = c->frames;
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    fund_update();
    if (!booted) { booted = true; slots_shape(); }
    energy_tick(c);
    cursor_tick(c);
    slots_shape();
    if (next_queued && !fading()) { next_queued = false; root_step(); }
    slots_tick(c->dt);
    knock_tick(c);
    rec_logic(c);
    room_tick();
    radio_tick();
    controls();
    ui_fill();
    led_tick(c);
    if (R.recording || R.analysing) ui_awake();
}

static void changed(int p)
{
    if (p == P_SOURCE && !applying && booted) {
        if ((int)pv[P_SOURCE] == SRC_ROOM && !eng_room_have) toast("ROOM > ROOM PASS MAKES IT", 1400);
        if ((int)pv[P_SOURCE] == SRC_WAVE && eng_wave_hz <= 0.0f) toast("HOLD REC AND SING: YOUR TONE", 1400);
    }
    if (p < 0 && booted && (int)pv[P_SOURCE] == SRC_ROOM && !eng_room_have) toast("ROOM > ROOM PASS MAKES THE ROOM", 1600);
    if (p == P_ROOT || p == P_VOICES || p < 0) slots_shape();
    if (!link_up) return;
    if (p == P_LINK || p == P_GROUP || p == P_RANGE) {
        air_release();                                                 // the link starts Wi-Fi its own way: let go of ours first
        link_enable(false, 1, 1);
        link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    }
    if (!applying && (p == P_KEY || p == P_SCALE)) harm_here();
    if (!applying && p == P_TEMPO) tempo_here();
}

static void action(int p)
{
    switch (p) {
    case P_PASS: room_start(); break;
    case P_HOME:
        if (root_b[CL] || root_c[CL]) root_next(-root_b[CL], -root_c[CL]);
        else toast("ALREADY HOME", 800);
        menu_leave();
        break;
    case P_PEERS: {
        link_peer_t pr[LINK_PEERS_MAX];
        int np = link_peers(pr, LINK_PEERS_MAX);
        char b[64];
        if (!link_radio()) snprintf(b, sizeof b, "LINK IS OFF");
        else if (!np) snprintf(b, sizeof b, "NO BOARD IN GROUP %d YET", (int)pv[P_GROUP]);
        else {
            int n = snprintf(b, sizeof b, "%d: ", np);
            for (int i = 0; i < np && n < (int)sizeof b - 10; i++) n += snprintf(b + n, sizeof b - (size_t)n, "%s%s", i ? " " : "", pr[i].name);
        }
        toast(b, 2000);
        break;
    }
    default: break;
    }
}

// MIDI notes (other than REC's 60 and the hold's 64) move 1/1 to that note
static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) == 0x90 && d2 > 0 && d1 >= 24 && d1 < 72) {
        applying = true;
        param_set(P_KEY, (float)(d1 % 12));
        param_set(P_OCTAVE, (float)clampi((int)d1 / 12 - 1, 1, 4));
        applying = false;
        harm_here();
    }
}

static void init(void)
{
    eng_init();
    fund_update();
    D.knock_frames = 1 << 30;
    D.fade = -1.0f;
    lead_b = 0; lead_c = 0;
    sl[0][0] = (slot_t){ .on = true, .root = true };
    sl[0][1] = (slot_t){ .on = true, .lead = true };
    for (int i = 0; i < NV; i++) eng_slot_hz[i] = fund_hz;
    slots_tick(0.0f);                                                  // the first targets before the first block
}

#ifdef ESP_PLATFORM
static void audio(const float *in, float *out, int n) { eng_audio(in, out, n); }
#else
#include <time.h>
double eng_audio_ns, eng_audio_max_ns;
long   eng_audio_blocks;
static void audio(const float *in, float *out, int n)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    eng_audio(in, out, n);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ns = (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
    eng_audio_ns += ns; eng_audio_blocks++;
    if (ns > eng_audio_max_ns) eng_audio_max_ns = ns;
}
#endif
static void draw(const view_t *v) { scene_draw(v); }

const instrument_t INST = {
    .name = "DWELL", .tagline = "A DRONE YOU LIVE INSIDE",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = false, .looper = false, .knob = "dens",
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = scene_saver,
};

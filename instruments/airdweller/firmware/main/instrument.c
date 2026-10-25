// AIRDWELLER (inst_ad.h says what it is). The UI side: params and sounds; AIR's strokes, grid and loops; DWELL's
// energy and shake; the hand's control of the material (tilt, twist, roll, movement, shake); the two hands on the link
// (who is LEFT, the shared beat and key, every DRUM hit sent with its tick so the other box strikes on the same
// instant); REC (hold: a loop; tap: CUT, back in on the next beat); MIDI and the LED.
#include "core.h"
#include "dsp.h"
#include "link.h"
#include "inst_ad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "PLAY", "KIT", "DRAG", "NOTES", "MATERIAL", "ENERGY", "LOOPS", "FX", "FEEL", "SONG" };
enum { G_PLAY, G_KIT, G_DRAG, G_NOTES, G_MAT, G_ENERGY, G_LOOPS, G_FX, G_FEEL, G_SONG, G_N };
const char *const role_name[NROLE] = { "DRUM", "PAINT", "PUMP", "NOTES", "SHAPE" };
const char *const nv_name[NNV] = { "BASS", "SQUELCH", "LEAD", "BELL" };
static const char *const easy_l[] = { "OFF", "STEP", "GROOVE" };
static const char *const patt_l[NPATTERN] = { "FOUR", "BACKBEAT", "BREAK", "HALF", "BOUNCE", "ROLL" };
static const char *const scale_l[NSCALES] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };   // SING's, as the link numbers them
static const char *const hand_l[] = { "AUTO", "LEFT", "RIGHT" };
static const char *const off_on[] = { "OFF", "ON" };
const char *const dv_name[NDV + 1] = { "KICK", "SNAP", "CLANG", "HAT", "GLASS", "SUB", "WHIP", "CREAK", "OFF", "AUTO" };
const char *const mat_name[NMAT] = { "METAL", "GLASS", "RUBBER", "LATEX" };
static const char *const tuneto_l[] = { "THE KICK", "THE KEY" };
static const char *const ratio_l[] = { "X1", "X2", "X3", "X4", "X6", "X8" };
static const float ratio_v[] = { 1, 2, 3, 4, 6, 8 };
static const char *const chord_l[] = { "ONE", "FIFTH", "OCTAVE", "TRIAD 7" };
static const char *const loop_l[] = { "EMPTY", "OFF", "ON" };
static const char *const quant_l[] = { "OFF", "1/8", "1/16" };
static const char *const esync_l[] = { "OFF", "1 BEAT", "3/4", "1/2", "1/4" };
static const float esync_beats[] = { 0.0f, 1.0f, 0.75f, 0.5f, 0.25f };
static const char *const flip_l[] = { "TIP AWAY", "TIP TO ME" };
static const char *const sides_l[] = { "KICK LEFT", "KICK RIGHT" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };
#define SYS (PF_SYS | PF_NOCC)

static const param_t params[P_N] = {
    [P_ROLE]    = { "ROLE", "role", G_PLAY, PK_ENUM, PF_SYS, 0, NROLE - 1, 1, ROLE_DRUM, "", role_name, "WHAT THIS HAND DOES" },
    [P_EASY]    = { "EASY", "easy", G_PLAY, PK_ENUM, PF_SYS, 0, 2, 1, EASY_OFF, "", easy_l, "ANY SWING: THE NEXT STEP" },
    [P_PATTERN] = { "PATTERN", "patt", G_PLAY, PK_ENUM, 0, 0, NPATTERN - 1, 1, 0, "", patt_l, "WHAT EASY PLAYS" },
    [P_HAND]    = { "HAND", "hand", G_PLAY, PK_ENUM, PF_SYS, 0, 2, 1, HAND_AUTO, "", hand_l, "WHICH HAND THIS BOX IS" },
    [P_PAIR]    = { "PAIR", "pair", G_PLAY, PK_ENUM, PF_SYS, 0, 1, 1, 1, "", off_on, "THE OTHER HAND STRIKES IT" },
    [P_DUCK]    = { "DUCK", "duck", G_PLAY, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "ITS HITS PUSH THIS DOWN" },
    [P_LEVEL]   = { "LEVEL", "levl", G_PLAY, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD" },
    [P_FRONT]   = { "FRONT", "pfrt", G_KIT, PK_ENUM, 0, 0, NDV, 1, DV_AUTO, "", dv_name, "SWUNG FORWARD" },
    [P_LEFT]    = { "LEFT", "plft", G_KIT, PK_ENUM, 0, 0, NDV, 1, DV_AUTO, "", dv_name, "SWUNG LEFT" },
    [P_RIGHT]   = { "RIGHT", "prgt", G_KIT, PK_ENUM, 0, 0, NDV, 1, DV_AUTO, "", dv_name, "SWUNG RIGHT" },
    [P_BACK]    = { "BACK", "pbck", G_KIT, PK_ENUM, 0, 0, NDV, 1, DV_AUTO, "", dv_name, "SWUNG BACK" },
    [P_TUNE]    = { "TUNE", "tune", G_KIT, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "THE DRUMS UP OR DOWN" },
    [P_DECAY]   = { "DECAY", "decy", G_KIT, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.1f, 1.0f, "x", NULL, "SHORTER OR LONGER" },
    [P_SQUEEZE] = { "SQUEEZE", "sqez", G_KIT, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "FOLDED, PLASTIC, HARDER" },
    [P_DRAG]    = { "DRAG", "drag", G_DRAG, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "AFTER A HIT, MOVE: IT BENDS" },
    [P_REACH]   = { "REACH", "rech", G_DRAG, PK_NUM, PF_INT, 15, 180, 5, 60, "deg", NULL, "THIS FAR: A FULL DRAG" },
    [P_DBEND]   = { "DRAG BEND", "dbnd", G_DRAG, PK_NUM, PF_INT, 0, 24, 1, 5, "st", NULL, "TWIST IT WHILE DRAGGING" },
    [P_NVOICE]  = { "VOICE", "nvoc", G_NOTES, PK_ENUM, 0, 0, NNV - 1, 1, NV_BASS, "", nv_name, "WHAT THE NOTES ARE" },
    [P_SCALE]   = { "SCALE", "sscl", G_NOTES, PK_ENUM, PF_SYS, 0, NSCALES - 1, 1, 1, "", scale_l, "SHARED WITH LINKED BOARDS" },
    [P_OCTAVE]  = { "OCTAVE", "noct", G_NOTES, PK_NUM, PF_INT, 1, 5, 1, 2, "", NULL, "THE LOWEST NOTE'S OCTAVE" },
    [P_SPAN]    = { "SPAN", "nspn", G_NOTES, PK_NUM, PF_INT, 3, 15, 1, 8, "", NULL, "NOTES OVER THE TILT" },
    [P_NLEN]    = { "NOTE LENGTH", "nlen", G_NOTES, PK_NUM, PF_LOG, 0.04f, 1.0f, 1.12f, 0.15f, "s", NULL, "A FLICK: THIS LONG" },
    [P_GLIDE]   = { "GLIDE", "glid", G_NOTES, PK_NUM, PF_LOG, 0.01f, 0.5f, 1.12f, 0.06f, "s", NULL, "FROM NOTE TO NOTE" },
    [P_MAT]     = { "MATERIAL", "matl", G_MAT, PK_ENUM, 0, 0, NMAT - 1, 1, MAT_METAL, "", mat_name, "WHAT RINGS" },
    [P_MORPH]   = { "TILT MORPH", "mrph", G_MAT, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "TILT IT: ANOTHER MATERIAL" },
    [P_TUNETO]  = { "TUNE TO", "tnto", G_MAT, PK_ENUM, 0, 0, 1, 1, TT_KICK, "", tuneto_l, "IT RINGS WITH THE KICK" },
    [P_RATIO]   = { "RATIO", "rato", G_MAT, PK_ENUM, 0, 0, 5, 1, 3, "", ratio_l, "OVER THE KICK'S NOTE" },
    [P_CHORD]   = { "CHORD", "chrd", G_MAT, PK_ENUM, 0, 0, 3, 1, CH_FIFTH, "", chord_l, "THE NOTES IT RINGS ON" },
    [P_RING]    = { "RING", "ring", G_MAT, PK_NUM, PF_LOG, 0.1f, 12.0f, 1.12f, 2.5f, "s", NULL, "HOW LONG A STRIKE RINGS" },
    [P_BRIGHT]  = { "BRIGHT", "brit", G_MAT, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "ITS UPPER MODES" },
    [P_BEND]    = { "TWIST BEND", "bend", G_MAT, PK_NUM, PF_INT, 0, 24, 1, 7, "st", NULL, "TWIST IT: THE PITCH BENDS" },
    [P_CREAK]   = { "CREAK", "crek", G_MAT, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "SHAKE IT: IT SCRAPES" },
    [P_CLIMB]   = { "CLIMB", "clmb", G_ENERGY, PK_NUM, PF_LOG, 1.0f, 60.0f, 1.15f, 6.0f, "s", NULL, "MOVING THIS LONG: FULL" },
    [P_FALL]    = { "FALL", "fall", G_ENERGY, PK_NUM, PF_LOG, 2.0f, 120.0f, 1.15f, 15.0f, "s", NULL, "STILL THIS LONG: BACK DOWN" },
    [P_REST]    = { "REST LEVEL", "rstl", G_ENERGY, PK_NUM, PF_INT | PF_SIGNED, -40, 0, 1, -24, "dB", NULL, "PUMP: HOW QUIET UNMOVED" },
    [P_SHAKE]   = { "SHAKE", "shak", G_ENERGY, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "WHAT A SHAKE THROWS IN" },
    [P_TEMPO]   = { "TEMPO", "tmpo", G_LOOPS, PK_NUM, PF_INT | PF_SYS, 50, 200, 1, 120, "", NULL, "SHARED WITH LINKED BOARDS" },
    [P_LOOP1]   = { "LOOP 1", "lop1", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, LP_EMPTY, "", loop_l, "ON OR OFF" },
    [P_LOOP2]   = { "LOOP 2", "lop2", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, LP_EMPTY, "", loop_l, "ON OR OFF" },
    [P_LOOP3]   = { "LOOP 3", "lop3", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, LP_EMPTY, "", loop_l, "ON OR OFF" },
    [P_LOOP4]   = { "LOOP 4", "lop4", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, LP_EMPTY, "", loop_l, "ON OR OFF" },
    [P_ERASE_LAST] = { "ERASE LAST", "erl ", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE NEWEST LOOP GOES" },
    [P_ERASE_ALL]  = { "ERASE ALL", "era ", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "EVERY LOOP GOES" },
    [P_QUANT]   = { "QUANTIZE", "qant", G_LOOPS, PK_ENUM, PF_SYS, 0, 2, 1, 2, "", quant_l, "HITS LAND ON THIS GRID" },
    [P_SWING]   = { "SWING", "swng", G_LOOPS, PK_NUM, PF_INT, 0, 75, 5, 0, "%", NULL, "THE OFF SIXTEENTHS LATE" },
    [P_DRIVE]   = { "DRIVE", "drve", G_FX, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "OVERDRIVE ON EVERYTHING" },
    [P_TONE]    = { "TONE", "tone", G_FX, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "DARK TO BRIGHT" },
    [P_ECHO]    = { "ECHO", "echo", G_FX, PK_NUM, PF_LOG, 0.02f, 1.6f, 1.12f, 0.25f, "s", NULL, "THE ECHO'S TIME" },
    [P_ESYNC]   = { "ECHO SYNC", "esyn", G_FX, PK_ENUM, 0, 0, 4, 1, 2, "", esync_l, "ON THE BEAT" },
    [P_EFB]     = { "FEEDBACK", "efb ", G_FX, PK_NUM, PF_INT, 0, 95, 5, 30, "%", NULL, "HOW OFTEN IT COMES BACK" },
    [P_EMIX]    = { "ECHO MIX", "emix", G_FX, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "DRY TO ALL ECHO" },
    [P_ROOM]    = { "ROOM", "room", G_FX, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "HOW MUCH ROOM" },
    [P_RSIZE]   = { "ROOM SIZE", "rsiz", G_FX, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "A BOOTH TO A HANGAR" },
    [P_SENS]    = { "SENSE", "sens", G_FEEL, PK_NUM, PF_INT | PF_SYS, 0, 100, 5, 50, "%", NULL, "HOW EASILY A SWING COUNTS" },
    [P_STOP]    = { "STOP", "stop", G_FEEL, PK_NUM, PF_INT | PF_SYS, 0, 100, 5, 50, "%", NULL, "HOW SHARP A STOP IS A HIT" },
    [P_CONFIRM] = { "CONFIRM", "cnfm", G_FEEL, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "A HIT NEEDS A JOLT TOO" },
    [P_FLIP]    = { "FORWARD", "flip", G_FEEL, PK_ENUM, SYS, 0, 1, 1, 0, "", flip_l, "WHICH WAY IS FRONT" },
    [P_SIDES]   = { "SIDES", "side", G_FEEL, PK_ENUM, SYS, 0, 1, 1, 0, "", sides_l, "WHICH SIDE IS LEFT" },
    [P_KEY]     = { "KEY", "skey", G_SONG, PK_ENUM, PF_SYS, 0, 11, 1, 0, "", key_l, "THE KICK'S NOTE" },
    [P_LINK]    = { "LINK", "nlnk", G_SONG, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]   = { "GROUP", "ngrp", G_SONG, PK_NUM, SYS | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]   = { "RANGE", "npow", G_SONG, PK_ENUM, SYS, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_PEERS]   = { "WHO IS THERE", "nwho", G_SONG, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
};

// ---------------------------------------------------------------- sounds: each one a kit, a material and a room, for any role
static const preset_t presets[] = {
    { "FACESHOP",          "nvoc=0 sqez=50 drve=35 tone=75 matl=0 rato=3 chrd=1 ring=2 brit=60 room=15 emix=10" },
    { "PONY",              "nvoc=1 sqez=85 drve=65 tone=85 decy=0.8 matl=3 rato=4 ring=1 brit=70 bend=12 crek=80 room=10" },
    { "LATEX",             "nvoc=2 matl=3 mrph=30 rato=4 chrd=0 ring=1.2 brit=50 bend=12 sqez=60 drve=40" },
    { "STEEL",             "nvoc=3 matl=0 rato=3 chrd=2 ring=4 brit=75 mrph=40 drve=20 room=30 rsiz=60" },
    { "GLASSHOUSE",        "nvoc=3 matl=1 rato=5 chrd=1 ring=6 brit=80 mrph=20 room=40 rsiz=80 emix=20 efb=45" },
    { "RUBBER ROOM",       "nvoc=0 matl=2 rato=2 chrd=0 ring=1.5 brit=30 sqez=70 drve=50 room=25" },
    { "MECHANICAL ANIMAL", "nvoc=1 matl=0 rato=4 chrd=3 ring=3 brit=65 crek=100 bend=5 drve=45 tone=60" },
    { "SILICONE",          "nvoc=2 matl=3 mrph=80 rato=5 chrd=1 ring=2.5 brit=40 bend=19 emix=25 efb=50 esyn=3" },
    { "SUB PRESSURE",      "nvoc=0 sqez=30 drve=55 tone=50 decy=2 matl=2 rato=1 ring=5 brit=20 rstl=-30" },
    { "CHAIN PUMP",        "nvoc=1 matl=0 rato=2 chrd=2 ring=8 brit=60 crek=70 rstl=-30 clmb=8 fall=20 drve=40 room=30" },
    { "HARD STOPS",        "nvoc=1 sqez=70 drve=50 tone=90 decy=0.5 matl=1 ring=0.6 room=0 emix=0" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_HIT = LEARN_INST, L_FOUR, L_CUT, L_PAIR, L_PAINT, L_PUMP, L_DRAG, L_EASY, L_NOTES };
static const hint_t hints[] = {
    { "SWING IT LIKE A STICK: FOUR WAYS", L_HIT },
    { "LEFT, RIGHT, FORWARD, BACK", L_FOUR },
    { "HOLD REC AND PLAY: A LOOP", LEARN_REC },
    { "AFTER A HIT, MOVE ON SLOWLY: IT BENDS", L_DRAG },
    { "TAP REC: A HARD STOP. AGAIN: BACK IN", L_CUT },
    { "TWO BOXES, ONE IN EACH HAND", L_PAIR },
    { "PLAY > ROLE PAINT: STRIKE A MATERIAL", L_PAINT },
    { "PLAY > ROLE PUMP: MOVE IT, IT GROWS", L_PUMP },
    { "PLAY > ROLE NOTES: THE TILT IS THE NOTE", L_NOTES },
    { "PLAY > EASY: ANY SWING, THE NEXT STEP", L_EASY },
    { "HOLD THE KNOB: THE LOOPS STOP / GO", LEARN_HOLD },
    { "PRESS THE KNOB: SOUNDS, SETTINGS", LEARN_MENU },
};

// ---------------------------------------------------------------- state
ad_ui_t D;
static uint32_t now_frames;
static int64_t  now_tick;
static grid_t   grid;
static bool     applying, link_up, loops_fixed;
static uint32_t my_id;
static int      hand_now = HAND_AUTO;                 // what HAND comes to
static int      last_pad = PD_LEFT;
static uint8_t  pads_seen;
static float    flash[NPAD], peer_flash;
static int      peer_voice = DV_KICK;
// energy, motion, shake (DWELL's)
static float energy, motion, mfast, shake_s;
static vec3_t acc_prev; static bool acc_have;
static float bend_s;
// CUT: on at once; off on the beat after the tap
static bool cut_on; static int64_t cut_until = -1;
// the other box
typedef struct { bool on; uint32_t id; int role, hand, kick_note; uint8_t seq; bool seq_ok; float age; } peer_ad_t;
static peer_ad_t PEER;
// what this box tells the other: its last three hits (tick, voice, velocity), numbered
typedef struct { int64_t tick; uint8_t voice, vel; } sent_t;
static sent_t sent[3];
static uint8_t sent_seq;
static int64_t loop_sent_until = -1;                  // loop hits up to this tick have been sent ahead
static int led_flash_pad = -1; static float led_l;
// for the tests: the last strike sent to the engine from the other hand (its frame), how many
uint32_t ad_strike_at; int64_t ad_strike_tick; int ad_strikes; uint32_t ad_played_frame;
// ... where the last hit was caught (the sample's frame), its pad, its peak, rise and fall
uint32_t ad_hit_frame; int ad_hit_pad; float ad_hit_peak, ad_hit_rise, ad_hit_fall;
int64_t ad_now_tick(void) { return now_tick; }
// ... and every note event this side sent the engine (the last 64)
nev_t ad_nev[64]; int ad_nevs;
static void note_ev(const nev_t *e) { ad_nev[ad_nevs & 63] = *e; ad_nevs++; eng_note(e); }

static uint32_t fpb_of(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / bpm + 0.5f); }
static float grid_bpm(void) { return grid.fpb ? 60.0f * (float)AUDIO_SR / (float)grid.fpb : 120.0f; }
static int64_t bar_floor(int64_t t) { return eng_floor_div(t, BAR_TICKS) * BAR_TICKS; }

// ---------------------------------------------------------------- the hands' kits
static int pad_kit(int pad)
{
    static const int left_k[NPAD]  = { DV_WHIP, DV_KICK, DV_SNAP, DV_SUB };
    static const int right_k[NPAD] = { DV_HAT, DV_CLANG, DV_GLASS, DV_CREAK };
    static const int one_k[NPAD]   = { DV_HAT, DV_KICK, DV_SNAP, DV_CLANG };
    return hand_now == HAND_LEFT ? left_k[pad] : hand_now == HAND_RIGHT ? right_k[pad] : one_k[pad];
}
static int pad_voice(int pad)
{
    int v = (int)pv[P_FRONT + pad];
    return v >= DV_AUTO ? pad_kit(pad) : v;
}
// the kick's note: the key from F1 to E2 (44 to 82 Hz), TUNE on top
static int kick_note(void) { return 29 + (((int)pv[P_KEY] - 5 + 12) % 12) + (int)pv[P_TUNE]; }

// ---------------------------------------------------------------- the grid (the link's session, always)
static void grid_update(void)
{
    uint32_t o; float f; link_origin(&o, &f);
    grid.fpb = link_fpb(); grid.origin = o; grid.frac = f;
    eng_set_grid(&grid);
    now_tick = eng_tick_at(&grid, now_frames);
}
static int loops_beats(void) { int b = 0; for (int k = 0; k < NLOOP; k++) if (eng_loops[k].bars * 4 > b) b = eng_loops[k].bars * 4; return b; }
static void tempo_set(float bpm)
{
    uint32_t fpb = fpb_of(bpm);
    if (!grid.fpb || fpb == grid.fpb) return;
    float beat = ((float)(int32_t)(now_frames - grid.origin) - grid.frac) / (float)grid.fpb;
    uint32_t origin = now_frames - (uint32_t)(int32_t)(beat * (float)fpb);
    link_set_time(fpb, origin, 4, loops_beats());
    grid_update();
}

// ---------------------------------------------------------------- the loops (AIR's)
enum { R_IDLE, R_PENDING, R_WAIT, R_REC, R_CLOSING };
static struct { int st, slot; uint32_t press_frame; int64_t start, end; } rec;
typedef struct { int64_t tick; uint8_t pad, vel; } prov_t;
static prov_t prov[16];
static int nprov;
static uint32_t loop_serial;

static void loop_param(int k, int v) { applying = true; param_set(P_LOOP1 + k, (float)v); applying = false; }
static void loop_flag(void)
{
    bool any = false;
    for (int k = 0; k < NLOOP; k++) any |= eng_loops[k].bars > 0;
    link_set_flags(any ? LINKF_LOOP : 0);
    if (!any && link_time()->beats && link_time()->owner == my_id) link_set_time(grid.fpb, grid.origin, 4, 0);
}
static void loop_erase(int k)
{
    loop_t *l = &eng_loops[k];
    l->on = 0;
    __atomic_store_n(&l->bars, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&l->n, 0, __ATOMIC_RELEASE);
    l->serial = 0;
    loop_param(k, LP_EMPTY);
}
static void loop_set_on(int k, bool on)
{
    loop_t *l = &eng_loops[k];
    if (!l->bars) return;
    l->on = on;
    loop_param(k, on ? LP_ON : LP_OFF);
    if (on) eng_run = 1;
}
static int loop_newest(void)
{
    int best = -1; uint32_t s = 0;
    for (int k = 0; k < NLOOP; k++) if (eng_loops[k].bars && eng_loops[k].serial > s) { s = eng_loops[k].serial; best = k; }
    return best;
}
static void loop_toggle(int k)
{
    char b[40];
    if (!eng_loops[k].bars) { snprintf(b, sizeof b, "LOOP %d IS EMPTY: HOLD REC AND PLAY", k + 1); toast(b, 1400); return; }
    loop_set_on(k, !eng_loops[k].on);
    snprintf(b, sizeof b, "LOOP %d %s", k + 1, eng_loops[k].on ? "ON" : "OFF"); toast(b, 700);
}
static void loops_erase_all(void)
{
    for (int k = 0; k < NLOOP; k++) loop_erase(k);
    rec.st = R_IDLE; nprov = 0; loop_serial = 0;
    loop_flag();
}
static int record_hit(int64_t tick, int pad, int vel)                // the hit's index in the loop being recorded, or -1
{
    if (rec.st == R_PENDING) { if (nprov < 16) prov[nprov++] = (prov_t){ tick, (uint8_t)pad, (uint8_t)vel }; return -1; }
    if (rec.st != R_WAIT && rec.st != R_REC && rec.st != R_CLOSING) return -1;
    loop_t *l = &eng_loops[rec.slot];
    int64_t rel = tick - rec.start;
    if (rel < 0) return -1;
    if (rec.st == R_CLOSING && tick >= rec.end) return -1;
    if (rel >= BAR_MAX * BAR_TICKS) return -1;
    int n = l->n;
    if (n >= LOOP_HITS) { toast("THE LOOP IS FULL", 1000); return -1; }
    l->hits[n] = (hit_t){ .tick = (uint16_t)rel, .pad = (uint8_t)pad, .vel = (uint8_t)vel, .born = 0, .len = 0 };
    __atomic_store_n(&l->n, n + 1, __ATOMIC_RELEASE);
    return n;
}
static void rec_begin(void)
{
    int k = -1;
    for (int i = 0; i < NLOOP && k < 0; i++) if (!eng_loops[i].bars) k = i;
    if (k < 0) { k = loop_newest(); loop_erase(k); char b[32]; snprintf(b, sizeof b, "LOOP %d AGAIN", k + 1); toast(b, 900); }
    int64_t tp = eng_tick_at(&grid, rec.press_frame), line = bar_floor(tp);
    rec.start = tp - line <= PPB ? line : line + BAR_TICKS;
    rec.slot = k; rec.end = 0;
    loop_t *l = &eng_loops[k];
    __atomic_store_n(&l->n, 0, __ATOMIC_RELEASE);
    l->start = rec.start; l->on = 0; l->bars = 0;
    rec.st = now_tick >= rec.start ? R_REC : R_WAIT;
    for (int i = 0; i < nprov; i++) record_hit(prov[i].tick, prov[i].pad, prov[i].vel);
    nprov = 0;
    eng_run = 1;
}
static void rec_release(void)
{
    int64_t rel = now_tick - rec.start;
    int64_t bars = eng_floor_div(rel + BAR_TICKS / 2, BAR_TICKS);
    if (bars < 1) bars = 1;
    if (bars > BAR_MAX) bars = BAR_MAX;
    rec.end = rec.start + bars * BAR_TICKS;
    rec.st = R_CLOSING;
    loop_t *l = &eng_loops[rec.slot];
    l->serial = ++loop_serial;
    l->on = 1;
    __atomic_store_n(&l->bars, (int)bars, __ATOMIC_RELEASE);
    loop_param(rec.slot, LP_ON);
    eng_run = 1;
    if (!(link_flags_any() & LINKF_LOOP)) link_set_time(grid.fpb, grid.origin, 4, loops_beats());
    loop_flag();
}
static void rec_close(void)
{
    loop_t *l = &eng_loops[rec.slot];
    int bars = (int)((rec.end - rec.start) / BAR_TICKS), len = bars * BAR_TICKS;
    int n = l->n;
    rec.st = R_IDLE;
    if (n == 0) { loop_erase(rec.slot); loop_flag(); toast("NO HITS: NO LOOP", 1200); return; }
    for (int i = 0; i < n; i++) {
        hit_t *h = &l->hits[i];
        while (h->tick >= len) { h->tick = (uint16_t)(h->tick - len); h->born++; }
    }
    coach_done(LEARN_REC);
    char b[32]; snprintf(b, sizeof b, "LOOP %d: %d BAR%s", rec.slot + 1, bars, bars > 1 ? "S" : ""); toast(b, 1000);
}

// REC: a hold records a loop; a tap is CUT (a hard silence now) or, when cut, back in on the next beat
static void rec_logic(const ctl_t *c)
{
    if (c->rec_press) { rec.st = R_PENDING; rec.press_frame = c->frames; nprov = 0; }
    if (rec.st == R_PENDING && c->rec && c->rec_ms >= 300) rec_begin();
    if (rec.st == R_WAIT && now_tick >= rec.start) rec.st = R_REC;
    if (rec.st == R_REC && now_tick - rec.start >= BAR_MAX * BAR_TICKS) { rec.end = rec.start + BAR_MAX * BAR_TICKS; rec.st = R_CLOSING; }
    if (c->rec_release) {
        if (rec.st == R_PENDING) {
            rec.st = R_IDLE; nprov = 0;
            if (!cut_on) { cut_on = true; cut_until = -1; eng_cut = 1; coach_done(L_CUT); }
            else if (cut_until < 0) cut_until = eng_floor_div(now_tick, PPB) * PPB + PPB;   // the next beat
        } else if (rec.st == R_WAIT || rec.st == R_REC) rec_release();
    }
    if (rec.st == R_CLOSING && now_tick >= rec.end) rec_close();
    if (cut_on && cut_until >= 0 && now_tick >= cut_until) { cut_on = false; cut_until = -1; eng_cut = 0; }
}

// ---------------------------------------------------------------- the strokes (0.2: the gyro at 1 kHz)
// Every sample: where up is (the gyro turns it, the accelerometer pulls it back while the box is calm), and the turn
// split three ways against it: PITCH (about the level axis across the box: the front edge down or up), YAW (about the
// vertical: swept round) and ROLL (about the level axis along the box: the wrist's twist). A stroke is a turn in
// pitch and yaw faster than t_on; it is a HIT when it stops sharply: from its peak to half that speed within fall_max,
// and its rise `ratio` times longer than that fall (a drummer's stroke speeds up and stops dead; the hand coming back,
// a sweep or a slow turn speed up and slow down alike). Its pad is its direction while it rose. Three quick strokes
// back and forth along one axis, alike, are a SHAKE: each of its strokes rolls the pad on the grid. After a hit, the
// slow movement that follows DRAGS the sound it started, until the next hit. EASY: every swing counts, any way.
enum { S_IDLE, S_STROKE, S_AFTER };
enum { K_HIT, K_SHAKE, K_SWING };
#define NO_LINE INT64_MIN
static struct {
    int    st;
    vec3_t up;                                  // the room's up in the box's axes (unit)
    bool   up_ok;
    float  wr;                                  // the roll rate now (deg/s): the twist, for PAINT's bend
    float  peak, ms, rise_ms;                   // the stroke: its peak speed, its age, its age at the peak
    bool   falling;
    float  acc_p, acc_y, acc_r;                 // its direction, summed while it rises (weighted by speed)
    float  jolt;                                // the accelerometer's largest |a| - 1 g in it
    float  pitch0, roll0;                       // the tilt when it began (degrees: front edge up, right edge down)
    float  after_ms;                            // since it ended
    float  hit_p, hit_y, hit_peak, since_hit_ms; // the last hit: its direction and peak, and the time since
    int    hit_pad;
    int    sh_axis[3], sh_sign[3], sh_n;        // the shake: the last three strokes
    float  sh_peak[3], sh_len[3], sh_gap[3], sh_idle_ms;
    bool   shaking;
} S = { .up = { 0, 0, 1 }, .hit_pad = -1, .since_hit_ms = 1e6f, .sh_idle_ms = 1e6f };
static struct { float t_on, t_hit, v_full, a_min, fall_max, ratio; } TH;
static int64_t roll_line = NO_LINE, step_line = NO_LINE;   // the last line a roll's or EASY's step took
static uint8_t gid_last;
static uint8_t next_gid(void) { if (++gid_last >= 200) gid_last = 1; return gid_last; }   // live: 1..199; the loops' notes: 200..254

static int velocity(float peak)
{
    float v = sqrtf(clampf((peak - TH.t_hit) / (TH.v_full - TH.t_hit), 0.0f, 1.0f));
    return clampi((int)(12.0f + v * 115.0f + 0.5f), 12, 127);
}
static void thresholds(void)
{
    float sens = pv[P_SENS] * 0.01f, stop = pv[P_STOP] * 0.01f;
    TH.t_hit = (80.0f + (1.0f - sens) * 200.0f) * 1.8f;              // SENSE 50: a hit peaks over 324 deg/s
    TH.v_full = 600.0f + (1.0f - sens) * 800.0f;                       // full velocity at 1000
    TH.t_on = 0.5f * TH.t_hit;
    TH.a_min = pv[P_CONFIRM] >= 1.0f ? 0.15f + (1.0f - sens) * 0.2f : 0.0f;
    TH.fall_max = 70.0f - 40.0f * stop;                                // ms from the peak to half speed: STOP 50, 50 ms
    TH.ratio = 1.0f + 2.5f * stop;                                     // ... and the rise longer by this: STOP 50, 2.25
}
static float tilt_pitch(void) { return asinf(clampf(S.up.y, -1.0f, 1.0f)) * 57.29578f; }    // front edge up: +
static float tilt_roll(void) { return asinf(clampf(-S.up.x, -1.0f, 1.0f)) * 57.29578f; }    // right edge down: +
static int pad_of(float p, float y)
{
    if (fabsf(p) >= fabsf(y)) return (p < 0.0f) != (pv[P_FLIP] >= 1.0f) ? PD_FRONT : PD_BACK;   // the front edge down: FRONT
    return (y > 0.0f) != (pv[P_SIDES] >= 1.0f) ? PD_LEFT : PD_RIGHT;                             // swept round left: LEFT
}

// ---------------------------------------------------------------- the drag (the hand after a hit)
typedef struct { bool on; uint8_t gid; float ang, roll, amt, hold, bend, still_ms, slow_ms; } drag_t;
static drag_t DG;
static struct { bool on; uint8_t target; float amt, hold, bend; } RS;   // the other box (SHAPE) dragging this one's hit
static uint8_t sh_target;                                                // SHAPE: the other box's hit this one drags
static uint8_t last_live_seq;                                            // the number this box's last live hit went out with
static void drag_begin(uint8_t gid) { DG = (drag_t){ .on = true, .gid = gid, .still_ms = 1e3f }; }
static void drag_sample(float gm, float wr, float dt_ms)
{
    if (!DG.on) return;
    // slow, and slow for 60 ms (a pull, not the slow ends of a quick return): it drags
    if (gm > 12.0f && gm < 0.8f * TH.t_on) DG.slow_ms += dt_ms; else DG.slow_ms = 0.0f;
    if (DG.slow_ms >= 60.0f) { DG.ang += gm * dt_ms * 0.001f; DG.still_ms = 0.0f; }
    else DG.still_ms += dt_ms;
    DG.roll += wr * dt_ms * 0.001f;
}
static void drag_tick(void)
{
    DG.amt = clampf(DG.ang / pv[P_REACH], 0.0f, 1.0f);
    float want = DG.on && DG.ang > 4.0f && DG.still_ms < 120.0f ? 1.0f : 0.0f;
    DG.hold += (want - DG.hold) * (want > DG.hold ? 0.5f : 0.065f);
    if (DG.hold < 1e-3f) DG.hold = 0.0f;
    DG.bend = clampf(DG.roll / 45.0f, -1.0f, 1.0f) * pv[P_DBEND];
    float amt = DG.amt, hold = DG.hold, bend = DG.bend;
    D.peer_shapes = 0;
    if (RS.on && DG.gid && RS.target == last_live_seq) { amt = fmaxf(amt, RS.amt); hold = fmaxf(hold, RS.hold); bend += RS.bend; D.peer_shapes = 1; }
    if ((int)pv[P_ROLE] == ROLE_SHAPE) eng_drag_gid = 0;               // this hand drags the other's sound, not its own
    else { eng_drag_gid = DG.gid; eng_drag_amt = amt; eng_drag_hold = hold; eng_drag_bend = bend; }
    eng_drag_depth = pv[P_DRAG] * 0.01f;
    if (DG.amt > 0.5f && DG.gid) coach_done(L_DRAG);
    D.drag = amt; D.hold = hold; D.dbend = bend; D.dragging = DG.on && (amt > 0.02f || hold > 0.1f || D.peer_shapes);
}

// ---------------------------------------------------------------- where a hit goes on the grid
// The sound follows the stop by LAT (it is caught at half speed, up to a tick before the UI sees it: a constant
// latency, not a jitter). On the grid it goes on the nearest line to the stop (less LEAD), or at LAT if that line has
// passed. `after` (a roll, an EASY step): the next free line after the last one taken and still to come, never two
// on one line and never late, and let go if the hand is more than two lines ahead of the grid.
#define LAT_FRAMES  (AUDIO_SR * 12 / 1000)
#define LEAD_FRAMES (AUDIO_SR * 8 / 1000)
static int64_t place(uint32_t frame, uint32_t *at, int64_t *after)
{
    *at = frame + LAT_FRAMES;
    if (!grid.fpb) return 0;
    uint32_t meant = frame - LEAD_FRAMES;
    int64_t rel = (int32_t)(meant - grid.origin);
    int64_t t = eng_floor_div(rel * PPB * 2 - (int64_t)(grid.frac * (float)PPB * 2.0f) + (int64_t)grid.fpb, (int64_t)grid.fpb * 2);
    int q = (int)pv[P_QUANT];
    if (!q && !after) return t;
    int step = q == 1 ? PPB / 2 : PPB / 4;
    int64_t line = eng_floor_div(t + step / 2, step) * step;
    if (after) {                                                       // always on a line of its own, never late
        if (*after != NO_LINE && line <= *after) line = *after + step;
        while ((int32_t)(eng_frame_of(&grid, line) - now_frames) <= AUDIO_BLOCK) line += step;
        if (line - t > 2 * step) return NO_LINE;
        *after = line;
    }
    uint32_t lf = eng_frame_of(&grid, line);
    if ((int32_t)(lf - now_frames) > AUDIO_BLOCK) *at = lf;
    return line;
}

// ---------------------------------------------------------------- the other box: what this one sends
// 16 bytes with the link's packets (link_set_app): 4 (the format), role | hand << 3 | cut << 5, energy, the kick's
// note, the hits' number, then the last three hits (tick's low 16 bits, voice << 4 | velocity / 8; a NOTES box sends
// its notes as voice NDV + its VOICE), this box's material (the tilt's morph x 50). A SHAPE box sends no hits; its
// bytes 5..8 are its drag instead: the other box's hit number it drags, amount and hold (x 255), bend (quarter
// semitones, signed). A new hit pokes the link, so it goes at the next tick (about 10 ms).
static void send_hit(int64_t tick, int voice, int vel, uint8_t gid)
{
    sent[0] = sent[1]; sent[1] = sent[2];
    sent[2] = (sent_t){ tick, (uint8_t)voice, (uint8_t)vel };
    sent_seq++;
    if (gid) last_live_seq = sent_seq;
    link_poke();
}
static void app_update(void)
{
    uint8_t a[16] = { 4, (uint8_t)((int)pv[P_ROLE] | (hand_now << 3) | (cut_on ? 32 : 0)),
                      (uint8_t)clampi((int)(energy * 255.0f + 0.5f), 0, 255), (uint8_t)clampi(kick_note(), 0, 127), sent_seq };
    if ((int)pv[P_ROLE] == ROLE_SHAPE) {
        a[5] = sh_target; a[6] = (uint8_t)clampi((int)(DG.amt * 255.0f + 0.5f), 0, 255); a[7] = (uint8_t)clampi((int)(DG.hold * 255.0f + 0.5f), 0, 255);
        a[8] = (uint8_t)(int8_t)clampi((int)lroundf(DG.bend * 4.0f), -127, 127);
    } else for (int k = 0; k < 3; k++) {
        uint16_t lo = (uint16_t)(sent[k].tick & 0xFFFF);
        a[5 + 3 * k] = (uint8_t)(lo & 0xFF); a[6 + 3 * k] = (uint8_t)(lo >> 8);
        a[7 + 3 * k] = (uint8_t)((sent[k].voice & 15) << 4 | (sent[k].vel >> 3));
    }
    a[14] = (uint8_t)clampi((int)(eng_mat_morph * 50.0f), 0, 255);
    link_set_app(a, 16);
}
// the loops' hits a little ahead (a quarter of a beat), so the other box has them before they are due
#define LOOK_TICKS (PPB / 4)
static void loops_ahead(void)
{
    if (!PEER.on || !eng_run || (int)pv[P_ROLE] != ROLE_DRUM) { loop_sent_until = now_tick + LOOK_TICKS; return; }   // only a drummer sends
    int64_t from = loop_sent_until < now_tick ? now_tick : loop_sent_until + 1, to = now_tick + LOOK_TICKS;
    for (int64_t t = from; t <= to; t++)
        for (int k = 0; k < NLOOP; k++) {
            const loop_t *l = &eng_loops[k];
            if (!l->bars || !l->on || !l->n) continue;
            int len = l->bars * BAR_TICKS;
            int64_t since = t - l->start;
            int rel = (int)(since - eng_floor_div(since, len) * len);
            int32_t cycle = (int32_t)eng_floor_div(since, len);
            for (int i = 0; i < l->n; i++) {
                const hit_t *h = &l->hits[i];
                if (h->pad >= NPAD || eng_hit_tick(l, h) != rel || h->born == cycle) continue;
                send_hit(t, pad_voice(h->pad), h->vel, 0);
            }
        }
    loop_sent_until = to;
}

// ---------------------------------------------------------------- NOTES
// The tilt picks the note: the front edge from 40 degrees down to 40 up runs over SPAN notes of the scale from KEY in
// OCTAVE; the roll past 35 degrees either way an octave down or up. A hit plays the note the tilt pointed at when the
// stroke began. A flick: NOTE LENGTH. Moving on slowly after it (the drag's hold) holds it, and it slides to whatever
// note the tilt points at; still, it ends. Pending note ends and MIDI note ons wait in PN until their frame is near.
static const int8_t scales[NSCALES][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                           { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
static int tilt_idx_last = -1;
static int note_of(int idx, int oct)
{
    int sc = clampi((int)pv[P_SCALE], 0, NSCALES - 1);
    int o = (int)eng_floor_div(idx, 7), d = idx - 7 * o;
    return clampi(12 * ((int)pv[P_OCTAVE] + 1) + (int)pv[P_KEY] + 12 * (o + oct) + scales[sc][d], 0, 127);
}
static int tilt_idx(float pitch, int last)                           // which note of the SPAN the tilt points at
{
    int span = clampi((int)pv[P_SPAN], 1, 32);
    float pos = clampf((pitch + 40.0f) / 80.0f, 0.0f, 0.9999f) * (float)span;
    int idx = (int)pos;
    if (last >= 0 && last < span && pos > (float)last - 0.3f && pos < (float)last + 1.3f) idx = last;   // a little sticky
    return idx;
}
static int oct_of(float roll) { return roll > 35.0f ? 1 : roll < -35.0f ? -1 : 0; }
static int nlen_frames(void) { return (int)(pv[P_NLEN] * (float)AUDIO_SR); }
static int nlen_ticks(void) { return grid.fpb ? clampi((int)(pv[P_NLEN] * (float)AUDIO_SR * (float)PPB / (float)grid.fpb), 1, 65535) : 12; }
// the notes sounding (this box's own, live)
#define NNOTE 4
typedef struct { bool on, held; uint8_t gid; int idx, oct, note; uint32_t at; int lk, li; } note_t;
static note_t NT[NNOTE];
// pending: a note's end, or its MIDI note on, at a frame
typedef struct { bool on; uint8_t kind, note, vel, gid; uint32_t frame; } pend_ui_t;
enum { PN_OFF, PN_MIDI_ON };
#define NPN 24
static pend_ui_t PN[NPN];
static void pn_add(int kind, int note, int vel, uint8_t gid, uint32_t frame)
{
    for (int i = 0; i < NPN; i++) if (!PN[i].on) { PN[i] = (pend_ui_t){ true, (uint8_t)kind, (uint8_t)note, (uint8_t)vel, gid, frame }; return; }
}
static void pn_cancel_off(uint8_t gid) { for (int i = 0; i < NPN; i++) if (PN[i].on && PN[i].kind == PN_OFF && PN[i].gid == gid) PN[i].on = false; }
static void note_off_now(note_t *n, uint32_t at)
{
    nev_t e = { .at = at, .on = 0, .note = (uint8_t)n->note, .gid = n->gid };
    note_ev(&e);
    midi_note(n->note, 0);
    pn_cancel_off(n->gid);
    if (n->lk >= 0) {                                                  // a note held into a loop: its length
        loop_t *l = &eng_loops[n->lk];
        if (n->li < l->n && grid.fpb) l->hits[n->li].len = (uint16_t)clampi((int)((float)(int32_t)(at - n->at) * (float)PPB / (float)grid.fpb), 1, 65535);
    }
    n->on = false;
}
static void note_play(int idx, int oct, int vel, uint32_t at, uint8_t gid, int64_t tick)
{
    bool mono = (int)pv[P_NVOICE] != NV_BELL;
    for (int i = 0; i < NNOTE; i++) if (NT[i].on && mono) note_off_now(&NT[i], at);   // mono: the new note takes over
    int k = 0;
    for (int i = 0; i < NNOTE; i++) if (!NT[i].on) { k = i; break; } else if (NT[i].at < NT[k].at) k = i;
    if (NT[k].on) note_off_now(&NT[k], at);
    int note = note_of(idx, oct);
    NT[k] = (note_t){ .on = true, .gid = gid, .idx = idx, .oct = oct, .note = note, .at = at, .lk = -1, .li = -1 };
    nev_t e = { .at = at, .on = 1, .note = (uint8_t)note, .vel = (uint8_t)vel, .gid = gid, .legato = 0 };
    note_ev(&e);
    pn_add(PN_MIDI_ON, note, vel, gid, at);
    pn_add(PN_OFF, note, 0, gid, at + (uint32_t)nlen_frames());
    int li = record_hit(tick, NOTE_HIT + note, vel);
    if (li >= 0) { NT[k].lk = rec.slot; NT[k].li = li; }
}
static void notes_tick(void)
{
    float pitch = tilt_pitch(), roll = tilt_roll();
    for (int i = 0; i < NNOTE; i++) {
        note_t *n = &NT[i];
        if (!n->on) continue;
        bool dragged = DG.gid == n->gid && (int)pv[P_ROLE] == ROLE_NOTES;
        float hold = dragged ? fmaxf(DG.hold, D.peer_shapes ? RS.hold : 0.0f) : 0.0f;
        if (!n->held && hold > 0.3f) { n->held = true; pn_cancel_off(n->gid); }
        if (n->held) {
            int idx = tilt_idx(pitch, n->idx), oct = oct_of(roll);
            if (idx != n->idx || oct != n->oct) {                       // it slides to where the tilt points
                n->idx = idx; n->oct = oct; n->note = note_of(idx, oct);
                nev_t e = { .at = 0, .on = 1, .note = (uint8_t)n->note, .vel = 90, .gid = n->gid, .legato = 1 };
                note_ev(&e);
            }
            if (hold < 0.1f) note_off_now(n, now_frames);
        }
    }
    // what is due within the next 15 ms goes now, with its own frame
    for (int i = 0; i < NPN; i++) {
        pend_ui_t *p = &PN[i];
        if (!p->on || (int32_t)(p->frame - now_frames) > AUDIO_SR * 15 / 1000) continue;
        if (p->kind == PN_OFF) {
            p->on = false;
            for (int k = 0; k < NNOTE; k++) if (NT[k].on && NT[k].gid == p->gid && !NT[k].held) { note_off_now(&NT[k], p->frame); }
            if (p->gid >= 200 || p->gid == 0) { nev_t e = { .at = p->frame, .on = 0, .note = p->note, .gid = p->gid }; note_ev(&e); midi_note(p->note, 0); }
        } else if ((int32_t)(p->frame - now_frames) <= 0) {
            p->on = false;
            midi_note(p->note, p->vel);
            scene_hit(last_pad, p->vel, SRC_LIVE);
        }
    }
    int idx = tilt_idx(pitch, tilt_idx_last);
    tilt_idx_last = idx;
    D.note = note_of(idx, oct_of(roll));
    D.note_pos = clampf((pitch + 40.0f) / 80.0f, 0.0f, 1.0f);
    D.note_on = 0;
    for (int i = 0; i < NNOTE; i++) D.note_on |= NT[i].on;
}
// the loops' notes, played by this side (the engine plays only the pads): a quarter beat ahead, with exact frames;
// their gids are 200..254 (a live drag never reaches them)
static int64_t nloop_sent = -1;
static uint8_t loop_gid = 200;
static void note_loops_ahead(void)
{
    if (!eng_run || !grid.fpb) { nloop_sent = now_tick + LOOK_TICKS; return; }
    int64_t from = nloop_sent < now_tick ? now_tick : nloop_sent + 1, to = now_tick + LOOK_TICKS;
    for (int64_t t = from; t <= to; t++)
        for (int k = 0; k < NLOOP; k++) {
            const loop_t *l = &eng_loops[k];
            if (!l->bars || !l->on || !l->n) continue;
            int len = l->bars * BAR_TICKS;
            int64_t since = t - l->start;
            int rel = (int)(since - eng_floor_div(since, len) * len);
            int32_t cycle = (int32_t)eng_floor_div(since, len);
            for (int i = 0; i < l->n; i++) {
                const hit_t *h = &l->hits[i];
                if (h->pad < NOTE_HIT || eng_hit_tick(l, h) != rel || h->born == cycle) continue;
                if (++loop_gid > 254) loop_gid = 200;
                int note = h->pad - NOTE_HIT, nl = h->len ? h->len : nlen_ticks();
                uint32_t at = eng_frame_of(&grid, t);
                nev_t e = { .at = at, .on = 1, .note = (uint8_t)note, .vel = h->vel, .gid = loop_gid };
                note_ev(&e);
                pn_add(PN_MIDI_ON, note, h->vel, loop_gid, at);
                pn_add(PN_OFF, note, 0, loop_gid, eng_frame_of(&grid, t + nl));
            }
        }
    nloop_sent = to;
}

// ---------------------------------------------------------------- EASY
// The patterns, sixteen steps a bar: the pads for DRUM and PAINT (the hand's kit: L the LEFT pad, F FRONT, R RIGHT,
// B BACK), and for NOTES the notes of the scale from where the tilt points (0 that note, 7 an octave up).
#define L_ PD_LEFT
#define F_ PD_FRONT
#define R_ PD_RIGHT
#define B_ PD_BACK
#define __ -1
static const int8_t drum_pat[NPATTERN][16] = {
    { L_, __, F_, __, R_, __, F_, __, L_, __, F_, __, R_, __, F_, __ },   // FOUR
    { L_, __, F_, __, R_, __, F_, L_, __, L_, F_, __, R_, __, F_, B_ },   // BACKBEAT
    { L_, __, F_, __, R_, __, F_, R_, __, R_, F_, L_, __, R_, __, F_ },   // BREAK
    { L_, __, F_, __, F_, __, F_, __, R_, __, F_, __, F_, __, B_, __ },   // HALF
    { L_, __, __, L_, F_, __, R_, __, __, L_, F_, __, R_, __, F_, F_ },   // BOUNCE
    { L_, F_, F_, F_, R_, F_, F_, F_, L_, F_, F_, F_, R_, F_, R_, R_ },   // ROLL
};
static const int8_t note_pat[NPATTERN][16] = {
    {  0, __, __, __,  0, __, __, __,  0, __, __, __,  0, __, __, __ },
    {  0, __,  0, __,  4, __,  4, __,  0, __,  0, __,  6, __,  4, __ },
    {  0, __,  2, __,  4, __,  7, __,  4, __,  2, __,  0, __, __, __ },
    {  0, __, __, __, __, __, __, __,  4, __, __, __, __, __, __, __ },
    {  0, __, __,  0, __, __,  0, __,  3, __, __,  3, __,  4, __, __ },
    {  0,  2,  4,  7,  4,  2,  0,  2,  4,  7,  9,  7,  4,  2,  0, __ },
};
#undef L_
#undef F_
#undef R_
#undef B_
#undef __
static int step_i;
static int64_t groove_until = -1, groove_sent = -1;
static void groove_poke(void) { if (grid.fpb) groove_until = now_tick + BAR_TICKS; }
static int step_vel(int k) { return (k & 7) == 0 ? 112 : (k & 3) == 0 ? 100 : 80; }
static void play_step(int k, int vel, uint32_t at, int64_t line)
{
    int role = (int)pv[P_ROLE], pat = clampi((int)pv[P_PATTERN], 0, NPATTERN - 1);
    if (role == ROLE_NOTES) {
        int d = note_pat[pat][k];
        if (d < 0) return;
        note_play(tilt_idx_last < 0 ? 0 : tilt_idx_last + d, oct_of(tilt_roll()), vel, at, next_gid(), line);
        send_hit(line, NDV + (int)pv[P_NVOICE], vel, 0);
        return;
    }
    int pad = drum_pat[pat][k];
    if (pad < 0) return;
    trig_t t = { .pad = (uint8_t)pad, .vel = (uint8_t)vel, .src = SRC_LIVE, .gid = 0, .at = at };
    eng_trigger(&t);
    record_hit(line, pad, vel);
    if (role == ROLE_DRUM) send_hit(line, pad_voice(pad), vel, 0);
}
static void easy_step(int vel, uint32_t frame)                     // STEP: the pattern's next step, on the next free line
{
    int role = (int)pv[P_ROLE], pat = clampi((int)pv[P_PATTERN], 0, NPATTERN - 1);
    int k = step_i;
    for (int i = 0; i < 16; i++, k = (k + 1) & 15) if ((role == ROLE_NOTES ? note_pat[pat][k] : drum_pat[pat][k]) >= 0) break;
    uint32_t at;
    int64_t line = place(frame, &at, &step_line);
    if (line == NO_LINE) return;
    play_step(k, vel, at, line);
    D.step = k;
    step_i = (k + 1) & 15;
}
static void groove_tick(void)                                     // GROOVE: the pattern plays by itself while you move
{
    int role = (int)pv[P_ROLE];
    if ((int)pv[P_EASY] != EASY_GROOVE || role == ROLE_PUMP || role == ROLE_SHAPE || !grid.fpb) { groove_sent = now_tick + LOOK_TICKS; return; }
    if (motion > 0.4f) groove_poke();
    const int64_t step = PPB / 4;
    int64_t from = groove_sent < now_tick ? now_tick : groove_sent + 1, to = now_tick + LOOK_TICKS;
    for (int64_t L = eng_floor_div(from + step - 1, step) * step; L <= to; L += step) {
        if (L >= groove_until) continue;
        int k = (int)(eng_floor_div(L, step) & 15);
        play_step(k, step_vel(k), eng_frame_of(&grid, L), L);
        D.step = k;
    }
    groove_sent = to;
}

// ---------------------------------------------------------------- what a stroke plays
static void hit_pad(int pad, int vel, uint32_t frame, uint8_t gid, int64_t *after)
{
    int role = (int)pv[P_ROLE];
    uint32_t at;
    int64_t tick = place(frame, &at, after);
    if (tick == NO_LINE) return;
    if (role == ROLE_NOTES) {
        int idx = tilt_idx(S.pitch0, tilt_idx_last);
        note_play(idx, oct_of(S.roll0), vel, at, gid ? gid : next_gid(), tick);
        send_hit(tick, NDV + (int)pv[P_NVOICE], vel, gid);
    } else {
        trig_t t = { .pad = (uint8_t)pad, .vel = (uint8_t)vel, .src = SRC_LIVE, .gid = gid, .at = at };
        eng_trigger(&t);
        record_hit(tick, pad, vel);
        if (role == ROLE_DRUM) send_hit(tick, pad_voice(pad), vel, gid);
    }
    if (gid) drag_begin(gid);
    last_pad = pad;
    coach_done(L_HIT);
    pads_seen |= (uint8_t)(1u << pad);
    if (pads_seen == 0x0F) coach_done(L_FOUR);
    if (role == ROLE_PAINT) coach_done(L_PAINT);
    if (role == ROLE_NOTES) coach_done(L_NOTES);
    ui_awake();
}
static void play_stroke(int kind, int pad, int vel, uint32_t frame)
{
    int role = (int)pv[P_ROLE], easy = (int)pv[P_EASY];
    if (role == ROLE_PUMP || role == ROLE_SHAPE) return;               // PUMP: the movement is energy; SHAPE: a drag
    if (easy == EASY_STEP) { easy_step(vel, frame); coach_done(L_EASY); return; }
    if (easy == EASY_GROOVE) { coach_done(L_EASY); return; }          // GROOVE: every stroke keeps it going
    if (kind == K_SHAKE) { hit_pad(pad, vel, frame, 0, &roll_line); return; }   // a roll: the next free line, no drag
    hit_pad(pad, vel, frame, next_gid(), NULL);
}

// ---------------------------------------------------------------- the detector
// a stroke ended: was it a shake's (three quick strokes back and forth along one axis, alike, none stopped sharply:
// a shake swings like a sine; strokes that stop hard back and forth are hits, one after the other)?
static void shake_push(int axis, int sign, float peak, float len_ms, bool sharp)
{
    if (sharp) { S.sh_n = 0; S.shaking = false; S.sh_idle_ms = 0.0f; return; }
    for (int k = 0; k < 2; k++) { S.sh_axis[k] = S.sh_axis[k + 1]; S.sh_sign[k] = S.sh_sign[k + 1]; S.sh_peak[k] = S.sh_peak[k + 1]; S.sh_len[k] = S.sh_len[k + 1]; S.sh_gap[k] = S.sh_gap[k + 1]; }
    S.sh_axis[2] = axis; S.sh_sign[2] = sign; S.sh_peak[2] = peak; S.sh_len[2] = len_ms; S.sh_gap[2] = S.sh_idle_ms;
    S.sh_idle_ms = 0.0f;
    if (S.sh_n < 3) S.sh_n++;
    bool ok = S.sh_n >= 3;
    for (int k = 0; ok && k < 3; k++) ok = S.sh_axis[k] == axis && S.sh_len[k] < 160.0f;
    for (int k = 1; ok && k < 3; k++) ok = S.sh_sign[k] != S.sh_sign[k - 1] && S.sh_gap[k] < 200.0f && S.sh_peak[k] > 0.55f * S.sh_peak[k - 1] && S.sh_peak[k] < 1.8f * S.sh_peak[k - 1];
    if (ok) S.shaking = true;
    else if (S.shaking && !(axis == S.sh_axis[1] && sign != S.sh_sign[1] && S.sh_gap[2] < 250.0f)) S.shaking = false;
}
static void stroke_end(uint32_t frame)
{
    bool easy = pv[P_EASY] >= 1.0f;
    float n = sqrtf(S.acc_p * S.acc_p + S.acc_y * S.acc_y + S.acc_r * S.acc_r) + 1e-9f;
    float dp = S.acc_p / n, dy = S.acc_y / n, dr = S.acc_r / n;
    float fall = S.ms - S.rise_ms, rise = S.rise_ms;
    int axis = fabsf(dp) >= fabsf(dy) ? 0 : 1;
    if (easy && fabsf(dr) > fabsf(dp) && fabsf(dr) > fabsf(dy)) axis = 2;
    int sign = (axis == 0 ? dp : axis == 1 ? dy : dr) > 0.0f ? 1 : -1;
    bool sharp = fall <= TH.fall_max && fall * TH.ratio <= rise;
    bool strong = S.peak >= TH.t_hit, jolt = S.jolt >= TH.a_min;
    shake_push(axis, sign, S.peak, S.ms, sharp && strong);
    if (S.peak >= TH.t_on) groove_poke();
    int vel = velocity(S.peak), pad = pad_of(dp, dy);
    bool back = S.hit_pad >= 0 && S.since_hit_ms < 300.0f && (dp * S.hit_p + dy * S.hit_y) < -0.5f && S.peak < 0.8f * S.hit_peak;
    D.stop_ok = sharp && strong;
    if (S.shaking) { play_stroke(K_SHAKE, S.hit_pad >= 0 ? S.hit_pad : pad, vel, frame); return; }
    if (easy) { if (S.peak >= 0.8f * TH.t_hit) play_stroke(K_SWING, pad, vel, frame); return; }
    if (!(sharp && strong && jolt) || back) return;
    S.hit_p = dp; S.hit_y = dy; S.hit_peak = S.peak; S.since_hit_ms = 0.0f; S.hit_pad = pad;
    ad_hit_frame = frame; ad_hit_pad = pad; ad_hit_peak = S.peak; ad_hit_rise = rise; ad_hit_fall = fall;
    play_stroke(K_HIT, pad, vel, frame);
}
// one sample: a, g in the screen's axes (g, deg/s); frame: when it was taken; dt_ms: since the last
static void stroke_sample(const vec3_t *a, const vec3_t *g, uint32_t frame, float dt_ms)
{
    const float D2R = 0.0174533f, dt = dt_ms * 0.001f;
    bool easy = pv[P_EASY] >= 1.0f;
    // up: turned with the box (a vector fixed in the room turns the other way in the box's axes), and pulled towards
    // the accelerometer while the box is calm
    vec3_t u = S.up;
    float wx = g->x * D2R, wy = g->y * D2R, wz = g->z * D2R;
    vec3_t c = { wy * u.z - wz * u.y, wz * u.x - wx * u.z, wx * u.y - wy * u.x };
    u.x -= c.x * dt; u.y -= c.y * dt; u.z -= c.z * dt;
    float am = sqrtf(a->x * a->x + a->y * a->y + a->z * a->z), gm = sqrtf(g->x * g->x + g->y * g->y + g->z * g->z);
    if (!S.up_ok) { if (am > 0.5f) { u = (vec3_t){ a->x / am, a->y / am, a->z / am }; S.up_ok = true; } }
    else if (fabsf(am - 1.0f) < 0.1f && gm < 60.0f) { float k = fminf(0.02f * dt_ms, 1.0f); u.x += k * (a->x / am - u.x); u.y += k * (a->y / am - u.y); u.z += k * (a->z / am - u.z); }
    float un = sqrtf(u.x * u.x + u.y * u.y + u.z * u.z);
    if (un > 1e-6f) { u.x /= un; u.y /= un; u.z /= un; } else u = (vec3_t){ 0, 0, 1 };
    S.up = u;
    // the level axis along the box (its y, levelled: a roll of the wrist does not move it) and across it (along x up);
    // with the box upright (its y near the vertical), its x levelled instead
    vec3_t f = { -u.y * u.x, 1.0f - u.y * u.y, -u.y * u.z }, r;
    float fn = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    if (fn > 0.35f) {
        f.x /= fn; f.y /= fn; f.z /= fn;
        r = (vec3_t){ f.y * u.z - f.z * u.y, f.z * u.x - f.x * u.z, f.x * u.y - f.y * u.x };
    } else {
        r = (vec3_t){ 1.0f - u.x * u.x, -u.x * u.y, -u.x * u.z };
        float rn = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z) + 1e-9f;
        r.x /= rn; r.y /= rn; r.z /= rn;
        f = (vec3_t){ u.y * r.z - u.z * r.y, u.z * r.x - u.x * r.z, u.x * r.y - u.y * r.x };
    }
    float wp = g->x * r.x + g->y * r.y + g->z * r.z;                // pitch: the front edge up +
    float wyw = g->x * u.x + g->y * u.y + g->z * u.z;               // yaw: round to the left +
    float wr = g->x * f.x + g->y * f.y + g->z * f.z;                // roll: the right edge down +
    S.wr = wr;
    float sp = easy ? gm : sqrtf(wp * wp + wyw * wyw);
    drag_sample(gm, wr, dt_ms);
    S.since_hit_ms += dt_ms; S.sh_idle_ms += dt_ms;
    if (S.sh_idle_ms > 250.0f) S.shaking = false;
    float jo = fabsf(am - 1.0f);
    switch (S.st) {
    case S_IDLE:
        if (sp > TH.t_on) {
            S.st = S_STROKE; S.ms = 0.0f; S.rise_ms = 0.0f; S.peak = sp; S.falling = false;
            S.acc_p = wp * sp; S.acc_y = wyw * sp; S.acc_r = wr * sp; S.jolt = jo;
            S.pitch0 = tilt_pitch(); S.roll0 = tilt_roll();
        }
        break;
    case S_STROKE:
        S.ms += dt_ms;
        if (jo > S.jolt) S.jolt = jo;
        if (sp > S.peak) { S.peak = sp; S.rise_ms = S.ms; S.falling = false; }
        if (!S.falling) { S.acc_p += wp * sp; S.acc_y += wyw * sp; S.acc_r += wr * sp; if (sp < 0.92f * S.peak) S.falling = true; }
        if (sp <= 0.5f * S.peak) { stroke_end(frame); S.st = S_AFTER; S.after_ms = 0.0f; }
        else if (S.ms > 600.0f) { S.st = S_AFTER; S.after_ms = 0.0f; }   // a long sweep: no stroke
        break;
    default: {                                                          // ready again once slow, or turned back
        S.after_ms += dt_ms;
        float d = wp * S.acc_p + wyw * S.acc_y + (easy ? wr * S.acc_r : 0.0f);
        if (S.after_ms >= 15.0f && (sp < 0.6f * TH.t_on || d < 0.0f)) S.st = S_IDLE;
        break;
    }
    }
}
static void strokes_tick(const ctl_t *c)
{
    thresholds();
    if (!c->has_imu || c->frozen) { S.st = S_IDLE; D.gest = 0; return; }
    int n = c->nburst;
    if (c->gburst && n > 0)
        for (int i = 0; i < n; i++) stroke_sample(&c->burst[i], &c->gburst[i], c->frames - (uint32_t)((n - 1 - i) * (AUDIO_SR / 1000)), 1.0f);
    else stroke_sample(&c->acc, &c->gyro, c->frames, 10.0f);          // no gyro at 1 kHz: once a tick
    D.gest = S.st == S_STROKE ? 1 : S.st == S_AFTER ? 2 : 0;
}

// ---------------------------------------------------------------- the other box: what this one hears
static void peer_tick(float dt)
{
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    const link_peer_t *p = NULL;
    for (int i = 0; i < np; i++) if (!strncmp(pr[i].name, "AIRDWELL", 8) && pr[i].app_n >= 16 && pr[i].app[0] == 4) { p = &pr[i]; break; }
    if (!p) { if (PEER.on) toast("THE OTHER HAND IS GONE", 900); PEER.on = false; PEER.seq_ok = false; RS.on = false; return; }
    if (!PEER.on || PEER.id != p->id) { PEER.seq_ok = false; toast("THE OTHER HAND IS HERE", 900); coach_done(L_PAIR); }
    PEER.on = true; PEER.id = p->id; PEER.age = 0.0f; (void)dt;
    PEER.role = p->app[1] & 7; PEER.hand = (p->app[1] >> 3) & 3; PEER.kick_note = p->app[3];
    int role = (int)pv[P_ROLE];
    bool pair = pv[P_PAIR] >= 1.0f;
    // the other box is SHAPE: its drag, for this box's last live hit
    RS.on = PEER.role == ROLE_SHAPE && pair;
    if (RS.on) { RS.target = p->app[5]; RS.amt = (float)p->app[6] / 255.0f; RS.hold = (float)p->app[7] / 255.0f; RS.bend = (float)(int8_t)p->app[8] * 0.25f; }
    uint8_t seq = p->app[4];
    if (!PEER.seq_ok) { PEER.seq = seq; PEER.seq_ok = true; return; }   // the hits it sent before we met are past
    int fresh = (uint8_t)(seq - PEER.seq);
    PEER.seq = seq;
    if (fresh <= 0 || PEER.role == ROLE_SHAPE) return;
    if (fresh > 3) fresh = 3;
    for (int k = 3 - fresh; k < 3; k++) {
        uint16_t lo = (uint16_t)(p->app[5 + 3 * k] | p->app[6 + 3 * k] << 8);
        int64_t tick = now_tick + (int16_t)(lo - (uint16_t)(now_tick & 0xFFFF));   // the nearest tick with those low bits
        int voice = p->app[7 + 3 * k] >> 4, vel = ((p->app[7 + 3 * k] & 15) << 3) | 4;
        peer_flash = 1.0f; peer_voice = voice;
        scene_hit(-1, vel, SRC_REMOTE);
        if ((role == ROLE_PAINT || role == ROLE_PUMP) && pair) {
            int sv = voice < DV_OFF ? voice : DV_GLASS;                    // a note strikes like glass
            ad_strike_at = eng_frame_of(&grid, tick); ad_strike_tick = tick; ad_strikes++;
            eng_strike(ad_strike_at, vel, sv);
        }
    }
    if (role == ROLE_SHAPE && pair) { sh_target = seq; drag_begin(0); }   // this hand now drags the other's newest hit
}

// HAND AUTO: with another AIRDWELLER around, the lower link id is the LEFT hand
static void hand_tick(void)
{
    int h = (int)pv[P_HAND];
    if (h != HAND_AUTO) hand_now = h;
    else hand_now = PEER.on ? (my_id < PEER.id ? HAND_LEFT : HAND_RIGHT) : HAND_AUTO;
}

// ---------------------------------------------------------------- energy, motion, shake (DWELL's)
static void energy_tick(const ctl_t *c)
{
    float m = 0.0f, jerk = 0.0f;
    if (c->has_imu && !c->frozen) {
        float am = sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z);
        m = clampf((c->gyro_mag - 25.0f) / 350.0f, 0.0f, 1.0f) + 0.5f * clampf((fabsf(am - 1.0f) - 0.12f) / 0.8f, 0.0f, 1.0f);
        m = clampf(m, 0.0f, 1.0f);
        if (acc_have) { float dx = c->acc.x - acc_prev.x, dy = c->acc.y - acc_prev.y, dz = c->acc.z - acc_prev.z; jerk = sqrtf(dx * dx + dy * dy + dz * dz); }
        acc_prev = c->acc; acc_have = true;
    } else acc_have = false;
    motion += (m - motion) * 0.3f;
    mfast += (m - mfast) * (m > mfast ? 0.5f : 0.08f);
    float si = clampf((jerk - 0.35f) / 1.0f, 0.0f, 1.0f);
    shake_s += (si - shake_s) * (si > shake_s ? 0.06f : 0.015f);
    if (shake_s < 1e-4f) shake_s = 0.0f;
    float sk = shake_s * pv[P_SHAKE] * 0.01f;
    energy += c->dt * (motion / pv[P_CLIMB] - (1.0f - motion) / pv[P_FALL] + sk / pv[P_CLIMB]);
    energy = clampf(energy, 0.0f, 1.0f);
    if ((int)pv[P_ROLE] == ROLE_PUMP && energy > 0.5f) coach_done(L_PUMP);
    if (motion > 0.2f || shake_s > 0.2f) ui_awake();
}

// ---------------------------------------------------------------- the material, from the hand (PAINT and PUMP)
static void material_tick(const ctl_t *c)
{
    int role = (int)pv[P_ROLE];
    // tilt (forward and back) moves through the materials from MATERIAL; the roll is brightness; twisting bends
    float morph = pv[P_MAT];
    if (c->has_imu && role != ROLE_DRUM) morph += pv[P_MORPH] * 0.01f * clampf(-c->lean_y / 30.0f, -1.5f, 1.5f);
    eng_mat_morph = clampf(morph, 0.0f, (float)(NMAT - 1));
    float tw = c->has_imu && !c->frozen && role != ROLE_DRUM ? clampf(S.wr / 300.0f, -1.0f, 1.0f) : 0.0f;   // the wrist's twist
    bend_s += (tw * pv[P_BEND] - bend_s) * (fabsf(tw) > 0.05f ? 0.3f : 0.08f);
    eng_bend = bend_s;
    float br = pv[P_BRIGHT] * 0.01f + (c->has_imu && role != ROLE_DRUM ? c->lean_x / 60.0f : 0.0f);
    eng_mat_bright = clampf(br, 0.0f, 1.0f);
    eng_mat_ring = pv[P_RING];
    eng_mat_chord = (int)pv[P_CHORD];
    // its pitch: the other hand's kick (or this box's own), or the key, times RATIO
    int note = (pv[P_TUNETO] == TT_KICK && PEER.on && PEER.role == ROLE_DRUM) ? PEER.kick_note : kick_note();
    float base = pv[P_TUNETO] == TT_KICK ? midi_hz((float)note) : midi_hz((float)(48 + (int)pv[P_KEY])) * 0.25f;
    eng_mat_hz = base * ratio_v[clampi((int)pv[P_RATIO], 0, 5)];
    // the bow: PUMP's energy, PAINT's movement now; a shake scrapes
    float sk = shake_s * pv[P_SHAKE] * 0.01f;
    eng_bow = role == ROLE_PUMP ? energy : role == ROLE_PAINT ? 0.5f * clampf(mfast * 1.5f, 0.0f, 1.0f) : 0.0f;
    eng_creak = role == ROLE_DRUM ? 0.0f : sk * pv[P_CREAK] * 0.01f;
    eng_mat_level = role == ROLE_PUMP ? expf(0.1151293f * pv[P_REST] * (1.0f - energy)) : 1.0f;
    eng_duck = PEER.on && pv[P_PAIR] >= 1.0f ? pv[P_DUCK] * 0.01f : 0.0f;
}

// ---------------------------------------------------------------- the link
static void link_start(void)
{
    link_init("AIRDWELL", 1);
    link_boot_time(fpb_of(pv[P_TEMPO]), 0, 4);
    link_boot_harm((int)pv[P_KEY], (int)pv[P_SCALE], 0, 0, 0);
    link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    uint8_t mac[6]; plat_link_mac(mac);
    my_id = (uint32_t)mac[2] << 24 | (uint32_t)mac[3] << 16 | (uint32_t)mac[4] << 8 | mac[5];
    link_up = true;
}
static void link_follow(void)
{
    if (link_time_news()) {
        uint32_t fpb = link_fpb();
        int bpm = fpb ? (int)(60.0f * (float)AUDIO_SR / (float)fpb + 0.5f) : 0;
        if (bpm && bpm != (int)pv[P_TEMPO]) { applying = true; param_set(P_TEMPO, (float)clampi(bpm, 50, 200)); applying = false; }
    }
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        if ((int)pv[P_KEY] != h->key % 12) { applying = true; param_set(P_KEY, (float)(h->key % 12)); applying = false; }
        if (h->scale < NSCALES && (int)pv[P_SCALE] != h->scale) { applying = true; param_set(P_SCALE, (float)h->scale); applying = false; }
    }
}

// ---------------------------------------------------------------- what sounded: MIDI out, the screen, the LED
static const int gm_of[NDV] = { 36, 38, 49, 42, 81, 35, 39, 54 };
static void sounded(const played_t *p)
{
    ad_played_frame = p->frame;
    if (p->pad < NPAD) { flash[p->pad] = 1.0f; scene_hit(p->pad, p->vel, p->src); }
    if (p->voice < NDV - 1) {                                          // DV_OFF sends nothing
        midi_note_ch(10, gm_of[p->voice], p->vel);
        midi_note_ch(10, gm_of[p->voice], 0);
    }
    led_flash_pad = p->pad; led_l = 1.0f;
}
static void led_tick(void)
{
    static const uint8_t role_rgb[NROLE][3] = { { 255, 40, 120 }, { 80, 200, 255 }, { 255, 160, 20 }, { 120, 255, 80 }, { 200, 120, 255 } };
    const uint8_t *c = role_rgb[clampi((int)pv[P_ROLE], 0, NROLE - 1)];
    float beat = grid.fpb ? 1.0f - (float)(now_tick % PPB) / (float)PPB : 0.0f;
    float l = cut_on ? 0.02f : 0.08f + 0.12f * beat * beat + 0.5f * led_l + 0.4f * energy * ((int)pv[P_ROLE] == ROLE_PUMP);
    led_l *= 0.8f;
    l = clampf(l, 0.0f, 1.0f);
    led_rgb((uint8_t)(c[0] * l), (uint8_t)(c[1] * l), (uint8_t)(c[2] * l));
}

// ---------------------------------------------------------------- the core's calls
static void controls(void)
{
    eng_role = (int)pv[P_ROLE];
    for (int p = 0; p < NPAD; p++) eng_pad_voice[p] = pad_voice(p);
    static const int8_t spread[NPAD] = { 0, 0, 0, 0 };
    for (int p = 0; p < NPAD; p++) eng_pad_tune[p] = pv[P_TUNE] + (float)spread[p];
    eng_kick_hz = midi_hz((float)kick_note());
    eng_drum_decay = pv[P_DECAY];
    eng_squeeze = pv[P_SQUEEZE] * 0.01f;
    eng_drive = pv[P_DRIVE] * 0.01f;
    eng_tone = pv[P_TONE] * 0.01f;
    int es = clampi((int)pv[P_ESYNC], 0, 4);
    eng_echo_s = es && grid.fpb ? clampf(esync_beats[es] * 60.0f / grid_bpm(), 0.02f, 1.6f) : pv[P_ECHO];
    eng_echo_fb = pv[P_EFB] * 0.01f;
    eng_echo_mix = pv[P_EMIX] * 0.01f;
    eng_room = pv[P_ROOM] * 0.01f;
    eng_room_size = pv[P_RSIZE] * 0.01f;
    eng_level = pv[P_LEVEL] * 0.01f;
    eng_note_voice = (int)pv[P_NVOICE];
    eng_note_glide = pv[P_GLIDE];
}

static void ui_fill(void)
{
    D.role = (int)pv[P_ROLE]; D.hand = hand_now; D.paired = PEER.on; D.peer_role = PEER.role; D.peer_hand = PEER.hand;
    for (int p = 0; p < NPAD; p++) { D.pad_voice[p] = pad_voice(p); D.flash[p] = flash[p]; flash[p] *= 0.85f; }
    D.peer_flash = peer_flash; peer_flash *= 0.85f; D.peer_voice = peer_voice;
    D.beat_phase = grid.fpb ? (float)(eng_floor_div(now_tick, 1) % PPB) / (float)PPB : 0.0f;
    D.beat_in_bar = (int)(eng_floor_div(now_tick, PPB) & 3);
    D.bpm = grid_bpm();
    D.rec = rec.st == R_WAIT ? 1 : rec.st == R_REC ? 2 : rec.st == R_CLOSING ? 3 : 0;
    D.rec_slot = rec.slot;
    for (int k = 0; k < NLOOP; k++) {
        const loop_t *l = &eng_loops[k];
        D.loop_bars[k] = l->bars; D.loop_on[k] = l->on;
        int len = l->bars * BAR_TICKS;
        D.loop_pos[k] = len ? (float)(now_tick - l->start - eng_floor_div(now_tick - l->start, len) * len) / (float)len : 0.0f;
    }
    D.energy = energy; D.motion = motion; D.shake = shake_s;
    D.morph = eng_mat_morph; D.bend = eng_bend; D.mat_hz = eng_mat_hz; D.kick_hz = eng_kick_hz; D.chord = eng_mat_chord;
    D.mat_lvl = eng_mat_lvl; D.cut = cut_on; D.last_pad = last_pad; D.key = (int)pv[P_KEY];
    D.easy = (int)pv[P_EASY]; D.pattern = (int)pv[P_PATTERN]; D.scale = (int)pv[P_SCALE]; D.span = (int)pv[P_SPAN]; D.octave = (int)pv[P_OCTAVE];
}

static void tick(const ctl_t *c)
{
    now_frames = c->frames;
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    grid_update();
    if (!loops_fixed) { loops_fixed = true; for (int k = 0; k < NLOOP; k++) loop_param(k, LP_EMPTY); }
    peer_tick(c->dt);
    hand_tick();
    energy_tick(c);
    D.gyro = c->gyro;
    strokes_tick(c);
    rec_logic(c);
    loops_ahead();
    note_loops_ahead();
    groove_tick();
    drag_tick();
    notes_tick();
    app_update();
    material_tick(c);
    controls();
    played_t pl;
    while (eng_played_pop(&pl)) sounded(&pl);
    // the stick: up, right, down, left toggle loops 1..4
    static int stick_was = -1;
    int sd = -1;
    if (c->has_joy) {
        if (c->jy > 0.7f) sd = 0; else if (c->jx > 0.7f) sd = 1; else if (c->jy < -0.7f) sd = 2; else if (c->jx < -0.7f) sd = 3;
    }
    if (sd >= 0 && sd != stick_was) { loop_toggle(sd); coach_done(LEARN_JOY); }
    stick_was = sd;
    ui_fill();
    led_tick();
}

static void changed(int p)
{
    if (applying) return;
    if (p >= P_LOOP1 && p <= P_LOOP4) {
        int k = p - P_LOOP1;
        if (!eng_loops[k].bars) { if ((int)pv[p] != LP_EMPTY) loop_param(k, LP_EMPTY); }
        else if ((int)pv[p] == LP_EMPTY) loop_param(k, eng_loops[k].on ? LP_ON : LP_OFF);
        else loop_set_on(k, (int)pv[p] == LP_ON);
    }
    if (!link_up) return;
    if (p == P_TEMPO) {
        if (link_flags_any() & LINKF_LOOP && !(eng_loops[0].bars || eng_loops[1].bars || eng_loops[2].bars || eng_loops[3].bars)) {
            toast("A LOOP ELSEWHERE KEEPS THE TEMPO", 1200);
            applying = true; param_set(P_TEMPO, roundf(grid_bpm())); applying = false;
        } else tempo_set(pv[P_TEMPO]);
    }
    if (p == P_KEY || p == P_SCALE) { const link_harm_t *h = link_harm(); link_set_harm((int)pv[P_KEY], (int)pv[P_SCALE], h->prog, h->hold, h->bars, -1); }
    if (p == P_LINK || p == P_GROUP || p == P_RANGE) {
        link_enable(false, 1, 1);
        link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    }
}

static void action(int p)
{
    switch (p) {
    case P_ERASE_LAST: { int k = loop_newest(); if (k >= 0) { loop_erase(k); loop_flag(); toast("THE NEWEST LOOP IS GONE", 900); } else toast("NO LOOPS", 800); break; }
    case P_ERASE_ALL: loops_erase_all(); toast("EVERY LOOP IS GONE", 900); break;
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

// the knob held: every loop stops, or goes again
static bool hold(void)
{
    bool any = false;
    for (int k = 0; k < NLOOP; k++) any |= eng_loops[k].bars > 0;
    if (!any) return false;
    eng_run = !eng_run;
    toast(eng_run ? "LOOPS GO" : "LOOPS STOP", 700);
    return true;
}

static void init(void)
{
    eng_init();
    for (int k = 0; k < NLOOP; k++) eng_loops[k] = (loop_t){ 0 };
    D.hand = HAND_AUTO;
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
    .name = "AIRDWELLER", .tagline = "TWO HANDS, ONE INSTRUMENT",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .burst_gyro = true, .looper = false, .knob = NULL, .rec_button_off = false,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = NULL, .saver = scene_saver, .hold = hold,
};

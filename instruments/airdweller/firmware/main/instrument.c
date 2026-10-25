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
static const char *const groups[] = { "PLAY", "KIT", "MATERIAL", "ENERGY", "LOOPS", "FX", "FEEL", "SONG" };
enum { G_PLAY, G_KIT, G_MAT, G_ENERGY, G_LOOPS, G_FX, G_FEEL, G_SONG, G_N };
static const char *const role_l[] = { "DRUM", "PAINT", "PUMP" };
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
    [P_ROLE]    = { "ROLE", "role", G_PLAY, PK_ENUM, PF_SYS, 0, NROLE - 1, 1, ROLE_DRUM, "", role_l, "DRUM, PAINT OR PUMP" },
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
    { "FACESHOP",          "sqez=50 drve=35 tone=75 matl=0 rato=3 chrd=1 ring=2 brit=60 room=15 emix=10" },
    { "PONY",              "sqez=85 drve=65 tone=85 decy=0.8 matl=3 rato=4 ring=1 brit=70 bend=12 crek=80 room=10" },
    { "LATEX",             "matl=3 mrph=30 rato=4 chrd=0 ring=1.2 brit=50 bend=12 sqez=60 drve=40" },
    { "STEEL",             "matl=0 rato=3 chrd=2 ring=4 brit=75 mrph=40 drve=20 room=30 rsiz=60" },
    { "GLASSHOUSE",        "matl=1 rato=5 chrd=1 ring=6 brit=80 mrph=20 room=40 rsiz=80 emix=20 efb=45" },
    { "RUBBER ROOM",       "matl=2 rato=2 chrd=0 ring=1.5 brit=30 sqez=70 drve=50 room=25" },
    { "MECHANICAL ANIMAL", "matl=0 rato=4 chrd=3 ring=3 brit=65 crek=100 bend=5 drve=45 tone=60" },
    { "SILICONE",          "matl=3 mrph=80 rato=5 chrd=1 ring=2.5 brit=40 bend=19 emix=25 efb=50 esyn=3" },
    { "SUB PRESSURE",      "sqez=30 drve=55 tone=50 decy=2 matl=2 rato=1 ring=5 brit=20 rstl=-30" },
    { "CHAIN PUMP",        "matl=0 rato=2 chrd=2 ring=8 brit=60 crek=70 rstl=-30 clmb=8 fall=20 drve=40 room=30" },
    { "HARD STOPS",        "sqez=70 drve=50 tone=90 decy=0.5 matl=1 ring=0.6 room=0 emix=0" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_HIT = LEARN_INST, L_FOUR, L_CUT, L_PAIR, L_PAINT, L_PUMP };
static const hint_t hints[] = {
    { "SWING IT LIKE A STICK: FOUR WAYS", L_HIT },
    { "LEFT, RIGHT, FORWARD, BACK", L_FOUR },
    { "HOLD REC AND PLAY: A LOOP", LEARN_REC },
    { "TAP REC: A HARD STOP. AGAIN: BACK IN", L_CUT },
    { "TWO BOXES, ONE IN EACH HAND", L_PAIR },
    { "PLAY > ROLE PAINT: STRIKE A MATERIAL", L_PAINT },
    { "PLAY > ROLE PUMP: MOVE IT, IT GROWS", L_PUMP },
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
int64_t ad_now_tick(void) { return now_tick; }

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
static void record_hit(int64_t tick, int pad, int vel)
{
    if (rec.st == R_PENDING) { if (nprov < 16) prov[nprov++] = (prov_t){ tick, (uint8_t)pad, (uint8_t)vel }; return; }
    if (rec.st != R_WAIT && rec.st != R_REC && rec.st != R_CLOSING) return;
    loop_t *l = &eng_loops[rec.slot];
    int64_t rel = tick - rec.start;
    if (rel < 0) return;
    if (rec.st == R_CLOSING && tick >= rec.end) return;
    if (rel >= BAR_MAX * BAR_TICKS) return;
    int n = l->n;
    if (n >= LOOP_HITS) { toast("THE LOOP IS FULL", 1000); return; }
    l->hits[n] = (hit_t){ (uint16_t)rel, (uint8_t)pad, (uint8_t)vel, 0 };
    __atomic_store_n(&l->n, n + 1, __ATOMIC_RELEASE);
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

// ---------------------------------------------------------------- the strokes (AIR's detector)
enum { G_IDLE, G_STROKE, G_DONE };
static struct {
    int st, ms, since_ms, last_pad; float peak, last_peak, adev; vec3_t acc;
    float back_deg; int still_ms;
    float last_axis_sign; int last_axis;
    bool armed;
} G = { G_IDLE, 0, 10000, -1, 0, 0, 0, { 0, 0, 0 }, 0, 0, 0, 0, true };

static int opposite(int pad) { return pad == PD_FRONT ? PD_BACK : pad == PD_BACK ? PD_FRONT : pad == PD_LEFT ? PD_RIGHT : PD_LEFT; }
static int velocity(float peak, float t_hit, float v_full)
{
    float v = sqrtf(clampf((peak - t_hit) / (v_full - t_hit), 0.0f, 1.0f));
    return clampi((int)(12.0f + v * 115.0f + 0.5f), 12, 127);
}
static void thresholds(float *t_on, float *t_hit, float *v_full, float *a_min)
{
    float sens = pv[P_SENS] * 0.01f;
    *t_hit = (80.0f + (1.0f - sens) * 200.0f) * 1.8f;                // SENSE 50: a hit from 324 deg/s
    *v_full = 600.0f + (1.0f - sens) * 800.0f;                         // full velocity at 1000
    *t_on = 0.55f * *t_hit;
    *a_min = pv[P_CONFIRM] >= 1.0f ? 0.15f + (1.0f - sens) * 0.2f : 0.0f;
}
static float acc_dev(const ctl_t *c)
{
    float d = 0;
    if (c->nburst > 0) for (int i = 0; i < c->nburst; i++) { const vec3_t *a = &c->burst[i]; float m = sqrtf(a->x * a->x + a->y * a->y + a->z * a->z); float e = fabsf(m - 1.0f); if (e > d) d = e; }
    else { float m = sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z); d = fabsf(m - 1.0f); }
    return d;
}
static void hit_pad(int pad, int vel, uint32_t frame);
static void gestures(const ctl_t *c)
{
    if (!c->has_imu || c->frozen) { G.st = G_IDLE; D.gest = G_IDLE; return; }
    float t_on, t_hit, v_full, a_min;
    thresholds(&t_on, &t_hit, &v_full, &a_min);
    float mag = c->gyro_mag, dev = acc_dev(c);
    G.since_ms += 10;
    if (G.last_pad >= 0 && !G.armed) {                                 // the same pad again: the box back the other way, or still
        float r = G.last_axis == 0 ? c->gyro.x : G.last_axis == 1 ? c->gyro.y : c->gyro.z;
        if (r * G.last_axis_sign < 0.0f) G.back_deg += -r * G.last_axis_sign * c->dt;
        G.still_ms = mag < t_on * 0.5f ? G.still_ms + 10 : 0;
        if (G.back_deg >= 10.0f || G.still_ms >= 300) G.armed = true;
    }
    switch (G.st) {
    case G_IDLE:
        if (mag > t_on) { G.st = G_STROKE; G.peak = mag; G.ms = 0; G.adev = dev; G.acc = (vec3_t){ c->gyro.x * mag, c->gyro.y * mag, c->gyro.z * mag }; }
        break;
    case G_STROKE:
        G.ms += 10;
        G.acc.x += c->gyro.x * mag; G.acc.y += c->gyro.y * mag; G.acc.z += c->gyro.z * mag;
        if (mag > G.peak) G.peak = mag;
        if (dev > G.adev) G.adev = dev;
        if (G.peak >= t_hit && mag < 0.85f * G.peak) {
            float fx = G.acc.x, lr = fabsf(G.acc.y) > fabsf(G.acc.z) ? G.acc.y : G.acc.z;
            int axis = fabsf(fx) >= fabsf(lr) ? 0 : fabsf(G.acc.y) > fabsf(G.acc.z) ? 1 : 2;
            float sign = (axis == 0 ? fx : lr) > 0.0f ? 1.0f : -1.0f;
            int pad;
            if (axis == 0) pad = (fx > 0.0f) != (pv[P_FLIP] >= 1.0f) ? PD_FRONT : PD_BACK;
            else pad = (lr > 0.0f) != (pv[P_SIDES] >= 1.0f) ? PD_RIGHT : PD_LEFT;
            bool is_return = G.last_pad >= 0 && pad == opposite(G.last_pad) && G.since_ms < 300 && G.peak < 0.8f * G.last_peak;
            bool reset = G.last_pad < 0 || pad != G.last_pad || G.armed;
            bool jolt = G.adev >= a_min;
            if (!is_return && reset && jolt) {
                hit_pad(pad, velocity(G.peak, t_hit, v_full), c->frames);
                G.last_pad = pad; G.last_peak = G.peak; G.since_ms = 0;
                G.last_axis = axis; G.last_axis_sign = sign; G.back_deg = 0; G.still_ms = 0; G.armed = false;
            }
            G.st = G_DONE;
        } else if (mag < t_on) G.st = G_IDLE;
        else if (G.ms > 500) G.st = G_DONE;
        break;
    default:
        if (mag < t_on * 0.8f) G.st = G_IDLE;
        break;
    }
    D.gest = G.st;
}

// where a hit belongs (the nearest line of QUANTIZE) and when to sound it (on the line, or now if it has passed)
#define HIT_LEAD (AUDIO_SR * 25 / 1000)
static int64_t hit_place(uint32_t frame, uint32_t *at)
{
    *at = 0;
    if (!grid.fpb) return 0;
    uint32_t meant = frame - HIT_LEAD;
    int64_t rel = (int32_t)(meant - grid.origin);
    int64_t t = eng_floor_div(rel * PPB * 2 - (int64_t)(grid.frac * (float)PPB * 2.0f) + (int64_t)grid.fpb, (int64_t)grid.fpb * 2);
    int q = (int)pv[P_QUANT];
    if (!q) return t;
    int step = q == 1 ? PPB / 2 : PPB / 4;
    int64_t line = eng_floor_div(t + step / 2, step) * step;
    uint32_t lf = eng_frame_of(&grid, line);
    if ((int32_t)(lf - frame) > AUDIO_BLOCK) *at = lf;
    return line;
}

// ---------------------------------------------------------------- the other box: what this one sends
// 16 bytes with the link's packets (link_set_app): 3 (the format), role | hand << 2 | cut << 4, energy, the kick's
// note, the hits' number, then the last three hits (tick's low 16 bits, voice << 4 | velocity / 8), this box's
// material (the tilt's morph x 50). A new hit pokes the link, so it goes at the next tick (about 10 ms).
static void send_hit(int64_t tick, int voice, int vel)
{
    sent[0] = sent[1]; sent[1] = sent[2];
    sent[2] = (sent_t){ tick, (uint8_t)voice, (uint8_t)vel };
    sent_seq++;
    link_poke();
}
static void app_update(void)
{
    uint8_t a[16] = { 3, (uint8_t)((int)pv[P_ROLE] | (hand_now << 2) | (cut_on ? 16 : 0)),
                      (uint8_t)clampi((int)(energy * 255.0f + 0.5f), 0, 255), (uint8_t)clampi(kick_note(), 0, 127), sent_seq };
    for (int k = 0; k < 3; k++) {
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
                if (eng_hit_tick(l, h) != rel || h->born == cycle) continue;
                send_hit(t, pad_voice(h->pad), h->vel);
            }
        }
    loop_sent_until = to;
}

static void hit_pad(int pad, int vel, uint32_t frame)
{
    int role = (int)pv[P_ROLE];
    if (role == ROLE_PUMP) return;                                     // pumping: the movement is the energy, not hits
    uint32_t at;
    int64_t tick = hit_place(frame, &at);
    trig_t t = { (uint8_t)pad, (uint8_t)vel, SRC_LIVE, at };
    eng_trigger(&t);
    record_hit(tick, pad, vel);
    if (role == ROLE_DRUM) send_hit(tick, pad_voice(pad), vel);
    last_pad = pad;
    coach_done(L_HIT);
    pads_seen |= (uint8_t)(1u << pad);
    if (pads_seen == 0x0F) coach_done(L_FOUR);
    if (role == ROLE_PAINT) coach_done(L_PAINT);
    ui_awake();
}

// ---------------------------------------------------------------- the other box: what this one hears
static void peer_tick(float dt)
{
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    const link_peer_t *p = NULL;
    for (int i = 0; i < np; i++) if (!strncmp(pr[i].name, "AIRDWELL", 8) && pr[i].app_n >= 16 && pr[i].app[0] == 3) { p = &pr[i]; break; }
    if (!p) { if (PEER.on) toast("THE OTHER HAND IS GONE", 900); PEER.on = false; PEER.seq_ok = false; return; }
    if (!PEER.on || PEER.id != p->id) { PEER.seq_ok = false; toast("THE OTHER HAND IS HERE", 900); coach_done(L_PAIR); }
    PEER.on = true; PEER.id = p->id; PEER.age = 0.0f; (void)dt;
    PEER.role = p->app[1] & 3; PEER.hand = (p->app[1] >> 2) & 3; PEER.kick_note = p->app[3];
    uint8_t seq = p->app[4];
    if (!PEER.seq_ok) { PEER.seq = seq; PEER.seq_ok = true; return; }   // the hits it sent before we met are past
    int fresh = (uint8_t)(seq - PEER.seq);
    PEER.seq = seq;
    if (fresh <= 0) return;
    if (fresh > 3) fresh = 3;
    int role = (int)pv[P_ROLE];
    for (int k = 3 - fresh; k < 3; k++) {
        uint16_t lo = (uint16_t)(p->app[5 + 3 * k] | p->app[6 + 3 * k] << 8);
        int64_t tick = now_tick + (int16_t)(lo - (uint16_t)(now_tick & 0xFFFF));   // the nearest tick with those low bits
        int voice = p->app[7 + 3 * k] >> 4, vel = ((p->app[7 + 3 * k] & 15) << 3) | 4;
        peer_flash = 1.0f; peer_voice = voice;
        scene_hit(-1, vel, SRC_REMOTE);
        if (role != ROLE_DRUM && pv[P_PAIR] >= 1.0f) { ad_strike_at = eng_frame_of(&grid, tick); ad_strike_tick = tick; ad_strikes++; eng_strike(ad_strike_at, vel, voice); }
    }
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
    float tw = c->has_imu && !c->frozen && role != ROLE_DRUM ? clampf(c->gyro.z / 300.0f, -1.0f, 1.0f) : 0.0f;
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
    link_boot_harm((int)pv[P_KEY], 0, 0, 0, 0);
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
    static const uint8_t role_rgb[NROLE][3] = { { 255, 40, 120 }, { 80, 200, 255 }, { 255, 160, 20 } };
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
    gestures(c);
    rec_logic(c);
    loops_ahead();
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
    if (p == P_KEY) { const link_harm_t *h = link_harm(); link_set_harm((int)pv[P_KEY], h->scale, h->prog, h->hold, h->bars, -1); }
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
    .burst = true, .looper = false, .knob = NULL, .rec_button_off = false,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = NULL, .saver = scene_saver, .hold = hold,
};

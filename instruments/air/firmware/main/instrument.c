// AIR: drum on nothing. The box is the stick. A swing forward is the hi-hat, left the kick, right the snare, back
// the open hat: the gyro sees each stroke, which way it went and how fast, and the drum sounds as the stroke
// peaks, on the nearest sixteenth of the beat. There is always a beat: TEMPO, or the flock's over the link.
//
// Tap REC and make a sound: the sound lands on the next pad clockwise (FRONT, RIGHT, BACK, LEFT, round again).
// Hold REC and play: a loop of whole bars, from the bar after the press to the bar you let go in; up to four,
// stacked, each on or off (the menu's LOOPS page, the stick's four ways); hold the knob to stop and start them all.
// The loops are in beats, so they follow the tempo. Tilt the bottom edge down and the drums tune up.
//
// The sound engine is inst_engine.c, the samples in flash inst_store.c; this file is the UI side: params, sounds,
// the gesture detector, the loops, the link, the sampling flow, MIDI, the LED and the screens.
#include "core.h"
#include "dsp.h"
#include "ui.h"
#include "link.h"
#include "inst_air.h"
#include "inst_kit.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "LOOPS", "PADS", "FEEL", "FX", "BREAK", "LINK" };
enum { G_LOOPS, G_PADS, G_FEEL, G_FX, G_BREAK, G_LINK };
static const char *const voices_l[NVTYPE] = { "SAMPLE", "KICK", "SNARE", "HAT", "OPEN HAT", "TOM", "CLAP", "RIM", "COWBELL", "SHAKER", "CONGA", "ZAP", "PART 1", "PART 2", "PART 3", "PART 4", "OFF" };
static const char *const print_l[] = { "CLEAN", "TAPE", "VINYL", "12-BIT", "DUSTY" };
static const char *const length_l[] = { "AUTO", "1 BAR", "2 BARS", "4 BARS" };
static const char *const slices_l[] = { "AUTO", "8", "16", "32" };
static const char *const quant_l[] = { "OFF", "1/8", "1/16" };
static const char *const loop_l[] = { "EMPTY", "OFF", "ON" };
static const char *const hands_l[] = { "AUTO", "ONE", "LEFT", "RIGHT" };
static const char *const vel_l[] = { "SOFT", "NORMAL", "HARD", "FIXED" };
static const char *const hand_l[] = { "KICK LEFT", "KICK RIGHT" };
static const char *const flip_l[] = { "TIP AWAY", "TIP TO ME" };
static const char *const tilt_l[] = { "OFF", "TUNE" };
static const char *const stick_l[] = { "LOOPS", "ROLL+REPEAT", "OFF" };
static const char *const ends_l[] = { "SOUND DIES", "AT LONGEST" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };
#define SYS (PF_SYS | PF_NOCC)
#define KEPT (PF_SYS | PF_HIDE | PF_NOCC)

static const param_t params[P_N] = {
    [P_TEMPO]  = { "TEMPO", "tmpo", G_LOOPS, PK_NUM, PF_INT | PF_SYS, 60, 200, 1, 100, "", NULL, "THE BEAT, ALONE" },
    [P_LOOP1]  = { "LOOP 1", "lop1", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, 0, "", loop_l, "UP ON THE STICK" },
    [P_LOOP2]  = { "LOOP 2", "lop2", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, 0, "", loop_l, "RIGHT ON THE STICK" },
    [P_LOOP3]  = { "LOOP 3", "lop3", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, 0, "", loop_l, "DOWN ON THE STICK" },
    [P_LOOP4]  = { "LOOP 4", "lop4", G_LOOPS, PK_ENUM, SYS, 0, 2, 1, 0, "", loop_l, "LEFT ON THE STICK" },
    [P_ERASE_LAST] = { "ERASE LAST", "eral", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE NEWEST LOOP GOES" },
    [P_ERASE_ALL]  = { "ERASE ALL", "eraa", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "EVERY LOOP GOES" },
    [P_QUANT]  = { "QUANTIZE", "qunt", G_LOOPS, PK_ENUM, 0, 0, 2, 1, 2, "", quant_l, "HITS LAND ON THE GRID" },
    [P_SWING]  = { "SWING", "swng", G_LOOPS, PK_NUM, PF_INT, 0, 75, 5, 0, "%", NULL, "LATE OFFBEATS" },
    [P_LLEVEL] = { "LOOP LEVEL", "llvl", G_LOOPS, PK_NUM, PF_INT, 0, 100, 5, 90, "%", NULL, "HOW LOUD THE LOOPS PLAY" },
    [P_MUTATE] = { "MUTATE", "mutt", G_LOOPS, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HITS DROP OUT OR CHANGE" },
    [P_FRONT]  = { "FRONT", "pfrt", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_HAT, "", voices_l, "HIT FORWARD: THIS DRUM" },
    [P_LEFT]   = { "LEFT", "plft", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_KICK, "", voices_l, "HIT LEFT: THIS DRUM" },
    [P_RIGHT]  = { "RIGHT", "prgt", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_SNARE, "", voices_l, "HIT RIGHT: THIS DRUM" },
    [P_BACK]   = { "BACK", "pbck", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_OHAT, "", voices_l, "HIT BACK: THIS DRUM" },
    [P_SAMPLE] = { "SAMPLE", "smpl", G_PADS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "MAKE A SOUND: THE NEXT PAD" },
    [P_FORGET] = { "FORGET ALL", "sfgt", G_PADS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "SAMPLES GONE, DRUMS BACK" },
    [P_SMAX]   = { "LONGEST", "smax", G_PADS, PK_NUM, PF_LOG, 0.25f, 1.0f, 1.19f, 1.0f, "s", NULL, "A SAMPLE IS CUT HERE" },
    [P_SKEEP]  = { "ENDS", "send", G_PADS, PK_ENUM, 0, 0, 1, 1, 0, "", ends_l, "WHEN A SAMPLE STOPS" },
    [P_LEVEL]  = { "LEVEL", "plvl", G_PADS, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE DRUMS ARE" },
    [P_TUNE]   = { "TUNE", "tune", G_PADS, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "EVERYTHING UP OR DOWN" },
    [P_DECAY]  = { "DECAY", "dcay", G_PADS, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "SHORTER OR LONGER DRUMS" },
    [P_PUNCH]  = { "CLICK", "clik", G_PADS, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW HARD THE KICK STARTS" },
    [P_SETUP]  = { "SWING SETUP", "sset", G_FEEL, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "FOUR SWINGS: IT LEARNS YOU" },
    [P_SENS]   = { "SENSE", "sens", G_FEEL, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW SMALL A SWING COUNTS" },
    [P_VEL]    = { "VELOCITY", "velc", G_FEEL, PK_ENUM, 0, 0, 3, 1, 1, "", vel_l, "HOW HARD TO SWING FOR LOUD" },
    [P_HAND]   = { "SIDES", "hand", G_FEEL, PK_ENUM, 0, 0, 1, 1, 0, "", hand_l, "WHICH SIDE IS THE KICK" },
    [P_FLIP]   = { "FORWARD", "flip", G_FEEL, PK_ENUM, 0, 0, 1, 1, 0, "", flip_l, "WHICH WAY IS IN FRONT" },
    [P_CONFIRM] = { "CONFIRM", "cnfm", G_FEEL, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "A HIT NEEDS A JOLT" },
    [P_TILT]   = { "TILT", "tilt", G_FEEL, PK_ENUM, 0, 0, 1, 1, 1, "", tilt_l, "BOTTOM EDGE DOWN: TUNED UP" },
    [P_STICK]  = { "STICK", "stk2", G_FEEL, PK_ENUM, 0, 0, 2, 1, 0, "", stick_l, "WHAT THE STICK DOES" },
    [P_FILTER] = { "FILTER", "filt", G_FX, PK_NUM, PF_INT | PF_SIGNED, -100, 100, 5, 0, "%", NULL, "DOWN: DARK  UP: THIN" },
    [P_DRIVE]  = { "DRIVE", "drve", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "DIRT" },
    [P_CRUSH]  = { "CRUSH", "crsh", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "FEWER BITS, A LOWER RATE" },
    [P_COMP]   = { "PUNCH", "comp", G_FX, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "SQUEEZE: FATTER, LOUDER" },
    [P_ECHO]   = { "ECHO", "echo", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HOW MUCH GOES TO THE ECHO" },
    [P_ETIME]  = { "ECHO TIME", "etim", G_FX, PK_NUM, PF_LOG, 0.04f, 0.7f, 1.06f, 0.25f, "s", NULL, "TIME BETWEEN REPEATS" },
    [P_EFB]    = { "REPEATS", "efbk", G_FX, PK_NUM, PF_INT, 0, 90, 5, 35, "%", NULL, "HOW LONG THE ECHO GOES ON" },
    [P_ROOM]   = { "ROOM", "room", G_FX, PK_NUM, PF_INT, 0, 100, 5, 18, "%", NULL, "HOW MUCH ROOM" },
    [P_RSIZE]  = { "ROOM SIZE", "rsiz", G_FX, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SMALL BOX TO BIG HALL" },
    [P_GRAIN]  = { "GRAIN", "grai", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE LAST MOMENT, IN PIECES" },
    [P_GSIZE]  = { "GRAIN SIZE", "gsiz", G_FX, PK_NUM, PF_LOG | PF_INT, 10, 250, 1.15f, 60, "ms", NULL, "HOW LONG EACH PIECE IS" },
    [P_STRETCH] = { "STRETCH", "kstr", G_BREAK, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "A BREAK AT THE BEAT'S SPEED" },
    [P_KBREAK]  = { "BREAK", "kbrk", G_BREAK, PK_ENUM, 0, 0, NPAT - 1, 1, B_DRUMS, "", kit_break_l, "FOUR PARTS, FOUR PADS" },
    [P_KPRINT]  = { "PRINT", "kprt", G_BREAK, PK_ENUM, 0, 0, 4, 1, 1, "", print_l, "TAPE, A RECORD, A SAMPLER" },
    [P_KROOM]   = { "PLAYED IN", "krom", G_BREAK, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "THE ROOM IT WAS PLAYED IN" },
    [P_KTUNE]   = { "DRUM TUNE", "ktun", G_BREAK, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "THE DRUMS UP OR DOWN" },
    [P_KTONE]   = { "TONE", "ktne", G_BREAK, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK OR BRIGHT" },
    [P_KDECAY]  = { "RING", "kdcy", G_BREAK, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "MUFFLED OR RINGING DRUMS" },
    [P_KSWING]  = { "SHUFFLE", "kswg", G_BREAK, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE BREAK SWUNG" },
    [P_KGHOSTS] = { "GHOSTS", "kgho", G_BREAK, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SOFT SNARE NOTES" },
    [P_KHUMAN]  = { "HUMAN", "khum", G_BREAK, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW LOOSE THE DRUMMER IS" },
    [P_KTAKE]   = { "TAKE", "ktak", G_BREAK, PK_NUM, PF_INT, 1, 9, 1, 1, "", NULL, "ANOTHER TAKE OF IT" },
    [P_KLENGTH] = { "LENGTH", "klen", G_BREAK, PK_ENUM, KEPT, 0, 3, 1, 0, "", length_l, "" },
    [P_KSLICES] = { "SLICES", "ksli", G_BREAK, PK_ENUM, KEPT, 0, 3, 1, 0, "", slices_l, "" },
    [P_KGATE]   = { "GATE", "kgat", G_BREAK, PK_NUM, PF_INT | KEPT, -60, -12, 2, -36, "dB", NULL, "" },
    [P_LINK]   = { "LINK", "nlnk", G_LINK, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]  = { "GROUP", "ngrp", G_LINK, PK_NUM, SYS | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]  = { "RANGE", "npow", G_LINK, PK_ENUM, SYS, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_HANDS]  = { "HANDS", "hnd2", G_LINK, PK_ENUM, SYS, 0, 3, 1, 0, "", hands_l, "TWO AIRS: A BAR EACH" },
    [P_PEERS]  = { "WHO IS THERE", "nwho", G_LINK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
    [P_NEXT]   = { "NEXT PAD", "snxt", G_PADS, PK_NUM, PF_INT | KEPT, 0, 3, 1, 0, "", NULL, "" },
    [P_CALV]   = { "SWING FULL", "calv", G_FEEL, PK_NUM, PF_INT | KEPT, 0, 3000, 1, 0, "", NULL, "" },
    [P_CALH]   = { "SWING HIT", "calh", G_FEEL, PK_NUM, PF_INT | KEPT, 0, 2000, 1, 0, "", NULL, "" },
};

// ---------------------------------------------------------------- sounds
static const preset_t presets[] = {
    { "AIR KIT",       "pfrt=14 plft=12 prgt=13 pbck=15" },                                         // the printed drums
    { "AMEN",          "pfrt=12 plft=13 prgt=14 pbck=15 kbrk=0 kprt=1 krom=40 ktne=55" },
    { "THINK",         "pfrt=12 plft=13 prgt=14 pbck=15 kbrk=1 kprt=0 krom=20 ktne=65" },
    { "APACHE",        "pfrt=12 plft=13 prgt=14 pbck=15 kbrk=2 kprt=2 krom=50" },
    { "FUNKY",         "pfrt=12 plft=13 prgt=14 pbck=15 kbrk=3 kprt=1 krom=30" },
    { "BOOM BAP BREAK","pfrt=12 plft=13 prgt=14 pbck=15 kbrk=5 kprt=1 krom=10 ktun=-2 drve=20 comp=50" },
    { "DOOM",          "pfrt=12 plft=13 prgt=14 pbck=15 kbrk=7 kprt=1 krom=70 room=40 rsiz=80" },
    { "GABBA",         "pfrt=12 plft=13 prgt=14 pbck=15 kbrk=0 kprt=3 krom=25 ktne=60 drve=45 crsh=20" },
    { "TAPE DRUMS",    "pfrt=14 plft=12 prgt=13 pbck=15 kprt=1 krom=55 ktne=40 kdcy=70" },
    { "GRAIN STORM",   "pfrt=14 plft=12 prgt=13 pbck=15 grai=70 gsiz=40 echo=25 room=35" },
    { "SYNTH KIT",     "pfrt=3 plft=1 prgt=2 pbck=4" },                                            // KNOCK's voices
    { "808 PARTY",     "pfrt=3 plft=1 prgt=6 pbck=4 dcay=1.8 tune=-2 drve=25 comp=50 room=25" },
    { "TECHNO",        "pfrt=3 plft=1 prgt=6 pbck=4 dcay=1.2 echo=20 etim=0.32 efbk=45 comp=60 drve=15" },
    { "HAND DRUM",     "pfrt=9 plft=10 prgt=5 pbck=10 plvl=70 dcay=1.2 room=20" },
    { "COWBELL FEVER", "pfrt=8 plft=1 prgt=8 pbck=6 room=20 comp=45" },
    { "ZAP GUN",       "pfrt=11 plft=11 prgt=11 pbck=11 echo=35 etim=0.18 efbk=55 room=15" },
    { "TRASH CAN",     "pfrt=7 plft=5 prgt=6 pbck=11 drve=55 comp=50 room=35 rsiz=80" },
    { "DUB",           "pfrt=3 plft=1 prgt=7 pbck=4 echo=45 etim=0.375 efbk=60 filt=-30 room=30 rsiz=70" },
    { "MY SAMPLES",    "pfrt=0 plft=0 prgt=0 pbck=0" },
    { "HALF MINE",     "pfrt=14 plft=0 prgt=0 pbck=15" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_HIT = LEARN_INST, L_FOUR, L_SAMPLE, L_STOP };
static const hint_t hints[] = {
    { "SWING THE BOX LIKE A STICK", L_HIT },
    { "FORWARD, LEFT, RIGHT, BACK: FOUR DRUMS", L_FOUR },
    { "HOLD REC AND PLAY: A LOOP", LEARN_REC },
    { "TAP REC, MAKE A SOUND: A PAD IS YOURS", L_SAMPLE },
    { "HOLD THE KNOB: THE LOOPS STOP AND GO", L_STOP },
    { "TURN THE KNOB FOR ANOTHER KIT", LEARN_TURN },
    { "PUSH THE STICK: A LOOP ON OR OFF", LEARN_JOY },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

// ---------------------------------------------------------------- UI state
static uint32_t now_frames;
static int64_t  now_tick;
static grid_t   grid;                    // the beat: the link's session (alone, TEMPO from frame 0)
static int   last_pad = PD_RIGHT, last_vel = 100;
static uint8_t pads_seen;
static int   peers_now, peers_was;
static float lean_tune;                  // the tilt's tuning, taken before a stroke
static bool  applying;                   // a param_set from here: changed() must not act on it
static uint32_t my_id;                   // this board on the link (the low bytes of its MAC)
static int   hands_now = HANDS_ONE;      // what HANDS comes to (AUTO: by the other AIR's id)

// the loops: REC pressed (a tap samples, a hold records), waiting for the bar, recording, recording on to the bar
enum { R_IDLE, R_PENDING, R_WAIT, R_REC, R_CLOSING };
static struct { int st, slot; uint32_t press_frame; int64_t start, end; } rec;
typedef struct { int64_t tick; uint8_t pad, vel; } prov_t;
static prov_t prov[16];
static int nprov;
static uint32_t loop_serial;
static bool loops_fixed;                 // the LOOP n settings put back to EMPTY after boot

// the sampling flow
enum { S_OFF, S_LISTEN, S_SAVE };
static struct { int st, pad, ms, len; bool muted; } smp;
static const int cw[NPAD] = { PD_FRONT, PD_RIGHT, PD_BACK, PD_LEFT };   // the pads clockwise: where samples go

// the swing setup
static struct { int st, n, ms, pad; float peak[4]; } cal;

// what the screen shows
typedef struct { uint8_t pad, vel, src; } impact_t;
static impact_t impacts[16];
static int nimpacts;
static float flash[NPAD];
static float led_lvl, led_rgb_c[3], led_beat;
static int   roll_div, stut_div;         // what the stick is doing, for the screen
static vec3_t gyro_now; static float gyro_mag_now, adev_now;
static int   gest_state;                 // the detector's state, for the screen and the tests
static float G_adev_last;
int air_gest_state(void) { return gest_state; }
int air_peers(void) { return peers_now; }
int air_rec_state(void) { return rec.st; }
int air_hands(void) { return hands_now; }
float air_jolt(void) { return G_adev_last; }

// ---------------------------------------------------------------- the grid
static uint32_t fpb_of(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / bpm + 0.5f); }
static float grid_bpm(void) { return grid.fpb ? 60.0f * (float)AUDIO_SR / (float)grid.fpb : 100.0f; }
static int64_t bar_floor(int64_t t) { return eng_floor_div(t, BAR_TICKS) * BAR_TICKS; }

static void grid_update(void)
{
    uint32_t o; float f; link_origin(&o, &f);
    grid.fpb = link_fpb(); grid.origin = o; grid.frac = f;
    eng_set_grid(&grid);
    now_tick = eng_tick_at(&grid, now_frames);
}

// the loops' beats for the session: the longest loop's (the first loop made anywhere locks the flock's tempo)
static int loops_beats(void)
{
    int b = 0;
    for (int k = 0; k < NLOOP; k++) if (eng_loops[k].bars * 4 > b) b = eng_loops[k].bars * 4;
    return b;
}
static bool peer_has_loop(void)
{
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    for (int i = 0; i < np; i++) if (pr[i].flags & LINKF_LOOP) return true;
    return false;
}

// TEMPO turned: the same beat position at the new speed, told to the flock (polite: not while a board around has a
// loop at the session's tempo; our own loops are in beats and just follow)
static void tempo_set(float bpm)
{
    uint32_t fpb = fpb_of(bpm);
    if (!grid.fpb || fpb == grid.fpb) return;
    float beat = ((float)(int32_t)(now_frames - grid.origin) - grid.frac) / (float)grid.fpb;
    uint32_t origin = now_frames - (uint32_t)(int32_t)(beat * (float)fpb);
    link_set_time(fpb, origin, 4, loops_beats());
    grid_update();
}

// ---------------------------------------------------------------- the loops
static void loop_param(int k, int v) { applying = true; param_set(P_LOOP1 + k, (float)v); applying = false; }
static void loop_flag(void)
{
    bool any = false;
    for (int k = 0; k < NLOOP; k++) any |= eng_loops[k].bars > 0;
    link_set_flags(any ? LINKF_LOOP : 0);
    if (!any && link_time()->beats && link_time()->owner == my_id) link_set_time(grid.fpb, grid.origin, 4, 0);   // the flock's tempo is free again
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

static int loops_count(void) { int n = 0; for (int k = 0; k < NLOOP; k++) n += eng_loops[k].bars > 0; return n; }
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

// a hit into the loop being recorded (ticks from its first bar; one before the bar is kept aside: a late press)
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

// REC held: a new loop in the first free slot (all four taken: the newest again), from the bar after the press
// (or this bar, pressed within a beat after its line)
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

// REC let go: the loop ends on the nearest bar line (at least one bar) and plays from there; let go early it
// records on to that line. It is published now, not at the line, so the engine has it when the line comes.
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
    if (!(link_flags_any() & LINKF_LOOP)) link_set_time(grid.fpb, grid.origin, 4, loops_beats());   // the first loop anywhere: the flock's tempo is set
    loop_flag();
}

// the end line has come: what was played after it (a late let-go) goes to the next cycle's start
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

static void sample_start(void);
static void sample_stop(void);

static void rec_logic(const ctl_t *c)
{
    if (smp.st != S_OFF) { if (c->rec_press) sample_stop(); return; }   // REC while a pad listens: enough
    if (cal.st) return;
    if (c->rec_press) { rec.st = R_PENDING; rec.press_frame = c->frames; nprov = 0; }
    if (rec.st == R_PENDING && c->rec && c->rec_ms >= 300) rec_begin();   // a hold, not a tap
    if (rec.st == R_WAIT && now_tick >= rec.start) rec.st = R_REC;
    if (rec.st == R_REC && now_tick - rec.start >= BAR_MAX * BAR_TICKS) { rec.end = rec.start + BAR_MAX * BAR_TICKS; rec.st = R_CLOSING; }
    if (c->rec_release) {
        if (rec.st == R_PENDING) { rec.st = R_IDLE; nprov = 0; sample_start(); }   // a tap: a sample
        else if (rec.st == R_WAIT || rec.st == R_REC) rec_release();
    }
    if (rec.st == R_CLOSING && now_tick >= rec.end) rec_close();
}

// ---------------------------------------------------------------- the strokes
// The gyro at 100 Hz, in the screen's axes. A stroke: the rate climbs over the onset threshold, peaks and falls;
// the hit fires as soon as the rate has dropped 15 % from a peak high enough to count, with the stroke's direction
// (the rate summed over the stroke, weighted by its size) picking the pad: about the x axis (the bottom edge
// rising) is forward, about y or z (the box swung sideways) left or right. The hand coming back from a stroke
// turns the other way, slower: a stroke opposite to the last one, soon after it and weaker, is that return and
// is not a hit. CONFIRM: a hit also needs a jolt (the accelerometer's 1 kHz burst off 1 g): a swing has one, a
// slow turn or a walk has none.
enum { G_IDLE, G_STROKE, G_DONE };
static struct {
    int st, ms, since_ms, last_pad; float peak, last_peak, adev; vec3_t acc;
    float back_deg; int still_ms;         // since the last hit: how far the box turned back on that stroke's axis, and how long it has been still
    float last_axis_sign; int last_axis;  // the last hit's axis (0: x, 1: y, 2: z) and which way it turned
    bool armed;                           // the box came back (or rested) since the last hit: the same pad may sound again
} G = { G_IDLE, 0, 10000, -1, 0, 0, 0, { 0, 0, 0 }, 0, 0, 0, 0, true };

static void hit_pad(int pad, int vel, uint32_t frame);
static void cal_stroke(int pad, float peak);

static int opposite(int pad) { return pad == PD_FRONT ? PD_BACK : pad == PD_BACK ? PD_FRONT : pad == PD_LEFT ? PD_RIGHT : PD_LEFT; }

static int velocity(float peak, float t_hit, float v_full)
{
    float v = clampf((peak - t_hit) / (v_full - t_hit), 0.0f, 1.0f);
    switch ((int)pv[P_VEL]) {
    case 0: v = sqrtf(v); break;
    case 2: v = v * v; break;
    case 3: v = 0.8f; break;
    default: break;
    }
    return clampi((int)(12.0f + v * 115.0f + 0.5f), 12, 127);
}

// the thresholds: from SWING SETUP's measure of this player, else from SENSE alone; SENSE scales either
static void thresholds(float *t_on, float *t_hit, float *v_full, float *a_min)
{
    float sens = pv[P_SENS] * 0.01f;
    if (pv[P_CALV] > 0.0f) { float k = 1.5f - sens; *t_hit = pv[P_CALH] * k; *v_full = pv[P_CALV] * k; }
    else { *t_hit = (80.0f + (1.0f - sens) * 200.0f) * 1.8f; *v_full = 600.0f + (1.0f - sens) * 800.0f; }   // SENSE 50: hit 324, full 1000 deg/s
    *t_on = 0.55f * *t_hit;
    *a_min = pv[P_CONFIRM] >= 1.0f ? 0.15f + (1.0f - sens) * 0.2f : 0.0f;                                   // SENSE 50: a quarter g
}

// how far the accelerometer left 1 g this tick: the burst's worst sample, or the 100 Hz reading without one
static float acc_dev(const ctl_t *c)
{
    float d = 0;
    if (c->nburst > 0) for (int i = 0; i < c->nburst; i++) { const vec3_t *a = &c->burst[i]; float m = sqrtf(a->x * a->x + a->y * a->y + a->z * a->z); float e = fabsf(m - 1.0f); if (e > d) d = e; }
    else { float m = sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z); d = fabsf(m - 1.0f); }
    return d;
}

static void gestures(const ctl_t *c)
{
    gyro_now = c->gyro; gyro_mag_now = c->gyro_mag;
    if (!c->has_imu || c->frozen) { G.st = G_IDLE; gest_state = G_IDLE; return; }
    float t_on, t_hit, v_full, a_min;
    thresholds(&t_on, &t_hit, &v_full, &a_min);
    if (cal.st == 1) { t_on = 80.0f; t_hit = 150.0f; a_min = 0.0f; }   // the setup listens to whatever the player does
    float mag = c->gyro_mag, dev = acc_dev(c);
    adev_now = dev;
    G.since_ms += 10;
    // the reset: after a hit the same pad needs the box turned back the other way (10 degrees), or held still a
    // moment, before it sounds again; so one long turn is one hit, not one every 15 degrees
    if (G.last_pad >= 0 && !G.armed) {
        float r = G.last_axis == 0 ? c->gyro.x : G.last_axis == 1 ? c->gyro.y : c->gyro.z;
        if (r * G.last_axis_sign < 0.0f) G.back_deg += -r * G.last_axis_sign * c->dt;
        G.still_ms = mag < t_on * 0.5f ? G.still_ms + 10 : 0;
        if (G.back_deg >= 10.0f || G.still_ms >= 300) G.armed = true;
    }
    switch (G.st) {
    case G_IDLE:
        if (pv[P_TILT] >= 1.0f) lean_tune = clampf(c->lean_y / 30.0f, -1.0f, 1.0f) * 5.0f;   // the tilt before the stroke
        else lean_tune = 0.0f;
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
            else pad = (lr > 0.0f) != (pv[P_HAND] >= 1.0f) ? PD_RIGHT : PD_LEFT;
            bool is_return = G.last_pad >= 0 && pad == opposite(G.last_pad) && G.since_ms < 300 && G.peak < 0.8f * G.last_peak;
            bool reset = G.last_pad < 0 || pad != G.last_pad || G.armed;
            bool jolt = G.adev >= a_min;
            G_adev_last = G.adev;
            if (cal.st == 1) cal_stroke(pad, G.peak);
            else if (!is_return && reset && jolt) {
                hit_pad(pad, velocity(G.peak, t_hit, v_full), c->frames);
                G.last_pad = pad; G.last_peak = G.peak; G.since_ms = 0;
                G.last_axis = axis; G.last_axis_sign = sign; G.back_deg = 0; G.still_ms = 0; G.armed = false;
            }
            G.st = G_DONE;
        } else if (mag < t_on) G.st = G_IDLE;                           // a wobble that never became a stroke
        else if (G.ms > 500) G.st = G_DONE;                             // a slow turn, not a hit
        break;
    default:
        if (mag < t_on * 0.8f) G.st = G_IDLE;
        break;
    }
    gest_state = G.st;
}

// where a hit belongs: the nearest grid line (QUANTIZE), as a tick; and when to sound it: now if that line has
// passed (a late hit is not made later still), else on the line (an early hit waits for it)
#define HIT_LEAD (AUDIO_SR * 25 / 1000)          // the swing was meant a moment before the detector fired
static int64_t hit_place(uint32_t frame, uint32_t *at)
{
    *at = 0;
    if (!grid.fpb) return 0;
    uint32_t meant = frame - HIT_LEAD;
    int64_t rel = (int32_t)(meant - grid.origin);
    int64_t t = eng_floor_div(rel * PPB * 2 - (int64_t)(grid.frac * (float)PPB * 2.0f) + (int64_t)grid.fpb, (int64_t)grid.fpb * 2);   // the nearest tick
    int q = (int)pv[P_QUANT];
    if (!q) return t;
    int step = q == 1 ? PPB / 2 : PPB / 4;
    int64_t line = eng_floor_div(t + step / 2, step) * step;
    uint32_t lf = eng_frame_of(&grid, line);
    if ((int32_t)(lf - frame) > AUDIO_BLOCK) *at = lf;
    return line;
}

static void hit_pad(int pad, int vel, uint32_t frame)
{
    if (smp.st != S_OFF || cal.st) return;
    eng_tune = pv[P_TUNE] + lean_tune;
    uint32_t at;
    int64_t tick = hit_place(frame, &at);
    trig_t t = { (uint8_t)pad, (uint8_t)vel, SRC_LIVE, at };
    eng_trigger(&t);
    record_hit(tick, pad, vel);
    last_pad = pad; last_vel = vel;
    coach_done(L_HIT);
    pads_seen |= (uint8_t)(1u << pad);
    if (pads_seen == 0x0F) coach_done(L_FOUR);
    if (pv[P_FRONT + pad] < 1.0f && eng_pad_len[pad] == 0) toast("EMPTY PAD: TAP REC, MAKE A SOUND", 1500);
    ui_awake();
}

// ---------------------------------------------------------------- the swing setup
static void cal_start(void)
{
    if (cal.st) return;
    menu_leave(); knob_grab(true);
    cal.st = 1; cal.n = 0; cal.ms = 0; cal.pad = -1;
}
static void cal_end(void) { cal.st = 0; knob_grab(false); }
static void cal_stroke(int pad, float peak)
{
    if (cal.n >= 4) return;
    if (cal.n == 0) cal.pad = pad; else if (pad != cal.pad) return;   // the hand coming back goes the other way: not a swing
    cal.peak[cal.n++] = peak; cal.ms = 0;
    if (cal.n < 4) return;
    float s[4]; memcpy(s, cal.peak, sizeof s);
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) if (s[j] < s[i]) { float x = s[i]; s[i] = s[j]; s[j] = x; }
    float med = 0.5f * (s[1] + s[2]);
    param_set(P_CALV, clampf(1.3f * med, 300.0f, 3000.0f));
    param_set(P_CALH, clampf(0.4f * med, 150.0f, 2000.0f));
    cal.st = 2;
}
static void cal_tick(const ctl_t *c)
{
    if (!cal.st) return;
    coach_pause();
    cal.ms += 10;
    if (c->knob_tap || c->rec_press) { toast(cal.st == 2 ? "SWING SET" : "SETUP LEFT AS IT WAS", 1000); cal_end(); return; }
    if (cal.st == 1 && cal.ms > 20000) { toast("NO SWINGS: LEFT AS IT WAS", 1200); cal_end(); }
    if (cal.st == 2 && cal.ms > 1800) cal_end();
}

// ---------------------------------------------------------------- the sampling flow
static const char *const pad_name[NPAD] = { "FRONT", "LEFT", "RIGHT", "BACK" };
static const char *const kind_l[] = { "KICK", "SNARE", "HAT", "RIDE", "CRASH", "TOM", "PART" };
static const int kind_gm[] = { 36, 38, 42, 51, 49, 45, 0 };
// what a pad sounds like, for the screen: its drum, its sample, or the part of the break it holds (DRUMS: that drum)
static const char *pad_label(int pad, char *b, int n)
{
    int vt = (int)pv[P_FRONT + pad];
    if (vt < V_PART1 || vt > V_PART4) return vt_label[vt];
    int k = eng_part_kind(vt - V_PART1);
    if ((int)pv[P_KBREAK] == B_DRUMS && k >= 0 && k < 6) return kind_l[k];
    snprintf(b, n, "%s %d", kit_break()->name, vt - V_PART1 + 1);
    return b;
}
static int next_pad(void) { return cw[clampi((int)pv[P_NEXT], 0, 3)]; }

// tap REC: the next pad clockwise listens at once
static void sample_start(void)
{
    if (!eng_pad[0] || !eng_pad[NPAD - 1]) { toast("NO MEMORY FOR SAMPLES", 1500); return; }
    if (smp.st != S_OFF || cal.st) return;
    menu_leave();
    knob_grab(true);
    smp.pad = next_pad(); smp.ms = 0;
    eng_cap_hold = pv[P_SKEEP] >= 1.0f ? 1 : 0;
    eng_capture(smp.pad, pv[P_SMAX]);
    smp.st = S_LISTEN;
    flash[smp.pad] = 1.0f;
}

static void sample_end(void)
{
    if (smp.muted) { store_mute(false); smp.muted = false; }
    smp.st = S_OFF;
    knob_grab(false);
}

static void sample_stop(void)
{
    if (smp.st != S_LISTEN) return;
    if (eng_cap_state == CAP_REC) eng_capture_stop();
    else { eng_capture_cancel(); toast("NO SAMPLE", 900); sample_end(); }
}

static void pad_gain(int pad)
{
    int n = eng_pad_len[pad]; const int16_t *b = eng_pad[pad];
    int pk = 1;
    for (int i = 0; i < n; i++) { int v = b[i] < 0 ? -b[i] : b[i]; if (v > pk) pk = v; }
    eng_pad_gain[pad] = clampf(0.6f * 32768.0f / (float)pk, 0.5f, 24.0f);   // a sample peaks near the drums' level
}

static void sample_done(void)
{
    int pad = smp.pad, n = eng_cap_len;
    eng_cap_state = CAP_IDLE; eng_cap_pad = -1;
    smp.len = n;
    eng_pad_len[pad] = n;
    pad_gain(pad);
    param_set(P_FRONT + pad, (float)V_SAMPLE);
    param_set(P_NEXT, (float)(((int)pv[P_NEXT] + 1) & 3));
    char b[40]; snprintf(b, sizeof b, "KEPT. NEXT: %s PAD", pad_name[next_pad()]); toast(b, 1600);
    coach_done(L_SAMPLE);
    if (store_save_begin(pad, eng_pad[pad], n)) { store_mute(true); smp.muted = true; smp.st = S_SAVE; smp.ms = 0; }
    else sample_end();
}

static void sample_tick(const ctl_t *c)
{
    if (smp.st == S_OFF) return;
    coach_pause();
    smp.ms += 10;
    switch (smp.st) {
    case S_LISTEN:
        if (eng_cap_state == CAP_DONE) { sample_done(); break; }
        if (eng_cap_state == CAP_IDLE && smp.ms > 30) { toast("TOO SHORT: NO SAMPLE", 1200); sample_end(); break; }
        if (c->knob_tap) sample_stop();
        else if (eng_cap_state == CAP_ARMED && smp.ms > 15000) { eng_capture_cancel(); toast("HEARD NOTHING: NO SAMPLE", 1400); sample_end(); }
        break;
    case S_SAVE:
        if (!store_silent() && smp.ms < 250) break;                     // the mute's fade first (20 ms, 250 at most)
        if (!store_save_step()) sample_end();
        break;
    default: break;
    }
}

// the knob held in play: every loop stops, or goes again
static bool hold(void)
{
    if (smp.st != S_OFF || cal.st) return true;
    if (!loops_count()) { toast("NO LOOP YET: HOLD REC AND PLAY", 1400); return true; }
    eng_run = !eng_run;
    toast(eng_run ? "LOOPS GO" : "LOOPS STOPPED", 900);
    coach_done(L_STOP);
    return true;
}

// ---------------------------------------------------------------- the link
static bool link_up;
static void link_start(void)
{
    link_init("AIR", 1);
    uint8_t mac[6]; plat_link_mac(mac);
    my_id = (uint32_t)mac[2] << 24 | (uint32_t)mac[3] << 16 | (uint32_t)mac[4] << 8 | mac[5];
    link_boot_time(fpb_of(pv[P_TEMPO]), 0, 4);                        // TEMPO from frame 0 until someone says otherwise
    link_boot_harm(0, 0, 0, 0, 0);
    link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    link_up = true;
}

static void link_follow(void)
{
    link_peer_t pr[LINK_PEERS_MAX];
    peers_now = link_peers(pr, LINK_PEERS_MAX);
    link_time_news();
    // TEMPO shows the session's tempo, whoever set it
    int bpm = (int)(grid_bpm() + 0.5f);
    if (bpm != (int)pv[P_TEMPO] && bpm >= 60 && bpm <= 200) { applying = true; param_set(P_TEMPO, (float)bpm); applying = false; }
    int key = link_harm()->key % 12;                                   // the tuned drums sit in the flock's key
    eng_key = (float)(key > 6 ? key - 12 : key);
    // HANDS AUTO: one other AIR, and the lower id is the left hand
    int h = (int)pv[P_HANDS];
    if (h == HANDS_AUTO) {
        int nair = 0; uint32_t other = 0;
        for (int i = 0; i < peers_now; i++) if (!strcmp(pr[i].name, "AIR")) { nair++; other = pr[i].id; }
        h = nair == 1 ? (my_id < other ? HANDS_LEFT : HANDS_RIGHT) : HANDS_ONE;
    }
    if (h != hands_now) { hands_now = h; if (h == HANDS_LEFT || h == HANDS_RIGHT) toast(h == HANDS_LEFT ? "TWO AIRS: THIS IS THE LEFT HAND" : "TWO AIRS: THIS IS THE RIGHT HAND", 1800); }
    if (peers_now != peers_was) {
        char b[40];
        if (peers_now > peers_was && peers_now > 0) snprintf(b, sizeof b, "LINKED: %s", pr[peers_now - 1].name);
        else snprintf(b, sizeof b, peers_now ? "A BOARD LEFT" : "LINK: ALONE");
        toast(b, 1400);
        peers_was = peers_now;
    }
}

// ---------------------------------------------------------------- MIDI, LED, the screen's events
static const uint8_t pad_rgb[NPAD][3] = { { 255, 200, 0 }, { 255, 40, 20 }, { 0, 170, 255 }, { 40, 255, 60 } };
static int gm_note(int pad)
{
    int vt = (int)pv[P_FRONT + pad];
    if (vt >= V_PART1 && vt <= V_PART4) { int k = eng_part_kind(vt - V_PART1); if (k >= 0 && k < 6 && kind_gm[k]) return kind_gm[k]; }
    return vt_gm[vt] ? vt_gm[vt] : pad_gm[pad];
}

typedef struct { float x, y, age; char text[8]; } label_t;
static label_t labels[4]; static int label_next;
static void float_label(float x, float y, const char *s) { label_t *l = &labels[label_next++ % 4]; l->x = x; l->y = y; l->age = 0.001f; snprintf(l->text, sizeof l->text, "%s", s); }

static void sounded(const played_t *p)
{
    int note = gm_note(p->pad);
    midi_note_ch(10, note, p->vel);
    midi_note_ch(10, note, 0);
    flash[p->pad] = 1.0f;
    if (nimpacts < 16) impacts[nimpacts++] = (impact_t){ p->pad, p->vel, p->src };
    float k = (float)p->vel / 127.0f;
    led_lvl = fmaxf(led_lvl, 0.35f + 0.65f * k);
    for (int i = 0; i < 3; i++) led_rgb_c[i] = pad_rgb[p->pad][i];
    if (p->src == SRC_ROLL) { uint32_t at; int64_t t = hit_place(p->frame + HIT_LEAD, &at); record_hit(t, p->pad, p->vel); }
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) != 0x90 || d2 == 0) return;
    int pad = -1;
    for (int s = 0; s < NPAD && pad < 0; s++) if (gm_note(s) == d1) pad = s;
    if (pad < 0) pad = d1 % NPAD;
    if (smp.st != S_OFF || cal.st) return;
    eng_tune = pv[P_TUNE] + lean_tune;
    uint32_t at;
    int64_t tick = hit_place(now_frames + HIT_LEAD, &at);
    trig_t t = { (uint8_t)pad, d2, SRC_MIDI, at };
    eng_trigger(&t);
    record_hit(tick, pad, d2);
}

// the stick: LOOPS: its four ways switch the four loops; ROLL+REPEAT: up a roll, down a beat repeat, x a filter
static int stick_was = -1;
static void stick(const ctl_t *c)
{
    int mode = (int)pv[P_STICK];
    float jx = c->has_joy ? c->jx : 0.0f, jy = c->has_joy ? c->jy : 0.0f;
    float fx = pv[P_FILTER] * 0.01f + (mode == STICK_ROLL ? jx : 0.0f);
    eng_filter = clampf(fx, -1.0f, 1.0f);
    float bs = grid.fpb ? (float)grid.fpb / (float)AUDIO_SR : 0.6f;
    roll_div = stut_div = 0;
    if (mode == STICK_ROLL) {
        roll_div = jy > 0.3f ? (jy > 0.9f ? 8 : jy > 0.6f ? 4 : 2) : 0;
        stut_div = jy < -0.3f ? (jy < -0.9f ? 8 : jy < -0.6f ? 4 : 2) : 0;
    } else if (mode == STICK_LOOPS && c->has_joy) {
        int d = jy > 0.6f ? 0 : jx > 0.6f ? 1 : jy < -0.6f ? 2 : jx < -0.6f ? 3 : -1;
        if (d >= 0 && stick_was < 0 && smp.st == S_OFF && !cal.st) { loop_toggle(d); coach_done(LEARN_JOY); }
        if (d >= 0) stick_was = d; else if (fabsf(jx) < 0.4f && fabsf(jy) < 0.4f) stick_was = -1;
    }
    eng_roll_pad = last_pad; eng_roll_vel = clampi((int)((float)last_vel * 0.85f), 20, 127);
    eng_roll_hz = roll_div ? (float)roll_div / bs : 0.0f;
    eng_stutter = stut_div ? bs / (float)stut_div : 0.0f;
}

static void tick(const ctl_t *c)
{
    now_frames = c->frames;
    if (!link_up) link_start();
    link_tick(c->dt);
    grid_update();
    link_follow();
    if (!loops_fixed) { loops_fixed = true; for (int k = 0; k < NLOOP; k++) loop_param(k, LP_EMPTY); }   // nothing survives a boot
    sample_tick(c);
    cal_tick(c);
    stick(c);
    eng_mutate = pv[P_MUTATE] * 0.01f;
    // the break's parts: stretched to the grid's tempo (the printed DRUMS never are), a bar each with two hands
    {
        const brk_t *b = kit_break();
        bool stretch = pv[P_STRETCH] >= 1.0f && (int)pv[P_KBREAK] != B_DRUMS && b->bpm > 1.0f;
        eng_part_rate = stretch ? clampf(grid_bpm() / b->bpm, 0.5f, 2.0f) : 1.0f;
        eng_part_bar = hands_now == HANDS_LEFT ? 0 : hands_now == HANDS_RIGHT ? 1 : -1;
    }
    kit_tick();
    rec_logic(c);
    gestures(c);
    if (smp.st == S_OFF) eng_tune = pv[P_TUNE] + lean_tune;
    played_t p;
    while (eng_played_pop(&p)) sounded(&p);
    // the beat, for the LED and the head
    {
        static int64_t last_beat = -1;
        int64_t b = eng_floor_div(now_tick, PPB);
        if (b != last_beat) { led_beat = (b % 4 == 0) ? 0.35f : 0.15f; last_beat = b; }
    }
    // the LED: the colour of the last pad, fading; a white tick on the beat; a red pulse while recording; green
    // while a pad listens
    led_lvl *= 0.86f; led_beat *= 0.8f;
    bool recording = rec.st == R_REC || rec.st == R_CLOSING || rec.st == R_WAIT;
    float base = recording ? 0.25f + 0.2f * sinf((float)c->ticks * 0.25f) : 0.0f;
    float l = fmaxf(led_lvl, base);
    if (smp.st == S_LISTEN) led_rgb(0, (uint8_t)(120.0f + 100.0f * sinf((float)c->ticks * 0.3f)), 20);
    else if (base > led_lvl) led_rgb((uint8_t)(255.0f * l), 0, 0);
    else if (led_beat > led_lvl) led_rgb((uint8_t)(255.0f * led_beat), (uint8_t)(255.0f * led_beat), (uint8_t)(255.0f * led_beat));
    else led_rgb((uint8_t)(led_rgb_c[0] * l), (uint8_t)(led_rgb_c[1] * l), (uint8_t)(led_rgb_c[2] * l));
}

static void action(int p)
{
    switch (p) {
    case P_ERASE_LAST: {
        int k = loop_newest();
        if (k < 0) { toast("NO LOOP YET", 900); break; }
        if (rec.st != R_IDLE && rec.st != R_PENDING && rec.slot == k) { rec.st = R_IDLE; nprov = 0; }
        loop_erase(k); loop_flag();
        char b[24]; snprintf(b, sizeof b, "LOOP %d GONE", k + 1); toast(b, 900);
        break;
    }
    case P_ERASE_ALL: loops_erase_all(); toast("LOOPS GONE", 900); break;
    case P_SAMPLE: sample_start(); break;
    case P_SETUP: cal_start(); break;
    case P_FORGET: {
        static const int back[NPAD] = { V_PART3, V_PART1, V_PART2, V_PART4 };
        for (int k = 0; k < NPAD; k++) { eng_pad_len[k] = 0; store_forget(k); if (pv[P_FRONT + k] < 1.0f) param_set(P_FRONT + k, (float)back[k]); }
        param_set(P_NEXT, 0);
        toast("SAMPLES FORGOTTEN", 1100);
        break;
    }
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

static void changed(int p)
{
    kit_changed(p);
    if (applying || !link_up) return;
    if (p >= P_LOOP1 && p <= P_LOOP4) {                                // the menu's switches: only a loop that exists
        int k = p - P_LOOP1, v = (int)pv[p];
        if (!eng_loops[k].bars) { if (v != LP_EMPTY) { loop_param(k, LP_EMPTY); char b[40]; snprintf(b, sizeof b, "LOOP %d IS EMPTY: HOLD REC AND PLAY", k + 1); toast(b, 1400); } }
        else if (v == LP_EMPTY) { loop_param(k, eng_loops[k].on ? LP_ON : LP_OFF); toast("TO ERASE: ERASE LAST, ERASE ALL", 1400); }
        else loop_set_on(k, v == LP_ON);
    }
    if (p == P_TEMPO) {
        if (peer_has_loop()) { char b[40]; snprintf(b, sizeof b, "THE BEAT IS THE FLOCK'S: %d", (int)(grid_bpm() + 0.5f)); toast(b, 1400); applying = true; param_set(P_TEMPO, (float)(int)(grid_bpm() + 0.5f)); applying = false; }
        else tempo_set(pv[P_TEMPO]);
    }
    if (p == P_LINK || p == P_GROUP || p == P_RANGE) {
        link_enable(false, 1, 1);
        link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    }
}

static void init(void)
{
    eng_init();
    kit_init();
    if (store_init()) for (int k = 0; k < NPAD; k++) if (eng_pad[k]) { eng_pad_len[k] = store_load(k, eng_pad[k], PAD_MAX); if (eng_pad_len[k]) pad_gain(k); }
}

static void audio(const float *in, float *out, int n) { eng_audio(in, out, n); }

// ---------------------------------------------------------------- drawing: the drummer
// A stick figure at a kit, seen from the front: the hi-hat on the left, the snare in front, the kick drum at the
// feet. A hit swings the arm (or the foot) to that piece and the piece answers: the hat claps shut or flies open,
// the snare drops, the kick bulges. The pads' names float up from the piece they hit.
static float arm[2];                   // 0 left (hat), 1 right (snare): 0 at rest .. 1 on the drum
static float foot, bulge, bulge_v, snare_dy, hat_gap = 1.0f, hat_shimmer;
static float head_bob;
static parts_t parts;
static uint32_t pseed = 0x2468aceu;
#define DX 0                           // the kit's left edge

static void draw_drummer(int dx, int dy, bool with_kit)
{
    int cx = dx + 44;
    // the floor, the hi-hat on its stand (the top cymbal lifted by the gap), the snare on its stand
    int hx = dx + 16, hy = dy + 31;
    int sx = dx + 64, sy = dy + 42 + (int)lroundf(snare_dy);
    if (with_kit) {
        oled_hline_pat(dx + 4, dy + 58, 66, 0x55, true);
        oled_line(hx, hy + 1, hx, dy + 57, true);
        oled_line(hx - 6, hy, hx + 6, hy, true);
        int g = (int)lroundf(hat_gap);
        oled_line(hx - 6, hy - g - 1, hx, hy - g - 2, true); oled_line(hx, hy - g - 2, hx + 6, hy - g - 1, true);
        oled_frame(sx - 8, sy - 2, 17, 5);
        oled_line(sx, sy + 3, sx, dy + 57, true);
        oled_line(sx - 4, dy + 57, sx + 4, dy + 57, true);
    }
    // the figure: head, body, seat, legs (the kick drum hides the shins)
    int hy0 = dy + 24 + (int)lroundf(head_bob);
    oled_circle(cx, hy0, 3, true);
    oled_line(cx, hy0 + 3, cx, dy + 38, true);
    oled_line(cx - 5, dy + 39, cx + 5, dy + 39, true);
    oled_line(cx, dy + 38, cx - 6, dy + 47, true);
    oled_line(cx, dy + 38, cx + 6, dy + 47, true);
    // the arms, each with a stick: shoulder -> hand -> tip, the hand between rest and the drum
    int shx = cx, shy = dy + 30;
    {   // left arm to the hat
        float a = ease_out(arm[0]);
        int hhx = (int)lroundf(lerpf((float)dx + 36.0f, (float)dx + 33.0f, a)), hhy = (int)lroundf(lerpf((float)dy + 35.0f, (float)dy + 33.0f, a));
        int tx = (int)lroundf(lerpf((float)dx + 27.0f, (float)hx + 2.0f, a)), ty = (int)lroundf(lerpf((float)dy + 30.0f, (float)hy - hat_gap - 2.0f, a));
        oled_line(shx, shy, hhx, hhy, true);
        oled_line(hhx, hhy, tx, ty, true);
    }
    {   // right arm to the snare
        float a = ease_out(arm[1]);
        int hhx = (int)lroundf(lerpf((float)dx + 53.0f, (float)dx + 54.0f, a)), hhy = (int)lroundf(lerpf((float)dy + 35.0f, (float)dy + 36.0f, a));
        int tx = (int)lroundf(lerpf((float)sx - 3.0f, (float)sx - 2.0f, a)), ty = (int)lroundf(lerpf((float)dy + 37.0f, (float)sy - 2.0f, a));
        oled_line(shx, shy, hhx, hhy, true);
        oled_line(hhx, hhy, tx, ty, true);
    }
    // the kick drum in front, its skin bulging on a hit; the beater's foot pedal at the right
    int kx = cx, ky = dy + 51, kr = 8 + (int)lroundf(bulge);
    if (with_kit) {
        oled_disc(kx, ky, kr, false);
        oled_circle(kx, ky, kr, true);
        oled_circle(kx, ky, 3, true);
        int fy = dy + 57 - (int)lroundf(foot * 3.0f);
        oled_line(kx + kr + 1, fy, kx + kr + 6, dy + 57, true);              // the pedal, rising with the foot
        oled_line(kx + kr + 1, fy, kx + kr - 1, ky + 1, true);               // the beater onto the skin
    }
    // the open hat shimmers
    if (hat_shimmer > 0.05f) {
        int n = (int)(hat_shimmer * 3.0f) + 1;
        for (int i = 0; i < n; i++) { float r = rng_uni(&pseed); oled_pixel(hx - 7 + (int)(r * 15.0f), hy - 6 - (int)(rng_uni(&pseed) * 4.0f), true); }
    }
}

static void physics(void)
{
    for (int i = 0; i < nimpacts; i++) {
        float k = 0.4f + 0.6f * (float)impacts[i].vel / 127.0f;
        bool live = impacts[i].src != SRC_LOOP;
        if (impacts[i].src == SRC_LOOP) k *= 0.6f;
        char nb[20]; const char *name = pad_label(impacts[i].pad, nb, sizeof nb);
        switch (impacts[i].pad) {
        case PD_LEFT:
            bulge_v += 40.0f * k; foot = 1.0f;
            parts_emit(&parts, 44.0f, 51.0f, 0, 0, 0.3f, PART_RING, (uint8_t)(1 + impacts[i].vel / 50));
            if (live) float_label(30, 44, name);
            break;
        case PD_RIGHT:
            arm[1] = 1.0f; snare_dy = 2.0f * k;
            for (int j = 0; j < 4; j++) parts_emit(&parts, 64.0f, 40.0f, 50.0f * rng_bi(&pseed), -40.0f - 30.0f * rng_uni(&pseed), 0.3f, PART_STREAK, 1);
            if (live) float_label(66, 31, name);
            break;
        case PD_FRONT:
            arm[0] = 1.0f; hat_gap = 0.0f; hat_shimmer = 0.3f;
            for (int j = 0; j < 3; j++) parts_emit(&parts, 16.0f, 29.0f, 40.0f * rng_bi(&pseed), -30.0f, 0.25f, PART_SPARK, 1);
            if (live) float_label(18, 20, name);
            break;
        default:
            arm[0] = 1.0f; hat_gap = 4.0f; hat_shimmer = 1.0f;
            for (int j = 0; j < 5; j++) parts_emit(&parts, 16.0f, 27.0f, 50.0f * rng_bi(&pseed), -40.0f * rng_uni(&pseed), 0.5f, PART_SPARK, 1);
            if (live) float_label(18, 20, name);
            break;
        }
    }
    nimpacts = 0;
    for (int s = 0; s < 4; s++) {                                       // the kick's skin on a spring
        const float dt = 0.01f;
        bulge_v += (-260.0f * bulge - 14.0f * bulge_v) * dt; bulge += bulge_v * dt;
    }
    bulge = clampf(bulge, -1.5f, 3.0f);
    arm[0] *= 0.72f; arm[1] *= 0.72f;
    foot *= 0.7f; snare_dy *= 0.6f;
    hat_gap += (1.0f - hat_gap) * (hat_gap < 1.0f ? 0.5f : 0.08f);
    hat_shimmer *= 0.9f;
    head_bob *= 0.6f;
    parts_step(&parts, 0.04f, 120.0f, 2.0f);
    for (int s = 0; s < NPAD; s++) flash[s] *= 0.78f;
    for (int i = 0; i < 4; i++) if (labels[i].age > 0.0f) { labels[i].age += 0.04f; if (labels[i].age > 0.6f) labels[i].age = 0.0f; }
}

static void draw_labels(void)
{
    for (int i = 0; i < 4; i++) {
        label_t *l = &labels[i];
        if (l->age <= 0.0f) continue;
        int y = (int)(l->y - ease_out(l->age / 0.6f) * 8.0f);
        if (y < 16) y = 16;
        int w = oled_ftext_w(F_TINY, l->text, 1), x = (int)l->x - w / 2;
        if (x < 0) x = 0;
        oled_rect(x - 1, y - 1, w + 2, 7, false);
        oled_ftext_ex(F_TINY, x, y, l->text, 1);
    }
}

// ---------------------------------------------------------------- drawing: the rose and the loops' orbit
// A compass of the four strokes around the gyro's live vector, so the player sees each swing register; around it
// the beat runs as a clock (a bar, or the longest loop), each loop a ring with a mark per hit, one hand for all.
#define RX 105
#define RY 40
#define ROSE_R 9
static void draw_rose(int cx, int cy, int r, bool letters)
{
    static const int ax[NPAD] = { 0, -1, 1, 0 }, ay[NPAD] = { -1, 0, 0, 1 };
    for (int p = 0; p < NPAD; p++) {
        int tx = cx + ax[p] * r, ty = cy + ay[p] * r;
        bool lit = flash[p] > 0.3f;
        // an arrowhead pointing out
        if (ay[p]) { oled_line(tx - 3, ty - ay[p] * 3, tx, ty, true); oled_line(tx + 3, ty - ay[p] * 3, tx, ty, true); if (lit) { oled_line(tx - 2, ty - ay[p] * 2, tx + 2, ty - ay[p] * 2, true); oled_line(tx - 1, ty - ay[p], tx + 1, ty - ay[p], true); } }
        else { oled_line(tx - ax[p] * 3, ty - 3, tx, ty, true); oled_line(tx - ax[p] * 3, ty + 3, tx, ty, true); if (lit) { oled_line(tx - ax[p] * 2, ty - 2, tx - ax[p] * 2, ty + 2, true); oled_line(tx - ax[p], ty - 1, tx - ax[p], ty + 1, true); } }
        if (letters) {
            char nb[20]; const char *nm = pad_label(p, nb, sizeof nb);
            int vt = (int)pv[P_FRONT + p];
            char ch[2] = { vt >= V_PART1 && vt <= V_PART4 && (int)pv[P_KBREAK] != B_DRUMS ? (char)('1' + vt - V_PART1) : nm[0], 0 };
            int lx = cx + ax[p] * (r + 4), ly = cy + ay[p] * (r + 4);
            oled_ftext_c(F_TINY, lx, ly - 2, ch, 0);
        }
    }
    if (rec.st == R_WAIT) {                                              // the count-in, in the middle
        int beats = (int)((rec.start - now_tick + PPB - 1) / PPB);
        char b[4]; snprintf(b, sizeof b, "%d", clampi(beats, 1, 9));
        oled_rect(cx - 3, cy - 4, 7, 9, false);
        oled_ftext_c(F_SMALL, cx, cy - 4, b, 0);
        return;
    }
    // the gyro now: forward is up, right is right
    float fx = gyro_now.x, lr = fabsf(gyro_now.y) > fabsf(gyro_now.z) ? gyro_now.y : gyro_now.z;
    if (pv[P_FLIP] >= 1.0f) fx = -fx;
    if (pv[P_HAND] >= 1.0f) lr = -lr;
    float sc = (float)(r - 2) / 400.0f;
    float vx = lr * sc, vy = -fx * sc, vl = sqrtf(vx * vx + vy * vy);
    if (vl > (float)(r - 2)) { vx *= (float)(r - 2) / vl; vy *= (float)(r - 2) / vl; }
    if (vl > 0.8f) { oled_line(cx, cy, cx + (int)lroundf(vx), cy + (int)lroundf(vy), true); oled_disc(cx + (int)lroundf(vx), cy + (int)lroundf(vy), 1, true); }
    else oled_pixel(cx, cy, true);
}

static void polar(int cx, int cy, float r, float frac, int *x, int *y)
{
    float rad = (frac * 360.0f - 90.0f) * 0.0174533f;
    *x = cx + (int)lroundf(r * cosf(rad)); *y = cy + (int)lroundf(r * sinf(rad));
}

static void draw_orbit(int cx, int cy)
{
    // the clock's cycle: the longest loop, the loop being recorded, or one bar
    int cyc = 1;
    for (int k = 0; k < NLOOP; k++) if (eng_loops[k].bars > cyc) cyc = eng_loops[k].bars;
    if (rec.st == R_REC || rec.st == R_CLOSING) { int b = (int)((now_tick - rec.start) / BAR_TICKS) + 1; if (b > cyc) cyc = b; }
    int64_t cyct = (int64_t)cyc * BAR_TICKS;
    float ph = (float)(now_tick - eng_floor_div(now_tick, cyct) * cyct) / (float)cyct;
    int x, y;
    for (int b = 0; b < cyc * 4; b++) {                                 // beat ticks outside, bars longer
        polar(cx, cy, 22.0f, (float)b / (float)(cyc * 4), &x, &y);
        oled_pixel(x, y, true);
        if (b % 4 == 0) { polar(cx, cy, 21.0f, (float)b / (float)(cyc * 4), &x, &y); oled_pixel(x, y, true); }
    }
    for (int k = 0; k < NLOOP; k++) {
        const loop_t *l = &eng_loops[k];
        float r = 14.0f + 2.0f * (float)k;
        bool recording = (rec.st == R_WAIT || rec.st == R_REC || rec.st == R_CLOSING) && rec.slot == k;
        if (recording) {                                                 // the ring grows from its first bar
            int64_t s = rec.start, e = rec.st == R_CLOSING ? rec.end : now_tick;
            if (e > s) {
                float a0 = (float)(s - eng_floor_div(s, cyct) * cyct) / (float)cyct * 360.0f - 90.0f;
                float a1 = a0 + fminf((float)(e - s) / (float)cyct, 1.0f) * 360.0f;
                ui_arc(cx, cy, (int)r, a0, a1);
            }
        }
        if (!l->bars) continue;
        int len = l->bars * BAR_TICKS, reps = (int)((cyct + len - 1) / len);
        if (l->on && eng_run) for (int a = 0; a < 360; a += 10) { polar(cx, cy, r, (float)a / 360.0f, &x, &y); oled_pixel(x, y, true); }   // a dotted ring: it plays
        int n = l->n;
        for (int i = 0; i < n; i++) {
            int64_t t0 = l->start + eng_hit_tick(l, &l->hits[i]);
            for (int rep = 0; rep < reps; rep++) {
                int64_t t = t0 + (int64_t)rep * len;
                float f = (float)(t - eng_floor_div(t, cyct) * cyct) / (float)cyct;
                polar(cx, cy, r, f, &x, &y);
                float since = ph - f; if (since < 0) since += 1.0f;
                if (l->on && eng_run && since < 0.03f) oled_disc(x, y, 1, true);
                else oled_pixel(x, y, true);
            }
        }
    }
    int x0, y0, x1, y1;                                                  // the hand
    polar(cx, cy, 12.0f, ph, &x0, &y0); polar(cx, cy, 22.0f, ph, &x1, &y1);
    oled_line(x0, y0, x1, y1, true);
}

static void draw_antenna(int x, int y)                                  // a board heard on the link
{
    oled_line(x, y, x, y + 5, true);
    oled_pixel(x - 2, y - 1, true); oled_pixel(x + 2, y - 1, true);
    oled_pixel(x - 1, y, true); oled_pixel(x + 1, y, true);
    oled_pixel(x - 3, y + 1, true); oled_pixel(x + 3, y + 1, true);
}

// ---------------------------------------------------------------- drawing: the sampling flow and the swing setup
static void draw_sampling(const view_t *v)
{
    char b[48];
    if (smp.st == S_LISTEN) {
        bool rec_now = eng_cap_state == CAP_REC;
        ui_head(rec_now ? "REC" : "LISTEN", true);
        snprintf(b, sizeof b, "%s PAD", pad_name[smp.pad]);
        oled_ftext_c(F_SMALL, 44, 19, b, 1);
        float e = clampf((20.0f * log10f(eng_in_env + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
        oled_rframe(4, 31, 80, 9, 2);
        int w = (int)(e * 76.0f);
        if (w > 0) oled_rect(6, 33, w, 5, true);
        if (rec_now) {
            float s = (float)eng_cap_len / (float)AUDIO_SR;
            snprintf(b, sizeof b, "%.2f S", (double)s);
            oled_ftext_c(F_TINY, 44, 43, b, 1);
            int pw = (int)(80.0f * s / pv[P_SMAX]);
            oled_rect(4, 51, clampi(pw, 0, 80), 2, true);
        } else {
            oled_ftext_c(F_TINY, 44, 43, ((v->frame >> 3) & 1) ? "MAKE A SOUND" : "", 1);
        }
        flash[smp.pad] = 1.0f;
        draw_rose(RX, RY, ROSE_R, true);
        if (!v->cover) oled_ftext_c(F_TINY, 64, 56, rec_now ? "TAP: THAT IS ENOUGH" : "TAP: NEVER MIND", 0);
        return;
    }
    ui_head("KEEP", true);
    snprintf(b, sizeof b, "%s PAD: %.2f S", pad_name[smp.pad], (double)smp.len / AUDIO_SR);
    oled_ftext_c(F_SMALL, 64, 22, b, 1);
    // the sample's shape
    int n = smp.len; const int16_t *buf = eng_pad[smp.pad];
    if (buf && n > 0) for (int x = 0; x < 100; x++) {
        int i0 = x * n / 100, i1 = (x + 1) * n / 100, pk = 0;
        for (int i = i0; i < i1 && i < n; i += 8) { int a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > pk) pk = a; }
        int h = (int)((float)pk * eng_pad_gain[smp.pad] / 32768.0f * 10.0f); if (h > 10) h = 10;
        oled_line(14 + x, 42 - h, 14 + x, 42 + h, true);
    }
    snprintf(b, sizeof b, "KEEPING IT. NEXT: %s", pad_name[next_pad()]);
    oled_ftext_c(F_TINY, 64, 55, b, 1);
}

static void draw_setup(const view_t *v)
{
    char b[40];
    ui_head("SWING SETUP", true);
    if (cal.st == 1) {
        oled_ftext_c(F_SMALL, 44, 20, "SWING FORWARD", 1);
        oled_ftext_c(F_TINY, 44, 31, "FOUR TIMES, AS YOU", 1);
        oled_ftext_c(F_TINY, 44, 39, "WOULD PLAY", 1);
        for (int i = 0; i < 4; i++) { if (i < cal.n) oled_disc(32 + i * 8, 50, 2, true); else oled_circle(32 + i * 8, 50, 2, true); }
        draw_rose(RX, RY, ROSE_R, true);
        if (!v->cover) oled_ftext_c(F_TINY, 64, 56, "TAP: NEVER MIND", 0);
    } else {
        oled_ftext_c(F_SMALL, 64, 22, "SWING SET", 1);
        snprintf(b, sizeof b, "FULL AT %d, A HIT FROM %d", (int)pv[P_CALV], (int)pv[P_CALH]);
        oled_ftext_c(F_TINY, 64, 36, b, 1);
        oled_ftext_c(F_TINY, 64, 46, "SENSE STILL SCALES IT", 1);
    }
}

// ---------------------------------------------------------------- drawing: the play screen
static int frames_drawn;
static void draw(const view_t *v)
{
    frames_drawn++;
    physics();
    if (cal.st) { draw_setup(v); return; }
    if (smp.st != S_OFF) { draw_sampling(v); return; }
    // the head bobs on the beat
    {
        static int64_t last_beat = -1;
        int64_t b = eng_floor_div(now_tick, PPB);
        if (b != last_beat) { head_bob = (b % 4 == 0) ? 1.8f : 1.2f; last_beat = b; }
    }
    // the header: the loops' state, or the name
    char hb[20];
    int nl = loops_count();
    switch (rec.st) {
    case R_WAIT: snprintf(hb, sizeof hb, "REC IN %d", clampi((int)((rec.start - now_tick + PPB - 1) / PPB), 1, 9)); ui_head(hb, true); break;
    case R_REC: case R_CLOSING: { int b = (int)((now_tick - rec.start) / BAR_TICKS) + 1; if (rec.st == R_CLOSING) b = (int)((rec.end - rec.start) / BAR_TICKS); snprintf(hb, sizeof hb, "REC %d BAR%s", b, b > 1 ? "S" : ""); ui_head(hb, true); break; }
    default:
        if (!nl) ui_head(NULL, false);
        else if (!eng_run) ui_head("STOP", false);
        else { int on = 0; for (int k = 0; k < NLOOP; k++) on += eng_loops[k].bars && eng_loops[k].on; snprintf(hb, sizeof hb, "LOOP %d/%d", on, nl); ui_head(hb, false); }
        break;
    }
    draw_drummer(DX, 0, true);
    parts_draw(&parts);
    draw_labels();
    draw_rose(RX, RY, ROSE_R, true);
    draw_orbit(RX, RY);
    if (peers_now > 0 && (!link_tempo_clash() || (v->frame & 4))) {   // blinks while our loops and the flock disagree on the tempo
        draw_antenna(80, 17);
        if (peers_now > 1) { snprintf(hb, sizeof hb, "%d", peers_now); oled_ftext_ex(F_TINY, 85, 16, hb, 0); }
    }
    // what the stick is doing
    static const char *const div_l[] = { "", "", "1/8", "", "1/16", "", "", "", "1/32" };
    if (roll_div) { snprintf(hb, sizeof hb, "ROLL %s", div_l[roll_div]); oled_pill(F_TINY, 48, 17, hb, true); }
    else if (stut_div) { snprintf(hb, sizeof hb, "REPEAT %s", div_l[stut_div]); oled_pill(F_TINY, 48, 17, hb, true); }
    else if (fabsf(eng_filter) > 0.05f) { snprintf(hb, sizeof hb, "%s %d", eng_filter < 0 ? "LP" : "HP", (int)(fabsf(eng_filter) * 100.0f)); oled_pill(F_TINY, 48, 17, hb, false); }
    else if (fabsf(lean_tune) >= 0.5f) { snprintf(hb, sizeof hb, "%+d ST", (int)lroundf(lean_tune)); oled_pill(F_TINY, 48, 17, hb, false); }
    else if (kit_rendering()) { snprintf(hb, sizeof hb, "PRINT %d%%", kit_rendering()); oled_pill(F_TINY, 48, 17, hb, false); }
}

// ---------------------------------------------------------------- splash: a drummer with no kit, then a kit
static bool splash(int f)
{
    static float sarm[2], sbulge, shat;
    if (f == 0) { parts.n = 0; sarm[0] = sarm[1] = sbulge = shat = 0; }
    // the figure flails at nothing, then the pieces land one by one, each with a hit
    bool kick = f >= 14, snare = f >= 26, hat = f >= 38;
    if (f < 14) { sarm[0] = 0.5f + 0.5f * sinf((float)f * 0.9f); sarm[1] = 0.5f + 0.5f * sinf((float)f * 0.9f + 2.0f); }
    if (f == 14) { sbulge = 3.0f; parts_emit(&parts, 44, 51, 0, 0, 0.4f, PART_RING, 3); }
    if (f == 26) { sarm[1] = 1.0f; for (int j = 0; j < 6; j++) parts_emit(&parts, 64, 40, 60.0f * rng_bi(&pseed), -50.0f * rng_uni(&pseed), 0.4f, PART_STREAK, 1); }
    if (f == 38) { sarm[0] = 1.0f; shat = 1.0f; for (int j = 0; j < 6; j++) parts_emit(&parts, 16, 28, 50.0f * rng_bi(&pseed), -40.0f * rng_uni(&pseed), 0.5f, PART_SPARK, 1); }
    if (f > 14) { sarm[0] *= 0.8f; sarm[1] *= 0.8f; sbulge *= 0.8f; shat *= 0.9f; }
    if (f >= 56 && (f % 12) == 0) { sarm[f / 12 & 1] = 1.0f; }         // then it plays
    arm[0] = sarm[0]; arm[1] = sarm[1]; bulge = sbulge; hat_shimmer = shat; hat_gap = 1.0f + 3.0f * shat;
    parts_step(&parts, 0.04f, 90.0f, 1.5f);
    // the kit appears piece by piece: draw the figure, then the pieces that have landed
    oled_hline_pat(4, 58, 66, 0x55, true);
    if (snare) { oled_frame(56, 40, 17, 5); oled_line(64, 45, 64, 57, true); oled_line(60, 57, 68, 57, true); }
    if (hat) { int g = (int)lroundf(hat_gap); oled_line(16, 32, 16, 57, true); oled_line(10, 31, 22, 31, true); oled_line(10, 31 - g - 1, 16, 31 - g - 2, true); oled_line(16, 31 - g - 2, 22, 31 - g - 1, true); }
    draw_drummer(0, 0, false);
    if (kick) { int kr = 8 + (int)lroundf(sbulge); oled_disc(44, 51, kr, false); oled_circle(44, 51, kr, true); oled_circle(44, 51, 3, true); oled_line(kr + 45, 57, kr + 50, 57, true); oled_line(kr + 45, 57, kr + 43, 52, true); }
    parts_draw(&parts);
    if (f < 14) { const char *q = (f / 4) & 1 ? "?" : ""; oled_ftext_ex(F_SMALL, 50, 16, q, 1); }
    // the title lands with a bounce, the tagline after it
    if (f >= 40) {
        float t = ease_bounce(clampf((float)(f - 40) / 16.0f, 0.0f, 1.0f));
        int y = (int)(-14.0f + 36.0f * t);
        oled_ftext_ex(F_MED, 86, y, "AIR", 3); oled_ftext_ex(F_MED, 87, y, "AIR", 3);   // twice: bold
        if (f >= 60) { oled_ftext_ex(F_TINY, 84, y + 15, "DRUM ON", 1); oled_ftext_ex(F_TINY, 84, y + 22, "NOTHING", 1); }
    }
    return f < 96;
}

// ---------------------------------------------------------------- screensaver: the drummer keeps time in the dark
static void saver(uint32_t f)
{
    static float x = 20, vx = 9;
    static parts_t sp;
    const float dt = 0.04f;
    x += vx * dt;
    if (x < 0 || x > 56) { vx = -vx; x = clampf(x, 0, 56); }
    uint32_t s = (uint32_t)(f % 32);
    if (s == 0) { arm[1] = 1.0f; snare_dy = 2.0f; }
    if (s == 16) { foot = 1.0f; bulge = 2.5f; parts_emit(&sp, x + 36.0f, 50.0f - 8.0f, 0, 0, 0.3f, PART_RING, 2); }
    if ((s & 7) == 4) { arm[0] = 1.0f; hat_gap = 0.0f; }
    if (s == 0 || s == 16) head_bob = 1.4f;
    arm[0] *= 0.72f; arm[1] *= 0.72f; foot *= 0.7f; snare_dy *= 0.6f; bulge *= 0.8f; head_bob *= 0.6f;
    hat_gap += (1.0f - hat_gap) * 0.5f;
    parts_step(&sp, dt, 0, 0);
    draw_drummer((int)x, -8, true);
    parts_draw(&sp);
}

const instrument_t INST = {
    .name = "AIR", .tagline = "DRUM ON NOTHING",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 6,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .looper = false,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver, .hold = hold,
};

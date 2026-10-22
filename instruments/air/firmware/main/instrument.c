// AIR: drum on nothing. The box is the stick. A swing forward is the hi-hat, left the kick, right the snare, back
// the open hat: the gyro sees each stroke, which way it went and how fast, and the drum sounds as the stroke
// peaks. Each pad is a drum (KNOCK's voices) or a sample of your own: hold the knob, hit the air for the pad,
// make a sound. REC is an event looper: hold it while you play, let go and it loops; hold again to add, tap to
// stop and play. On the link the loop is whole beats of the flock's tempo and sits on its beat; alone, the loop
// sets the tempo for the others. The stick: up a roll of the last drum, down a beat repeat, left and right a filter.
// Tilt the bottom edge down and the drums tune up.
//
// The sound engine is inst_engine.c, the samples in flash inst_store.c; this file is the UI side: params, sounds,
// the gesture detector, the looper, the link, the sampling flow, MIDI, the LED and the screens.
#include "core.h"
#include "dsp.h"
#include "ui.h"
#include "link.h"
#include "inst_air.h"
#include "inst_kit.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "PADS", "BREAK", "FEEL", "SAMPLE", "LOOP", "BUS", "LINK" };
enum { G_PADS, G_BREAK, G_FEEL, G_SAMPLE, G_LOOP, G_BUS, G_LINK };
static const char *const voices_l[NVTYPE] = { "SAMPLE", "KICK", "SNARE", "HAT", "OPEN HAT", "TOM", "CLAP", "RIM", "COWBELL", "SHAKER", "CONGA", "ZAP", "PART 1", "PART 2", "PART 3", "PART 4", "OFF" };
static const char *const print_l[] = { "CLEAN", "TAPE", "VINYL", "12-BIT", "DUSTY" };
static const char *const length_l[] = { "AUTO", "1 BAR", "2 BARS", "4 BARS" };
static const char *const slices_l[] = { "AUTO", "8", "16", "32" };
static const char *const quant_l[] = { "OFF", "1/8", "1/16" };
static const char *const hands_l[] = { "ONE", "LEFT", "RIGHT" };
static const char *const vel_l[] = { "SOFT", "NORMAL", "HARD", "FIXED" };
static const char *const hand_l[] = { "KICK LEFT", "KICK RIGHT" };
static const char *const flip_l[] = { "TIP AWAY", "TIP TO ME" };
static const char *const tilt_l[] = { "OFF", "TUNE" };
static const char *const joyy_l[] = { "OFF", "ROLL", "ROLL+REPEAT" };
static const char *const ends_l[] = { "SOUND DIES", "AT LONGEST" };
static const char *const beats_l[] = { "AUTO", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };
static const char *const grid_l[] = { "OFF", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };
#define SYS (PF_SYS | PF_NOCC)

static const param_t params[P_N] = {
    [P_FRONT]  = { "FRONT", "pfrt", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_HAT, "", voices_l, "HIT FORWARD: THIS DRUM" },
    [P_LEFT]   = { "LEFT", "plft", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_KICK, "", voices_l, "HIT LEFT: THIS DRUM" },
    [P_RIGHT]  = { "RIGHT", "prgt", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_SNARE, "", voices_l, "HIT RIGHT: THIS DRUM" },
    [P_BACK]   = { "BACK", "pbck", G_PADS, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_OHAT, "", voices_l, "HIT BACK: THIS DRUM" },
    [P_LEVEL]  = { "LEVEL", "plvl", G_PADS, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE DRUMS ARE" },
    [P_TUNE]   = { "TUNE", "tune", G_PADS, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "EVERYTHING UP OR DOWN" },
    [P_DECAY]  = { "DECAY", "dcay", G_PADS, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "SHORTER OR LONGER DRUMS" },
    [P_PUNCH]  = { "CLICK", "clik", G_PADS, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW HARD THE KICK STARTS" },
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
    [P_KLENGTH] = { "LENGTH", "klen", G_BREAK, PK_ENUM, PF_SYS | PF_HIDE | PF_NOCC, 0, 3, 1, 0, "", length_l, "" },
    [P_KSLICES] = { "SLICES", "ksli", G_BREAK, PK_ENUM, PF_SYS | PF_HIDE | PF_NOCC, 0, 3, 1, 0, "", slices_l, "" },
    [P_KGATE]   = { "GATE", "kgat", G_BREAK, PK_NUM, PF_INT | PF_SYS | PF_HIDE | PF_NOCC, -60, -12, 2, -36, "dB", NULL, "" },
    [P_STRETCH] = { "STRETCH", "kstr", G_BREAK, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "A BREAK AT THE BEAT'S SPEED" },
    [P_SENS]   = { "SENSE", "sens", G_FEEL, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW SMALL A SWING COUNTS" },
    [P_VEL]    = { "VELOCITY", "velc", G_FEEL, PK_ENUM, 0, 0, 3, 1, 1, "", vel_l, "HOW HARD TO SWING FOR LOUD" },
    [P_HAND]   = { "SIDES", "hand", G_FEEL, PK_ENUM, 0, 0, 1, 1, 0, "", hand_l, "WHICH SIDE IS THE KICK" },
    [P_FLIP]   = { "FORWARD", "flip", G_FEEL, PK_ENUM, 0, 0, 1, 1, 0, "", flip_l, "WHICH WAY IS IN FRONT" },
    [P_TILT]   = { "TILT", "tilt", G_FEEL, PK_ENUM, 0, 0, 1, 1, 1, "", tilt_l, "BOTTOM EDGE DOWN: TUNED UP" },
    [P_JOYY]   = { "STICK Y", "joyy", G_FEEL, PK_ENUM, 0, 0, 2, 1, 2, "", joyy_l, "UP ROLLS, DOWN REPEATS" },
    [P_QUANT]  = { "QUANTIZE", "qunt", G_FEEL, PK_ENUM, 0, 0, 2, 1, 2, "", quant_l, "HITS WAIT FOR THE GRID" },
    [P_TEMPO]  = { "TEMPO", "tmpo", G_FEEL, PK_NUM, PF_INT, 60, 200, 1, 136, "", NULL, "THE GRID ALONE, NO LOOP" },
    [P_SAMPLE] = { "SAMPLE", "smpl", G_SAMPLE, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "HIT THE AIR, MAKE A SOUND" },
    [P_SMAX]   = { "LONGEST", "smax", G_SAMPLE, PK_NUM, PF_LOG, 0.25f, 1.0f, 1.19f, 1.0f, "s", NULL, "A SAMPLE IS CUT HERE" },
    [P_SKEEP]  = { "ENDS", "send", G_SAMPLE, PK_ENUM, 0, 0, 1, 1, 0, "", ends_l, "WHEN A SAMPLE STOPS" },
    [P_FORGET] = { "FORGET ALL", "sfgt", G_SAMPLE, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "SAMPLES GONE, DRUMS BACK" },
    [P_BEATS]  = { "BEATS", "beat", G_LOOP, PK_ENUM, 0, 0, 16, 1, 0, "", beats_l, "AUTO: IT GUESSES" },
    [P_GRID]   = { "GRID", "grid", G_LOOP, PK_ENUM, 0, 0, 6, 1, 4, "", grid_l, "WHAT HITS ARE PULLED TO" },
    [P_SNAP]   = { "STRENGTH", "snap", G_LOOP, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "HOW HARD THEY ARE PULLED" },
    [P_SWING]  = { "SWING", "swng", G_LOOP, PK_NUM, PF_INT, 0, 75, 5, 0, "%", NULL, "LATE OFFBEATS" },
    [P_MUTATE] = { "MUTATE", "mutt", G_LOOP, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HITS DROP OUT OR CHANGE" },
    [P_LLEVEL] = { "LOOP LEVEL", "llvl", G_LOOP, PK_NUM, PF_INT, 0, 100, 5, 90, "%", NULL, "HOW LOUD THE LOOP PLAYS" },
    [P_UNDO]   = { "UNDO LAST", "undo", G_LOOP, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "TAKE AWAY THE LAST LAYER" },
    [P_CLEAR]  = { "CLEAR LOOP", "clrl", G_LOOP, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE LOOP" },
    [P_FILTER] = { "FILTER", "filt", G_BUS, PK_NUM, PF_INT | PF_SIGNED, -100, 100, 5, 0, "%", NULL, "DOWN: DARK  UP: THIN" },
    [P_DRIVE]  = { "DRIVE", "drve", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "DIRT" },
    [P_CRUSH]  = { "CRUSH", "crsh", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "FEWER BITS, A LOWER RATE" },
    [P_COMP]   = { "PUNCH", "comp", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "SQUEEZE: FATTER, LOUDER" },
    [P_ECHO]   = { "ECHO", "echo", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HOW MUCH GOES TO THE ECHO" },
    [P_ETIME]  = { "ECHO TIME", "etim", G_BUS, PK_NUM, PF_LOG, 0.04f, 0.7f, 1.06f, 0.25f, "s", NULL, "TIME BETWEEN REPEATS" },
    [P_EFB]    = { "REPEATS", "efbk", G_BUS, PK_NUM, PF_INT, 0, 90, 5, 35, "%", NULL, "HOW LONG THE ECHO GOES ON" },
    [P_ROOM]   = { "ROOM", "room", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 18, "%", NULL, "HOW MUCH ROOM" },
    [P_RSIZE]  = { "ROOM SIZE", "rsiz", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SMALL BOX TO BIG HALL" },
    [P_GRAIN]  = { "GRAIN", "grai", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE LAST MOMENT, IN PIECES" },
    [P_GSIZE]  = { "GRAIN SIZE", "gsiz", G_BUS, PK_NUM, PF_LOG | PF_INT, 10, 250, 1.15f, 60, "ms", NULL, "HOW LONG EACH PIECE IS" },
    [P_LINK]   = { "LINK", "nlnk", G_LINK, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]  = { "GROUP", "ngrp", G_LINK, PK_NUM, SYS | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]  = { "RANGE", "npow", G_LINK, PK_ENUM, SYS, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_HANDS]  = { "HANDS", "hnds", G_LINK, PK_ENUM, SYS, 0, 2, 1, 0, "", hands_l, "TWO BOARDS: A BAR EACH" },
    [P_PEERS]  = { "WHO IS THERE", "nwho", G_LINK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
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
    { "TECHNO",        "pfrt=3 plft=1 prgt=6 pbck=4 dcay=1.2 snap=100 echo=20 etim=0.32 efbk=45 comp=60 drve=15" },
    { "HAND DRUM",     "pfrt=9 plft=10 prgt=5 pbck=10 plvl=70 dcay=1.2 room=20" },
    { "COWBELL FEVER", "pfrt=8 plft=1 prgt=8 pbck=6 room=20 comp=45" },
    { "ZAP GUN",       "pfrt=11 plft=11 prgt=11 pbck=11 echo=35 etim=0.18 efbk=55 room=15" },
    { "TRASH CAN",     "pfrt=7 plft=5 prgt=6 pbck=11 drve=55 comp=50 room=35 rsiz=80" },
    { "DUB",           "pfrt=3 plft=1 prgt=7 pbck=4 echo=45 etim=0.375 efbk=60 filt=-30 room=30 rsiz=70" },
    { "MY SAMPLES",    "pfrt=0 plft=0 prgt=0 pbck=0" },
    { "HALF MINE",     "pfrt=14 plft=0 prgt=0 pbck=15" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_HIT = LEARN_INST, L_FOUR, L_SAMPLE };
static const hint_t hints[] = {
    { "SWING THE BOX LIKE A STICK", L_HIT },
    { "FORWARD, LEFT, RIGHT, BACK: FOUR DRUMS", L_FOUR },
    { "HOLD REC AND PLAY: A LOOP", LEARN_REC },
    { "HOLD THE KNOB: SAMPLE A PAD", L_SAMPLE },
    { "BREAK IN THE MENU: THE AMEN IN FOUR", LEARN_MENU },
    { "TURN THE KNOB FOR ANOTHER KIT", LEARN_TURN },
    { "PUSH THE STICK UP: A DRUM ROLL", LEARN_JOY },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

// ---------------------------------------------------------------- UI state
static uint32_t now_frames;
static int   last_pad = PD_RIGHT, last_vel = 100;
static uint8_t pads_seen;
static int   peers_now;
static float lean_tune;                  // the tilt's tuning, taken before a stroke

// the looper
enum { L_EMPTY, L_REC, L_PLAY, L_DUB, L_STOP };
static int lstate, layer_n;
static bool rec_pending;
static uint32_t rec_start;
typedef struct { uint32_t frame; uint8_t pad, vel; } prov_t;
static prov_t prov[16];
static int nprov;
static int press_state;

// the sampling flow
enum { S_OFF, S_PICK, S_LISTEN, S_SAVE };
static struct { int st, pad, ms, len; bool muted; } smp;

// what the screen shows
typedef struct { uint8_t pad, vel, src; } impact_t;
static impact_t impacts[16];
static int nimpacts;
static float flash[NPAD];
static float led_lvl, led_rgb_c[3];
static int   roll_div, stut_div;         // what the stick is doing, for the screen
static vec3_t gyro_now; static float gyro_mag_now;
static int   gest_state;                 // the detector's state, for the screen and the tests
int air_gest_state(void) { return gest_state; }
int air_peers(void) { return peers_now; }

// ---------------------------------------------------------------- the looper
static void loop_flag(void) { link_set_flags(eng_loop_len ? LINKF_LOOP : 0); }

static void loop_clear(void)
{
    __atomic_store_n(&eng_loop_len, 0, __ATOMIC_RELEASE);
    int n = eng_nhits;
    for (int i = 0; i < n; i++) eng_hits[i].dead = 1;
    __atomic_store_n(&eng_nhits, 0, __ATOMIC_RELEASE);
    eng_beats_fixed = 0;
    lstate = L_EMPTY; layer_n = 0; rec_pending = false; nprov = 0;
    loop_flag();
}

static void add_hit(uint32_t frame, int pad, int vel)
{
    int n = eng_nhits;
    if (n >= MAXHITS) { toast("LOOP IS FULL", 1000); return; }
    hit_t h = { 0 };
    uint32_t len = eng_loop_len;
    if (lstate == L_REC) {
        if ((int32_t)(frame - rec_start) < 0) return;
        h.off = frame - rec_start; h.born = 0;
    } else if (lstate == L_DUB && len) {
        h.off = (frame - eng_loop_start) % len; h.born = (uint16_t)eng_pass(frame);
    } else return;
    h.pad = (uint8_t)pad; h.vel = (uint8_t)vel; h.layer = (uint8_t)layer_n; h.dead = 0;
    eng_hits[n] = h;
    __atomic_store_n(&eng_nhits, n + 1, __ATOMIC_RELEASE);
}

// a hit that sounded: into the loop when recording, or kept aside while a press might still be a tap
static void record_hit(uint32_t frame, int pad, int vel)
{
    if (rec_pending) { if (nprov < 16) prov[nprov++] = (prov_t){ frame, (uint8_t)pad, (uint8_t)vel }; return; }
    if (lstate == L_REC || lstate == L_DUB) add_hit(frame, pad, vel);
}

static void loop_undo(void)
{
    if (layer_n <= 1) { loop_clear(); toast("LOOP CLEARED", 900); return; }
    int n = eng_nhits;
    for (int i = 0; i < n; i++) if (eng_hits[i].layer == layer_n && !eng_hits[i].dead) eng_hits[i].dead = 1;
    layer_n--;
    toast("LAST LAYER GONE", 900);
}

// the beat of the flock nearest to a frame (the session's grid, in our frames)
static uint32_t nearest_beat(uint32_t frame)
{
    uint32_t o; float f; link_origin(&o, &f);
    uint32_t fpb = link_fpb();
    if (!fpb) return frame;
    int32_t rel = (int32_t)(frame - o);
    int32_t k = (int32_t)((float)rel / (float)fpb + (rel >= 0 ? 0.5f : -0.5f));
    return o + (uint32_t)(k * (int32_t)fpb);
}
static uint32_t next_beat(uint32_t frame)
{
    uint32_t b = nearest_beat(frame);
    return (int32_t)(b - frame) >= 0 ? b : b + link_fpb();
}

// the take is over: trim it to whole beats of the flock's tempo (with boards around), or take it as played and
// tell the flock its tempo (alone)
static void loop_close(uint32_t end)
{
    uint32_t len = end - rec_start;
    int n = eng_nhits;
    if (len < AUDIO_SR / 4 || n == 0) { loop_clear(); if (len >= AUDIO_SR / 4) toast("NO HITS: NO LOOP", 1200); return; }
    uint32_t start = rec_start, fpb = link_fpb();
    int beats;
    if (peers_now > 0 && fpb) {
        // on the flock's grid: the loop starts on the beat nearest the press and is whole beats long
        start = nearest_beat(rec_start);
        beats = (int)((float)len / (float)fpb + 0.5f); if (beats < 1) beats = 1; if (beats > 64) beats = 64;
        len = (uint32_t)beats * fpb;
        for (int i = 0; i < n; i++) {
            uint32_t abs = rec_start + eng_hits[i].off;
            eng_hits[i].off = (abs - start) % len;
        }
        eng_beats_fixed = beats;
    } else {
        // alone: the first hit within a moment of the press is the downbeat, and the loop sets the tempo
        uint32_t first = len;
        for (int i = 0; i < n; i++) if (eng_hits[i].off < first) first = eng_hits[i].off;
        uint32_t shift = first < (uint32_t)(0.12f * (float)AUDIO_SR) ? first : 0;
        if (shift) for (int i = 0; i < n; i++) eng_hits[i].off -= shift;
        start = rec_start + shift; len -= shift;
        eng_beats_fixed = 0;
        __atomic_store_n(&eng_loop_len, len, __ATOMIC_RELEASE);
        beats = eng_beats();
        fpb = len / (uint32_t)beats;
        len = fpb * (uint32_t)beats;                                   // whole frames per beat, so the flock's grid is exact
        for (int i = 0; i < n; i++) if (eng_hits[i].off >= len) eng_hits[i].off = len - 1;
        eng_beats_fixed = beats;
        if (!(link_flags_any() & LINKF_LOOP)) link_set_time(fpb, start, 4, beats);
    }
    __atomic_store_n(&eng_loop_start, start, __ATOMIC_RELEASE);
    eng_loop_run = 1;
    __atomic_store_n(&eng_loop_len, len, __ATOMIC_RELEASE);
    lstate = L_PLAY;
    loop_flag();
}

static void rec_logic(const ctl_t *c)
{
    if (smp.st != S_OFF) return;                                       // REC means nothing while a pad listens
    if (c->rec_press) {
        rec_pending = true; nprov = 0; press_state = lstate;
        rec_start = c->frames;
    }
    if (rec_pending && c->rec && c->rec_ms >= 300) {                    // a hold, not a tap
        rec_pending = false;
        if (press_state == L_EMPTY || press_state == L_STOP) {
            if (press_state == L_STOP) loop_clear();
            lstate = L_REC; layer_n = 1;
        } else { lstate = L_DUB; layer_n++; }
        for (int i = 0; i < nprov; i++) add_hit(prov[i].frame, prov[i].pad, prov[i].vel);
        nprov = 0;
    }
    if (c->rec_release) {
        if (rec_pending) {                                                // a tap: stop or play
            rec_pending = false; nprov = 0;
            if (lstate == L_PLAY) { lstate = L_STOP; eng_loop_run = 0; }
            else if (lstate == L_STOP) {
                int n = eng_nhits;
                for (int i = 0; i < n; i++) eng_hits[i].born = 0xFFFF;
                uint32_t start = peers_now > 0 ? next_beat(c->frames) : c->frames;   // with the flock: from its next beat
                __atomic_store_n(&eng_loop_start, start, __ATOMIC_RELEASE);
                eng_loop_run = 1; lstate = L_PLAY;
            }
        } else if (lstate == L_REC) loop_close(c->frames);
        else if (lstate == L_DUB) lstate = L_PLAY;
    }
}

// ---------------------------------------------------------------- the strokes
// The gyro at 100 Hz, in the screen's axes. A stroke: the rate climbs over the onset threshold, peaks and falls;
// the hit fires as soon as the rate has dropped 15 % from a peak high enough to count, with the stroke's direction
// (the rate summed over the stroke, weighted by its size) picking the pad: about the x axis (the bottom edge
// rising) is forward, about y or z (the box swung sideways) left or right. The hand coming back from a stroke
// turns the other way, slower: a stroke opposite to the last one, soon after it and weaker, is that return and
// is not a hit.
enum { G_IDLE, G_STROKE, G_DONE };
static struct {
    int st, ms, since_ms, last_pad; float peak, last_peak; vec3_t acc;
    float back_deg; int still_ms;         // since the last hit: how far the box turned back on that stroke's axis, and how long it has been still
    float last_axis_sign; int last_axis;  // the last hit's axis (0: x, 1: y, 2: z) and which way it turned
    bool armed;                           // the box came back (or rested) since the last hit: the same pad may sound again
} G = { G_IDLE, 0, 10000, -1, 0, 0, { 0, 0, 0 }, 0, 0, 0, 0, true };

static void hit_pad(int pad, int vel, uint32_t frame);
static void sample_picked(int pad);

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

static void gestures(const ctl_t *c)
{
    gyro_now = c->gyro; gyro_mag_now = c->gyro_mag;
    if (!c->has_imu || c->frozen) { G.st = G_IDLE; gest_state = G_IDLE; return; }
    float sens = pv[P_SENS] * 0.01f;
    float t_on = 80.0f + (1.0f - sens) * 200.0f;                       // 80..280 deg/s (SENSE 50: 180)
    float t_hit = t_on * 1.8f;                                         // (SENSE 50: 324)
    float v_full = 600.0f + (1.0f - sens) * 800.0f;                    // the peak that is full velocity (SENSE 50: 1000)
    float mag = c->gyro_mag;
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
        if (mag > t_on) { G.st = G_STROKE; G.peak = mag; G.ms = 0; G.acc = (vec3_t){ c->gyro.x * mag, c->gyro.y * mag, c->gyro.z * mag }; }
        break;
    case G_STROKE:
        G.ms += 10;
        G.acc.x += c->gyro.x * mag; G.acc.y += c->gyro.y * mag; G.acc.z += c->gyro.z * mag;
        if (mag > G.peak) G.peak = mag;
        if (G.peak >= t_hit && mag < 0.85f * G.peak) {
            float fx = G.acc.x, lr = fabsf(G.acc.y) > fabsf(G.acc.z) ? G.acc.y : G.acc.z;
            int axis = fabsf(fx) >= fabsf(lr) ? 0 : fabsf(G.acc.y) > fabsf(G.acc.z) ? 1 : 2;
            float sign = (axis == 0 ? fx : lr) > 0.0f ? 1.0f : -1.0f;
            int pad;
            if (axis == 0) pad = (fx > 0.0f) != (pv[P_FLIP] >= 1.0f) ? PD_FRONT : PD_BACK;
            else pad = (lr > 0.0f) != (pv[P_HAND] >= 1.0f) ? PD_RIGHT : PD_LEFT;
            bool is_return = G.last_pad >= 0 && pad == opposite(G.last_pad) && G.since_ms < 300 && G.peak < 0.8f * G.last_peak;
            bool reset = G.last_pad < 0 || pad != G.last_pad || G.armed;
            if (!is_return && reset) {
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

// the grid hits are pulled to: the loop's beat, else the flock's, else TEMPO from boot; QUANTIZE says how fine
static void grid_now(uint32_t *fpb, uint32_t *origin)
{
    if (eng_loop_len) { *fpb = eng_loop_len / (uint32_t)eng_beats(); *origin = eng_loop_start; return; }
    if (peers_now > 0 && link_fpb()) { float f; link_origin(origin, &f); *fpb = link_fpb(); return; }
    *fpb = (uint32_t)(60.0f * (float)AUDIO_SR / pv[P_TEMPO]); *origin = 0;
}
static float grid_bpm(void) { uint32_t f, o; grid_now(&f, &o); return f ? 60.0f * (float)AUDIO_SR / (float)f : 120.0f; }
static uint32_t quantized(uint32_t frame)
{
    int q = (int)pv[P_QUANT];
    if (!q) return 0;
    uint32_t fpb, o; grid_now(&fpb, &o);
    uint32_t step = fpb / (q == 1 ? 2 : 4);
    if (!step) return 0;
    int32_t rel = (int32_t)(frame - o);
    int32_t k = rel >= 0 ? (rel + (int32_t)step - 1) / (int32_t)step : -((-rel) / (int32_t)step);
    uint32_t at = o + (uint32_t)(k * (int32_t)step);
    if ((int32_t)(at - frame) < 64) at += step;                        // the block that is playing now is too late
    return at;
}

static void hit_pad(int pad, int vel, uint32_t frame)
{
    if (smp.st == S_PICK) { sample_picked(pad); return; }
    if (smp.st != S_OFF) return;
    eng_tune = pv[P_TUNE] + lean_tune;
    uint32_t at = quantized(frame);
    trig_t t = { (uint8_t)pad, (uint8_t)vel, SRC_LIVE, at };
    eng_trigger(&t);
    record_hit(at ? at : frame, pad, vel);
    last_pad = pad; last_vel = vel;
    coach_done(L_HIT);
    pads_seen |= (uint8_t)(1u << pad);
    if (pads_seen == 0x0F) coach_done(L_FOUR);
    if (pv[P_FRONT + pad] < 1.0f && eng_pad_len[pad] == 0) toast("EMPTY PAD: HOLD THE KNOB TO SAMPLE", 1500);
    ui_awake();
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

static void sample_start(void)
{
    if (!eng_pad[0] || !eng_pad[NPAD - 1]) { toast("NO MEMORY FOR SAMPLES", 1500); return; }
    if (smp.st != S_OFF) return;
    menu_leave();
    knob_grab(true);
    smp.st = S_PICK; smp.ms = 0; smp.pad = -1;
}

static void sample_end(void)
{
    if (smp.muted) { store_mute(false); smp.muted = false; }
    smp.st = S_OFF;
    knob_grab(false);
}

static void sample_picked(int pad)
{
    smp.pad = pad; smp.ms = 0;
    eng_cap_hold = pv[P_SKEEP] >= 1.0f ? 1 : 0;
    eng_capture(pad, pv[P_SMAX]);
    smp.st = S_LISTEN;
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
    char b[40]; snprintf(b, sizeof b, "%s PAD: %.2f S", pad_name[pad], (double)n / AUDIO_SR); toast(b, 1400);
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
    case S_PICK:
        if (c->knob_tap || smp.ms > 8000) { toast(c->knob_tap ? "NO SAMPLE" : "NOTHING HIT: NO SAMPLE", 1000); sample_end(); }
        break;
    case S_LISTEN:
        if (eng_cap_state == CAP_DONE) { sample_done(); break; }
        if (eng_cap_state == CAP_IDLE && smp.ms > 30) { toast("TOO SHORT: NO SAMPLE", 1200); sample_end(); break; }
        if (c->knob_tap || c->rec_press) {
            if (eng_cap_state == CAP_REC) eng_capture_stop();
            else { eng_capture_cancel(); toast("NO SAMPLE", 900); sample_end(); }
        } else if (eng_cap_state == CAP_ARMED && smp.ms > 15000) { eng_capture_cancel(); toast("HEARD NOTHING: NO SAMPLE", 1400); sample_end(); }
        break;
    case S_SAVE:
        if (!store_silent() && smp.ms < 250) break;                     // the mute's fade first (20 ms, 250 at most)
        if (!store_save_step()) sample_end();
        break;
    default: break;
    }
}

static bool hold(void) { if (smp.st != S_OFF) return true; sample_start(); return true; }

// ---------------------------------------------------------------- the link
static bool link_up;
static void link_start(void)
{
    link_init("AIR", 1);
    uint32_t o; float f; link_origin(&o, &f);
    link_boot_time(16000, 0, 4);                                       // 120 BPM until someone says otherwise
    link_boot_harm(0, 0, 0, 0, 0);
    link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    link_up = true;
    (void)o; (void)f;
}

static void link_follow(void)
{
    link_peer_t pr[LINK_PEERS_MAX];
    peers_now = link_peers(pr, LINK_PEERS_MAX);
    link_time_news();                                                  // a loop here keeps its tempo (the link flags a clash)
    int key = link_harm()->key % 12;                                   // the tuned drums sit in the flock's key
    eng_key = (float)(key > 6 ? key - 12 : key);
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
    if (p->src == SRC_ROLL) record_hit(p->frame, p->pad, p->vel);
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) != 0x90 || d2 == 0) return;
    int pad = -1;
    for (int s = 0; s < NPAD && pad < 0; s++) if (gm_note(s) == d1) pad = s;
    if (pad < 0) pad = d1 % NPAD;
    if (smp.st == S_PICK) { sample_picked(pad); return; }
    eng_tune = pv[P_TUNE] + lean_tune;
    uint32_t at = quantized(now_frames);
    trig_t t = { (uint8_t)pad, d2, SRC_MIDI, at };
    eng_trigger(&t);
    record_hit(at ? at : now_frames, pad, d2);
}

static float beat_s(void)
{
    uint32_t len = eng_loop_len;
    return len ? (float)len / (float)AUDIO_SR / (float)eng_beats() : 0.0f;
}

static void tick(const ctl_t *c)
{
    now_frames = c->frames;
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    sample_tick(c);
    // the stick: x a DJ filter; up a roll of the last drum, down a repeat of the last slice
    float fx = pv[P_FILTER] * 0.01f + (c->has_joy ? c->jx : 0.0f);
    eng_filter = clampf(fx, -1.0f, 1.0f);
    int jm = (int)pv[P_JOYY];
    float jy = c->has_joy && jm ? c->jy : 0.0f;
    float bs = beat_s();
    if (bs <= 0.0f && peers_now > 0 && link_fpb()) bs = (float)link_fpb() / (float)AUDIO_SR;   // the flock's beat, without a loop
    roll_div = jy > 0.3f ? (jy > 0.9f ? 8 : jy > 0.6f ? 4 : 2) : 0;
    stut_div = jm == 2 && jy < -0.3f ? (jy < -0.9f ? 8 : jy < -0.6f ? 4 : 2) : 0;
    eng_roll_pad = last_pad; eng_roll_vel = clampi((int)((float)last_vel * 0.85f), 20, 127);
    eng_roll_hz = roll_div ? (bs > 0 ? (float)roll_div / bs : 3.0f * (float)roll_div) : 0.0f;
    eng_stutter = stut_div ? (bs > 0 ? bs / (float)stut_div : 0.24f / (float)stut_div) : 0.0f;
    eng_mutate = pv[P_MUTATE] * 0.01f;
    // the break's parts: stretched to the grid's tempo (FOLLOW TEMPO; the printed DRUMS never are), a bar each with two hands
    {
        const brk_t *b = kit_break();
        bool stretch = pv[P_STRETCH] >= 1.0f && (int)pv[P_KBREAK] != B_DRUMS && b->bpm > 1.0f;
        eng_part_rate = stretch ? clampf(grid_bpm() / b->bpm, 0.5f, 2.0f) : 1.0f;
        int h = (int)pv[P_HANDS];
        eng_part_bar = h == HANDS_LEFT ? 0 : h == HANDS_RIGHT ? 1 : -1;
    }
    kit_tick();
    rec_logic(c);
    gestures(c);
    if (smp.st == S_OFF) eng_tune = pv[P_TUNE] + lean_tune;
    played_t p;
    while (eng_played_pop(&p)) sounded(&p);
    // the LED: the colour of the last pad, fading; a red pulse while recording; green while a pad listens
    led_lvl *= 0.86f;
    float base = lstate == L_REC || lstate == L_DUB ? 0.25f + 0.2f * sinf((float)c->ticks * 0.25f) : 0.0f;
    float l = fmaxf(led_lvl, base);
    if (smp.st == S_LISTEN) led_rgb(0, (uint8_t)(120.0f + 100.0f * sinf((float)c->ticks * 0.3f)), 20);
    else if (base > led_lvl) led_rgb((uint8_t)(255.0f * l), 0, 0);
    else led_rgb((uint8_t)(led_rgb_c[0] * l), (uint8_t)(led_rgb_c[1] * l), (uint8_t)(led_rgb_c[2] * l));
}

static void action(int p)
{
    switch (p) {
    case P_UNDO: if (lstate != L_EMPTY && lstate != L_REC) loop_undo(); else toast("NO LOOP YET", 900); break;
    case P_CLEAR: loop_clear(); toast("LOOP CLEARED", 900); break;
    case P_SAMPLE: sample_start(); break;
    case P_FORGET: {
        static const int back[NPAD] = { V_PART3, V_PART1, V_PART2, V_PART4 };
        for (int k = 0; k < NPAD; k++) { eng_pad_len[k] = 0; store_forget(k); if (pv[P_FRONT + k] < 1.0f) param_set(P_FRONT + k, (float)back[k]); }
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
    if (!link_up) return;
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

// ---------------------------------------------------------------- drawing: the rose and the loop's orbit
// A compass of the four strokes around the gyro's live vector, so the player sees each swing register; the loop
// runs around it as an orbit with a mark per hit and a hand for the playhead.
#define RX 108
#define RY 40
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
            int lx = cx + ax[p] * (r + 5), ly = cy + ay[p] * (r + 5);
            oled_ftext_c(F_TINY, lx, ly - 2, ch, 0);
        }
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

static void draw_orbit(int cx, int cy, int r)
{
    uint32_t len = eng_loop_len;
    if (lstate == L_REC) {                                               // recording: an arc grows from the top
        float t = (float)(now_frames - rec_start) / (float)AUDIO_SR;
        ui_arc(cx, cy, r, -90.0f, -90.0f + fminf(t, 8.0f) / 8.0f * 360.0f);
        ui_arc(cx, cy, r - 1, -90.0f, -90.0f + fminf(t, 8.0f) / 8.0f * 360.0f);
        return;
    }
    if (!len) return;
    for (int a = 0; a < 360; a += 10) {                                 // a dotted orbit
        float rad = (float)a * 0.0174533f;
        oled_pixel(cx + (int)lroundf((float)r * cosf(rad)), cy + (int)lroundf((float)r * sinf(rad)), true);
    }
    int beats = eng_beats();
    for (int b = 0; b < beats; b++) {                                   // beat ticks outside
        float rad = ((float)b / (float)beats * 360.0f - 90.0f) * 0.0174533f;
        oled_pixel(cx + (int)lroundf((float)(r + 2) * cosf(rad)), cy + (int)lroundf((float)(r + 2) * sinf(rad)), true);
    }
    float ph = eng_loop_run ? (float)((now_frames - eng_loop_start) % len) / (float)len : 0.0f;
    int n = eng_nhits;
    for (int i = 0; i < n; i++) {
        const hit_t *h = &eng_hits[i];
        if (h->dead) continue;
        float f = (float)eng_qoff(h) / (float)len;
        float rad = (f * 360.0f - 90.0f) * 0.0174533f, rr = (float)(r - 2) - (float)h->pad * 1.2f;
        int x = cx + (int)lroundf(rr * cosf(rad)), y = cy + (int)lroundf(rr * sinf(rad));
        float since = ph - f; if (since < 0) since += 1.0f;
        if (since < 0.05f && eng_loop_run) oled_disc(x, y, 1, true);
        else oled_pixel(x, y, true);
    }
    if (eng_loop_run) {
        float rad = (ph * 360.0f - 90.0f) * 0.0174533f;
        oled_line(cx + (int)lroundf((float)(r - 6) * cosf(rad)), cy + (int)lroundf((float)(r - 6) * sinf(rad)),
                  cx + (int)lroundf((float)(r + 1) * cosf(rad)), cy + (int)lroundf((float)(r + 1) * sinf(rad)), true);
    }
}

static void draw_antenna(int x, int y)                                  // a board heard on the link
{
    oled_line(x, y, x, y + 5, true);
    oled_pixel(x - 2, y - 1, true); oled_pixel(x + 2, y - 1, true);
    oled_pixel(x - 1, y, true); oled_pixel(x + 1, y, true);
    oled_pixel(x - 3, y + 1, true); oled_pixel(x + 3, y + 1, true);
}

// ---------------------------------------------------------------- drawing: the sampling flow
static void draw_sampling(const view_t *v)
{
    char b[48];
    if (smp.st == S_PICK) {
        ui_head("SAMPLE", true);
        oled_ftext_c(F_SMALL, 40, 22, "HIT THE AIR", 1);
        oled_ftext_c(F_TINY, 40, 33, "FOR THE PAD", 1);
        oled_ftext_c(F_TINY, 40, 41, "TO SAMPLE", 1);
        draw_rose(100, 40, 11, true);
        if (!v->cover) oled_ftext_c(F_TINY, 64, 56, "TAP THE KNOB: NEVER MIND", 0);
        return;
    }
    if (smp.st == S_LISTEN) {
        bool rec = eng_cap_state == CAP_REC;
        ui_head(rec ? "REC" : "LISTEN", true);
        snprintf(b, sizeof b, "%s PAD", pad_name[smp.pad]);
        oled_ftext_c(F_SMALL, 64, 19, b, 1);
        float e = clampf((20.0f * log10f(eng_in_env + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
        oled_rframe(14, 31, 100, 9, 2);
        int w = (int)(e * 96.0f);
        if (w > 0) oled_rect(16, 33, w, 5, true);
        if (rec) {
            float s = (float)eng_cap_len / (float)AUDIO_SR;
            snprintf(b, sizeof b, "%.2f S", (double)s);
            oled_ftext_c(F_TINY, 64, 43, b, 1);
            int pw = (int)(100.0f * s / pv[P_SMAX]);
            oled_rect(14, 51, clampi(pw, 0, 100), 2, true);
        } else {
            oled_ftext_c(F_TINY, 64, 43, ((v->frame >> 3) & 1) ? "MAKE A SOUND" : "", 1);
        }
        if (!v->cover) oled_ftext_c(F_TINY, 64, 56, rec ? "TAP THE KNOB: THAT IS ENOUGH" : "TAP THE KNOB: NEVER MIND", 0);
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
    oled_ftext_c(F_TINY, 64, 55, "KEEPING IT", 1);
}

// ---------------------------------------------------------------- drawing: the play screen
static int frames_drawn;
static void draw(const view_t *v)
{
    frames_drawn++;
    physics();
    if (smp.st != S_OFF) { draw_sampling(v); return; }
    // the head bobs on the beat
    {
        uint32_t fpb = eng_loop_len ? eng_loop_len / (uint32_t)eng_beats() : (peers_now > 0 ? link_fpb() : 0);
        static uint32_t last_beat;
        if (fpb) {
            uint32_t o; float f; link_origin(&o, &f);
            uint32_t base = eng_loop_len ? eng_loop_start : o;
            uint32_t b = (now_frames - base) / fpb;
            if (b != last_beat && (eng_loop_run || !eng_loop_len)) head_bob = 1.4f;
            last_beat = b;
        }
    }
    // the header: the looper's state, or the name
    char hb[20];
    switch (lstate) {
    case L_REC: snprintf(hb, sizeof hb, "REC %.1fs", (double)((float)(now_frames - rec_start) / (float)AUDIO_SR)); ui_head(hb, true); break;
    case L_DUB: ui_head("ADD", true); break;
    case L_PLAY: snprintf(hb, sizeof hb, "LOOP %d", (int)(eng_bpm() + 0.5f)); ui_head(hb, false); break;
    case L_STOP: ui_head("STOP", false); break;
    default: ui_head(NULL, false); break;
    }
    draw_drummer(DX, 0, true);
    parts_draw(&parts);
    draw_labels();
    draw_rose(RX, RY, 11, true);
    draw_orbit(RX, RY, 19);
    if (peers_now > 0 && (!link_tempo_clash() || (v->frame & 4))) draw_antenna(124, 17);   // blinks while our loop and the flock disagree on the tempo
    // what the stick is doing
    static const char *const div_l[] = { "", "", "1/8", "", "1/16", "", "", "", "1/32" };
    float bs = beat_s();
    if (roll_div) { snprintf(hb, sizeof hb, "ROLL %s", bs > 0 ? div_l[roll_div] : roll_div == 8 ? "FAST" : roll_div == 4 ? "MED" : "SLOW"); oled_pill(F_TINY, 70, 17, hb, true); }
    else if (stut_div) { snprintf(hb, sizeof hb, "REPEAT %s", bs > 0 ? div_l[stut_div] : ""); oled_pill(F_TINY, 70, 17, hb, true); }
    else if (fabsf(eng_filter) > 0.05f) { snprintf(hb, sizeof hb, "%s %d", eng_filter < 0 ? "LP" : "HP", (int)(fabsf(eng_filter) * 100.0f)); oled_pill(F_TINY, 70, 17, hb, false); }
    else if (fabsf(lean_tune) >= 0.5f) { snprintf(hb, sizeof hb, "%+d ST", (int)lroundf(lean_tune)); oled_pill(F_TINY, 70, 17, hb, false); }
    else if (kit_rendering()) { snprintf(hb, sizeof hb, "PRINT %d%%", kit_rendering()); oled_pill(F_TINY, 70, 17, hb, false); }
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
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 7,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = false, .looper = false,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver, .hold = hold,
};

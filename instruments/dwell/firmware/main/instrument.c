// DWELL: a drone you live inside (inst_dwell.h says what it is). This file is the UI side: the params and sounds, the
// tilt's walk on the lattice of ratios and the trail of tones it leaves, DRIFT (every change by a swell or a glide),
// the knob from PURE to DENSE, the stick (the lead's octave, lead against chord), the knock (a strum), REC (hold: your
// tone, sampled; tap: the drone rests), ROOM PASS's control, the link (tempo, key; FOLLOW a linked chord; SPREAD one
// tone per board), MIDI and the LED.
#include "core.h"
#include "dsp.h"
#include "link.h"
#include "inst_dwell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "DRONE", "LATTICE", "CLUSTER", "SOURCE", "STRUM", "ROOM", "SPACE", "SONG" };
enum { G_DRONE, G_LAT, G_CLUS, G_SRC, G_STRUM, G_ROOM, G_SPACE, G_SONG };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const move_l[] = { "SWELL", "GLIDE" };
static const char *const limit_l[] = { "7 (YOUNG)", "5", "3" };
static const char *const source_l[NSRC] = { "SINE", "REED", "BOW", "TANPURA", "SHRUTI", "GURDY", "WAVE", "ROOM" };
static const char *const bsync_l[] = { "OFF", "TEMPO" };
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
    [P_KNOCK]   = { "KNOCK", "knck", G_STRUM, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "A KNOCK STRUMS EVERY TONE" },
    [P_STRUM]   = { "STRUM", "strm", G_STRUM, PK_NUM, PF_LOG, 0.2f, 8.0f, 1.15f, 2.0f, "s", NULL, "HOW LONG A STRUM RINGS" },
    [P_SENSE]   = { "SENSE", "sens", G_STRUM, PK_NUM, PF_INT | PF_SYS, 0, 100, 5, 50, "%", NULL, "HOW EASILY A KNOCK COUNTS" },
    [P_PASSES]  = { "PASSES", "pass", G_ROOM, PK_NUM, PF_INT, 1, 30, 1, 12, "", NULL, "TIMES THROUGH THE ROOM" },
    [P_PASSLEN] = { "PASS LEN", "plen", G_ROOM, PK_NUM, PF_INT, 2, ROOM_MAX_S, 1, 4, "s", NULL, "SECONDS PER PASS" },
    [P_PASS]    = { "ROOM PASS", "rpas", G_ROOM, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "PLAY, RECORD, AGAIN, AGAIN" },
    [P_SPACE]   = { "SPACE", "spce", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "HOW MUCH HALL" },
    [P_TAIL]    = { "TAIL", "tail", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "A SMALL ROOM TO ENDLESS" },
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
    { "DREAM HOUSE",    "dens=0 voic=4 root=1 limt=0 srce=0 drft=20 spce=20 tail=50" },
    { "NIBLOCK",        "dens=80 copy=8 beat=0.3 srce=2 brit=60 voic=3 root=0 spce=30 levl=85 body=60" },
    { "ADNOS",          "dens=30 srce=1 move=1 drft=90 voic=4 beat=0.12 brit=30 spce=40 tail=60" },
    { "IN A ROOM",      "srce=7 pass=12 plen=4 voic=2 dens=20 spce=10" },
    { "TANPURA",        "srce=3 voic=4 root=1 pluk=0.8 jawa=55 dens=20 spce=30 tail=55" },
    { "SHRUTI BOX",     "srce=4 voic=3 brth=50 dens=25 beat=0.25 spce=35" },
    { "HURDY GURDY",    "srce=5 voic=3 whel=1.2 dens=40 body=60 knck=1 strm=1" },
    { "STRUMMING",      "srce=0 brit=60 strm=5 knck=1 voic=6 dens=35 spce=50 tail=70" },
    { "CONRAD",         "srce=2 limt=1 dens=55 copy=4 beat=0.8 brit=70 voic=3 root=1 body=70" },
    { "DAVACHI",        "srce=1 move=1 drft=45 voic=6 dens=15 brit=35 body=40 spce=45 tail=65" },
    { "DEEP LISTENING", "srce=1 dens=20 beat=0.1 spce=70 tail=90 voic=5 brth=30" },
    { "TEMPO BEATS",    "bsyn=1 dens=60 copy=4 srce=0 brit=50 voic=3" },
    { "YOUR TONE",      "srce=6 dens=50 copy=6 beat=0.4 voic=3" },
    { "TARTINI",        "diff=100 voic=2 root=0 dens=0 srce=0 spce=10" },
    { "PYTHAGORAS",     "limt=2 srce=0 voic=5 dens=10 spce=30" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_ROLL = LEARN_INST, L_KNOCK, L_REST, L_ROOM };
static const hint_t hints[] = {
    { "TIP IT AWAY FROM YOU: UP THE FIFTHS", LEARN_TILT },
    { "ROLL IT RIGHT: UP THE SEVENTHS", L_ROLL },
    { "TURN THE KNOB: PURE TO DENSE", LEARN_TURN },
    { "KNOCK ON THE BOX: A STRUM", L_KNOCK },
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
static float fund_hz = 65.4f;              // 1/1
static float follow_ratio = 1.0f;          // FOLLOW: a linked chord's root as a ratio on the key
static int   oct_shift;                    // the stick: the lead's octave
static float balance;                      // the stick: -1 chord .. 1 lead
static float rest;                         // REC tapped
static int   knock_cool;                   // ticks since the last knock
static int   midi_note_out = -1;
static float led_breath;
static struct { bool recording, started, analysing, ignore_release; int ms; } R;
static struct { bool checked0; } RM;

typedef struct {
    bool  on, root, lead;
    int8_t b, c;
    float age;                 // seconds since it took its cell
    float lvl;                 // its level target before SPREAD and the balance
    bool  pend;                // SWELL: fading out, then it takes (pb, pc)
    int8_t pb, pc;
    float pend_s;
} slot_t;
static slot_t sl[NV];

static int my_rank;

static float hz_midi_f(float hz) { return 69.0f + 12.0f * log2f(hz / 440.0f); }

// ---------------------------------------------------------------- the slots: what each voice plays
static float slot_hz(const slot_t *s)
{
    float r = lat_ratio((int)pv[P_LIMIT], s->b, s->c) * fund_hz * follow_ratio;
    if (s->root) return r * 0.5f;
    if (s->lead) r *= oct_shift > 0 ? 2.0f : oct_shift < 0 ? 0.5f : 1.0f;
    return r;
}

static int slot_free(void)
{
    for (int i = 0; i < NV; i++) if (!sl[i].on) return i;
    return -1;
}
static int slot_oldest_trail(void)
{
    int k = -1; float age = -1.0f;
    for (int i = 0; i < NV; i++) if (sl[i].on && !sl[i].root && !sl[i].lead && sl[i].age > age) { age = sl[i].age; k = i; }
    return k;
}
static int slot_lead(void) { for (int i = 0; i < NV; i++) if (sl[i].on && sl[i].lead) return i; return -1; }
static int slot_at(int b, int c) { for (int i = 0; i < NV; i++) if (sl[i].on && !sl[i].root && sl[i].b == b && sl[i].c == c && !sl[i].pend) return i; return -1; }
static int slots_wanted(void)
{
    int root = pv[P_ROOT] >= 1.0f;
    int v = clampi((int)pv[P_VOICES], 1, NV - root);
    return v;                              // tones besides the root
}

// the lead moves to (b, c): a tone that is already there becomes the lead; otherwise a free slot, or the oldest
// tone of the trail (SWELL: it fades out first, then comes back on the new cell; GLIDE: it slides there)
static void lead_move(int b, int c)
{
    int old = slot_lead();
    int here = slot_at(b, c);
    if (here >= 0 && here != old) {
        if (old >= 0) { sl[old].lead = false; sl[old].age = 0.0f; }
        sl[here].lead = true; sl[here].age = 0.0f;
    } else if (here < 0) {
        int want = slots_wanted();
        int inuse = 0; for (int i = 0; i < NV; i++) inuse += sl[i].on && !sl[i].root;
        int spare = inuse < want ? slot_free() : -1;
        if (spare < 0) spare = slot_oldest_trail();
        if (pv[P_MOVE] == MOVE_GLIDE && old >= 0) {
            // GLIDE: the lead itself slides to the new cell; the cell it leaves goes to the spare slot, which slides to it
            if (spare >= 0 && spare != old) {
                slot_t *t = &sl[spare];
                t->on = true; t->root = false; t->lead = false; t->pend = false; t->b = sl[old].b; t->c = sl[old].c; t->age = 0.0f;
            }
            sl[old].b = (int8_t)b; sl[old].c = (int8_t)c; sl[old].age = 0.0f; sl[old].pend = false;
        } else {
            // SWELL: a new slot swells in on the new cell (a spare one, or the oldest of the trail after it has faded out);
            // the old lead keeps its tone and becomes trail
            int k = spare >= 0 ? spare : old;                          // one voice: the lead itself moves
            if (old >= 0 && old != k) { sl[old].lead = false; sl[old].age = 0.0f; }
            slot_t *s = &sl[k];
            bool was_on = s->on;
            s->on = true; s->root = false; s->lead = true; s->age = 0.0f;
            if (was_on) { s->pend = true; s->pb = (int8_t)b; s->pc = (int8_t)c; s->pend_s = 0.0f; }
            else { s->pend = false; s->b = (int8_t)b; s->c = (int8_t)c; }
        }
    }
    lead_b = b; lead_c = c;
    scene_cell_new(b, c);
    // MIDI: the lead as a note with the bend that makes it just
    float hz = lat_ratio((int)pv[P_LIMIT], b, c) * fund_hz * follow_ratio;
    float n = hz_midi_f(hz);
    int note = clampi((int)lroundf(n), 0, 127);
    if (midi_note_out >= 0) midi_note(midi_note_out, 0);
    midi_bend(clampi(8192 + (int)((n - (float)note) * 4096.0f), 0, 16383));
    midi_note(note, 90);
    midi_note_out = note;
}

// the root and the number of voices, as the params say
static void slots_shape(void)
{
    bool root = pv[P_ROOT] >= 1.0f;
    for (int i = 0; i < NV; i++) if (sl[i].root && !root) { sl[i].on = false; sl[i].root = false; }
    if (root) {
        int r = -1; for (int i = 0; i < NV; i++) if (sl[i].on && sl[i].root) r = i;
        if (r < 0) { r = slot_free(); if (r < 0) r = slot_oldest_trail(); if (r >= 0) { sl[r] = (slot_t){ .on = true, .root = true, .b = 0, .c = 0 }; } }
    }
    int want = slots_wanted();
    for (;;) {                                                         // too many tones: the oldest go
        int n = 0; for (int i = 0; i < NV; i++) n += sl[i].on && !sl[i].root;
        if (n <= want) break;
        int k = slot_oldest_trail(); if (k < 0) break;
        sl[k].on = false;
    }
    if (slot_lead() < 0) lead_move(lead_b, lead_c);
}

static void slots_tick(float dt)
{
    float drift = pv[P_DRIFT];
    int spread = pv[P_SPREAD] >= 1.0f && D.peers > 0;
    int nvoice = 0; for (int i = 0; i < NV; i++) nvoice += sl[i].on;
    int sound_k = -1;
    if (spread && nvoice > 0) {                                        // this board's one tone: by its rank among the DWELLs
        int k = my_rank % nvoice, j = 0;
        for (int i = 0; i < NV; i++) if (sl[i].on) { if (j == k) sound_k = i; j++; }
    }
    for (int i = 0; i < NV; i++) {
        slot_t *s = &sl[i];
        if (!s->on) { eng_slot_lvl[i] = 0.0f; continue; }
        s->age += dt;
        if (s->pend) {                                                 // SWELL: out, then back on the new cell
            s->pend_s += dt;
            eng_slot_lvl[i] = 0.0f;
            if (eng_slot_lvl_now[i] < 0.03f || s->pend_s > 1.3f * drift) { s->pend = false; s->b = s->pb; s->c = s->pc; s->age = 0.0f; }
            else continue;
        }
        float lvl = s->root ? 0.8f : s->lead ? 1.0f : 0.7f;
        if (s->lead) lvl *= 1.0f + 0.5f * balance; else lvl *= 1.0f - 0.5f * balance;
        if (spread && i != sound_k) lvl = 0.0f;
        s->lvl = lvl;
        eng_slot_hz[i] = slot_hz(s);
        eng_slot_lvl[i] = lvl;
    }
    for (int i = 0; i < NV; i++) if (!sl[i].on) eng_slot_hz[i] = eng_slot_hz[i] > 0.0f ? eng_slot_hz[i] : fund_hz;
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
    // the cell under the cursor, with some stickiness round the lead's own cell
    int b = (int)lroundf(cur_b), cc = (int)lroundf(cur_c);
    if (fabsf(cur_b - (float)lead_b) < 0.7f) b = lead_b;
    if (fabsf(cur_c - (float)lead_c) < 0.7f) cc = lead_c;
    if (b != lead_b || cc != lead_c) {
        if (b != cand_b || cc != cand_c) { cand_b = b; cand_c = cc; cand_s = 0.0f; }
        cand_s += c->dt;
        if (cand_s >= pv[P_DWELLMS]) {
            if (cc != lead_c) coach_done(L_ROLL);
            lead_move(b, cc);
        }
    } else cand_s = 0.0f;
    // the stick: up and down an octave for the lead, left and right the chord against the lead
    if (c->has_joy && !(!c->has_imu)) {
        int o = c->jy > 0.6f ? 1 : c->jy < -0.6f ? -1 : 0;
        if (o != oct_shift) { oct_shift = o; coach_done(LEARN_JOY); }
        balance += (c->jx - balance) * 0.2f;
    } else { oct_shift = 0; balance *= 0.9f; }
}

// ---------------------------------------------------------------- the knock: a strum
static void knock_tick(const ctl_t *c)
{
    knock_cool++;
    D.knock_frames += (int)(c->dt * (float)AUDIO_SR);
    if (!c->has_imu || pv[P_KNOCK] < 1.0f) return;
    float am = sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z);
    float jolt = fabsf(am - 1.0f);
    float thr = 0.95f - 0.75f * pv[P_SENSE] * 0.01f;                 // 0.575 g at 50
    if (jolt > thr && knock_cool > 25) {
        knock_cool = 0;
        float s = clampf(0.35f + (jolt - thr) / 0.8f, 0.35f, 1.0f);
        eng_strum(s);
        scene_knock(s);
        D.knock_frames = 0;
        coach_done(L_KNOCK);
        ui_awake();
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
    eng_room_start(passes, (int)pv[P_PASSLEN] * AUDIO_SR, fund_hz * follow_ratio);
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
    // the sevenths' axis from blue to amber; breathing at the beat when dense; near dark when resting
    float t = (float)(lead_c + LAT_C) / (float)(2 * LAT_C);
    float r = 60.0f + 195.0f * t, g = 60.0f + 80.0f * t, b = 255.0f - 230.0f * t;
    float breathe = 0.6f + 0.4f * (0.5f + 0.5f * sin_turns(eng_beat_phase)) * D.dense;
    led_breath += (breathe - led_breath) * 0.3f;
    float l = (0.12f + 0.5f * clampf(eng_out_lvl * 3.0f, 0.0f, 1.0f)) * led_breath * (1.0f - 0.85f * rest);
    led_rgb((uint8_t)(r * l), (uint8_t)(g * l), (uint8_t)(b * l));
}

// ---------------------------------------------------------------- the core's calls
static void controls(void)
{
    eng_glide_s = pv[P_DRIFT];
    eng_dense = pv[P_DENSE] * 0.01f;
    eng_copies = clampi((int)pv[P_COPIES], 1, NCOPY);
    float beat = pv[P_BEAT];
    if (pv[P_BSYNC] >= 1.0f && D.bpm > 0.0f) beat = D.bpm / 60.0f;
    eng_beat_hz = beat;
    eng_bright = pv[P_BRIGHT] * 0.01f;
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
}

static void ui_fill(void)
{
    int limit = (int)pv[P_LIMIT];
    D.lead_b = lead_b; D.lead_c = lead_c; D.cur_b = cur_b; D.cur_c = cur_c;
    D.limit = limit; D.fund_hz = fund_hz * follow_ratio;
    D.voices = slots_wanted(); D.root_on = pv[P_ROOT] >= 1.0f; D.move = (int)pv[P_MOVE];
    for (int i = 0; i < NV; i++) {
        dw_slot_t *d = &D.slot[i];
        d->on = sl[i].on; d->b = sl[i].b; d->c = sl[i].c; d->is_lead = sl[i].lead; d->is_root = sl[i].root;
        d->hz = eng_slot_hz_now[i]; d->lvl = eng_slot_lvl_now[i]; d->age = sl[i].age;
        if (sl[i].on) lat_name(limit, sl[i].b, sl[i].c, d->name, sizeof d->name); else d->name[0] = 0;
    }
    float r = lat_ratio(limit, lead_b, lead_c);
    lat_name(limit, lead_b, lead_c, D.lead_name, sizeof D.lead_name);
    D.lead_cents = lat_cents(r); D.lead_et = lat_cents_et(r); D.lead_hz = r * fund_hz * follow_ratio;
    D.dense = pv[P_DENSE] * 0.01f; D.source = (int)pv[P_SOURCE];
    D.beat_hz = eng_beat_hz; D.diff_hz = eng_diff_hz; D.strum = eng_strum_env; D.rest = rest > 0.5f;
    D.rec = R.recording; D.rec_s = (float)eng_take_frames / (float)AUDIO_SR; D.analysing = R.analysing; D.wave_hz = eng_wave_hz;
    D.room_state = eng_room_state; D.room_pass = eng_room_pass; D.room_passes = eng_room_passes;
    D.room_frac = eng_room_frames ? (float)eng_room_pos / (float)eng_room_frames : 0.0f; D.room_peak = eng_room_peak;
}

static void tick(const ctl_t *c)
{
    now = c->frames;
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    fund_update();
    if (!booted) { booted = true; slots_shape(); }
    cursor_tick(c);
    slots_shape();
    slots_tick(c->dt);
    knock_tick(c);
    rec_logic(c);
    room_tick();
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
    lead_b = 0; lead_c = 0;
    sl[0] = (slot_t){ .on = true, .root = true };
    sl[1] = (slot_t){ .on = true, .lead = true };
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
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 8,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = false, .looper = false, .knob = "dens",
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = scene_saver,
};

// KNOCK: hit the box to play drums. The mic hears when and how you knock; the accelerometer's 1 kHz
// stream says which side was hit, so each side is its own drum: the face, the left and right sides, the
// top and bottom edges. Each hit is a synthesized drum (VOICE) and/or the knock itself ringing a set of
// tuned resonators (BODY): wood, marimba, bell, glass, tabla, steel pan, membrane, metal.
// REC is an event looper: hold it while you knock to make a loop, hold again to add, tap to stop / play.
// The stick: left/right a DJ filter, up a drum roll of the last drum, down a stutter of the last beat.
// Tilt: bottom edge down tunes up, a sideways lean mutates the loop.
//
// The sound engine is inst_engine.c; this file is the UI side: params, sounds, knock classification, the
// looper, MIDI, the LED and the screens.
#include "core.h"
#include "dsp.h"
#include "ui.h"
#include "inst_engine.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "KIT", "BODY", "FEEL", "LOOP", "BUS" };
enum { G_KIT, G_BODY, G_FEEL, G_LOOP, G_BUS };
static const char *const voices_l[NVTYPE] = { "NONE", "KICK", "SNARE", "HAT", "OPEN HAT", "TOM", "CLAP", "RIM", "COWBELL", "SHAKER", "CONGA", "ZAP" };
static const char *const bodies_l[NBODY] = { "OFF", "WOOD", "MARIMBA", "BELL", "GLASS", "TABLA", "STEEL PAN", "MEMBRANE", "METAL" };
static const char *const bpitch_l[] = { "ONE", "BY SIDE" };
static const char *const notes_l[] = {
    "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
    "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
    "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
    "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5", "C6" };
static const char *const scales_l[] = { "PENTA", "MINOR", "MAJ 7", "MIN 7", "FIFTHS", "HIJAZ", "WHOLE", "UNISON" };
static const char *const sides_l[] = { "SIDE", "TONE", "ONE DRUM" };
static const char *const vel_l[] = { "SOFT", "NORMAL", "HARD", "FIXED" };
static const char *const tilt_l[] = { "OFF", "TUNE", "TUNE+MUTATE" };
static const char *const joyy_l[] = { "OFF", "ROLL", "ROLL+REPEAT" };
static const char *const beats_l[] = { "AUTO", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };
static const char *const grid_l[] = { "OFF", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

static const param_t params[P_N] = {
    [P_FACE]   = { "FACE", "vfac", G_KIT, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_KICK, "", voices_l, "THE DRUM ON THE SCREEN" },
    [P_LEFT]   = { "LEFT", "vlft", G_KIT, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_SNARE, "", voices_l, "THE DRUM ON THE LEFT SIDE" },
    [P_RIGHT]  = { "RIGHT", "vrgt", G_KIT, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_HAT, "", voices_l, "THE DRUM ON THE RIGHT SIDE" },
    [P_TOP]    = { "TOP", "vtop", G_KIT, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_TOM, "", voices_l, "THE DRUM ON THE TOP EDGE" },
    [P_BOTTOM] = { "BOTTOM", "vbot", G_KIT, PK_ENUM, 0, 0, NVTYPE - 1, 1, V_CLAP, "", voices_l, "THE BOTTOM EDGE'S DRUM" },
    [P_VLEVEL] = { "DRUM LEVEL", "vlvl", G_KIT, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE DRUMS ARE" },
    [P_TUNE]   = { "TUNE", "tune", G_KIT, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "EVERYTHING UP OR DOWN" },
    [P_DECAY]  = { "DECAY", "dcay", G_KIT, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "SHORTER OR LONGER DRUMS" },
    [P_PUNCHV] = { "CLICK", "clik", G_KIT, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW HARD THE KICK STARTS" },
    [P_BODY]   = { "BODY", "body", G_BODY, PK_ENUM, 0, 0, NBODY - 1, 1, B_WOOD, "", bodies_l, "WHAT YOUR KNOCK RINGS" },
    [P_BLEVEL] = { "BODY LEVEL", "blvl", G_BODY, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW LOUD THE RINGING IS" },
    [P_BPITCH] = { "PITCH", "bpit", G_BODY, PK_ENUM, 0, 0, 1, 1, 0, "", bpitch_l, "ONE NOTE OR ONE PER SIDE" },
    [P_ROOT]   = { "ROOT", "root", G_BODY, PK_ENUM, 0, 36, 84, 1, 60, "", notes_l, "THE BODY'S NOTE" },
    [P_SCALE]  = { "SCALE", "bscl", G_BODY, PK_ENUM, 0, 0, 7, 1, 0, "", scales_l, "THE NOTES OF THE SIDES" },
    [P_RING]   = { "RING", "ring", G_BODY, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "HOW LONG IT RINGS" },
    [P_BRIGHT] = { "BRIGHT", "bbri", G_BODY, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "DULL OR BRIGHT STRIKES" },
    [P_TOUCH]  = { "TOUCH", "tuch", G_BODY, PK_NUM, PF_INT, 3, 32, 1, 10, "ms", NULL, "LONGER: RUBS RING TOO" },
    [P_SENS]   = { "SENSE", "sens", G_FEEL, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "HOW SOFT A KNOCK COUNTS" },
    [P_SIDES]  = { "SIDES", "side", G_FEEL, PK_ENUM, 0, 0, 2, 1, 0, "", sides_l, "HOW A KNOCK PICKS ITS DRUM" },
    [P_VEL]    = { "VELOCITY", "velc", G_FEEL, PK_ENUM, 0, 0, 3, 1, 1, "", vel_l, "HOW HARD TO KNOCK FOR LOUD" },
    [P_TILT]   = { "TILT", "tilt", G_FEEL, PK_ENUM, 0, 0, 2, 1, 2, "", tilt_l, "WHAT TILTING DOES" },
    [P_JOYY]   = { "STICK Y", "joyy", G_FEEL, PK_ENUM, 0, 0, 2, 1, 2, "", joyy_l, "UP ROLLS, DOWN REPEATS" },
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
    [P_COMP]   = { "PUNCH", "comp", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "SQUEEZE: FATTER, LOUDER" },
    [P_ECHO]   = { "ECHO", "echo", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HOW MUCH GOES TO THE ECHO" },
    [P_ETIME]  = { "ECHO TIME", "etim", G_BUS, PK_NUM, PF_LOG, 0.04f, 0.7f, 1.06f, 0.25f, "s", NULL, "TIME BETWEEN REPEATS" },
    [P_EFB]    = { "REPEATS", "efbk", G_BUS, PK_NUM, PF_INT, 0, 90, 5, 35, "%", NULL, "HOW LONG THE ECHO GOES ON" },
    [P_ROOM]   = { "ROOM", "room", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 18, "%", NULL, "HOW MUCH ROOM" },
    [P_RSIZE]  = { "ROOM SIZE", "rsiz", G_BUS, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SMALL BOX TO BIG HALL" },
};

// ---------------------------------------------------------------- sounds
static const preset_t presets[] = {
    { "STREET KIT",   "" },
    { "808 PARTY",    "vfac=1 vlft=6 vrgt=3 vtop=8 vbot=4 dcay=1.8 tune=-2 body=0 drve=25 comp=50 room=25" },
    { "MARIMBA",      "vfac=0 vlft=0 vrgt=0 vtop=0 vbot=0 body=2 blvl=95 bpit=1 root=60 bscl=0 ring=1.2 bbri=40 room=35" },
    { "BELL TOWER",   "vfac=0 vlft=0 vrgt=0 vtop=0 vbot=0 body=3 blvl=75 bpit=1 root=55 bscl=4 ring=1.5 echo=25 etim=0.45 efbk=40 room=45" },
    { "GLASS HARP",   "vfac=0 vlft=0 vrgt=0 vtop=0 vbot=0 body=4 blvl=85 bpit=1 root=67 bscl=2 bbri=80 ring=1.4 room=40" },
    { "TABLA",        "vfac=0 vlft=0 vrgt=9 vtop=0 vbot=0 vlvl=50 body=5 blvl=90 bpit=1 root=60 bscl=5 tuch=14 room=20" },
    { "STEEL PAN",    "vfac=0 vlft=0 vrgt=0 vtop=0 vbot=0 body=6 blvl=90 bpit=1 root=62 bscl=0 ring=1.2 echo=15 room=30" },
    { "BOOM BAP",     "vfac=1 vlft=2 vrgt=3 vtop=7 vbot=9 dcay=0.8 tune=-3 body=0 drve=35 comp=55 swng=45 snap=80 room=12" },
    { "TECHNO",       "vfac=1 vlft=6 vrgt=4 vtop=3 vbot=7 dcay=1.2 body=0 snap=100 echo=20 etim=0.32 efbk=45 comp=60 drve=15" },
    { "HAND DRUM",    "vfac=10 vlft=10 vrgt=7 vtop=10 vbot=9 vlvl=60 body=7 blvl=80 root=48 ring=1.2 tuch=20 room=20 bscl=1" },
    { "SCRAP METAL",  "vfac=1 vlft=0 vrgt=0 vtop=0 vbot=7 body=8 blvl=85 tuch=32 ring=2 bbri=90 drve=40 room=30" },
    { "ZAP GUN",      "vfac=11 vlft=11 vrgt=11 vtop=11 vbot=11 vlvl=80 body=0 echo=35 etim=0.18 efbk=55 room=15 bscl=4" },
    { "COWBELL FEVER","vfac=1 vlft=8 vrgt=8 vtop=8 vbot=6 body=0 bscl=4 room=20 comp=45" },
    { "SCRATCH BOX",  "vfac=0 vlft=0 vrgt=0 vtop=0 vbot=0 body=4 blvl=95 bpit=0 root=55 tuch=32 sens=80 ring=2.5 bbri=70 room=45" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_KNOCK = LEARN_INST, L_SIDES };
static const hint_t hints[] = {
    { "KNOCK ON THE BOX", L_KNOCK },
    { "EACH SIDE IS ITS OWN DRUM", L_SIDES },
    { "TURN THE KNOB FOR ANOTHER KIT", LEARN_TURN },
    { "HOLD REC AND KNOCK: A LOOP", LEARN_REC },
    { "PUSH THE STICK UP: A DRUM ROLL", LEARN_JOY },
    { "TILT IT TO TUNE THE DRUMS", LEARN_TILT },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

// ---------------------------------------------------------------- UI state
#define WAIT_MS 10           // the push is over within a few ms; the audio path's buffering adds a few more (measured in the family sim)
#define HIST 256
static struct { uint32_t frame; vec3_t a; } acc_hist[HIST];
static int acc_w;
static onset_t pending[4];
static int npending;
static float knock_avg = 0.1f;
static int last_side = SD_FACE, last_vel = 100;
static uint8_t sides_seen;
static uint32_t now_frames;
static bool has_imu_now;

// the looper
enum { L_EMPTY, L_REC, L_PLAY, L_DUB, L_STOP };
static int lstate, layer_n;
static bool rec_pending;
static uint32_t rec_start;
typedef struct { uint32_t frame; uint8_t side, vel; int16_t snip; } prov_t;
static prov_t prov[16];
static int nprov;
static int press_state;

// what the screen shows
typedef struct { uint8_t side, vel, src; } impact_t;
static impact_t impacts[16];
static int nimpacts;
static float flash[NSIDE];
#define TRACE 128
static uint8_t trace[TRACE];          // the mic's envelope, scrolling, one column per frame
static int8_t  trace_hit[TRACE];      // a side marker where a hit was played (-1 none)
static int     trace_w;
static float   led_lvl, led_rgb_c[3];
static int     roll_div, stut_div;    // what the stick is doing, for the screen

// ---------------------------------------------------------------- the knock: which side
static int classify(const onset_t *o, bool use_side)
{
    int mode = (int)pv[P_SIDES];
    if (mode == 2) return SD_FACE;                                       // ONE DRUM
    if (use_side && mode == 0) {
        vec3_t g = { 0, 0, 0 }; int ng = 0;
        const uint32_t ms = AUDIO_SR / 1000;
        for (int k = 0; k < HIST; k++) {                                // gravity just before the knock
            int32_t dt = (int32_t)(acc_hist[k].frame - o->frame);
            if (dt >= -(int32_t)(80 * ms) && dt < -(int32_t)(20 * ms)) { g.x += acc_hist[k].a.x; g.y += acc_hist[k].a.y; g.z += acc_hist[k].a.z; ng++; }
        }
        if (ng) { g.x /= (float)ng; g.y /= (float)ng; g.z /= (float)ng; }
        int idx[64], ni = 0;                                            // the window around the knock, oldest first
        for (int k = 0; k < HIST && ni < 64; k++) {
            int j = (acc_w + k) % HIST;
            int32_t dt = (int32_t)(acc_hist[j].frame - o->frame);
            if (dt >= -(int32_t)(20 * ms) && dt <= (int32_t)(WAIT_MS * ms)) idx[ni++] = j;
        }
        float best = 0; int bi = -1;
        for (int k = 0; k < ni; k++) {
            vec3_t d = { acc_hist[idx[k]].a.x - g.x, acc_hist[idx[k]].a.y - g.y, acc_hist[idx[k]].a.z - g.z };
            float m = d.x * d.x + d.y * d.y + d.z * d.z;
            if (m > best) { best = m; bi = k; }
        }
        if (ng && bi >= 0 && best > 0.35f * 0.35f) {
            // the first sample that gets halfway to the strongest push: the direction of the blow itself,
            // before the box rings back
            for (int k = 0; k <= bi; k++) {
                vec3_t d = { acc_hist[idx[k]].a.x - g.x, acc_hist[idx[k]].a.y - g.y, acc_hist[idx[k]].a.z - g.z };
                if (d.x * d.x + d.y * d.y + d.z * d.z < 0.25f * best) continue;
                float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z);
                if (az >= ax && az >= ay) return SD_FACE;
                if (ax >= ay) return d.x > 0 ? SD_LEFT : SD_RIGHT;        // pushed right: hit on the left
                return d.y > 0 ? SD_TOP : SD_BOTTOM;                      // pushed down the screen: hit on the top edge
            }
        }
    }
    return o->bright < 0.25f ? SD_FACE : o->bright < 0.55f ? SD_LEFT : SD_RIGHT;   // by tone: dull, middle, sharp
}

static int velocity(float level)
{
    float v = clampf(level / (1.6f * knock_avg), 0.0f, 1.0f);
    switch ((int)pv[P_VEL]) {
    case 0: v = sqrtf(v); break;
    case 2: v = v * v; break;
    case 3: v = 0.8f; break;
    default: break;
    }
    return clampi((int)(v * 127.0f + 0.5f), 12, 127);
}

// ---------------------------------------------------------------- the looper
static void unpin_all(void)
{
    int n = eng_nhits;
    for (int i = 0; i < n; i++) if (eng_hits[i].snip >= 0) eng_pinned[eng_hits[i].snip] = 0;
}

static void loop_clear(void)
{
    __atomic_store_n(&eng_loop_len, 0, __ATOMIC_RELEASE);
    int n = eng_nhits;
    for (int i = 0; i < n; i++) eng_hits[i].dead = 1;
    unpin_all();
    __atomic_store_n(&eng_nhits, 0, __ATOMIC_RELEASE);
    lstate = L_EMPTY; layer_n = 0; rec_pending = false; nprov = 0;
}

static void add_hit(uint32_t frame, int side, int vel, int16_t snip)
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
    h.side = (uint8_t)side; h.vel = (uint8_t)vel; h.layer = (uint8_t)layer_n; h.snip = snip; h.dead = 0;
    if (snip >= 0) eng_pinned[snip] = 1;
    eng_hits[n] = h;
    __atomic_store_n(&eng_nhits, n + 1, __ATOMIC_RELEASE);
}

// a hit that sounded: into the loop when recording, or kept aside while a press might still be a tap
static void record_hit(uint32_t frame, int side, int vel, int16_t snip)
{
    if (rec_pending) { if (nprov < 16) prov[nprov++] = (prov_t){ frame, (uint8_t)side, (uint8_t)vel, snip }; return; }
    if (lstate == L_REC || lstate == L_DUB) add_hit(frame, side, vel, snip);
}

static void loop_undo(void)
{
    if (layer_n <= 1) { loop_clear(); toast("LOOP CLEARED", 900); return; }
    int n = eng_nhits;
    for (int i = 0; i < n; i++) if (eng_hits[i].layer == layer_n && !eng_hits[i].dead) { eng_hits[i].dead = 1; if (eng_hits[i].snip >= 0) eng_pinned[eng_hits[i].snip] = 0; }
    layer_n--;
    toast("LAST LAYER GONE", 900);
}

static void rec_logic(const ctl_t *c)
{
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
        for (int i = 0; i < nprov; i++) add_hit(prov[i].frame, prov[i].side, prov[i].vel, prov[i].snip);
        nprov = 0;
    }
    if (c->rec_release) {
        if (rec_pending) {                                                // a tap: stop or play
            rec_pending = false; nprov = 0;
            if (lstate == L_PLAY) { lstate = L_STOP; eng_loop_run = 0; }
            else if (lstate == L_STOP) {
                int n = eng_nhits;
                for (int i = 0; i < n; i++) eng_hits[i].born = 0xFFFF;
                __atomic_store_n(&eng_loop_start, c->frames, __ATOMIC_RELEASE);
                eng_loop_run = 1; lstate = L_PLAY;
            }
        } else if (lstate == L_REC) {
            uint32_t len = c->frames - rec_start;
            // pressed REC and knocked at once: the knock is the downbeat, and the release lagged as much
            int n = eng_nhits;
            uint32_t first = len;
            for (int i = 0; i < n; i++) if (eng_hits[i].off < first) first = eng_hits[i].off;
            uint32_t shift = first < (uint32_t)(0.12f * AUDIO_SR) ? first : 0;
            if (shift) for (int i = 0; i < n; i++) eng_hits[i].off -= shift;
            if (len < AUDIO_SR / 4 || n == 0) { loop_clear(); if (len >= AUDIO_SR / 4) toast("NO KNOCKS: NO LOOP", 1200); }
            else {
                __atomic_store_n(&eng_loop_start, rec_start + shift, __ATOMIC_RELEASE);
                eng_loop_run = 1;
                __atomic_store_n(&eng_loop_len, len, __ATOMIC_RELEASE);
                lstate = L_PLAY;
            }
        } else if (lstate == L_DUB) lstate = L_PLAY;
    }
}

// ---------------------------------------------------------------- MIDI, LED, the screen's events
static const uint8_t side_rgb[NSIDE][3] = { { 255, 40, 20 }, { 255, 140, 0 }, { 0, 170, 255 }, { 40, 255, 60 }, { 200, 0, 255 } };

static void sounded(const played_t *p)
{
    int vt = (int)pv[P_FACE + p->side];
    int note = vt != V_NONE ? vt_gm[vt] : side_gm[p->side];
    midi_note_ch(10, note, p->vel);
    midi_note_ch(10, note, 0);
    flash[p->side] = 1.0f;
    if (nimpacts < 16) impacts[nimpacts++] = (impact_t){ p->side, p->vel, p->src };
    trace_hit[(trace_w + TRACE - 1) % TRACE] = (int8_t)p->side;
    float k = (float)p->vel / 127.0f;
    led_lvl = fmaxf(led_lvl, 0.35f + 0.65f * k);
    for (int i = 0; i < 3; i++) led_rgb_c[i] = side_rgb[p->side][i];
    if (p->src == SRC_ROLL) record_hit(p->frame, p->side, p->vel, -1);
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) != 0x90 || d2 == 0) return;
    int side = -1;
    for (int s = 0; s < NSIDE && side < 0; s++) { int vt = (int)pv[P_FACE + s]; if (vt != V_NONE && vt_gm[vt] == d1) side = s; }
    for (int s = 0; s < NSIDE && side < 0; s++) if (side_gm[s] == d1) side = s;
    if (side < 0) side = d1 % NSIDE;
    trig_t t = { (uint8_t)side, d2, SRC_MIDI, -1 };
    eng_trigger(&t);
}

static float beat_s(void)
{
    uint32_t len = eng_loop_len;
    return len ? (float)len / (float)AUDIO_SR / (float)eng_beats() : 0.0f;
}

static void tick(const ctl_t *c)
{
    now_frames = c->frames;
    has_imu_now = c->has_imu;
    // tilt: the bottom edge down tunes up (7 semitones at 30 degrees); a sideways lean mutates the loop
    int tm = (int)pv[P_TILT];
    float tl = c->has_imu && tm >= 1 ? clampf(c->lean_y / 30.0f, -1.0f, 1.0f) * 7.0f : 0.0f;
    eng_tune = pv[P_TUNE] + tl;
    float mu = pv[P_MUTATE] * 0.01f;
    if (c->has_imu && tm == 2) mu += clampf((fabsf(c->lean_x) - 15.0f) / 30.0f, 0.0f, 1.0f) * 0.6f;
    eng_mutate = clampf(mu, 0.0f, 1.0f);
    // the stick: x a DJ filter; up a roll of the last drum, down a stutter of the last slice
    float fx = pv[P_FILTER] * 0.01f + (c->has_joy ? c->jx : 0.0f);
    eng_filter = clampf(fx, -1.0f, 1.0f);
    int jm = (int)pv[P_JOYY];
    float jy = c->has_joy && jm ? c->jy : 0.0f;
    float bs = beat_s();
    roll_div = jy > 0.3f ? (jy > 0.9f ? 8 : jy > 0.6f ? 4 : 2) : 0;
    stut_div = jm == 2 && jy < -0.3f ? (jy < -0.9f ? 8 : jy < -0.6f ? 4 : 2) : 0;
    eng_roll_side = last_side; eng_roll_vel = clampi((int)((float)last_vel * 0.85f), 20, 127);
    eng_roll_hz = roll_div ? (bs > 0 ? (float)roll_div / bs : 3.0f * (float)roll_div) : 0.0f;
    eng_stutter = stut_div ? (bs > 0 ? bs / (float)stut_div : 0.24f / (float)stut_div) : 0.0f;

    // the accelerometer's history, stamped in audio frames (1 kHz = 32 frames apart, the newest is now)
    for (int i = 0; i < c->nburst; i++) {
        acc_hist[acc_w].frame = c->frames - (uint32_t)(c->nburst - 1 - i) * (AUDIO_SR / 1000);
        acc_hist[acc_w].a = c->burst[i];
        acc_w = (acc_w + 1) % HIST;
    }
    rec_logic(c);
    // knocks heard: wait until the accelerometer has the WAIT_MS after each, then decide its side
    onset_t o;
    while (npending < 4 && eng_onset_pop(&o)) pending[npending++] = o;
    for (int i = 0; i < npending; ) {
        int32_t age = (int32_t)(c->frames - pending[i].frame);
        bool use_side = c->has_imu && pv[P_SIDES] < 1.0f;
        if (use_side && age < WAIT_MS * AUDIO_SR / 1000) { i++; continue; }
        int side = classify(&pending[i], use_side);
        int vel = velocity(pending[i].level);
        knock_avg += 0.15f * (pending[i].level - knock_avg);
        eng_knock_avg = knock_avg;
        trig_t t = { (uint8_t)side, (uint8_t)vel, SRC_LIVE, pending[i].snip };
        eng_trigger(&t);
        record_hit(pending[i].frame, side, vel, pending[i].snip);
        last_side = side; last_vel = vel;
        coach_done(L_KNOCK);
        sides_seen |= (uint8_t)(1u << side);
        if (__builtin_popcount(sides_seen) >= 3) coach_done(L_SIDES);
        ui_awake();
        pending[i] = pending[--npending];
    }
    played_t p;
    while (eng_played_pop(&p)) sounded(&p);
    // the LED: the colour of the last side, fading; a red pulse while recording
    led_lvl *= 0.86f;
    float base = lstate == L_REC || lstate == L_DUB ? 0.25f + 0.2f * sinf((float)c->ticks * 0.25f) : 0.0f;
    float l = fmaxf(led_lvl, base);
    if (base > led_lvl) led_rgb((uint8_t)(255.0f * l), 0, 0);
    else led_rgb((uint8_t)(led_rgb_c[0] * l), (uint8_t)(led_rgb_c[1] * l), (uint8_t)(led_rgb_c[2] * l));
}

static void action(int p)
{
    if (p == P_UNDO) { if (lstate != L_EMPTY && lstate != L_REC) loop_undo(); else toast("NO LOOP YET", 900); }
    if (p == P_CLEAR) { loop_clear(); toast("LOOP CLEARED", 900); }
}

static void changed(int p) { (void)p; eng_changed(); }

static void init(void) { eng_init(); for (int i = 0; i < TRACE; i++) trace_hit[i] = -1; }

static void audio(const float *in, float *out, int n) { eng_audio(in, out, n); }

// ---------------------------------------------------------------- drawing: the box
// A box in three quarters view: the front face (the screen side), the top and the right side showing.
// It moves on springs: knocked on the left it jumps right, on the top it dips, on the face it squashes.
static float bx, by, bvx, bvy, sq, sqv;          // position, squash
#define BOX_X 52.0f                              // where the box rests
#define BOX_Y 31.0f
static parts_t parts;
static uint32_t pseed = 0x2468aceu;

static void fill_quad(const int *X, const int *Y, int level)    // a convex quad, level 1..4 of a pattern
{
    int y0 = Y[0], y1 = Y[0];
    for (int i = 1; i < 4; i++) { if (Y[i] < y0) y0 = Y[i]; if (Y[i] > y1) y1 = Y[i]; }
    for (int y = y0; y <= y1; y++) {
        float xa = 1e9f, xb = -1e9f;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4, ya = Y[i], yb = Y[j];
            if (ya == yb) { if (y == ya) { xa = fminf(xa, (float)fminf((float)X[i], (float)X[j])); xb = fmaxf(xb, (float)fmaxf((float)X[i], (float)X[j])); } continue; }
            if ((y < ya && y < yb) || (y > ya && y > yb)) continue;
            float x = (float)X[i] + (float)(X[j] - X[i]) * (float)(y - ya) / (float)(yb - ya);
            if (x < xa) xa = x;
            if (x > xb) xb = x;
        }
        for (int x = (int)ceilf(xa); x <= (int)floorf(xb); x++) {
            bool on = level >= 4 || (level == 3 && ((x + y) & 3)) || (level == 2 && !((x + y) & 1)) || (level == 1 && !(x & 1) && !(y & 1));
            if (on) oled_pixel(x, y, true);
        }
    }
}

typedef struct { int fx0, fy0, fx1, fy1, dx, dy; } boxgeo_t;
static boxgeo_t box_geo(float cx, float cy, float w, float h, float d)
{
    boxgeo_t g;
    g.fx0 = (int)lroundf(cx - w * 0.5f); g.fx1 = (int)lroundf(cx + w * 0.5f);
    g.fy0 = (int)lroundf(cy - h * 0.5f); g.fy1 = (int)lroundf(cy + h * 0.5f);
    g.dx = (int)lroundf(d); g.dy = -(int)lroundf(d * 0.66f);
    return g;
}

static void draw_box(const boxgeo_t *g, const float *fl)
{
    int X[4], Y[4];
    // fills for the flashing faces, under the lines
    int lv[NSIDE];
    for (int s = 0; s < NSIDE; s++) lv[s] = fl ? (fl[s] > 0.75f ? 4 : fl[s] > 0.5f ? 3 : fl[s] > 0.25f ? 2 : fl[s] > 0.05f ? 1 : 0) : 0;
    if (lv[SD_FACE]) { X[0] = g->fx0; Y[0] = g->fy0; X[1] = g->fx1; Y[1] = g->fy0; X[2] = g->fx1; Y[2] = g->fy1; X[3] = g->fx0; Y[3] = g->fy1; fill_quad(X, Y, lv[SD_FACE]); }
    if (lv[SD_TOP]) { X[0] = g->fx0; Y[0] = g->fy0; X[1] = g->fx1; Y[1] = g->fy0; X[2] = g->fx1 + g->dx; Y[2] = g->fy0 + g->dy; X[3] = g->fx0 + g->dx; Y[3] = g->fy0 + g->dy; fill_quad(X, Y, lv[SD_TOP]); }
    if (lv[SD_RIGHT]) { X[0] = g->fx1; Y[0] = g->fy0; X[1] = g->fx1 + g->dx; Y[1] = g->fy0 + g->dy; X[2] = g->fx1 + g->dx; Y[2] = g->fy1 + g->dy; X[3] = g->fx1; Y[3] = g->fy1; fill_quad(X, Y, lv[SD_RIGHT]); }
    // the edges: front face, top face, right face
    oled_frame(g->fx0, g->fy0, g->fx1 - g->fx0 + 1, g->fy1 - g->fy0 + 1);
    oled_line(g->fx0, g->fy0, g->fx0 + g->dx, g->fy0 + g->dy, true);
    oled_line(g->fx1, g->fy0, g->fx1 + g->dx, g->fy0 + g->dy, true);
    oled_line(g->fx1, g->fy1, g->fx1 + g->dx, g->fy1 + g->dy, true);
    oled_line(g->fx0 + g->dx, g->fy0 + g->dy, g->fx1 + g->dx, g->fy0 + g->dy, true);
    oled_line(g->fx1 + g->dx, g->fy0 + g->dy, g->fx1 + g->dx, g->fy1 + g->dy, true);
    // the hidden sides show their hits as strike marks beside the box
    if (lv[SD_LEFT]) { int x = g->fx0 - 3, cy = (g->fy0 + g->fy1) / 2; oled_line(x, cy - 4, x, cy + 4, true); if (lv[SD_LEFT] > 2) { oled_line(x - 2, cy - 2, x - 2, cy + 2, true); } }
    if (lv[SD_BOTTOM]) { int y = g->fy1 + 3, cx = (g->fx0 + g->fx1) / 2; oled_line(cx - 6, y, cx + 6, y, true); if (lv[SD_BOTTOM] > 2) oled_line(cx - 3, y + 2, cx + 3, y + 2, true); }
}

static const char *side_label(int s, char *b, int n)
{
    int vt = (int)pv[P_FACE + s];
    if (vt != V_NONE) return vt_label[vt];
    if (pv[P_BODY] < 1.0f) return "-";
    int note = (int)pv[P_ROOT] + (pv[P_BPITCH] >= 1.0f ? scale_deg[(int)pv[P_SCALE]][s] : 0);
    ui_note_name(note, b, n);
    return b;
}

static void label(int x, int y, const char *s, float fl, int align)   // align -1 right edge at x, 0 centred, 1 left edge
{
    int w = oled_ftext_w(F_TINY, s, 1);
    int x0 = align < 0 ? x - w : align == 0 ? x - w / 2 : x;
    oled_rect(x0 - 1, y - 1, w + 2, 7, false);                          // readable over a flashing face
    oled_ftext_ex(F_TINY, x0, y, s, 1);
    if (fl > 0.4f) oled_invert(x0 - 1, y - 1, w + 2, 7);
}

static void physics(void)
{
    // impulses from what sounded since the last frame
    for (int i = 0; i < nimpacts; i++) {
        float k = 0.4f + 0.6f * (float)impacts[i].vel / 127.0f;
        if (impacts[i].src == SRC_LOOP) k *= 0.6f;
        float ix = 0, iy = 0;
        switch (impacts[i].side) {
        case SD_LEFT: ix = 1; break; case SD_RIGHT: ix = -1; break;
        case SD_TOP: iy = 1; break; case SD_BOTTOM: iy = -1; break;
        default: sqv -= 3.0f * k; break;
        }
        bvx += ix * 110.0f * k; bvy += iy * 80.0f * k;
        // sparks off the struck face, a ring where it was hit
        float px = BOX_X + bx, py = BOX_Y + by, vx = 0, vy = 0;
        switch (impacts[i].side) {
        case SD_LEFT: px -= 19; vx = -60; break; case SD_RIGHT: px += 23; vx = 60; break;
        case SD_TOP: px += 4; py -= 10; vy = -50; break; case SD_BOTTOM: py += 8; vy = 50; break;
        default: break;
        }
        int nsp = impacts[i].src == SRC_LOOP ? 2 : 6;
        for (int k2 = 0; k2 < nsp; k2++) {
            float r1 = rng_bi(&pseed), r2 = rng_bi(&pseed);
            parts_emit(&parts, px, py, vx + 40.0f * r1, vy + 40.0f * r2 - (impacts[i].side == SD_FACE ? 20.0f : 0.0f), 0.35f + 0.2f * rng_uni(&pseed), PART_STREAK, 1);
        }
        if (impacts[i].src != SRC_LOOP) parts_emit(&parts, px, py, 0, 0, 0.3f, PART_RING, (uint8_t)(1 + impacts[i].vel / 50));
    }
    nimpacts = 0;
    for (int s = 0; s < 4; s++) {                                       // springs, 4 steps of 10 ms per frame
        const float dt = 0.01f, k = 260.0f, c = 14.0f;
        bvx += (-k * bx - c * bvx) * dt; bvy += (-k * by - c * bvy) * dt; sqv += (-k * sq - c * sqv) * dt;
        bx += bvx * dt; by += bvy * dt; sq += sqv * dt;
    }
    bx = clampf(bx, -7, 7); by = clampf(by, -5, 5); sq = clampf(sq, -0.35f, 0.3f);
    parts_step(&parts, 0.04f, 120.0f, 2.0f);
    for (int s = 0; s < NSIDE; s++) flash[s] *= 0.78f;
}

// ---------------------------------------------------------------- drawing: the loop
static void draw_ring(int cx, int cy, int r)
{
    uint32_t len = eng_loop_len;
    for (int a = 0; a < 360; a += 12) {                                 // a dotted orbit
        float rad = (float)a * 0.0174533f;
        oled_pixel(cx + (int)lroundf((float)r * cosf(rad)), cy + (int)lroundf((float)r * sinf(rad)), true);
    }
    if (lstate == L_REC) {                                               // recording: an arc grows from the top
        float t = (float)(now_frames - rec_start) / (float)AUDIO_SR;
        ui_arc(cx, cy, r, -90.0f, -90.0f + fminf(t, 8.0f) / 8.0f * 360.0f);
        ui_arc(cx, cy, r - 1, -90.0f, -90.0f + fminf(t, 8.0f) / 8.0f * 360.0f);
        char b[12]; snprintf(b, sizeof b, "%.1f", (double)t);
        oled_ftext_c(F_TINY, cx, cy - 2, b, 1);
        return;
    }
    if (!len) return;
    int beats = eng_beats();
    for (int b = 0; b < beats; b++) {                                   // beat ticks outside
        float rad = ((float)b / (float)beats * 360.0f - 90.0f) * 0.0174533f;
        oled_line(cx + (int)lroundf((float)(r + 2) * cosf(rad)), cy + (int)lroundf((float)(r + 2) * sinf(rad)),
                  cx + (int)lroundf((float)(r + 3) * cosf(rad)), cy + (int)lroundf((float)(r + 3) * sinf(rad)), true);
    }
    float ph = eng_loop_run ? (float)((now_frames - eng_loop_start) % len) / (float)len : 0.0f;
    int n = eng_nhits;
    for (int i = 0; i < n; i++) {
        const hit_t *h = &eng_hits[i];
        if (h->dead) continue;
        float f = (float)eng_qoff(h) / (float)len;
        float rad = (f * 360.0f - 90.0f) * 0.0174533f, rr = (float)(r - 3) - (float)h->side * 1.6f;
        int x = cx + (int)lroundf(rr * cosf(rad)), y = cy + (int)lroundf(rr * sinf(rad));
        float since = ph - f; if (since < 0) since += 1.0f;
        if (since < 0.05f && eng_loop_run) oled_disc(x, y, 2, true);
        else if (h->vel > 90) oled_rect(x, y, 2, 2, true);
        else oled_pixel(x, y, true);
    }
    float rad = (ph * 360.0f - 90.0f) * 0.0174533f;
    oled_line(cx, cy, cx + (int)lroundf((float)(r + 1) * cosf(rad)), cy + (int)lroundf((float)(r + 1) * sinf(rad)), true);
    char b[12];
    snprintf(b, sizeof b, "%d", (int)(eng_bpm() + 0.5f));
    int w = oled_ftext_w(F_TINY, b, 1);
    oled_rect(cx - w / 2 - 1, cy - 3, w + 2, 7, false);
    oled_ftext_c(F_TINY, cx, cy - 2, b, 1);
}

static void draw_meter(int x, int y, int h)
{
    // the mic: what it hears against the knock threshold (log scale), so SENSE can be set by eye
    float e = eng_env, t = eng_thr;
    float fe = clampf((20.0f * log10f(e + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
    float ft = clampf((20.0f * log10f(t + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
    oled_rframe(x, y, 7, h, 1);
    int f = (int)(fe * (float)(h - 4));
    if (f > 0) oled_rect(x + 2, y + h - 2 - f, 3, f, true);
    int ty = y + h - 2 - (int)(ft * (float)(h - 4));
    oled_line(x - 2, ty, x + 8, ty, true);
    oled_ftext_c(F_TINY, x + 3, y + h + 3, "MIC", 0);
}

static void draw_lane(int y0)
{
    // rows 47..63: the loop as five lanes with a playhead, or the mic's trace with the hits on it
    uint32_t len = eng_loop_len;
    if (len && lstate != L_REC) {
        int x0 = 2, w = OLED_W - 4, beats = eng_beats();
        for (int b = 0; b <= beats; b++) oled_rect(x0 + b * (w - 1) / beats, y0, 1, b % 4 == 0 ? 2 : 1, true);
        int n = eng_nhits;
        for (int i = 0; i < n; i++) {
            const hit_t *h = &eng_hits[i];
            if (h->dead) continue;
            int x = x0 + (int)((float)eng_qoff(h) / (float)len * (float)(w - 2));
            oled_rect(x, y0 + 3 + h->side * 3, h->vel > 90 ? 3 : 2, 2, true);   // face at the top, bottom edge at the bottom
        }
        if (eng_loop_run) {
            int px = x0 + (int)((float)((now_frames - eng_loop_start) % len) / (float)len * (float)(w - 2));
            oled_invert(px, y0 + 2, 1, 14);
        }
        return;
    }
    // the trace: newest at the right
    float t = eng_thr;
    int ty = y0 + 15 - (int)(clampf((20.0f * log10f(t + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f) * 14.0f);
    oled_hline_pat(0, ty, OLED_W, 0x11, true);
    for (int i = 0; i < TRACE; i++) {
        int k = (trace_w + i) % TRACE, h = trace[k];
        if (h) oled_line(i, y0 + 15, i, y0 + 15 - h, true);
        if (trace_hit[k] >= 0) { oled_rect(i - 1, y0 + 1, 3, 2, true); }
    }
}

static int frames_drawn;
static void draw(const view_t *v)
{
    frames_drawn++;
    // the trace moves on even under the menu strip
    float e = clampf((20.0f * log10f(eng_env + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
    trace[trace_w] = (uint8_t)(e * 14.0f);
    trace_w = (trace_w + 1) % TRACE;
    trace_hit[trace_w] = -1;
    physics();

    // the header: the looper's state, or the name
    char hb[20];
    switch (lstate) {
    case L_REC: snprintf(hb, sizeof hb, "REC %.1fs", (double)((float)(now_frames - rec_start) / (float)AUDIO_SR)); ui_head(hb, true); break;
    case L_DUB: ui_head("ADD", true); break;
    case L_PLAY: { float bs = beat_s(); snprintf(hb, sizeof hb, "LOOP %d", bs > 0 ? (int)(60.0f / bs + 0.5f) : 0); ui_head(hb, false); break; }
    case L_STOP: ui_head("STOP", false); break;
    default: ui_head(NULL, false); break;
    }
    // the box and its five drums: the face and the top carry their names, the sides theirs beside them
    boxgeo_t g = box_geo(BOX_X + bx, BOX_Y + by, 34.0f * (1.0f + sq), 13.0f * (1.0f + sq * 0.6f), 9.0f);
    draw_box(&g, flash);
    char b[8];
    int cy = (g.fy0 + g.fy1) / 2;
    label(g.fx0 - 6, cy - 2, side_label(SD_LEFT, b, sizeof b), flash[SD_LEFT], -1);
    label(g.fx1 + g.dx + 3, cy - 4, side_label(SD_RIGHT, b, sizeof b), flash[SD_RIGHT], 1);
    label((g.fx0 + g.fx1) / 2 + g.dx / 2, g.fy0 + g.dy / 2 - 2, side_label(SD_TOP, b, sizeof b), flash[SD_TOP], 0);
    label((g.fx0 + g.fx1) / 2, g.fy1 + 2, side_label(SD_BOTTOM, b, sizeof b), flash[SD_BOTTOM], 0);
    label((g.fx0 + g.fx1) / 2, cy - 2, side_label(SD_FACE, b, sizeof b), flash[SD_FACE], 0);
    parts_draw(&parts);
    // the right: the loop's orbit, or the mic against the threshold
    if (eng_loop_len || lstate == L_REC) draw_ring(115, 30, 11);
    else draw_meter(116, 17, 20);
    // what the stick is doing, bottom left of the scene
    static const char *const div_l[] = { "", "", "1/8", "", "1/16", "", "", "", "1/32" };
    if (roll_div) { snprintf(hb, sizeof hb, "ROLL %s", beat_s() > 0 ? div_l[roll_div] : roll_div == 8 ? "FAST" : roll_div == 4 ? "MED" : "SLOW"); oled_pill(F_TINY, 0, 39, hb, true); }
    else if (stut_div) { snprintf(hb, sizeof hb, "REPEAT %s", beat_s() > 0 ? div_l[stut_div] : ""); oled_pill(F_TINY, 0, 39, hb, true); }
    else if (fabsf(eng_filter) > 0.05f) { snprintf(hb, sizeof hb, "%s %d", eng_filter < 0 ? "LP" : "HP", (int)(fabsf(eng_filter) * 100.0f)); oled_pill(F_TINY, 0, 39, hb, false); }
    if (!v->cover) { oled_hline_pat(0, 47, OLED_W, 0x11, true); draw_lane(48); }
}

// ---------------------------------------------------------------- splash: knock, knock, knock
static void fist(int x, int y)
{
    // a fist coming from the left: the hand, four knuckles on its front, the thumb, the arm off screen
    oled_rrect(x, y, 14, 12, 3, false);
    oled_rframe(x, y, 14, 12, 3);
    for (int i = 0; i < 4; i++) { oled_pixel(x + 14, y + 1 + i * 3, true); oled_line(x + 10, y + 2 + i * 3, x + 12, y + 2 + i * 3, true); }
    oled_line(x + 2, y + 8, x + 8, y + 8, true);                                              // the thumb across
    oled_line(x - 20, y + 3, x - 1, y + 3, true); oled_line(x - 20, y + 9, x - 1, y + 9, true); // the arm
    oled_line(x - 3, y + 2, x - 3, y + 10, true);                                             // the cuff
}

static bool splash(int f)
{
    const int hits[3] = { 12, 24, 36 };
    static float sbx, sbv, sq_, sqv_;
    if (f == 0) { sbx = sbv = sq_ = sqv_ = 0; parts.n = 0; }
    const float bxc = 99.0f, byc = 44.0f;
    for (int i = 0; i < 3; i++) if (f == hits[i]) {                     // each knock: the box jolts, sparks, a ring
        sbv += 120.0f + 30.0f * (float)i; sqv_ -= 1.5f;
        parts_emit(&parts, bxc - 19, byc, 0, 0, 0.4f, PART_RING, (uint8_t)(2 + i));
        for (int k = 0; k < 6 + 3 * i; k++) parts_emit(&parts, bxc - 19, byc, -30.0f - 60.0f * rng_uni(&pseed), 70.0f * rng_bi(&pseed), 0.45f, PART_STREAK, 1);
    }
    for (int s2 = 0; s2 < 4; s2++) {
        sbv += (-300.0f * sbx - 12.0f * sbv) * 0.01f; sbx += sbv * 0.01f;
        sqv_ += (-300.0f * sq_ - 10.0f * sqv_) * 0.01f; sq_ += sqv_ * 0.01f;
    }
    float jump = f >= 40 && f < 60 ? sinf((float)(f - 40) / 20.0f * 3.14159f) * 14.0f : 0.0f;
    parts_step(&parts, 0.04f, 90.0f, 1.5f);
    boxgeo_t g = box_geo(bxc + sbx, byc - jump, 36.0f * (1.0f + 0.3f * sq_), 15.0f * (1.0f + 0.2f * sq_), 9.0f);
    float fl[NSIDE] = { 0 };
    for (int i = 0; i < 3; i++) if (f >= hits[i] && f < hits[i] + 5) fl[SD_LEFT] = 1.0f - (float)(f - hits[i]) / 5.0f;
    draw_box(&g, fl);
    oled_hline_pat(0, 53, OLED_W, 0x55, true);
    if (jump > 0.5f) oled_hline_pat(g.fx0 + 4, 55, g.fx1 - g.fx0 - 4, 0x11, true);   // its shadow on the floor
    // the fist swings in, knocks, pulls back, three times
    if (f < 44) {
        float x = 18.0f, hx = (float)g.fx0 - 18.0f;
        for (int i = 0; i < 3; i++) {
            int d = f - hits[i];
            if (d >= -7 && d < 0) x = 18.0f + (hx - 18.0f) * ease_in_out((float)(d + 7) / 7.0f);
            else if (d >= 0 && d < 5) x = hx - (hx - 24.0f) * ease_out((float)d / 5.0f);
        }
        fist((int)x, (int)byc - 6);
    }
    parts_draw(&parts);
    // KNOCK, KNOCK, KNOCK: each one floats up from where the knuckles land, bigger each time, one at a time
    static const gfx_font_t *const fnt[3] = { F_TINY, F_SMALL, F_MED };
    for (int i = 0; i < 3; i++) {
        int age = f - hits[i];
        if (age < 0 || age >= (i < 2 ? 12 : 10)) continue;
        float up = ease_out((float)age / 12.0f) * 12.0f;
        oled_ftext_ex(fnt[i], 40 + i * 2, (int)(byc - 16.0f - up), "KNOCK", 1);
    }
    // the title lands with a bounce, the tagline after it
    if (f >= 46) {
        float t = ease_bounce(clampf((float)(f - 46) / 14.0f, 0.0f, 1.0f));
        int y = (int)(-14.0f + 32.0f * t);
        oled_ftext_ex(F_MED, 4, y, "KNOCK", 2);
        if (f >= 60) oled_ftext_ex(F_TINY, 4, y + 12, INST.tagline, 1);
    }
    return f < 90;
}

// ---------------------------------------------------------------- screensaver: the box wanders
static void saver(uint32_t f)
{
    static float x = 40, y = 30, vx = 23, vy = 17;
    static parts_t sp;
    const float dt = 0.04f;
    x += vx * dt; y += vy * dt;
    if (x < 16 || x > 104) { vx = -vx; x = clampf(x, 16, 104); parts_emit(&sp, x < 64 ? 2 : 125, y, 0, 0, 0.4f, PART_RING, 2); }
    if (y < 10 || y > 56) { vy = -vy; y = clampf(y, 10, 56); parts_emit(&sp, x, y < 32 ? 2 : 62, 0, 0, 0.4f, PART_RING, 2); }
    parts_step(&sp, dt, 0, 0);
    boxgeo_t g = box_geo(x, y, 18, 8, 5);
    draw_box(&g, NULL);
    parts_draw(&sp);
    (void)f;
}

const instrument_t INST = {
    .name = "KNOCK", .tagline = "KNOCK ON IT TO DRUM",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 5,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .looper = false,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver,
};
